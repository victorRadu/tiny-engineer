# Voice cue files: time gestures to a clip

A cue file is a plain text list of timed actions for one WAV clip. The clip starts in the `talking` pose (or the pose given with `tiny-engineer play --name <pose>`: `typing`, `reading`, `thinking`, `waiting`, `none`). `tiny-engineer play clip.wav` sends `clip.cues` (same folder, same base name) with the audio. The robot runs each action when the audio reaches its time. Without a cue file, the robot only nods and sways with the loudness of the speech.

Follow the steps in order. Do not skip step 4.

## 1. Get the clip in the right format

The robot plays only **16-bit mono PCM at 22050 Hz**. Check the clip first:

```bash
ffprobe -v error -show_entries stream=codec_name,channels,sample_rate -of default=nw=1 input.wav
```

`codec_name=pcm_s16le`, `channels=1` and `sample_rate=22050` mean the clip is ready: use it as it is. Otherwise convert it to a **new** file (never write over the source):

```bash
ffmpeg -y -i input.wav -ac 1 -ar 22050 -c:a pcm_s16le -map_metadata -1 -fflags +bitexact -flags:a +bitexact clip.wav
```

The cue file and the clip you play must have the same base name in the same folder (`clip.wav` + `clip.cues`).

## 2. Find when the words are

```bash
<this skill's base directory>/scripts/marks clip.wav
```

It prints `duration_ms=...` and one line per speech part, for example `speech 2: 1166-4534 ms`. Time each cue to the start of a speech part or a word in it. If parts merge or split badly, change the threshold: `marks clip.wav -30 0.2`.

- **You have the text:** match the speech parts to the sentences in order, and put each gesture on the sentence it belongs to.
- **You do not have the text:** plan by position. The first part or two is the opening (greeting), the last part or two is the ending, the parts between are the middle. Long parts (2 s or more) are good places for a `pose` switch; short parts are good for a single `move` or `blink`. Tell the user that the cues follow the speech timing, not the words.

## 3. Write the file

Name it like the clip, with `.cues` (not `.cue`): `clip.wav` → `clip.cues`. The `.cue` files next to the stock clips are a different format and are never sent.

### Line format

```text
<ms>=<action> [argument] [key=value ...]   # comment
```

- `<ms>`: whole number 0..600000, milliseconds from the first audio sample. No decimals, no sign.
- One cue per line. Times must **not go down** from one line to the next. Equal times are fine (actions at the same moment).
- `#` starts a comment. Blank lines are fine.
- Tokens are separated by spaces. Never put spaces inside `key=value` (`head=0.5`, not `head = 0.5`).
- Limits: 64 cues, 120 bytes per line, 8192 bytes per file, at most 8 tokens after `=`.

### Actions

| Action | Form | Defaults | Effect |
| --- | --- | --- | --- |
| `move` | `move <joint>=<value> ... [ease=<ms>] [hold=<ms>]` | `ease=300`, `hold=500` | Moves the named joints. `ease` (0..10000) is the travel time; `hold` (0..60000) is the time at the target after it arrives. Then the running animation takes the joint back smoothly |
| `blink` | `blink` | — | One blink. Takes no arguments |
| `look` | `look <left\|right\|up\|down\|center> [hold=<ms>]` | `hold=800` | Eyes look that way on the screen |
| `mood` | `mood <name\|auto> [hold=<ms>]` | `hold=0` | Face mood (kawaii eye style only). `auto` gives the mood back to the animation |
| `pose` | `pose <talking\|typing\|reading\|thinking\|waiting\|none>` | — | Switches the running animation. Takes no other arguments |

`hold=0` means "keep it until a later cue for the same joint (or a later `look` / `mood`) or the clip end". When a `look` or `mood` hold ends, the eyes and the face go back to what the running animation shows (the same as `mood auto`). When a `move` hold ends, the joint goes back to the animation's motion.

### Joints and directions

Values are −1..1, mapped to the robot's calibrated range: `-1` = one end, `0` = middle, `1` = other end. Values outside −1..1 are an error.

| Joint | `-1` | `1` |
| --- | --- | --- |
| `head` | face down | face up |
| `neck` | head turns to the robot's right | head turns to the robot's left |
| `body` | torso turns to the robot's right | torso turns to the robot's left |
| `hand_left` | **up** | down (rest on the keyboard) |
| `hand_right` | down (rest on the keyboard) | **up** |

The two hands use **opposite** signs: both hands up is `hand_left=-1 hand_right=1`.

### Moods (kawaii eyes)

`content`, `smile`, `grin`, `wink_left`, `wink_right`, `excited`, `love`, `focused`, `reading`, `thinking`, `surprised`, `curious`, `angry`, `panic`, `sleepy`, `sad`, `dizzy`, `talking`, `pleased`, `aha`, `suspense`, `yell`, `bliss`, `afterglow`, plus `auto`. `asleep` is not allowed. Other eye styles ignore `mood`; kaomoji also ignores `look`. The robot's style is `eyes_style` in `GET /settings` (`classic` by default). If you do not know it, ask the user, or write the file so it works without moods: give every important moment a `move` or `pose` too, and use `mood` only as extra. The mouth keeps following the speech in any mood while the pose is `talking`.

