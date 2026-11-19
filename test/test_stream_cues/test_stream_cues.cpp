#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "audio/stream_cues.h"

// Runs every case in test/fixtures/stream_cues.txt (shared with the CLI tests),
// then the limits that are easier to build in code.

namespace {

struct FixtureCase {
  std::string name;
  std::string text;
  bool ok = false;
  size_t count = 0;
  std::vector<std::string> expected;
  std::string error;
};

std::string readFixture() {
  const char* candidates[] = {
    "test/fixtures/stream_cues.txt",
    "../test/fixtures/stream_cues.txt",
  };

  std::string fromSource = __FILE__;
  const size_t slash = fromSource.rfind("test_stream_cues/");
  if (slash != std::string::npos) {
    fromSource = fromSource.substr(0, slash) + "fixtures/stream_cues.txt";
  }

  std::vector<std::string> paths(candidates, candidates + 2);
  paths.push_back(fromSource);

  for (const std::string& path : paths) {
    FILE* file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
      continue;
    }
    std::string text;
    char buffer[1024];
    size_t got = 0;
    while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) {
      text.append(buffer, got);
    }
    fclose(file);
    return text;
  }

  return std::string();
}

std::vector<FixtureCase> parseFixture(const std::string& text) {
  std::vector<FixtureCase> cases;
  FixtureCase* current = nullptr;
  bool inExpected = false;
  size_t start = 0;

  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      end = text.size();
    }
    std::string line = text.substr(start, end - start);
    start = end + 1;

    if (line.rfind("@@ case ", 0) == 0) {
      cases.push_back(FixtureCase());
      current = &cases.back();
      current->name = line.substr(8);
      inExpected = false;
    } else if (current != nullptr && line.rfind("@@ ok ", 0) == 0) {
      current->ok = true;
      current->count = static_cast<size_t>(atoi(line.c_str() + 6));
      inExpected = true;
    } else if (current != nullptr && line.rfind("@@ error ", 0) == 0) {
      current->error = line.substr(9);
      inExpected = false;
      current = nullptr;
    } else if (current != nullptr && inExpected) {
      if (!line.empty()) {
        current->expected.push_back(line);
      }
    } else if (current != nullptr) {
      current->text += line;
      current->text += '\n';
    }

    if (end == text.size()) {
      break;
    }
  }

  return cases;
}

void feedInSlices(StreamCueParser& parser, const std::string& text, size_t slice) {
  parser.reset();
  parser.begin(static_cast<uint32_t>(text.size()));
  for (size_t i = 0; i < text.size(); i += slice) {
    const size_t n = text.size() - i < slice ? text.size() - i : slice;
    parser.feed(reinterpret_cast<const uint8_t*>(text.data() + i), n);
  }
  parser.finish();
}

StreamCueParser g_parser;

void test_fixture_cases() {
  const std::string text = readFixture();
  TEST_ASSERT_FALSE_MESSAGE(text.empty(), "cannot read test/fixtures/stream_cues.txt");

  const std::vector<FixtureCase> cases = parseFixture(text);
  TEST_ASSERT_TRUE(cases.size() > 20);

  const size_t slices[] = {1, 7, 4096};

  for (const FixtureCase& c : cases) {
    for (size_t slice : slices) {
      feedInSlices(g_parser, c.text, slice);
      if (c.ok) {
        TEST_ASSERT_FALSE_MESSAGE(g_parser.failed(), c.name.c_str());
        TEST_ASSERT_EQUAL_MESSAGE(c.count, g_parser.count(), c.name.c_str());
        TEST_ASSERT_EQUAL_MESSAGE(c.count, c.expected.size(), c.name.c_str());
        for (size_t i = 0; i < g_parser.count(); i++) {
          char line[160];
          StreamCueParser::format(g_parser.at(i), line, sizeof(line));
          TEST_ASSERT_EQUAL_STRING_MESSAGE(c.expected[i].c_str(), line, c.name.c_str());
        }
      } else {
        TEST_ASSERT_TRUE_MESSAGE(g_parser.failed(), c.name.c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.error.c_str(), g_parser.error(), c.name.c_str());
      }
    }
  }
}

