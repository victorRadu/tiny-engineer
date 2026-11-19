# Design

## Context

- `POST /play` (`src/http/play_handlers.cpp`) and serial `play` (`src/serial/play.cpp`) both feed the body to `WavStreamParser`, which calls `Sink::onPcm()` per 256-sample batch. `loop()` is blocked during the body; the session calls `tickRobot()` after each batch. `samplesDelivered()` is the audio clock.
- `WavStreamParser` already skips unknown chunks (`State::Skip`). A chunk before `data` is fully read before the first sample plays.
- Animations move joints by calling `servoAt(i).setNormTarget()`, `setTarget()`, `stop()`, `snapTo()` and `setPosition()` directly (`src/animation/*.cpp`). Many of them wait for `!isMoving()` before the next command. There is no shared "who owns this joint" layer.
- Speech-energy motion: the session feeds PCM to `audio_motion` only when the session's pose is `talking`; `talking.cpp` reads `audioMotionSnapshot()`.
- Stock `.cue` files (`src/audio/audio_cues.cpp`) are named phase marks per clip, read from LittleFS at boot. They stay as they are.
- The CLI reads the WAV in `cli.js` for HTTP; for serial, the daemon reads the file by path in `src/daemon/link.js`.

## Goals / Non-Goals

**Goals:**
- One cue grammar, parsed by one firmware parser and one JS validator, with matching tests.
- Cues start on the audio clock, so jitter from the network or serial credits does not shift them.
- A cue never fights the animation for a joint; the joint returns smoothly.

**Non-Goals:**
- Cues for the stock LittleFS clips (they keep `.cue` phrase marks).
- Cues with no audio (a pure motion timeline), or cues sent while a clip already plays.
- LED cues, sound effects, kaomoji face mapping.
- Web UI editor for cue files.

## Decisions

### D1. Transport: RIFF chunk `tecu` before `data`
The cue text rides in the WAV as its own chunk. The body stays one WAV, so HTTP and serial need no new field, the route table and API page do not change, and old firmware skips the chunk.
- Chunk ID `tecu`, not `cue `: `cue ` is a standard binary cue-point chunk that editors write; reusing it would make real WAV files fail.
- Before `data` only: the firmware must know all cues before the first sample. A `tecu` after `data` is skipped (it arrives too late).
- Alternatives: HTTP header or query param (HTTP only, needs a second serial field, header size limits); multipart body (much more parser work on the C3).

### D2. Sidecar name `<clip>.cues`, not `<clip>.cue`
`mods/*/assets/` and `data/` already pair `welcome.wav` with `welcome.cue` in the stock key format. If the CLI read `<clip>.cue`, `tiny-engineer play data/welcome.wav` would fail on the stock file. A distinct name avoids a format guess. The line style (`key=value`, `#` comments) stays the same as `.cue`, so it feels familiar.

### D3. Grammar
`<ms>=<action> [args]`. Actions: `move`, `blink`, `look`, `mood`, `pose`. Limits: 64 cues, 120-byte lines, 8192-byte chunk, times 0..600000 (same max as `kCueMaxMs`), non-decreasing times. Strict rejection (whole list) instead of skipping bad lines: an author wants to see the mistake, and the stock `.cue` loader also rejects the whole file.

Defaults: `move ease=300 hold=500`; `look hold=800`; `mood hold=0` (sustained). `hold=0` everywhere means "until a later cue for the same target or the clip ends". Moves and looks default to a short hold because the user asked for auto motion to resume; a mood is a state, so it stays.

### D4. Parser: streaming, fixed table, shared between targets
New pure module `src/audio/stream_cues.{h,cpp}` (no Arduino headers, so `[env:native]` can test it):
- `StreamCueParser::feed(bytes, len)` with a 121-byte line buffer, `finish()`, `failed()`, `error()` (`bad cue line N: reason`).
- Output `StreamCue cues[64]` sorted by input order: `{ uint32_t ms; uint8_t kind; uint8_t jointMask; float values[5]; uint16_t easeMs; uint16_t holdMs; uint8_t arg; }` (~36 B → ~2.3 KB static, one instance; only one play session runs at a time because of `apiBusy`).
- `WavStreamParser::Sink` gets default no-op `onChunkBegin(id, size)` / `onChunkData(bytes, len)` / `onChunkEnd()` for `tecu`; the parser forwards bytes instead of skipping. `wav_parser` stays generic (any sink may ignore it). The cue parser checks the declared size (> 8192 fails at once), so a too-big chunk is a 400 cue error, not a 415 WAV error.

### D5. Cue player on the audio clock
New `src/animation/cue_player.{h,cpp}`: `cuePlayerBegin(table)`, `cuePlayerAdvance(audioMs, nowMs)`, `cuePlayerEnd()`. The session calls `cuePlayerAdvance(samplesDelivered * 1000 / SAMPLE_RATE)` in `onPcm()` before the batch goes to the speaker, with `samplesDelivered` counting that batch. So every cue up to the end of the batch starts with it, also in the last (partial) batch of the clip: every cue timed before the clip end starts. Resolution is one batch (256 samples ≈ 11.6 ms at 22050 Hz), well below what eyes can see. A cue can start up to one batch plus the I2S buffer depth before its sound is heard. Accepted: servos need longer than that to move. Cues past the last sample never fire; `cuePlayerEnd()` releases all holds before the session restores the previous animation.

