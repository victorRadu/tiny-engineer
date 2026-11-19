#pragma once

#include <cstdint>

#include "audio/stream_cues.h"

// Eye and face overrides from voice cues (src/animation/cue_player.cpp).
// holdMs 0 = until the next cue of the same kind or eyesCueClear().

void eyesCueBlink(uint32_t now);
void eyesCueLook(CueLook look, uint32_t holdMs, uint32_t now);
// mood: kawaii::Mood value, or kCueMoodAuto to give the mood back.
void eyesCueMood(uint8_t mood, uint32_t holdMs, uint32_t now);
void eyesCueClear();

// Pixel offset the cued look adds to both eyes; false when none is active.
bool eyesCueLookOffset(uint32_t now, int16_t& dx, int16_t& dy);
// Cued kawaii mood; false when the animation owns the mood.
bool eyesCueMoodActive(uint32_t now, uint8_t& mood);
