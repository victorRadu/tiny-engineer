import test from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { run } from "../src/cli.js";
import { createSerialLink } from "../src/daemon/link.js";
import { loadCueFile, sidecarCuesPath } from "../src/robot/cues.js";
import { parsePlayArgs } from "../src/util/args.js";

/** Minimal valid WAV: fmt + 4 bytes of PCM. */
function tinyWav() {
  const fmt = Buffer.alloc(16);
  fmt.writeUInt16LE(1, 0);
  fmt.writeUInt16LE(1, 2);
  fmt.writeUInt32LE(22050, 4);
  fmt.writeUInt32LE(44100, 8);
  fmt.writeUInt16LE(2, 12);
  fmt.writeUInt16LE(16, 14);
  const parts = [
    Buffer.from("RIFF\0\0\0\0WAVE", "latin1"),
    Buffer.from("fmt \x10\0\0\0", "latin1"),
    fmt,
    Buffer.from("data\x04\0\0\0", "latin1"),
    Buffer.from([1, 0, 2, 0]),
  ];
  const out = Buffer.concat(parts);
  out.writeUInt32LE(out.length - 8, 4);
  return out;
}

/** @param {Buffer} body */
function cueText(body) {
  const at = body.indexOf("tecu", 12, "latin1");
  if (at < 0) return null;
  const size = body.readUInt32LE(at + 4);
  return body.toString("utf8", at + 8, at + 8 + size);
}

function tempDir() {
  return mkdtempSync(join(tmpdir(), "te-cues-"));
}

/** @type {typeof fetch | undefined} */
let originalFetch;
/** @type {{ log: typeof console.log, error: typeof console.error } | undefined} */
let originalConsole;
/** @type {Buffer[]} */
let posted = [];

test.beforeEach(() => {
  originalFetch = globalThis.fetch;
  originalConsole = { log: console.log, error: console.error };
  console.log = () => {};
  console.error = () => {};
  posted = [];
  globalThis.fetch = /** @type {typeof fetch} */ (
    async (_url, init) => {
      posted.push(Buffer.from(/** @type {Uint8Array} */ (init?.body)));
      return /** @type {Response} */ ({ ok: true, status: 200, text: async () => '{"ok":true,"played_ms":1}' });
    }
  );
  process.exitCode = 0;
});

test.afterEach(() => {
  globalThis.fetch = /** @type {typeof fetch} */ (originalFetch);
  if (originalConsole) {
    console.log = originalConsole.log;
    console.error = originalConsole.error;
  }
  process.exitCode = 0;
});

test("parsePlayArgs reads --cues and --no-cues", () => {
  assert.equal(parsePlayArgs(["a.wav", "--cues", "x.cues"]).cues, "x.cues");
  assert.equal(parsePlayArgs(["a.wav", "--cues=y.cues"]).cues, "y.cues");
  assert.equal(parsePlayArgs(["a.wav", "--no-cues"]).noCues, true);
  assert.equal(parsePlayArgs(["a.wav", "--cues"]).error, "--cues requires a value");
  assert.equal(parsePlayArgs(["a.wav", "--cues", "-x.cues"]).cues, "-x.cues");
  assert.equal(parsePlayArgs(["a.wav", "--cues", "--no-cues"]).error, "--cues requires a value");
  assert.equal(parsePlayArgs(["a.wav", "--cues", "x", "--no-cues"]).error, "use --cues or --no-cues, not both");
});

test("sidecar path swaps .wav for .cues", () => {
  assert.equal(sidecarCuesPath("/a/hello.wav"), "/a/hello.cues");
  assert.equal(sidecarCuesPath("/a/HELLO.WAV"), "/a/HELLO.cues");
  assert.equal(sidecarCuesPath("/a/clip"), "/a/clip.cues");
});