void test_64_cues_ok_65_rejected() {
  std::string text;
  for (int i = 0; i < 64; i++) {
    text += std::to_string(i * 10) + "=blink\n";
  }
  feedInSlices(g_parser, text, 64);
  TEST_ASSERT_FALSE(g_parser.failed());
  TEST_ASSERT_EQUAL(64, g_parser.count());

  text += "9999=blink\n";
  feedInSlices(g_parser, text, 64);
  TEST_ASSERT_TRUE(g_parser.failed());
  TEST_ASSERT_EQUAL_STRING("bad cue line 0: too many cues", g_parser.error());
}

void test_120_byte_line_ok() {
  std::string line = "100=blink";
  line += std::string(120 - line.size(), ' ');
  TEST_ASSERT_EQUAL(120, line.size());
  feedInSlices(g_parser, line + "\n", 5);
  TEST_ASSERT_FALSE(g_parser.failed());
  TEST_ASSERT_EQUAL(1, g_parser.count());

  feedInSlices(g_parser, line + " \n", 5);
  TEST_ASSERT_EQUAL_STRING("bad cue line 1: line too long", g_parser.error());
}

void test_chunk_size_limit() {
  g_parser.reset();
  g_parser.begin(8193);
  TEST_ASSERT_EQUAL_STRING("bad cue line 0: cue chunk too big", g_parser.error());

  std::string text(8193, '#');
  g_parser.reset();
  g_parser.feed(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  TEST_ASSERT_EQUAL_STRING("bad cue line 0: cue chunk too big", g_parser.error());

  // Exactly at the limit: many short comment lines.
  std::string many;
  while (many.size() + 2 <= 8192) {
    many += "#\n";
  }
  feedInSlices(g_parser, many, 100);
  TEST_ASSERT_FALSE(g_parser.failed());
}

void test_last_line_without_newline() {
  feedInSlices(g_parser, "100=blink\n200=look up", 3);
  TEST_ASSERT_FALSE(g_parser.failed());
  TEST_ASSERT_EQUAL(2, g_parser.count());
  TEST_ASSERT_EQUAL(CueKind::Look, g_parser.at(1).kind);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(CueLook::Up), g_parser.at(1).arg);
}

void test_move_values_and_mask() {
  feedInSlices(g_parser, "0=move hand_right=0.7 body=-1\n", 4096);
  const StreamCue& cue = g_parser.at(0);
  TEST_ASSERT_EQUAL((1u << SERVO_HAND_RIGHT) | (1u << SERVO_BODY), cue.jointMask);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.7f, cue.values[SERVO_HAND_RIGHT]);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.0f, cue.values[SERVO_BODY]);
}

void test_crlf_line_ends() {
  feedInSlices(g_parser, "50=move head=0.1 hold=20\r\n60=blink\r\n", 3);
  TEST_ASSERT_FALSE(g_parser.failed());
  TEST_ASSERT_EQUAL(2, g_parser.count());
  char line[160];
  StreamCueParser::format(g_parser.at(1), line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING("60 blink", line);
}

void test_negative_zero_prints_as_zero() {
  feedInSlices(g_parser, "0=move head=-0\n", 64);
  char line[160];
  StreamCueParser::format(g_parser.at(0), line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING("0 move head=0.000 ease=300 hold=500", line);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fixture_cases);
  RUN_TEST(test_64_cues_ok_65_rejected);
  RUN_TEST(test_120_byte_line_ok);
  RUN_TEST(test_chunk_size_limit);
  RUN_TEST(test_last_line_without_newline);
  RUN_TEST(test_move_values_and_mask);
  RUN_TEST(test_crlf_line_ends);
  RUN_TEST(test_negative_zero_prints_as_zero);
  return UNITY_END();
}
