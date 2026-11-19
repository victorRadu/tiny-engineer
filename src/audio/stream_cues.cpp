#include "audio/stream_cues.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "display/eyes/styles/kawaii/expression.h"

namespace {

constexpr uint32_t kMaxMs = 600000;
constexpr uint32_t kMaxEaseMs = 10000;
constexpr uint32_t kMaxHoldMs = 60000;
constexpr uint16_t kDefaultMoveEaseMs = 300;
constexpr uint16_t kDefaultMoveHoldMs = 500;
constexpr uint16_t kDefaultLookHoldMs = 800;
constexpr uint16_t kDefaultMoodHoldMs = 0;

// Servo index order (SERVO_HEAD..SERVO_BODY).
const char* const kJointNames[SERVO_COUNT] = {"head", "neck", "hand_left", "hand_right", "body"};
const char* const kPoseNames[] = {"talking", "typing", "reading", "thinking", "waiting", "none"};
const char* const kLookNames[] = {"left", "right", "up", "down", "center"};
constexpr size_t kPoseCount = sizeof(kPoseNames) / sizeof(kPoseNames[0]);
constexpr size_t kLookCount = sizeof(kLookNames) / sizeof(kLookNames[0]);

bool isSpace(char c) {
  return c == ' ' || c == '\t';
}

// Whole number, digits only, at most `max`.
bool parseWhole(const char* text, uint32_t max, uint32_t* out) {
  if (text == nullptr || *text == '\0') {
    return false;
  }

  uint32_t value = 0;

  for (const char* cursor = text; *cursor != '\0'; cursor++) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }
    value = value * 10 + static_cast<uint32_t>(*cursor - '0');
    if (value > max) {
      return false;
    }
  }

  *out = value;
  return true;
}

// -?(digits[.digits*] | .digits), within -1..1. No exponent, no '+'.
// Only the first six fraction digits count for the value.
bool parseNorm(const char* text, float* out) {
  const char* cursor = text;
  bool negative = false;

  if (*cursor == '-') {
    negative = true;
    cursor++;
  }

  float whole = 0.0f;
  size_t wholeDigits = 0;

  while (*cursor >= '0' && *cursor <= '9') {
    // Leading zeros are fine; anything over 1 fails the range check below,
    // so the sum stops growing at 2 (no float overflow on long input).
    if (whole < 2.0f) {
      whole = whole * 10.0f + static_cast<float>(*cursor - '0');
    }
    wholeDigits++;
    cursor++;
  }

  float fraction = 0.0f;
  float scale = 1.0f;
  size_t fractionDigits = 0;
  bool fractionNonZero = false;

  if (*cursor == '.') {
    cursor++;
    while (*cursor >= '0' && *cursor <= '9') {
      if (fractionDigits < 6) {
        scale *= 0.1f;
        fraction += scale * static_cast<float>(*cursor - '0');
      }
      fractionNonZero = fractionNonZero || *cursor != '0';
      fractionDigits++;
      cursor++;
    }
  }

  if (*cursor != '\0' || (wholeDigits == 0 && fractionDigits == 0)) {
    return false;
  }

  // Range check on the exact text: 1.0000001 is over 1 even though only six
  // fraction digits count for the value.
  if (whole > 1.0f || (whole == 1.0f && fractionNonZero)) {
    return false;
  }

  float value = whole + fraction;

  if (negative && value != 0.0f) {
    value = -value;
  }

  if (value < -1.0f || value > 1.0f) {
    return false;
  }

  *out = value;
  return true;
}