test("play sends the sidecar cues in a tecu chunk over HTTP", async () => {
  const dir = tempDir();
  try {
    writeFileSync(join(dir, "hello.wav"), tinyWav());
    writeFileSync(join(dir, "hello.cues"), "0=blink\n300=move hand_right=0.7\n");
    await run(["play", join(dir, "hello.wav"), "--url", "http://robot.test", "--transport", "http"]);
    assert.equal(process.exitCode, 0);
    assert.equal(posted.length, 1);
    assert.equal(cueText(posted[0]), "0=blink\n300=move hand_right=0.7\n");
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("--no-cues sends the WAV bytes unchanged", async () => {
  const dir = tempDir();
  try {
    writeFileSync(join(dir, "hello.wav"), tinyWav());
    writeFileSync(join(dir, "hello.cues"), "0=blink\n");
    await run(["play", join(dir, "hello.wav"), "--no-cues", "--url", "http://robot.test", "--transport", "http"]);
    assert.equal(process.exitCode, 0);
    assert.deepEqual(posted[0], tinyWav());
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("a bad cue file stops play before anything is sent", async () => {
  const dir = tempDir();
  /** @type {string[]} */
  const errors = [];
  console.error = (/** @type {unknown[]} */ ...args) => errors.push(args.join(" "));
  try {
    writeFileSync(join(dir, "hello.wav"), tinyWav());
    writeFileSync(join(dir, "hello.cues"), "0=blink\nabc=blink\n");
    await run(["play", join(dir, "hello.wav"), "--url", "http://robot.test", "--transport", "http"]);
    assert.equal(process.exitCode, 1);
    assert.equal(posted.length, 0);
    assert.ok(errors.some((line) => line.includes("hello.cues:2") && line.includes("bad time")), errors.join("\n"));
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("a stock .cue phrase-mark file next to the clip is not used", async () => {
  const dir = tempDir();
  try {
    writeFileSync(join(dir, "welcome.wav"), tinyWav());
    writeFileSync(join(dir, "welcome.cue"), "greeting_end_ms=840\n");
    await run(["play", join(dir, "welcome.wav"), "--url", "http://robot.test", "--transport", "http"]);
    assert.equal(process.exitCode, 0);
    assert.deepEqual(posted[0], tinyWav());
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("--cues names a missing file: error, nothing sent", async () => {
  const dir = tempDir();
  try {
    writeFileSync(join(dir, "hello.wav"), tinyWav());
    await run(["play", join(dir, "hello.wav"), "--cues", join(dir, "nope.cues"), "--url", "http://robot.test", "--transport", "http"]);
    assert.equal(process.exitCode, 1);
    assert.equal(posted.length, 0);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("loadCueFile counts cues and returns null without a sidecar", () => {
  const dir = tempDir();
  try {
    writeFileSync(join(dir, "a.wav"), tinyWav());
    assert.equal(loadCueFile(join(dir, "a.wav")), null);
    writeFileSync(join(dir, "a.cues"), "0=blink\n10=blink\n");
    assert.equal(loadCueFile(join(dir, "a.wav"))?.count, 2);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("daemon serial link embeds the cue file in the streamed WAV", async () => {
  const dir = tempDir();
  /** @type {Buffer[]} */
  const streamed = [];
  try {
    writeFileSync(join(dir, "hello.wav"), tinyWav());
    writeFileSync(join(dir, "hello.cues"), "5=look left\n");
    const link = createSerialLink({
      connect: async ({ port }) => ({
        port: port || "/dev/a",
        client: /** @type {any} */ ({
          request: async () => ({ ok: true }),
          play: async (/** @type {Buffer} */ wav) => {
            streamed.push(Buffer.from(wav));
            return { ok: true, played_ms: 1 };
          },
          setEventHandler() {},
          setLogHandler() {},
          setCloseHandler() {},
          close: async () => {},
        }),
      }),
    });
    await link.play(join(dir, "hello.wav"), "talking", null, join(dir, "hello.cues"));
    await link.play(join(dir, "hello.wav"), "talking", null);
    assert.equal(cueText(streamed[0]), "5=look left\n");
    assert.equal(cueText(streamed[1]), null);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
