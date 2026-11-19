import test from "node:test";
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import {
  CueError,
  KAWAII_MOODS,
  formatCue,
  parseCues,
  withCueChunk,
} from "../src/robot/cues.js";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..", "..", "..");
const fixturePath = join(repoRoot, "test", "fixtures", "stream_cues.txt");
const kawaiiPath = join(repoRoot, "src", "display", "eyes", "styles", "kawaii", "expression.cpp");
const apiDocPath = join(repoRoot, "docs", "api.md");
const integrationDocPath = join(repoRoot, "docs", "integration.md");

/**
 * @returns {{ name: string, text: string, ok: boolean, count: number, expected: string[], error: string }[]}
 */
function loadFixture() {
  const lines = readFileSync(fixturePath, "utf8").split("\n");
  /** @type {any[]} */
  const cases = [];
  /** @type {any} */
  let current = null;
  let inExpected = false;
  for (const line of lines) {
    if (line.startsWith("@@ case ")) {
      current = { name: line.slice(8), text: "", ok: false, count: 0, expected: [], error: "" };
      cases.push(current);
      inExpected = false;
    } else if (current && line.startsWith("@@ ok ")) {
      current.ok = true;
      current.count = Number(line.slice(6));
      inExpected = true;
    } else if (current && line.startsWith("@@ error ")) {
      current.error = line.slice(9);
      current = null;
      inExpected = false;
    } else if (current && inExpected) {
      if (line !== "") current.expected.push(line);
    } else if (current) {
      current.text += `${line}\n`;
    }
  }
  return cases;
}

test("shared fixture cases match the firmware parser results", { skip: !existsSync(fixturePath) }, () => {
  const cases = loadFixture();
  assert.ok(cases.length > 20);
  for (const c of cases) {
    if (c.ok) {
      const cues = parseCues(c.text);
      assert.equal(cues.length, c.count, c.name);
      assert.deepEqual(cues.map(formatCue), c.expected, c.name);
    } else {
      assert.throws(() => parseCues(c.text), (err) => err instanceof CueError && err.message === c.error, c.name);
    }
  }
});

test("64 cues are accepted, 65 are rejected", () => {
  let text = "";
  for (let i = 0; i < 64; i++) text += `${i * 10}=blink\n`;
  assert.equal(parseCues(text).length, 64);
  assert.throws(() => parseCues(`${text}9999=blink\n`), { message: "bad cue line 0: too many cues" });
});

test("120-byte line is accepted, 121 is too long", () => {
  const line = "100=blink".padEnd(120, " ");
  assert.equal(parseCues(`${line}\n`).length, 1);
  assert.throws(() => parseCues(`${line} \n`), { message: "bad cue line 1: line too long" });
});

test("CRLF line ends and -0 match the firmware", () => {
  assert.deepEqual(parseCues("50=move head=0.1 hold=20\r\n60=blink\r\n").map(formatCue), [
    "50 move head=0.100 ease=300 hold=20",
    "60 blink",
  ]);
  assert.deepEqual(parseCues("0=move head=-0\n").map(formatCue), ["0 move head=0.000 ease=300 hold=500"]);
});

test("chunk over 8192 bytes is rejected", () => {
  assert.equal(parseCues("#\n".repeat(4096)).length, 0);
  assert.throws(() => parseCues(`${"#\n".repeat(4096)}#`), { message: "bad cue line 0: cue chunk too big" });
});

test("mood list matches the firmware kawaii mood names", { skip: !existsSync(kawaiiPath) }, () => {
  const source = readFileSync(kawaiiPath, "utf8");
  const block = /kMoodNames\[\] = \{([\s\S]*?)\};/.exec(source);
  assert.ok(block);
  const names = [...block[1].matchAll(/"([a-z_]+)"/g)].map((m) => m[1]);
  assert.deepEqual(KAWAII_MOODS, names);
});

test("example cue file in docs/api.md is valid", { skip: !existsSync(apiDocPath) }, () => {
  const doc = readFileSync(apiDocPath, "utf8");
  const block = /```text\n(# hello\.cues[\s\S]*?)```/.exec(doc);
  assert.ok(block, "docs/api.md has the hello.cues example");
  assert.equal(parseCues(block[1]).length, 5);
});

