#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only
import fs from "fs";
import path from "path";

const BUTTON_MAP = {
  FX: 0, SCL: 1, SEL: 1, ENV: 2, LFO: 3, EDIT: 4, GLO: 5, HOME: 6,
  SAVE: 7, ARP: 8, SEQ: 9, PLAY: 10, REC: 11,
  OCTDN: 12, "OCT-": 12, OCT_DN: 12,
  OCTUP: 13, "OCT+": 13, OCT_UP: 13
};

const ENCODER_MAP = {
  SELECT: 0, SEL: 0, ALGO: 1, PRESET: 2, PRESETS: 2,
  K1: 3, K2: 4, K3: 5, K4: 6
};

const NOTE_BASE = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };

function parseNote(str) {
  const m = str.trim().toUpperCase().match(/^([A-G])([#B]?)(-?\d+)$/);
  if (!m) throw new Error(`Invalid note name: ${str}`);
  let semi = NOTE_BASE[m[1]];
  if (m[2] === "#") semi += 1;
  else if (m[2] === "B") semi -= 1;
  const oct = parseInt(m[3], 10);
  const midi = (oct + 1) * 12 + semi;
  const idx = midi - 53; // F3 is lowest key (idx 0, MIDI 53)
  if (idx < 0 || idx >= 27) throw new Error(`Note ${str} (MIDI ${midi}) out of FM-1 27-key range`);
  return idx;
}

const args = process.argv.slice(2);
if (args.length < 2) {
  console.error("Usage: node wasm_render.mjs <script.json> [wasm_file] <out.raw>");
  process.exit(1);
}

const scriptPath = args[0];
let wasmPath, outPath;
if (args.length >= 3) {
  wasmPath = args[1];
  outPath = args[2];
} else {
  outPath = args[1];
}

const script = JSON.parse(fs.readFileSync(scriptPath, "utf-8"));
if (!wasmPath) {
  wasmPath = `build/wasm/${script.firmware}.wasm`;
}

const wasmBytes = fs.readFileSync(wasmPath);
const { instance } = await WebAssembly.instantiate(wasmBytes, {});
const ex = instance.exports;

if (ex._initialize) ex._initialize();
if (ex.web_nor_erase) ex.web_nor_erase();
if (script.seed !== undefined && ex.web_seed) ex.web_seed(script.seed);
if (ex.web_master) ex.web_master(script.master ?? 700);
if (ex.web_boot) ex.web_boot();

const QUANTUM = 128;
const totalFrames = (script.seconds ?? 12) * 44100;
const HOLD_MS = 24;
const inputQueue = [];
let nextOkMs = 0;
let eventIdx = 0;
let framesRendered = 0;

const events = script.events || [];
const outFd = fs.openSync(outPath, "w");
const interleaved = new Float32Array(QUANTUM * 2);

while (framesRendered < totalFrames) {
  let nowMs = ex.web_now_ms ? ex.web_now_ms() : 0;

  // Ingest events due by now
  while (eventIdx < events.length && events[eventIdx].ms <= nowMs) {
    const ev = events[eventIdx];
    if (ev.buttons !== undefined) {
      let mask = 0;
      for (const b of ev.buttons) {
        const idx = BUTTON_MAP[b.toUpperCase()];
        if (idx !== undefined) mask |= (1 << idx);
      }
      inputQueue.push({ isKeys: false, mask: mask >>> 0 });
    }
    if (ev.keys !== undefined) {
      let mask = 0;
      for (const k of ev.keys) {
        mask |= (1 << parseNote(k));
      }
      inputQueue.push({ isKeys: true, mask: mask >>> 0 });
    }
    if (ev.enc) {
      for (const [roleName, steps] of Object.entries(ev.enc)) {
        const r = ENCODER_MAP[roleName.toUpperCase()];
        if (r !== undefined && ex.web_enc) ex.web_enc(r, steps | 0);
      }
    }
    if (ev.master !== undefined) {
      if (ex.web_master) ex.web_master(ev.master | 0);
    }
    if (ev.midi) {
      if (ex.web_midi) ex.web_midi(ev.midi[0] | 0, ev.midi[1] | 0, ev.midi[2] | 0);
    }
    if (ev.call === "test_heavy") {
      if (ex.web_test_heavy) ex.web_test_heavy();
    }
    eventIdx++;
  }

  // Apply gated events per HOLD_MS
  nowMs = ex.web_now_ms ? ex.web_now_ms() : 0;
  while (inputQueue.length > 0 && (nowMs - nextOkMs) >= 0) {
    const ev = inputQueue.shift();
    if (ev.isKeys) {
      if (ex.web_keys) ex.web_keys(ev.mask);
    } else {
      if (ex.web_buttons) ex.web_buttons(ev.mask);
    }
    nextOkMs = nowMs + HOLD_MS;
  }

  const framesThisStep = Math.min(QUANTUM, totalFrames - framesRendered);
  ex.web_render(framesThisStep);
  const l = new Float32Array(ex.memory.buffer, ex.web_out_l(), framesThisStep);
  const r = new Float32Array(ex.memory.buffer, ex.web_out_r(), framesThisStep);
  for (let i = 0; i < framesThisStep; i++) {
    interleaved[2 * i] = l[i];
    interleaved[2 * i + 1] = r[i];
  }
  fs.writeSync(outFd, Buffer.from(interleaved.buffer, interleaved.byteOffset, framesThisStep * 2 * 4));
  framesRendered += framesThisStep;
}

fs.closeSync(outFd);
console.log(`Rendered ${framesRendered} frames (${(framesRendered / 44100).toFixed(2)} s) to ${outPath}`);
