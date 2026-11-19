#pragma once

#include <cstddef>
#include <cstdint>

#include "servos.h"

// Timed cues for streamed /play audio, read from the WAV `tecu` chunk.
// Pure parser: no Arduino, no servos, no eyes. Grammar: docs/api.md
// ("Voice cues"); shared test cases: test/fixtures/stream_cues.txt.

enum class CueKind : uint8_t { Move, Blink, Look, Mood, Pose };

enum class CueLook : uint8_t { Left, Right, Up, Down, Center };

// Mood arg value for `mood auto`; other values are kawaii::Mood.
constexpr uint8_t kCueMoodAuto = 0xFF;

struct StreamCue {
  uint32_t ms;
  CueKind kind;
  // Move: bit i set = servo index i (SERVO_HEAD..SERVO_BODY) has values[i].
  uint8_t jointMask;
  float values[SERVO_COUNT];
  uint16_t easeMs;
  uint16_t holdMs;
  // Look: CueLook. Mood: kawaii::Mood or kCueMoodAuto. Pose: streamCuePoseName index.
  uint8_t arg;
};

// Pose names a cue may switch to, by StreamCue::arg; nullptr past the end.
const char* streamCuePoseName(uint8_t index);
const char* streamCueLookName(CueLook look);

class StreamCueParser {
public:
  static constexpr size_t kMaxCues = 64;
  static constexpr size_t kMaxLineBytes = 120;
  static constexpr size_t kMaxBytes = 8192;

  void reset();
  // Size from the chunk header; fails at once when it is over kMaxBytes.
  void begin(uint32_t declaredBytes);
  void feed(const uint8_t* bytes, size_t length);
  void finish();

  bool failed() const { return failed_; }
  // "bad cue line N: reason" when failed(), else nullptr.
  const char* error() const { return failed_ ? message_ : nullptr; }
  size_t count() const { return count_; }
  const StreamCue& at(size_t index) const { return cues_[index]; }

  // Canonical one-line form used by the shared tests, e.g.
  // "1200 move hand_right=0.600 ease=250 hold=500".
  static void format(const StreamCue& cue, char* out, size_t size);

private:
  StreamCue cues_[kMaxCues] = {};
  size_t count_ = 0;
  char line_[kMaxLineBytes + 2] = {};
  size_t lineFill_ = 0;
  bool lineTooLong_ = false;
  bool lineHasNul_ = false;
  uint32_t lineNumber_ = 0;
  uint32_t totalBytes_ = 0;
  uint32_t lastMs_ = 0;
  bool failed_ = false;
  char message_[64] = {};

  void endLine();
  void parseLine(char* text);
  void fail(uint32_t line, const char* reason);
};
