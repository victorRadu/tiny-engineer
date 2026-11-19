import { existsSync, readFileSync } from "node:fs";

// Voice cues for `tiny-engineer play`: a `<clip>.cues` file next to the WAV is
// checked here and sent as a RIFF `tecu` chunk before `data`. The rules and
// error texts match the firmware parser (src/audio/stream_cues.cpp); both test
// suites run the cases in test/fixtures/stream_cues.txt at the repo root.

const MAX_MS = 600000;
const MAX_EASE_MS = 10000;
const MAX_HOLD_MS = 60000;
const DEFAULT_MOVE_EASE_MS = 300;
const DEFAULT_MOVE_HOLD_MS = 500;
const DEFAULT_LOOK_HOLD_MS = 800;
const DEFAULT_MOOD_HOLD_MS = 0;

export const MAX_CUES = 64;
export const MAX_LINE_BYTES = 120;
export const MAX_CUE_BYTES = 8192;
export const CUE_CHUNK_ID = "tecu";

/** Servo index order (head, neck, hand_left, hand_right, body). */
export const CUE_JOINTS = ["head", "neck", "hand_left", "hand_right", "body"];
export const CUE_POSES = ["talking", "typing", "reading", "thinking", "waiting", "none"];
export const CUE_LOOKS = ["left", "right", "up", "down", "center"];
/** Kawaii mood names in firmware order (kawaii::Mood); `asleep` is not a cue mood. */
export const KAWAII_MOODS = [
  "content", "smile", "grin", "wink_left", "wink_right", "excited", "love",
  "focused", "reading", "thinking", "surprised", "curious", "angry", "panic",
  "sleepy", "sad", "dizzy", "talking", "asleep", "pleased", "aha", "suspense", "yell", "bliss", "afterglow",
];

/**
 * @typedef {{
 *   ms: number,
 *   kind: "move" | "blink" | "look" | "mood" | "pose",
 *   joints?: Record<string, number>,
 *   ease?: number,
 *   hold?: number,
 *   arg?: string,
 * }} Cue
 */

/** Error with the 1-based line in the cue list (0 for size and count limits). */
export class CueError extends Error {
  /**
   * @param {number} line
   * @param {string} reason
   */
  constructor(line, reason) {
    super(`bad cue line ${line}: ${reason}`);
    this.name = "CueError";
    this.line = line;
    this.reason = reason;
  }
}

/**
 * @param {string} text
 * @param {number} max
 */
function parseWhole(text, max) {
  if (!/^[0-9]+$/.test(text)) return null;
  const value = Number(text);
  return value <= max ? value : null;
}

/** -?(digits[.digits*] | .digits) within -1..1; like the firmware, only six fraction digits count for the value. */
function parseNorm(/** @type {string} */ text) {
  const match = /^(-?)([0-9]*)(?:\.([0-9]*))?$/.exec(text);
  if (!match) return null;
  const [, sign, whole, fraction = ""] = match;
  if (whole.length === 0 && fraction.length === 0) return null;
  // Range check on the exact text: 1.0000001 is over 1 even though only six
  // fraction digits count for the value.
  const wholeValue = Number(whole || "0");
  if (wholeValue > 1 || (wholeValue === 1 && /[1-9]/.test(fraction))) return null;
  let value = Number(`${whole || "0"}.${fraction.slice(0, 6) || "0"}`);
  if (sign) value = -value;
  if (value < -1 || value > 1) return null;
  return value;
}

/**
 * Parses a cue list. Throws CueError on the first broken rule.
 * @param {string | Uint8Array} input
 * @returns {Cue[]}
 */
export function parseCues(input) {
  const bytes = typeof input === "string" ? Buffer.from(input, "utf8") : Buffer.from(input);
  if (bytes.length > MAX_CUE_BYTES) throw new CueError(0, "cue chunk too big");

  /** @type {Cue[]} */
  const cues = [];
  let lastMs = 0;
  let lineNumber = 0;
  let start = 0;

  while (start < bytes.length) {
    let end = bytes.indexOf(0x0a, start);
    if (end < 0) end = bytes.length;
    let raw = bytes.subarray(start, end);
    start = end + 1;
    lineNumber++;

    if (raw.length > 0 && raw[raw.length - 1] === 0x0d) raw = raw.subarray(0, raw.length - 1);
    if (raw.length > MAX_LINE_BYTES) throw new CueError(lineNumber, "line too long");
    // Same rule as the firmware: a NUL would hide the rest of the line there.
    if (raw.includes(0)) throw new CueError(lineNumber, "bad character");

    const cue = parseLine(raw.toString("utf8"), lineNumber, cues.length > 0, lastMs, cues.length);
    if (cue) {
      cues.push(cue);
      lastMs = cue.ms;
    }
  }

  return cues;
}

