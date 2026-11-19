#include "animation/play_cues.h"

#include "animation.h"
#include "audio/audio_motion.h"

void PlayCues::reset() {
  cuePlayerParser().reset();
  fedSpeech_ = false;
  started_ = false;
}

void PlayCues::onChunkBegin(uint32_t size) {
  // A second `tecu` chunk replaces the first.
  cuePlayerParser().reset();
  cuePlayerParser().begin(size);
}

void PlayCues::onChunkData(const uint8_t* bytes, size_t length) {
  cuePlayerParser().feed(bytes, length);
}

void PlayCues::onChunkEnd() {
  cuePlayerParser().finish();
}

void PlayCues::start(uint32_t nowMs) {
  (void)nowMs;
  started_ = true;
  cuePlayerBegin();
}

void PlayCues::beforeBatch(
  const int16_t* samples,
  size_t count,
  uint32_t samplesThrough,
  uint32_t sampleRate,
  uint32_t nowMs
) {
  const uint32_t audioMs = static_cast<uint32_t>(static_cast<uint64_t>(samplesThrough) * 1000ULL / sampleRate);
  cuePlayerAdvance(audioMs, nowMs);

  const bool talking = getAnimation() == AnimationId::Talking;

  if (talking && !fedSpeech_) {
    audioMotionBegin(nowMs);
    fedSpeech_ = true;
  } else if (!talking && fedSpeech_) {
    audioMotionEnd(nowMs);
    fedSpeech_ = false;
  }

  if (talking) {
    audioMotionFeed(samples, count, nowMs);
  }
}

void PlayCues::end(uint32_t nowMs) {
  if (!started_) {
    return;
  }

  started_ = false;
  cuePlayerEnd();

  if (fedSpeech_) {
    audioMotionEnd(nowMs);
    fedSpeech_ = false;
  }
}
