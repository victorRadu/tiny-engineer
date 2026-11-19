# Spec Delta

## Purpose

Lets a streamed `/play` voice clip carry timed motion, eye and pose cues, so custom speech gets the same exact gesture timing that the stock clips get from their `.cue` files.

## ADDED Requirements

### Requirement: Cue line format
A cue list SHALL be UTF-8 text, one cue per line. Text after `#` SHALL be a comment; blank lines SHALL be ignored. Each cue line SHALL be `<ms>=<action> [key=value ...]`, where `<ms>` is a whole number 0..600000 that counts milliseconds from the first audio sample. Tokens SHALL be separated by spaces or tabs. Times SHALL NOT decrease from one cue line to the next; equal times SHALL be allowed.

#### Scenario: Valid list with comments
- **WHEN** the list is `# hello\n0=pose talking\n1200=move hand_right=0.6 ease=250\n1200=blink`
- **THEN** the list is accepted with three cues, two of them at 1200 ms

#### Scenario: Decreasing time
- **WHEN** a cue line at 900 ms follows a cue line at 1200 ms
- **THEN** the list is rejected with the line number of the 900 ms line

#### Scenario: Bad time
- **WHEN** a line starts with `-5=blink`, `1.5=blink` or `700000=blink`
- **THEN** the list is rejected with that line number

### Requirement: Move action
`move` SHALL take one or more `<joint>=<value>` pairs, where joint is `head`, `neck`, `body`, `hand_left` or `hand_right` and value is a number −1..1 in pose units (mapped to the saved servo min/max). Optional `ease=<ms>` (0..10000, default 300) SHALL set the travel time; optional `hold=<ms>` (0..60000, default 500) SHALL set how long the joint stays at the target after it arrives.

#### Scenario: Two joints move together
- **WHEN** the cue `1000=move head=0.4 neck=-0.2 ease=300` plays
- **THEN** head and neck start at 1000 ms and both reach their targets about 300 ms later

#### Scenario: Value out of range
- **WHEN** a cue line is `500=move head=1.4`
- **THEN** the list is rejected with that line number

#### Scenario: Unknown or repeated joint
- **WHEN** a cue line names `arm=0.2` or names `head` twice
- **THEN** the list is rejected with that line number

### Requirement: Cued joint returns to automatic motion
A `move` cue SHALL control only the joints it names, until each joint has arrived and `hold` has passed since arrival. Meanwhile the running animation SHALL NOT move those joints. Then each joint SHALL go back to the target the animation asked for last, without a jump, and automatic motion SHALL continue on it. A later `move` on the same joint SHALL replace the earlier one. `hold=0` SHALL keep the joint until a later `move` names it or the clip ends.

#### Scenario: Hand raised then released
- **WHEN** the pose is `talking` and `2000=move hand_right=0.7 ease=200 hold=600` plays
- **THEN** the right hand rises, stays raised until about 2800 ms, and then speech-energy motion moves it again
- **AND** head, neck and body keep their speech-energy motion during the whole cue

#### Scenario: Static pose joint restored
- **WHEN** the pose is `waiting` (body turned to `+0.8`) and `1000=move body=0 hold=500` plays
- **THEN** after the hold the body goes back to `+0.8`

#### Scenario: Hold until clip end
- **WHEN** `500=move head=0.3 hold=0` plays and no later cue names `head`
- **THEN** the head stays at 0.3 until the clip ends

### Requirement: Eye and face actions
`blink` SHALL start one blink. `look <left|right|up|down|center>` SHALL turn the eyes that way; optional `hold=<ms>` (0..60000, default 800) SHALL set how long. `mood <name>` SHALL set the kawaii face mood to a kawaii mood name; `mood auto` SHALL give the mood back to the animation; optional `hold=<ms>` (default 0, meaning until a later `mood` cue or clip end). Eye styles that cannot show an action SHALL ignore it.

#### Scenario: Look left for a while
- **WHEN** the eye style is `classic` and `3000=look left hold=700` plays
- **THEN** the eyes look left from 3000 ms to about 3700 ms and then follow the animation again

#### Scenario: Mood on kawaii
- **WHEN** the eye style is `kawaii` and `1500=mood grin` plays
- **THEN** the face shows the grin mood until a later `mood` cue or the clip end

#### Scenario: Mood on a style without moods
- **WHEN** the eye style is `cover` and a `mood surprised` cue plays
- **THEN** the cue has no effect and the clip continues

#### Scenario: Unknown mood or direction
- **WHEN** a cue line is `100=mood furious` or `100=look behind`
- **THEN** the list is rejected with that line number