/**
 * @param {string} text
 * @param {number} line
 * @param {boolean} haveCues
 * @param {number} lastMs
 * @param {number} count
 * @returns {Cue | null}
 */
function parseLine(text, line, haveCues, lastMs, count) {
  const hash = text.indexOf("#");
  if (hash >= 0) text = text.slice(0, hash);
  text = text.replace(/^[ \t]+/, "");
  if (text === "") return null;

  const equals = text.indexOf("=");
  if (equals < 0) throw new CueError(line, "expected <ms>=<action>");

  const ms = parseWhole(text.slice(0, equals).replace(/[ \t]+$/, ""), MAX_MS);
  if (ms === null) throw new CueError(line, "bad time");

  const tokens = text.slice(equals + 1).split(/[ \t]+/).filter((token) => token !== "");
  if (tokens.length === 0) throw new CueError(line, "expected <ms>=<action>");
  if (haveCues && ms < lastMs) throw new CueError(line, "time goes back");
  if (count === MAX_CUES) throw new CueError(0, "too many cues");
  // Longest valid line: move + five joints + ease + hold = 8 tokens.
  if (tokens.length > 8) throw new CueError(line, "extra argument");

  const action = tokens[0];
  /** @type {Cue} */
  let cue;
  let next = 1;
  let allowEase = false;
  let allowHold = false;

  if (action === "move") {
    cue = { ms, kind: "move", joints: {}, ease: DEFAULT_MOVE_EASE_MS, hold: DEFAULT_MOVE_HOLD_MS };
    allowEase = true;
    allowHold = true;
  } else if (action === "blink") {
    cue = { ms, kind: "blink" };
  } else if (action === "look" || action === "mood" || action === "pose") {
    if (tokens.length < 2 || tokens[1].includes("=")) throw new CueError(line, "missing argument");
    const arg = tokens[1];
    next = 2;
    if (action === "look") {
      if (!CUE_LOOKS.includes(arg)) throw new CueError(line, "unknown direction");
      cue = { ms, kind: "look", arg, hold: DEFAULT_LOOK_HOLD_MS };
      allowHold = true;
    } else if (action === "mood") {
      if (arg !== "auto" && (arg === "asleep" || !KAWAII_MOODS.includes(arg))) {
        throw new CueError(line, "unknown mood");
      }
      cue = { ms, kind: "mood", arg, hold: DEFAULT_MOOD_HOLD_MS };
      allowHold = true;
    } else {
      if (!CUE_POSES.includes(arg)) throw new CueError(line, "unknown pose");
      cue = { ms, kind: "pose", arg };
    }
  } else {
    throw new CueError(line, "unknown action");
  }

  const keyed = cue.kind === "move" || cue.kind === "look" || cue.kind === "mood";
  let easeSeen = false;
  let holdSeen = false;

  for (const token of tokens.slice(next)) {
    const eq = token.indexOf("=");
    if (eq < 0) throw new CueError(line, "extra argument");
    const key = token.slice(0, eq);
    const value = token.slice(eq + 1);

    if (key === "ease" || key === "hold") {
      const ease = key === "ease";
      if (ease ? !allowEase : !allowHold) throw new CueError(line, keyed ? "unknown key" : "extra argument");
      if (ease ? easeSeen : holdSeen) throw new CueError(line, "repeated key");
      const parsed = parseWhole(value, ease ? MAX_EASE_MS : MAX_HOLD_MS);
      if (parsed === null) throw new CueError(line, ease ? "bad ease" : "bad hold");
      if (ease) {
        easeSeen = true;
        cue.ease = parsed;
      } else {
        holdSeen = true;
        cue.hold = parsed;
      }
      continue;
    }

    if (cue.kind !== "move") throw new CueError(line, keyed ? "unknown key" : "extra argument");
    if (!CUE_JOINTS.includes(key)) throw new CueError(line, "unknown joint");
    const joints = /** @type {Record<string, number>} */ (cue.joints);
    if (key in joints) throw new CueError(line, "repeated joint");
    const norm = parseNorm(value);
    if (norm === null) throw new CueError(line, "bad value");
    joints[key] = norm;
  }

  if (cue.kind === "move" && Object.keys(/** @type {Record<string, number>} */ (cue.joints)).length === 0) {
    throw new CueError(line, "move needs a joint");
  }

  return cue;
}

