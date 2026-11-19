// Checks a Tiny Engineer voice cue file with the same parser as the CLI and
// the firmware, warns about timing problems, and can write a WAV with the
// cues inside (a `tecu` chunk before `data`).
//
// Usage (through the `check` wrapper next to this file):
//   check <clip.cues>                          rules only
//   check <clip.cues> --wav <clip.wav>         rules + clip format + timing
//   check <clip.cues> --wav <clip.wav> --embed <out.wav>
//
// Exit 0 = valid (warnings may print), 1 = invalid or usage error.

import { readFileSync, writeFileSync } from "node:fs";

// cues.mjs is a byte-for-byte copy of packages/tiny-engineer-cli/src/robot/cues.js
// (a CLI test fails when they differ), so this skill works without the repo.
import { CUE_JOINTS, CueError, formatCue, parseCues, withCueChunk } from "./cues.mjs";

const args = process.argv.slice(2);
const cuesPath = args[0];
const wavPath = optionValue("--wav");
const embedPath = optionValue("--embed");

if (!cuesPath || cuesPath.startsWith("--") || (embedPath && !wavPath)) {
  console.error("usage: check <clip.cues> [--wav <clip.wav> [--embed <out.wav>]]");
  process.exit(1);
}

/** @param {string} name */
function optionValue(name) {
  const at = args.indexOf(name);
  return at >= 0 ? args[at + 1] : undefined;
}

const text = readFileSync(cuesPath);
let cues;
try {
  cues = parseCues(text);
} catch (err) {
  if (err instanceof CueError) {
    console.error(`INVALID ${cuesPath}:${err.line}: ${err.reason}`);
    process.exit(1);
  }
  throw err;
}

console.log(`OK ${cuesPath}: ${cues.length} cue${cues.length === 1 ? "" : "s"}, ${text.length} bytes`);
for (const cue of cues) console.log(`  ${formatCue(cue)}`);

/** @type {string[]} */
const warnings = [];

// A later move on the same joint replaces the earlier one while it still holds.
/** @type {Record<string, { ms: number, end: number }>} */
const busy = {};
for (const cue of cues) {
  if (cue.kind !== "move") continue;
  for (const joint of Object.keys(cue.joints ?? {})) {
    const prev = busy[joint];
    if (prev && prev.end !== Infinity && cue.ms < prev.end) {
      warnings.push(`${cue.ms} ms: move on ${joint} cuts the move from ${prev.ms} ms short (it still holds until ~${prev.end} ms)`);
    }
    busy[joint] = { ms: cue.ms, end: cue.hold === 0 ? Infinity : cue.ms + (cue.ease ?? 0) + (cue.hold ?? 0) };
  }
}
for (const joint of CUE_JOINTS) {
  if (busy[joint]?.end === Infinity) {
    warnings.push(`${joint} has hold=0 at ${busy[joint].ms} ms: it stays there until the clip ends`);
  }
}

if (wavPath) {
  const wav = readFileSync(wavPath);
  const info = wavInfo(wav);
  if (!info) {
    console.error(`INVALID ${wavPath}: not a WAV file with fmt and data chunks`);
    process.exit(1);
  }
  if (info.format !== 1 || info.channels !== 1 || info.bits !== 16 || info.rate !== 22050) {
    console.error(
      `INVALID ${wavPath}: ${info.bits}-bit, ${info.channels} channel(s), ${info.rate} Hz; the robot needs 16-bit mono PCM at 22050 Hz.\n` +
        `  Fix: ffmpeg -y -i ${wavPath} -ac 1 -ar 22050 -c:a pcm_s16le -map_metadata -1 -fflags +bitexact -flags:a +bitexact <out.wav>`,
    );
    process.exit(1);
  }
  const durationMs = Math.floor((info.dataBytes / 2 / info.rate) * 1000);
  console.log(`OK ${wavPath}: ${durationMs} ms`);
  for (const cue of cues) {
    if (cue.ms >= durationMs) warnings.push(`${cue.ms} ms: after the clip ends (${durationMs} ms); this cue never starts`);
    else if (cue.kind === "move" && cue.hold !== 0 && cue.ms + (cue.ease ?? 0) + (cue.hold ?? 0) > durationMs) {
      warnings.push(`${cue.ms} ms: move is cut by the clip end (${durationMs} ms); the joint goes back when the clip ends`);
    }
  }

  if (embedPath) {
    writeFileSync(embedPath, withCueChunk(wav, text));
    console.log(`WROTE ${embedPath} (WAV with the cues inside; play it with POST /play or the tiny-engineer-play skill)`);
  }
}

for (const warning of warnings) console.log(`WARN ${warning}`);

/**
 * @param {Buffer} wav
 * @returns {{ format: number, channels: number, rate: number, bits: number, dataBytes: number } | null}
 */
function wavInfo(wav) {
  if (wav.length < 12 || wav.toString("latin1", 0, 4) !== "RIFF" || wav.toString("latin1", 8, 12) !== "WAVE") return null;
  let offset = 12;
  /** @type {{ format: number, channels: number, rate: number, bits: number } | null} */
  let fmt = null;
  while (offset + 8 <= wav.length) {
    const id = wav.toString("latin1", offset, offset + 4);
    const size = wav.readUInt32LE(offset + 4);
    if (id === "fmt " && size >= 16) {
      let format = wav.readUInt16LE(offset + 8);
      if (format === 0xfffe && size >= 26) format = wav.readUInt16LE(offset + 32);
      fmt = {
        format,
        channels: wav.readUInt16LE(offset + 10),
        rate: wav.readUInt32LE(offset + 12),
        bits: wav.readUInt16LE(offset + 22),
      };
    }
    if (id === "data") {
      if (!fmt) return null;
      const available = wav.length - offset - 8;
      const dataBytes = size === 0 || size === 0xffffffff ? available : Math.min(size, available);
      return { ...fmt, dataBytes };
    }
    offset += 8 + size + (size % 2);
  }
  return null;
}
