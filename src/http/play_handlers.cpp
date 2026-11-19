#include <Arduino.h>

#include <optional>

#include "animation.h"
#include "animation/play_cues.h"
#include "api/busy.h"
#include "audio/audio.h"
#include "audio/wav_parser.h"
#include "extensions/push_button/push_button.h"
#include "http/access.h"
#include "http/handlers.h"
#include "http/response.h"
#include "pins.h"
#include "robot_tick.h"
#include "settings/settings.h"
#include "serial_log.h"
#include "sleep.h"

namespace {

constexpr size_t kPlayPrefixLen = sizeof("/play/") - 1;

// One POST /play request. loop() is blocked while the body streams, so the
// session runs tickRobot() between speaker writes.
class PlaySession : public WavStreamParser::Sink {
public:
  void begin(WebServer& server) {
    parser_.reset();
    started_ = false;
    heldBusy_ = false;
    refusal_ = Refusal::check(server, Access::Configured);

    if (refusal_) {
      return;
    }

    if (!apiBusyTryAcquire()) {
      refusal_ = Refusal{503, "busy"};
      return;
    }
    heldBusy_ = true;

    // "/play" or "/play/<name>"; pathArg() would assert on the route without a name.
    const String uri = server.uri();
    const char* name = uri.length() > kPlayPrefixLen ? uri.c_str() + kPlayPrefixLen : "talking";

    // Continuous animations and none play no sound of their own, so they cannot talk over the clip.
    if (!parseAnimationName(name, animation_) ||
        (animation_ != AnimationId::None && !animationIsContinuous(animation_))) {
      refusal_ = Refusal{400, "name must be talking, typing, reading, thinking, waiting or none"};
      releaseBusy_();
      return;
    }

    stopAllWavPlayback();
    // Shared with serial play: reset only once this session holds the busy lock.
    cues_.reset();
    parser_.emplace(SAMPLE_RATE, *this);
    serialLogPrintln("[play] start");
  }

  void feed(const uint8_t* bytes, size_t length) {
    if (parser_) {
      parser_->feed(bytes, length);
    }
  }

  void end() {
    if (!parser_) {
      releaseBusy_();
      return;
    }

    parser_->finish();
    serialLogPrint("[play] end ms=");
    serialLogPrintln(playedMs());

    if (started_) {
      cues_.end(millis());
      setAnimationImmediately(animationIsContinuous(previous_) ? previous_ : AnimationId::None);
    }
    releaseBusy_();
  }

  void respond(WebServer& server) {
    if (refusal_) {
      refusal_->send(server);
      return;
    }

    if (cues_.failed()) {
      sendError(server, 400, cues_.error());
      return;
    }

    if (parser_->failed()) {
      sendError(server, 415, parser_->error());
      return;
    }

    JsonDocument body;
    body["ok"] = true;
    body["played_ms"] = playedMs();
    sendJson(server, 200, body);
  }

  void onChunkBegin(uint32_t size) override { cues_.onChunkBegin(size); }
  void onChunkData(const uint8_t* bytes, size_t length) override { cues_.onChunkData(bytes, length); }
  void onChunkEnd() override { cues_.onChunkEnd(); }

  void onPcm(const int16_t* samples, size_t count) override {
    // A bad cue list rejects the clip: no sound, no motion.
    if (cues_.failed()) {
      return;
    }

    const uint32_t now = millis();

    if (!started_) {
      // The format is known good by now, so the robot only moves for clips it can play.
      started_ = true;
      previous_ = getAnimation();
      setAnimationImmediately(animation_);
      cues_.start(now);
    }

    // samplesDelivered() already counts this batch.
    cues_.beforeBatch(samples, count, parser_->samplesDelivered(), SAMPLE_RATE, now);

    writeMonoToSpeaker(samples, count);
    noteActivity(now);
    pushButtonPoll();
    tickRobot();
  }

private:
  std::optional<WavStreamParser> parser_;
  PlayCues cues_;
  AnimationId animation_ = AnimationId::Talking;
  AnimationId previous_ = AnimationId::None;
  bool started_ = false;
  bool heldBusy_ = false;
  std::optional<Refusal> refusal_;

  void releaseBusy_() {
    if (heldBusy_) {
      apiBusyRelease();
      heldBusy_ = false;
    }
  }

  uint32_t playedMs() const {
    return static_cast<uint32_t>(parser_->samplesDelivered() * 1000ULL / SAMPLE_RATE);
  }
};

PlaySession g_session;

}  // namespace

void handlePlayBody(WebServer& server) {
  HTTPRaw& raw = server.raw();

  switch (raw.status) {
    case RAW_START:
      g_session.begin(server);
      break;
    case RAW_WRITE:
      g_session.feed(raw.buf, raw.currentSize);
      break;
    case RAW_END:
    case RAW_ABORTED:
      g_session.end();
      break;
  }

  pushButtonPoll();
}

void handlePlayDone(WebServer& server) {
  g_session.respond(server);
}