/**
 * Canonical one-line form, same as the firmware's StreamCueParser::format().
 * @param {Cue} cue
 */
export function formatCue(cue) {
  switch (cue.kind) {
    case "move": {
      const joints = /** @type {Record<string, number>} */ (cue.joints);
      const parts = CUE_JOINTS.filter((name) => name in joints).map((name) => `${name}=${joints[name].toFixed(3)}`);
      return `${cue.ms} move ${parts.join(" ")} ease=${cue.ease} hold=${cue.hold}`;
    }
    case "blink":
      return `${cue.ms} blink`;
    case "pose":
      return `${cue.ms} pose ${cue.arg}`;
    default:
      return `${cue.ms} ${cue.kind} ${cue.arg} hold=${cue.hold}`;
  }
}

/**
 * Returns a copy of `wav` with `cueText` as the `tecu` chunk right before
 * `data`. Any `tecu` chunk already before `data` is dropped. Bytes from
 * `data` on are copied unchanged. Throws when the WAV has no RIFF header or
 * no `data` chunk.
 * @param {Uint8Array} wav
 * @param {string | Uint8Array} cueText
 * @returns {Buffer}
 */
export function withCueChunk(wav, cueText) {
  const input = Buffer.from(wav.buffer, wav.byteOffset, wav.byteLength);
  if (
    input.length < 12 ||
    input.toString("latin1", 0, 4) !== "RIFF" ||
    input.toString("latin1", 8, 12) !== "WAVE"
  ) {
    throw new Error("not a WAV file");
  }

  const cue = typeof cueText === "string" ? Buffer.from(cueText, "utf8") : Buffer.from(cueText);
  /** @type {Buffer[]} */
  const parts = [input.subarray(0, 12)];
  let offset = 12;

  while (offset + 8 <= input.length) {
    const id = input.toString("latin1", offset, offset + 4);
    if (id === "data") {
      const header = Buffer.alloc(8);
      header.write(CUE_CHUNK_ID, 0, "latin1");
      header.writeUInt32LE(cue.length, 4);
      parts.push(header, cue);
      if (cue.length % 2 === 1) parts.push(Buffer.alloc(1));
      parts.push(input.subarray(offset));
      const out = Buffer.concat(parts);
      out.writeUInt32LE(out.length - 8, 4);
      return out;
    }

    const size = input.readUInt32LE(offset + 4);
    const next = offset + 8 + size + (size % 2);
    if (id !== CUE_CHUNK_ID) parts.push(input.subarray(offset, Math.min(next, input.length)));
    offset = next;
  }

  throw new Error("WAV has no data chunk");
}

/**
 * Finds and checks the cue file for `wavPath`: `cuesPath` when given, else
 * `<clip>.cues` next to the WAV when it exists. The stock `<clip>.cue`
 * phrase-mark files are never used. Returns null when there is no cue file
 * or `noCues` is set. Throws an Error whose message names the file (and the
 * line, for a bad cue list).
 * @param {string} wavPath
 * @param {{ cuesPath?: string, noCues?: boolean, readFile?: (path: string) => Buffer, exists?: (path: string) => boolean }} [opts]
 * @returns {{ path: string, text: Buffer, count: number } | null}
 */
export function loadCueFile(wavPath, opts = {}) {
  if (opts.noCues) return null;
  const readFile = opts.readFile ?? ((/** @type {string} */ path) => readFileSync(path));
  const exists = opts.exists ?? existsSync;

  let path = opts.cuesPath;
  if (!path) {
    const sidecar = sidecarCuesPath(wavPath);
    if (!exists(sidecar)) return null;
    path = sidecar;
  }

  let text;
  try {
    text = readFile(path);
  } catch (err) {
    throw new Error(`cannot read ${path}: ${err instanceof Error ? err.message : String(err)}`);
  }

  try {
    return { path, text, count: parseCues(text).length };
  } catch (err) {
    if (err instanceof CueError) throw new Error(`${path}:${err.line}: ${err.reason}`);
    throw err;
  }
}

/**
 * `clip.wav` → `clip.cues`; a name without `.wav` gets `.cues` appended.
 * @param {string} wavPath
 */
export function sidecarCuesPath(wavPath) {
  return /\.wav$/i.test(wavPath) ? wavPath.replace(/\.wav$/i, ".cues") : `${wavPath}.cues`;
}
