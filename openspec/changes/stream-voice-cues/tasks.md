# Tasks

## 1. Cue grammar and shared fixture

- [x] 1.1 Add `test/fixtures/stream_cues.txt` (plain text: the native tests have no JSON library) with valid lists (expected cues in canonical form) and invalid lines (expected `bad cue line N: reason`) for every line rule in the `stream-cues` spec; the 64-cue and 8192-byte limits are built in code by each test suite; verify both test suites read it (2.1, 5.1)
- [x] 1.2 Make the kawaii mood names usable by the parser without Arduino: `kawaii::moodName()` in `expression.cpp` is already pure and in the native build, so the parser links it (no move needed); verify `pio test -e native` passes

## 2. Firmware cue parser

- [x] 2.1 Add `src/audio/stream_cues.{h,cpp}` (`StreamCueParser`, fixed 64-entry table, line buffer, errors per design D3/D4) and add it to the `[env:native]` source filter; verify a new `test/test_stream_cues` that runs every fixture case passes with `pio test -e native`
- [x] 2.2 Extend `WavStreamParser::Sink` with default no-op `onChunkBegin` (id, declared size) / `onChunkData` / `onChunkEnd` and forward `tecu` chunk bytes before `data`; skip `tecu` after `data`; the size limit is checked by the cue parser from the declared size (so it can answer 400, not 415); verify new cases in `test/test_wav_parser` (chunk delivered in slices with its size, chunk after data skipped, padding byte not forwarded, no chunk unchanged) pass with `pio test -e native`

## 3. Joint hold in the servo layer

- [x] 3.1 Add `cueMove`, `cueHoldUntil`, `cueRelease`, `cueHeld` and the shadow target to `ServoWrapper` per design D6 (animation calls recorded, not applied, while held; release applies the shadow target at its speed; ease → speed capped at max); verify `pio run` passes (the speed math lives in the Arduino-only wrapper, so no native case)
- [x] 3.2 End all cue holds in `apiParkServos()` and `i2cNoteServoDriverFault()` (now `servoCancelCues()`, which also blocks later cues in the clip, task 7.2); not in `anim::stopAnimServos()`, because a `pose` cue starts an animation that calls it and would cancel a `move` cue at the same time (clip end releases them, task 4.3); verify by grep of each call site and `pio run`

## 4. Cue player, eyes and play sessions

- [x] 4.1 Add `src/animation/cue_player.{h,cpp}` (begin/advance on audio ms/end; `move`, `pose` dispatch; release on end); verify `pio run`
- [x] 4.2 Add eye hooks: `blink` via `blinkScheduleSoon`, `look` offset override with hold in the eyes controller, kawaii mood override with hold and `auto`; kaomoji and non-kawaii styles ignore `mood`; verify `pio run` and `pio test -e native` (`test_kawaii`)
- [x] 4.3 Wire the parser and player into `PlaySession` (`src/http/play_handlers.cpp`) and `SerialPlaySession` (`src/serial/play.cpp`): parse `tecu`, reject a bad list with HTTP 400 / serial play error before any audio, advance cues in `onPcm` before `tickRobot()`, end cues before the previous animation is restored, feed `audio_motion` while the current animation is `talking` (design D8); shared session code in `src/animation/play_cues.*`; verify `pio run` and `pio run -e expression-demo`
- [x] 4.4 Update `docs/api.md` `POST /play` (body: optional `tecu` chunk, grammar summary, link to the cue section, new 400 rows in its error table and the global Errors table) and `docs/robot-movement.md` (cue moves use pose units; hold and hand-back); verify `ui/src/views/ApiReference.tsx` needs no change (path, method and query unchanged) and both docs use the exact grammar from the spec

## 5. CLI