test("example cue file in docs/integration.md is valid", { skip: !existsSync(integrationDocPath) }, () => {
  const doc = readFileSync(integrationDocPath, "utf8");
  const block = /```text\n(# clip\.cues[\s\S]*?)```/.exec(doc);
  assert.ok(block, "docs/integration.md has the clip.cues example");
  assert.equal(parseCues(block[1]).length, 4);
});

/**
 * @param {{ id: string, body: Buffer, declared?: number }[]} chunks
 */
function riff(chunks) {
  const parts = [Buffer.from("RIFF\0\0\0\0WAVE", "latin1")];
  for (const { id, body, declared } of chunks) {
    const header = Buffer.alloc(8);
    header.write(id, 0, "latin1");
    header.writeUInt32LE(declared ?? body.length, 4);
    parts.push(header, body);
    if (body.length % 2 === 1) parts.push(Buffer.alloc(1));
  }
  const out = Buffer.concat(parts);
  out.writeUInt32LE(out.length - 8, 4);
  return out;
}

/** @param {Buffer} wav */
function chunkIds(wav) {
  const ids = [];
  let offset = 12;
  while (offset + 8 <= wav.length) {
    const id = wav.toString("latin1", offset, offset + 4);
    ids.push(id);
    if (id === "data") break;
    const size = wav.readUInt32LE(offset + 4);
    offset += 8 + size + (size % 2);
  }
  return ids;
}

const fmt = Buffer.alloc(16, 1);
const pcm = Buffer.from([1, 2, 3, 4, 5, 6]);

test("withCueChunk inserts tecu right before data and fixes the RIFF size", () => {
  const wav = riff([{ id: "fmt ", body: fmt }, { id: "LIST", body: Buffer.from([7, 7, 7]) }, { id: "data", body: pcm }]);
  const out = withCueChunk(wav, "0=blink\n");
  assert.deepEqual(chunkIds(out), ["fmt ", "LIST", "tecu", "data"]);
  assert.equal(out.readUInt32LE(4), out.length - 8);
  const at = out.indexOf("tecu", 12, "latin1");
  assert.equal(out.readUInt32LE(at + 4), 8);
  assert.equal(out.toString("utf8", at + 8, at + 16), "0=blink\n");
  assert.deepEqual(out.subarray(out.length - pcm.length), pcm);
});

test("withCueChunk pads an odd-length cue list", () => {
  const out = withCueChunk(riff([{ id: "fmt ", body: fmt }, { id: "data", body: pcm }]), "0=blink");
  const at = out.indexOf("tecu", 12, "latin1");
  assert.equal(out.readUInt32LE(at + 4), 7);
  assert.equal(out[at + 8 + 7], 0);
  assert.equal(out.toString("latin1", at + 16, at + 20), "data");
});

test("withCueChunk replaces an existing tecu chunk", () => {
  const wav = riff([{ id: "fmt ", body: fmt }, { id: "tecu", body: Buffer.from("0=pose none") }, { id: "data", body: pcm }]);
  const out = withCueChunk(wav, "5=blink\n");
  assert.deepEqual(chunkIds(out), ["fmt ", "tecu", "data"]);
  assert.ok(!out.includes("pose none"));
});

test("withCueChunk keeps a streaming data size and the bytes after data", () => {
  const wav = riff([{ id: "fmt ", body: fmt }, { id: "data", body: pcm, declared: 0 }]);
  const out = withCueChunk(wav, "0=blink\n");
  const at = out.indexOf("data", 12, "latin1");
  assert.equal(out.readUInt32LE(at + 4), 0);
});

test("withCueChunk rejects a body that is not a WAV or has no data", () => {
  assert.throws(() => withCueChunk(Buffer.from("hello world!"), "0=blink"), /not a WAV file/);
  assert.throws(() => withCueChunk(riff([{ id: "fmt ", body: fmt }]), "0=blink"), /no data chunk/);
});