### Requirement: Pose switch action
`pose <name>` SHALL switch the running animation to `talking`, `typing`, `reading`, `thinking`, `waiting` or `none` at the cue time. Speech-energy motion SHALL apply while the running animation is `talking`. When the clip ends, the robot SHALL go back to the animation that ran before the request if it was continuous (`talking`, `typing`, `reading`, `thinking`, `waiting`), otherwise to `none`, as it does without cues.

#### Scenario: Talk then think
- **WHEN** a clip plays on `/play` and `4000=pose thinking` plays
- **THEN** the robot talks with speech-energy motion until 4000 ms and runs `thinking` after that

#### Scenario: Restore after clip
- **WHEN** `typing` ran before the request and the clip cues `pose thinking`
- **THEN** after the clip ends the robot runs `typing`

#### Scenario: Restore after a one-shot animation
- **WHEN** `welcome` ran before the request and the clip cues `pose thinking`
- **THEN** after the clip ends the robot runs `none`

#### Scenario: Pose with its own sound
- **WHEN** a cue line is `100=pose welcome`
- **THEN** the list is rejected with that line number

### Requirement: Cue chunk in the WAV body
The firmware SHALL read cues from an optional RIFF chunk with ID `tecu` that holds the cue list text. The chunk SHALL come before the `data` chunk; a `tecu` chunk after `data` SHALL be ignored. The chunk SHALL be at most 8192 bytes, each line at most 120 bytes, and the list at most 64 cues. A body with no `tecu` chunk SHALL play exactly as before this change. This SHALL apply to `POST /play`, `POST /play/{name}` and the serial `play` op.

#### Scenario: Same body over HTTP and serial
- **WHEN** the same WAV with a `tecu` chunk is sent once with `POST /play` and once with serial `play`
- **THEN** both play the same cues at the same audio times

#### Scenario: No chunk
- **WHEN** a WAV with only `fmt ` and `data` chunks is posted
- **THEN** playback and motion are the same as before this change

#### Scenario: Too many cues
- **WHEN** the `tecu` chunk holds 65 cues
- **THEN** the request is rejected

### Requirement: Cues follow the audio clock
Each cue SHALL start when the number of audio samples handed to the speaker reaches its `<ms>`, not by wall-clock time from the request. Cues whose time is after the last sample SHALL NOT start. When the clip ends, all cue holds (joints, look, mood) SHALL end.

#### Scenario: Slow network
- **WHEN** the body arrives slowly and the speaker waits for data at 1000 ms of audio
- **THEN** a cue at 1500 ms starts when 1500 ms of audio has played, not 1500 ms after the request

#### Scenario: Cue after the end
- **WHEN** the clip is 2000 ms long and has a cue at 2500 ms
- **THEN** that cue never starts and the clip ends normally

### Requirement: Bad cue list rejects the request before audio
A `tecu` chunk that breaks any rule above SHALL fail the request before any audio plays and before the robot moves. HTTP SHALL answer **400** `{"ok":false,"error":"bad cue line <n>: <reason>"}` (`<n>` = 1-based line in the chunk, or `0` for size and count limits). Serial SHALL send the same text as the play error.

#### Scenario: Unknown action over HTTP
- **WHEN** a `POST /play` body has a `tecu` chunk whose line 3 is `100=dance`
- **THEN** the answer is 400 with error `bad cue line 3: unknown action`
- **AND** no audio plays and no servo moves

### Requirement: CLI finds and embeds the sidecar cue file
`tiny-engineer play <clip.wav>` SHALL look for `<clip>.cues` (same folder and base name). When it exists, the CLI SHALL check it with the rules above and send the WAV with the list in a `tecu` chunk placed before `data`, on HTTP and on serial. `--cues <path>` SHALL use that file instead; `--no-cues` SHALL send the WAV unchanged. A bad cue file SHALL stop the command with the line number and reason, and nothing SHALL be sent. A WAV that already has a `tecu` chunk SHALL have it replaced.

#### Scenario: Sidecar found
- **WHEN** `hello.wav` and `hello.cues` are in the same folder and the user runs `tiny-engineer play hello.wav`
- **THEN** the robot gets the clip with the cues from `hello.cues`

#### Scenario: Bad sidecar
- **WHEN** `hello.cues` line 2 is `abc=blink`
- **THEN** the command exits with code 1, prints `hello.cues:2` and the reason, and sends nothing

#### Scenario: Opt out
- **WHEN** the user runs `tiny-engineer play hello.wav --no-cues`
- **THEN** the robot gets the WAV bytes unchanged

#### Scenario: Stock cue file is not used
- **WHEN** `welcome.wav` and `welcome.cue` (stock phrase marks) are in the same folder and no `welcome.cues` exists
- **THEN** the CLI sends the WAV without cues
