#include "display/eyes/cue.h"

#include "display/eyes/core/blink.h"
#include "display/eyes/core/internal.h"

namespace {

struct Hold {
  bool active = false;
  bool untilCleared = false;
  uint32_t untilMs = 0;

  void start(uint32_t holdMs, uint32_t now) {
    active = true;
    untilCleared = holdMs == 0;
    untilMs = now + holdMs;
  }

  bool live(uint32_t now) {
    if (active && !untilCleared && static_cast<int32_t>(now - untilMs) >= 0) {
      active = false;
    }
    return active;
  }
};

Hold g_look;
int16_t g_lookDx = 0;
int16_t g_lookDy = 0;

Hold g_mood;
uint8_t g_moodValue = 0;

// Same glance sizes as the waiting animation: ±5 px sideways, 3 px up, 2 px down.
void lookOffset(CueLook look, int16_t& dx, int16_t& dy) {
  dx = 0;
  dy = 0;
  switch (look) {
    case CueLook::Left:
      dx = -5;
      break;
    case CueLook::Right:
      dx = 5;
      break;
    case CueLook::Up:
      dy = -3;
      break;
    case CueLook::Down:
      dy = 2;
      break;
    case CueLook::Center:
      break;
  }
}

}  // namespace

void eyesCueBlink(uint32_t now) {
  blinkScheduleSoon(now, 0, 0);
}

void eyesCueLook(CueLook look, uint32_t holdMs, uint32_t now) {
  lookOffset(look, g_lookDx, g_lookDy);
  g_look.start(holdMs, now);
  eyes::requestForceRedraw();
}

void eyesCueMood(uint8_t mood, uint32_t holdMs, uint32_t now) {
  if (mood == kCueMoodAuto) {
    g_mood.active = false;
  } else {
    g_moodValue = mood;
    g_mood.start(holdMs, now);
  }
  eyes::requestForceRedraw();
}

void eyesCueClear() {
  g_look.active = false;
  g_mood.active = false;
  eyes::requestForceRedraw();
}

bool eyesCueLookOffset(uint32_t now, int16_t& dx, int16_t& dy) {
  if (!g_look.live(now)) {
    return false;
  }
  dx = g_lookDx;
  dy = g_lookDy;
  return true;
}

bool eyesCueMoodActive(uint32_t now, uint8_t& mood) {
  if (!g_mood.live(now)) {
    return false;
  }
  mood = g_moodValue;
  return true;
}
