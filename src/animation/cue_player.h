#pragma once

#include <cstdint>

#include "audio/stream_cues.h"

// Plays voice cues (the WAV `tecu` chunk) against the /play audio clock.
// One play session runs at a time (apiBusy), so one parser serves both
// HTTP and serial play.

StreamCueParser& cuePlayerParser();

// Starts playing the cues the parser holds (call after it finished OK).
void cuePlayerBegin();
// Starts every cue whose time is at or before `audioMs` (ms of audio played).
void cuePlayerAdvance(uint32_t audioMs, uint32_t nowMs);
// Ends all holds (joints, look, mood). Safe to call when nothing started.
void cuePlayerEnd();
