#pragma once

#include <cstddef>
#include <cstdint>

#include "animation/cue_player.h"
#include "audio/wav_parser.h"

// Session-side voice cue work shared by HTTP and serial /play:
// - reads the `tecu` chunk into the cue parser,
// - feeds speech-energy motion while the running animation is `talking`
//   (a `pose` cue can switch it in the middle of the clip),
// - starts cues on the audio clock and ends them before the session
//   restores the previous animation.
class PlayCues {
public:
  void reset();

  void onChunkBegin(uint32_t size);
  void onChunkData(const uint8_t* bytes, size_t length);
  void onChunkEnd();

  // Bad cue list: no audio may play and the request fails with error().
  bool failed() const { return cuePlayerParser().failed(); }
  const char* error() const { return cuePlayerParser().error(); }

  // Call once when the first sample of a good clip arrives, after the
  // session set its start animation.
  void start(uint32_t nowMs);
  // Call per PCM batch before it goes to the speaker. `samplesThrough` =
  // samples delivered including this batch, so a cue anywhere in the batch
  // (the last one of the clip too) starts with it.
  void beforeBatch(const int16_t* samples, size_t count, uint32_t samplesThrough, uint32_t sampleRate,
                   uint32_t nowMs);
  // Call before the session restores the previous animation.
  void end(uint32_t nowMs);

private:
  bool fedSpeech_ = false;
  bool started_ = false;
};