int indexOf(const char* const* names, size_t count, const char* name) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(names[i], name) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int moodIndex(const char* name) {
  for (size_t i = 0; i < static_cast<size_t>(kawaii::Mood::Count); i++) {
    const kawaii::Mood mood = static_cast<kawaii::Mood>(i);
    // Asleep blanks the face, which makes no sense while the robot speaks.
    if (mood != kawaii::Mood::Asleep && strcmp(kawaii::moodName(mood), name) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// Splits `text` in place into whitespace-separated tokens.
size_t tokenize(char* text, char** tokens, size_t max) {
  size_t count = 0;
  char* cursor = text;

  while (*cursor != '\0') {
    while (isSpace(*cursor)) {
      cursor++;
    }
    if (*cursor == '\0') {
      break;
    }
    if (count == max) {
      return max + 1;
    }
    tokens[count++] = cursor;
    while (*cursor != '\0' && !isSpace(*cursor)) {
      cursor++;
    }
    if (*cursor != '\0') {
      *cursor = '\0';
      cursor++;
    }
  }

  return count;
}

void appendf(char* out, size_t size, size_t& used, const char* fmt, ...) __attribute__((format(printf, 4, 5)));

void appendf(char* out, size_t size, size_t& used, const char* fmt, ...) {
  if (used >= size) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  const int wrote = vsnprintf(out + used, size - used, fmt, args);
  va_end(args);
  if (wrote > 0) {
    used += static_cast<size_t>(wrote);
  }
}

}  // namespace

const char* streamCuePoseName(uint8_t index) {
  return index < kPoseCount ? kPoseNames[index] : nullptr;
}

const char* streamCueLookName(CueLook look) {
  const size_t index = static_cast<size_t>(look);
  return index < kLookCount ? kLookNames[index] : nullptr;
}

void StreamCueParser::reset() {
  count_ = 0;
  lineFill_ = 0;
  lineTooLong_ = false;
  lineHasNul_ = false;
  lineNumber_ = 0;
  totalBytes_ = 0;
  lastMs_ = 0;
  failed_ = false;
  message_[0] = '\0';
}

void StreamCueParser::begin(uint32_t declaredBytes) {
  if (declaredBytes > kMaxBytes) {
    fail(0, "cue chunk too big");
  }
}

void StreamCueParser::feed(const uint8_t* bytes, size_t length) {
  for (size_t i = 0; i < length && !failed_; i++) {
    totalBytes_++;
    if (totalBytes_ > kMaxBytes) {
      fail(0, "cue chunk too big");
      return;
    }

    const char c = static_cast<char>(bytes[i]);

    if (c == '\n') {
      endLine();
      continue;
    }

    if (c == '\0') {
      // A NUL would end the line early in parseLine() and hide what follows.
      lineHasNul_ = true;
    }

    if (lineFill_ < sizeof(line_) - 1) {
      line_[lineFill_++] = c;
    } else {
      lineTooLong_ = true;
    }
  }
}

void StreamCueParser::finish() {
  if (!failed_ && (lineFill_ > 0 || lineTooLong_ || lineHasNul_)) {
    endLine();
  }
}

void StreamCueParser::endLine() {
  lineNumber_++;

  if (lineFill_ > 0 && line_[lineFill_ - 1] == '\r') {
    lineFill_--;
  }

  if (lineTooLong_ || lineFill_ > kMaxLineBytes) {
    fail(lineNumber_, "line too long");
    return;
  }

  if (lineHasNul_) {
    fail(lineNumber_, "bad character");
    return;
  }

  line_[lineFill_] = '\0';
  lineFill_ = 0;
  parseLine(line_);
}

void StreamCueParser::parseLine(char* text) {
  char* comment = strchr(text, '#');
  if (comment != nullptr) {
    *comment = '\0';
  }

  while (isSpace(*text)) {
    text++;
  }
  if (*text == '\0') {
    return;
  }

  char* equals = strchr(text, '=');
  if (equals == nullptr) {
    fail(lineNumber_, "expected <ms>=<action>");
    return;
  }
  *equals = '\0';

  char* timeEnd = equals;
  while (timeEnd > text && isSpace(timeEnd[-1])) {
    *--timeEnd = '\0';
  }

  uint32_t ms = 0;
  if (!parseWhole(text, kMaxMs, &ms)) {
    fail(lineNumber_, "bad time");
    return;
  }

  // Longest valid line: move + five joints + ease + hold = 8 tokens.
  char* tokens[8];
  const size_t tokenCount = tokenize(equals + 1, tokens, 8);

  if (tokenCount == 0) {
    fail(lineNumber_, "expected <ms>=<action>");
    return;
  }

  if (count_ > 0 && ms < lastMs_) {
    fail(lineNumber_, "time goes back");
    return;
  }

  if (count_ == kMaxCues) {
    fail(0, "too many cues");
    return;
  }

  if (tokenCount > 8) {
    fail(lineNumber_, "extra argument");
    return;
  }

  StreamCue cue = {};
  cue.ms = ms;
  const char* action = tokens[0];
  size_t next = 1;
  bool allowEase = false;
  bool allowHold = false;

  if (strcmp(action, "move") == 0) {
    cue.kind = CueKind::Move;
    cue.easeMs = kDefaultMoveEaseMs;
    cue.holdMs = kDefaultMoveHoldMs;
    allowEase = true;
    allowHold = true;
  } else if (strcmp(action, "blink") == 0) {
    cue.kind = CueKind::Blink;
  } else if (strcmp(action, "look") == 0 || strcmp(action, "mood") == 0 || strcmp(action, "pose") == 0) {
    if (tokenCount < 2 || strchr(tokens[1], '=') != nullptr) {
      fail(lineNumber_, "missing argument");
      return;
    }
    const char* arg = tokens[1];
    next = 2;

    if (action[0] == 'l') {
      const int index = indexOf(kLookNames, kLookCount, arg);
      if (index < 0) {
        fail(lineNumber_, "unknown direction");
        return;
      }
      cue.kind = CueKind::Look;
      cue.arg = static_cast<uint8_t>(index);
      cue.holdMs = kDefaultLookHoldMs;
      allowHold = true;
    } else if (action[0] == 'm') {
      const int index = strcmp(arg, "auto") == 0 ? kCueMoodAuto : moodIndex(arg);
      if (index < 0) {
        fail(lineNumber_, "unknown mood");
        return;
      }
      cue.kind = CueKind::Mood;
      cue.arg = static_cast<uint8_t>(index);
      cue.holdMs = kDefaultMoodHoldMs;
      allowHold = true;
    } else {
      const int index = indexOf(kPoseNames, kPoseCount, arg);
      if (index < 0) {
        fail(lineNumber_, "unknown pose");
        return;
      }
      cue.kind = CueKind::Pose;
      cue.arg = static_cast<uint8_t>(index);
    }
  } else {
    fail(lineNumber_, "unknown action");
    return;
  }

  bool easeSeen = false;
  bool holdSeen = false;

  for (size_t i = next; i < tokenCount; i++) {
    char* token = tokens[i];
    char* eq = strchr(token, '=');

    if (eq == nullptr) {
      fail(lineNumber_, "extra argument");
      return;
    }
    *eq = '\0';
    const char* key = token;
    const char* value = eq + 1;

    if (strcmp(key, "ease") == 0 || strcmp(key, "hold") == 0) {
      const bool ease = key[0] == 'e';
      if (ease ? !allowEase : !allowHold) {
        fail(lineNumber_, cue.kind == CueKind::Move || cue.kind == CueKind::Look || cue.kind == CueKind::Mood
                            ? "unknown key"
                            : "extra argument");
        return;
      }
      if (ease ? easeSeen : holdSeen) {
        fail(lineNumber_, "repeated key");
        return;
      }
      uint32_t parsed = 0;
      if (!parseWhole(value, ease ? kMaxEaseMs : kMaxHoldMs, &parsed)) {
        fail(lineNumber_, ease ? "bad ease" : "bad hold");
        return;
      }
      if (ease) {
        easeSeen = true;
        cue.easeMs = static_cast<uint16_t>(parsed);
      } else {
        holdSeen = true;
        cue.holdMs = static_cast<uint16_t>(parsed);
      }
      continue;
    }

    if (cue.kind != CueKind::Move) {
      fail(lineNumber_, cue.kind == CueKind::Blink || cue.kind == CueKind::Pose ? "extra argument" : "unknown key");
      return;
    }

    const int joint = indexOf(kJointNames, SERVO_COUNT, key);
    if (joint < 0) {
      fail(lineNumber_, "unknown joint");
      return;
    }
    if (cue.jointMask & (1u << joint)) {
      fail(lineNumber_, "repeated joint");
      return;
    }
    float norm = 0.0f;
    if (!parseNorm(value, &norm)) {
      fail(lineNumber_, "bad value");
      return;
    }
    cue.jointMask |= static_cast<uint8_t>(1u << joint);
    cue.values[joint] = norm;
  }

  if (cue.kind == CueKind::Move && cue.jointMask == 0) {
    fail(lineNumber_, "move needs a joint");
    return;
  }

  cues_[count_++] = cue;
  lastMs_ = ms;
}

void StreamCueParser::fail(uint32_t line, const char* reason) {
  if (failed_) {
    return;
  }
  failed_ = true;
  snprintf(message_, sizeof(message_), "bad cue line %lu: %s", static_cast<unsigned long>(line), reason);
}

void StreamCueParser::format(const StreamCue& cue, char* out, size_t size) {
  size_t used = 0;
  appendf(out, size, used, "%lu", static_cast<unsigned long>(cue.ms));

  switch (cue.kind) {
    case CueKind::Move:
      appendf(out, size, used, " move");
      for (int i = 0; i < SERVO_COUNT; i++) {
        if (cue.jointMask & (1u << i)) {
          appendf(out, size, used, " %s=%.3f", kJointNames[i], static_cast<double>(cue.values[i]));
        }
      }
      appendf(out, size, used, " ease=%u hold=%u", static_cast<unsigned>(cue.easeMs), static_cast<unsigned>(cue.holdMs));
      break;
    case CueKind::Blink:
      appendf(out, size, used, " blink");
      break;
    case CueKind::Look:
      appendf(out, size, used, " look %s hold=%u", kLookNames[cue.arg], static_cast<unsigned>(cue.holdMs));
      break;
    case CueKind::Mood:
      appendf(out, size, used, 
        " mood %s hold=%u",
        cue.arg == kCueMoodAuto ? "auto" : kawaii::moodName(static_cast<kawaii::Mood>(cue.arg)),
        static_cast<unsigned>(cue.holdMs)
      );
      break;
    case CueKind::Pose:
      appendf(out, size, used, " pose %s", kPoseNames[cue.arg]);
      break;
  }
}