- [x] 5.1 Add `packages/tiny-engineer-cli/src/robot/cues.js` (`parseCues`, `withCueChunk` per design D9); verify a new `test/cues.test.js` that runs every case in `test/fixtures/stream_cues.txt` plus chunk cases (insert before `data`, replace existing `tecu`, odd-length padding, RIFF size) passes with `npm test --prefix packages/tiny-engineer-cli`
- [x] 5.2 Add `--cues <path>` and `--no-cues` to `parsePlayArgs`, resolve `<clip>.cues`, validate before sending, embed on HTTP; ignore `<clip>.cue`; verify new cases in `test/play-cues.test.js` (args, posted body, `--no-cues`, bad file, stock `.cue` ignored), plus `npm test --prefix packages/tiny-engineer-cli`
- [x] 5.3 Pass the cue path through the daemon RPC (`rpc.js`, `server.js`, `link.js`) and embed with the same helper on serial; verify a case in `test/play-cues.test.js` that the daemon link streams bytes with the `tecu` chunk
- [x] 5.4 Document `.cues` files and the new flags in `packages/tiny-engineer-cli/README.md` and `docs/integration.md` (with one example file and the daemon restart note); verify the examples in `docs/api.md` and `docs/integration.md` pass `parseCues` (tests in `test/cues.test.js`)

## 6. Integration

- [x] 6.1 Run `pio run`, `pio run -e expression-demo`, `pio test -e native`, `npm test --prefix packages/tiny-engineer-cli`; compare the `pio run` RAM line with the value before the change (≤ ~3 KB more); verify all pass
- [x] 6.2 Run `openspec validate stream-voice-cues --strict`; verify it passes
- [x] 6.3 On the real robot (user test): play a speech clip with a `.cues` file over HTTP and over serial — big moves on every joint with hold and return, `look`, `blink`, `mood` (kawaii), `pose thinking` and `pose waiting` mid-clip and restore after; a bad cue list is rejected by the CLI and by the firmware (400, no sound, no motion); the same clip with `--no-cues` shows only normal talking.

## 7. Review feedback (fresh review agent, CodeRabbit CLI)

- [x] 7.1 Servo hold: always go back to the animation's last target (static pose joints too), count `hold` from arrival, step returning joints and turn direct position writes into stepped moves until the joint is back (no jump), restore the shadow speed unchanged, fresh step clock for a cue on a still joint; verify `pio run`
- [x] 7.2 Park and servo driver fault block cues for the rest of the clip (`servoCancelCues()`, also when turning the output on faults inside `cueMove`); next clip allows them again; verify `pio run`
- [x] 7.3 Parsers: allow 8 tokens (`move` + five joints + `ease` + `hold`), reject `1.0000001` and `-1.0000001`, print `-0` as `0.000`; CRLF case moved from the fixture into code; verify new fixture and code cases pass in `pio test -e native` and `npm test --prefix packages/tiny-engineer-cli`
- [x] 7.4 Docs and spec: `--no-cues` skips only the cue file (embedded cues still play); spec release rule counts `hold` from arrival; design notes on hold, return, cancel and the ~12 ms early-cue limit; verify `openspec validate stream-voice-cues --strict`
- [x] 7.6 Second CodeRabbit run: reset the servo step clock on cue start and release (no jump after a long hold), accept leading zeros in values (both parsers), accept `--cues` file names that start with `-`, fix the `blink` text in the proposal; verify `pio test -e native`, `npm test --prefix packages/tiny-engineer-cli`, `pio run`
- [x] 7.7 Third CodeRabbit run: start cues with the batch that holds their time (end-of-batch audio clock), so cues in the last partial batch start too; design text updated; verify `pio run` and `pio run -e expression-demo`
- [x] 7.8 Fourth CodeRabbit run: reject a NUL byte in a cue line (`bad character`, both parsers, shared fixture case, `docs/api.md`), keep the step clock of a moving joint when a cue retargets it, fix stale `servoReleaseAllCues()` text for park and fault in design and tasks; verify `pio test -e native`, `npm test --prefix packages/tiny-engineer-cli`, `pio run`
- [x] 7.9 Fifth CodeRabbit run: spec restore rule matches `docs/api.md` and the code (previous continuous animation, else `none`), new scenario; verify `openspec validate stream-voice-cues --strict`
- [x] 7.5 On the real robot (user test): in `waiting`, `move body=0 hold=500` returns the body to its turned pose; in `thinking`, a `move head=...` returns without a jump; `move` with all five joints works

## Workflow follow-up

- Open the pull request after review.
- Archive the change after the PR is merged.