### D6. Joint ownership: cue hold in `ServoWrapper`
Add to `ServoWrapper`: `cueMove(normTarget, easeMs, holdMs)`, `cueRelease()`, `cueHeld()`, `cueBusy()`, plus `servoReleaseAllCues()`, `servoCancelCues()` and `servoAllowCues()`.
- On the first cue, the joint's current target is saved as the "shadow" target: the place the joint goes back to. For a still joint this is its rest pose, so a static pose joint (`waiting` body at `+0.8`) comes back even though the animation never commands it again.
- While held, animation calls to `setTarget`/`setNormTarget`/`setPosition`/`setNormPosition`/`snapTo` do not move the servo; they replace the shadow target and speed (position writes use `kCueReturnSpeedDegS` = 60°/s). `stop()` keeps the shadow.
- `hold` counts from arrival: `update()` notes the first tick the joint is still, then releases `hold` ms later. A speed-capped move keeps its full hold.
- On release the joint goes to the shadow target at the shadow speed. Until it is there ("returning"), position writes from animations that drive the joint directly (`thinking` head, `waiting` look) are stepped at 60°/s instead of written at once, so the servo never jumps. `setTarget` or `stop()` from the animation ends the return.
- `cuePlayerAdvance()` steps every held or returning joint once per audio batch, because some animations do not call `update()` on every joint in every phase. `update()` is time-based, so a second call in a tick is a no-op.
- Speed from ease: `speed = |Δdeg| / (easeMs/1000)`, `ease=0` → `SERVO_MAX_SPEED_DEG_S`; capped at that max, so a very short ease is a fast move, not a jump. A cue on a still joint starts on a fresh step clock.
- `isMoving()` during a hold reports the cue motion, so animations that wait for a stopped joint wait, as they would for their own move.
- Park and a servo driver fault call `servoCancelCues()`: all holds end and later cues in the same clip do not take joints (also when turning the output on faults inside `cueMove`). The next clip calls `servoAllowCues()`.
- `moveTo`, `moveToElectrical` and `servoMoveAllSmoothTo` bypass the hold; only test and setup routes use them, and the `/play` busy lock keeps those out.
- Alternatives: a layer above `servoAt()` that every animation must call (touches all 15 animation files and is easy to bypass); pausing the whole animation (breaks "other joints keep moving").

### D7. Eyes and face
- `blink`: `blinkScheduleSoon(now, 0, 0)` in `src/display/eyes/core/blink`.
- `look`: a small override in the eyes controller: target offset (±5 px x / 3 px up / 2 px down, the same sizes `waiting` uses) applied after the mode update until the hold ends. Classic, cover, dots and kawaii pupils follow the classic pose, so one hook covers them; kaomoji ignores it.
- `mood`: kawaii-only override in the kawaii director (`update()` returns the cued mood while set). Names come from `kawaii::moodName()`; the parser checks names against the same table (moved to a pure header so native tests and the parser share it). `asleep` is rejected (it blanks the face mid-speech).
- The kawaii speech mouth keeps following the audio while the pose is `talking`, whatever the mood.

### D8. Pose switch and speech energy
`pose` calls `setAnimationImmediately()`. The session feeds `audio_motion` while `getAnimation() == Talking` (today: while the session's start pose is `Talking`), and calls `audioMotionBegin/End` on the switch edges. The "previous animation" restored at clip end stays the one from before the request.

### D9. CLI
New `src/robot/cues.js`: `parseCues(text)` (same rules and messages as the firmware) and `withCueChunk(wavBytes, cueText)` (drop any existing `tecu`, insert the new one right before `data`, fix the RIFF size, pad to even length). `cli.js` resolves the sidecar and validates before choosing a transport. For serial, the CLI passes the cue path to the daemon RPC (`cues` param); `link.js` reads both files and calls the same `withCueChunk`. A shared plain-text fixture of valid and invalid lists (`test/fixtures/stream_cues.txt` at repo root; the native tests have no JSON library) drives both the Unity tests and the Node tests, so the two parsers cannot drift.

## Risks / Trade-offs

- [Two parsers, C++ and JS, can drift] → shared fixture file used by both test suites (D9).
- [Older npm daemon does not know the `cues` RPC param and plays without cues] → daemon already replaces older instances by version; CLI README notes `daemon restart` after upgrade.
- [Shadow target restore can jump if the animation moved on to a far target] → restore uses the animation's own last speed, so it looks like the animation's own move.
- [A joint held with `hold=0` across a pose switch] → allowed; the new pose's commands go to the shadow target and apply on release or clip end.
- [~2.3 KB static RAM on the C3] → one table; checked with the `pio run` RAM report before and after.
- [Gestures fight with servo safety (park, sleep, i2c fault)] → `servoCancelCues()` in park and the I2C fault path (ends every hold and blocks later cues in the clip), and `servoReleaseAllCues()` at clip end, so safety code always wins. Not in `stopAnimServos()`: a `pose` cue starts an animation that calls it, which would cancel a `move` cue at the same time.

## Migration Plan

Additive. Flash new firmware and update the CLI in any order: new CLI + old firmware plays without cues; old CLI + new firmware sends no chunk. Rollback = flash the previous firmware.

## Open Questions

- Should kaomoji map some moods to its faces (for example `grin` → Happy)? Can be added later without changing the grammar.
