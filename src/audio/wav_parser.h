#pragma once

#include <cstddef>
#include <cstdint>

// Chunk ID of the voice cue list (see audio/stream_cues.h).
constexpr char kCueChunkId[4] = {'t', 'e', 'c', 'u'};

// Incremental RIFF/WAVE reader for bodies that arrive in arbitrary slices.
// Accepts 16-bit mono PCM at one sample rate and hands samples to a sink as
// soon as they are complete; nothing beyond one small batch is buffered.
class WavStreamParser {
public:
  class Sink {
  public:
    virtual void onPcm(const int16_t* samples, size_t count) = 0;
    // The `tecu` (voice cue) chunk, forwarded when it comes before `data`.
    // `size` is the declared size; data may arrive in any slices.
    virtual void onChunkBegin(uint32_t size) { (void)size; }
    virtual void onChunkData(const uint8_t* bytes, size_t length) {
      (void)bytes;
      (void)length;
    }
    virtual void onChunkEnd() {}

  protected:
    ~Sink() = default;
  };

  WavStreamParser(uint32_t sampleRate, Sink& sink);

  void feed(const uint8_t* bytes, size_t length);
  void finish();

  bool failed() const;
  // Human-readable reason when failed(), else nullptr.
  const char* error() const;
  // Includes the batch currently being handed to Sink::onPcm.
  uint32_t samplesDelivered() const;

private:
  enum class State {
    RiffHeader,
    ChunkHeader,
    Format,
    Skip,
    Forward,
    Pcm,
    Trailer,
    Failed
  };

  static constexpr size_t kHeaderBytes = 40;
  static constexpr size_t kBatchSamples = 256;
  static constexpr uint32_t kUnknownLength = 0xFFFFFFFFu;

  Sink& sink_;
  uint32_t sampleRate_;
  State state_ = State::RiffHeader;
  char message_[80] = {};

  uint8_t header_[kHeaderBytes] = {};
  size_t headerFill_ = 0;
  size_t headerNeed_ = 12;

  uint32_t chunkRemaining_ = 0;
  bool chunkPadded_ = false;
  bool formatSeen_ = false;
  bool reachedPcm_ = false;

  int16_t batch_[kBatchSamples] = {};
  size_t batchFill_ = 0;
  bool hasCarry_ = false;
  uint8_t carry_ = 0;
  uint32_t samplesDelivered_ = 0;

  size_t fillHeader(const uint8_t* bytes, size_t length);
  void beginChunk();
  void parseFormat();
  void endChunk();
  size_t consumePcm(const uint8_t* bytes, size_t length);
  void pushSample(int16_t sample);
  void flush();
  void fail(const char* message);
};
