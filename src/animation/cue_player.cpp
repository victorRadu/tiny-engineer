#include "animation/cue_player.h"

#include "animation.h"
#include "display/eyes/cue.h"
#include "hardware/servo_wrapper.h"
#include "serial_log.h"
#include "servos.h"

namespace {

StreamCueParser g_parser;
size_t g_next = 0;
bool g_active = false;

void applyCue(const StreamCue& cue, uint32_t now) {
  switch (cue.kind) {
    case CueKind::Move:
      for (int i = 0; i < SERVO_COUNT; i++) {
        if (cue.jointMask & (1u << i)) {
          servoAt(i).cueMove(cue.values[i], cue.easeMs, cue.holdMs, now);
        }
      }
      break;
    case CueKind::Blink:
      eyesCueBlink(now);
      break;
    case CueKind::Look:
      eyesCueLook(static_cast<CueLook>(cue.arg), cue.holdMs, now);
      break;
    case CueKind::Mood:
      eyesCueMood(cue.arg, cue.holdMs, now);
      break;
    case CueKind::Pose: {
      AnimationId id = AnimationId::None;
      if (parseAnimationName(streamCuePoseName(cue.arg), id)) {
        setAnimationImmediately(id);
      }
      break;
    }
  }
}

}  // namespace

StreamCueParser& cuePlayerParser() {
  return g_parser;
}

void cuePlayerBegin() {
  servoAllowCues();
  g_next = 0;
  g_active = !g_parser.failed() && g_parser.count() > 0;

  if (g_active) {
    serialLogPrint("[play] cues ");
    serialLogPrintln(static_cast<uint32_t>(g_parser.count()));
  }
}

void cuePlayerAdvance(uint32_t audioMs, uint32_t nowMs) {
  if (!g_active) {
    return;
  }

  while (g_next < g_parser.count() && g_parser.at(g_next).ms <= audioMs) {
    applyCue(g_parser.at(g_next), nowMs);
    g_next++;
  }

  // Some animations update only the joints they drive in their current phase
  // (thinking moves the head by direct writes), so a held or returning joint
  // is stepped here. update() is time-based: a second call in the same tick
  // is a no-op.
  for (int i = 0; i < SERVO_COUNT; i++) {
    if (servoAt(i).cueBusy()) {
      servoAt(i).update();
    }
  }
}

void cuePlayerEnd() {
  if (!g_active) {
    return;
  }

  g_active = false;
  servoReleaseAllCues();
  eyesCueClear();
}