### Make it visible

Normal talking already nods, drifts and lifts the right hand. Small cues look the same as that motion. To make a cue clearly visible:

- Use big values: at least ±0.6, often ±1.
- Hold for 1000–3000 ms. A hold under ~500 ms is easy to miss.
- On one joint, leave at least ~1 s between the end of one move's hold and the start of the next big move on that joint. Moves on **different** joints can start together or overlap: that reads as one bigger gesture.
- Leave about 1.5 s between the starts of separate gestures, so each one is seen on its own.
- Use `pose` switches for longer sections (3 s or more). `thinking` and `waiting` look very different from `talking`.
- Use `ease` 300–600 ms for a natural move. `ease=0` is the servo's top speed, not a jump.
- Put a gesture on the start of the word it belongs to, or 100–200 ms before it: servos need time to move.

### Rules that prevent surprises

- A later `move` on a joint replaces an earlier one that still holds. Plan one move per joint at a time.
- After a `pose` switch, the new animation needs ~0.5–1 s to settle. Do not put a `move` on the same moment unless you mean it.
- Cues after the clip end never start. A hold that runs past the clip end is cut at the end.
- `hold=0` on a hand at the end of a clip is a good "finale" (the hand stays up until the sound stops).
- When the clip ends, the robot goes back to its animation from before the clip (if that was `talking`, `typing`, `reading`, `thinking` or `waiting`), else to `none`.

### Example

`marks` gave: `speech 1: 200-1400 ms` ("Good morning!"), `speech 2: 2000-4600 ms` ("Let me think about today's plan..."), `speech 3: 5300-7800 ms` ("Got it, let's ship it!").

```text
# morning.cues
0=mood grin
150=move hand_right=1 ease=300 hold=1000       # wave on "Good morning"
2000=pose thinking                              # "Let me think..."
2000=mood thinking
3200=look up hold=1200
5200=pose talking                               # "Got it..."
5200=mood excited hold=2500
5300=move hand_left=-1 hand_right=1 ease=300 hold=0   # both hands up to the end
6500=blink
```

## 4. Check the file

```bash
<this skill's base directory>/scripts/check clip.cues --wav clip.wav
```

- `OK ...` and the cue list in canonical form: the file is valid.
- `INVALID clip.cues:<line>: <reason>`: fix that line (see the table below) and run it again.
- `WARN ...`: valid, but probably not what you meant: a cue after the clip end, a move cut short by the clip end or by a later move on the same joint, or a joint with `hold=0` (it stays there until the clip ends). Fix it, or tell the user why it is fine. `check` does not warn about `mood` or `look` with `hold=0`.

`check` needs Node 18 or newer. `tiny-engineer play` also refuses a bad file with `clip.cues:<line>: <reason>` before it sends anything.

## 5. Play it

```bash
tiny-engineer play clip.wav                 # sends clip.cues automatically
tiny-engineer play clip.wav --cues other.cues
tiny-engineer play clip.wav --no-cues       # to compare with normal talking
```

Use a CLI that knows cues (from the repo: `node packages/tiny-engineer-cli/bin/tiny-engineer.js`). The output says `played clip.wav (talking, N cues)`. If it shows no cue count, the cue file was not found: check the name and folder.

Without that CLI, write the cues into the WAV and play it with `curl` (or `scripts/play` from section 2 of `SKILL.md`):

```bash
<this skill's base directory>/scripts/check clip.cues --wav clip.wav --embed clip-cued.wav
curl -X POST http://tiny-engineer.local/play -H 'Content-Type: audio/wav' --data-binary @clip-cued.wav
```

## Errors

| Reason | Cause and fix |
| --- | --- |
| `bad time` | Time is not a whole number 0..600000 (`1.5=`, `-5=`, `abc=`) |
| `expected <ms>=<action>` | No `=` after the time, or nothing after it |
| `time goes back` | This line's time is lower than the line before. Sort the lines by time |
| `unknown action` | Not `move`, `blink`, `look`, `mood` or `pose` |
| `unknown joint` | Not `head`, `neck`, `body`, `hand_left`, `hand_right` |
| `repeated joint` / `repeated key` | Same joint or `ease`/`hold` twice on one line |
| `bad value` | Joint value not in −1..1, or not a plain decimal (no `1e-1`, no `+`) |
| `bad ease` / `bad hold` | Not a whole number in 0..10000 / 0..60000 |
| `move needs a joint` | `move` with only `ease`/`hold` |
| `missing argument` | `look`, `mood` or `pose` without its name |
| `extra argument` | Arguments on `blink` or `pose`, a word without `=`, or more than 8 tokens |
| `unknown key` | A key other than `hold` on `look`/`mood` (for example `ease`) |
| `unknown direction` / `unknown mood` / `unknown pose` | Name not in the lists above (`asleep` mood and `welcome` pose are not allowed) |
| `bad character` | A NUL byte in the line |
| `line too long` / `too many cues` / `cue chunk too big` | Over 120 bytes per line, 64 cues, or 8192 bytes |
| HTTP `415 expected 16-bit mono PCM at 22050 Hz` | The clip format is wrong: redo step 1 |

Full reference: `docs/api.md`, section "Voice cues".
