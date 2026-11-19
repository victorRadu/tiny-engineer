# Proposal

## Why

`POST /play` streams any voice clip, but the robot can only pick one pose for the whole clip and, with `talking`, move from speech loudness. The stock clips get exact timing from their `.cue` files; streamed clips cannot. Users who record or generate their own speech want the same control: "raise the hand at 1.2 s, look left at 3 s, switch to thinking at 5 s".

## What Changes

- New sidecar cue file `<clip>.cues` next to `<clip>.wav`. One cue per line, `<ms>=<action> [key=value ...]`, `#` comments, same style as the stock `.cue` files. `ms` counts from the first audio sample.
- Three action kinds:
  - `move`: servo targets in −1..1 pose units for `head`, `neck`, `body`, `hand_left`, `hand_right`, with `ease` (travel time) and `hold` (time at the target before automatic motion takes the joint back).
  - `blink`, `look <left|right|up|down|center>` and `mood <name>`: eye and face cues. `look` and `mood` take an optional `hold`; `blink` takes no arguments.
  - `pose <name>`: switch the continuous animation in the middle of the clip (`talking`, `typing`, `reading`, `thinking`, `waiting`, `none`).
- A cue overrides only the joints (or the eye feature) it names. Automatic motion (speech-energy motion in `talking`, the pose's own motion in other poses) continues on all other joints, and takes a cued joint back when its `hold` ends.
- Firmware: the `/play` WAV parser reads a new optional RIFF chunk `tecu` (Tiny Engineer cues) that comes before the `data` chunk, and plays its cues against the audio clock. The same body works over HTTP `POST /play` and the serial `play` op.
- A bad `tecu` chunk rejects the request before any audio plays (HTTP **400**, serial error frame). Clips without the chunk behave exactly as today.
- CLI: `tiny-engineer play clip.wav` finds `clip.cues`, checks it, and embeds it as the `tecu` chunk (HTTP and serial). New flags `--cues <path>` and `--no-cues`.
- Docs: `docs/api.md` (`POST /play` body, errors), `docs/integration.md`, CLI README, `docs/robot-movement.md`.

No breaking change. Old firmware skips the unknown chunk and plays the clip without cues. The stock `.cue` files and their keys do not change.

## Capabilities

### New Capabilities
- `stream-cues`: timed motion, eye and pose cues for streamed `/play` audio — the cue line format, the `tecu` WAV chunk, how the firmware plays cues and hands joints back to automatic motion, error handling, and how the CLI finds and embeds the sidecar file.

### Modified Capabilities
<!-- None. serial-protocol already requires serial play to match POST /play; it gets cues through that requirement without a text change. -->

## Impact

- Firmware: `src/audio/wav_parser.*` (deliver the `tecu` chunk), new cue parser and cue player (`src/audio/stream_cues.*`, `src/animation/cue_player.*`), `src/hardware/servo_wrapper.*` (per-joint cue hold), `src/http/play_handlers.cpp`, `src/serial/play.cpp`, eye hooks in `src/display/eyes/` (blink, look, kawaii mood override).
- Native tests: new `test/test_stream_cues`, more cases in `test/test_wav_parser`; `platformio.ini` `[env:native]` source filter.
- CLI: `packages/tiny-engineer-cli` (`play` args, cue validation and chunk embed shared by the HTTP path and the daemon serial path in `src/daemon/link.js`), tests.
- API: additive. `/play` path, method and query do not change, so `ui/src/views/ApiReference.tsx`, `README.md` and `kRoutes` need no change. `docs/api.md` gets the body format and the new 400 errors.
- RAM: cues are parsed into a fixed table (64 cues max) during the request; no LittleFS use.
