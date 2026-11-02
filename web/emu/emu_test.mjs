// SPDX-License-Identifier: GPL-3.0-only
// The WebAssembly build (build/emu/sloop.wasm, web/emu/build.sh) as the page and the worklet run it, in Node:
//   - it boots with the built-in loops (FMX's 14: AMEN A ..) and nothing in the slots; the standard set put in USR1 (as the page
//     does at the first switch-on) adds BR1..BR3 after them;
//   - it plays the BREAK track (its screen, PLAY with its steps on) and the sound is finite;
//   - break loops from the page (web/breaks.js: one bar of each file into the BR slots) written into the flash image by the
//     worklet's writeSlot and read again by the firmware (web_slot_changed): BR4.. appear in LOOP A / B and play;
//   - the page's saved sectors (the slots, the autosaved project) come back after a power-off: the loops and the track;
//   - the labels (app_help.js) follow the screen web_ui reports.
//   node web/emu/emu_test.mjs      (after web/emu/build.sh)
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { cut, pack, userSlot, readWav, NSLOTS } from "../breaks.js";
import { writeSlot, storeSectors, restoreSectors, poolScan, poolPut } from "./worklet.js";
import { makeCard, entriesOf } from "../sounds.js";
import { analyse, sliceLoop, sliceNotes, sliceMeta, sliceSlot } from "../slice.js";
import { controlHelp, parseUi } from "../app_help.js";

const HERE = fileURLToPath(new URL(".", import.meta.url));
let bad = 0;
const ok = (c, what) => { console.log(`  ${c ? "ok" : "FAIL"} ${what}`); if (!c) bad++; };

/* the companion app's script links (its imports found, no name declared twice): run beside its modules, it gets as far
 * as the page itself (window, document: not in node) */
{
  let err = null;
  try { await import(HERE + "../app.js"); } catch (e) { err = e; }
  ok(!err || (err instanceof ReferenceError && /document|window|navigator/.test(err.message)), `the app's script links and runs (${err ? err.message : "no error"})`);
}

async function boot(sectors) {
  const { instance } = await WebAssembly.instantiate(readFileSync(HERE + "../../build/emu/sloop.wasm"), {});
  const ex = instance.exports;
  ex._initialize();
  ex.web_nor_erase();
  if (sectors) restoreSectors(new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size()), sectors);
  ex.web_master(700);
  ex.web_boot();
  return ex;
}
const render = (ex, ms) => {
  let pk = 0, nan = 0;
  for (let k = 0, n = Math.round(ms * 44.1); k < n; k += 128) {
    const m = Math.min(128, n - k);
    ex.web_render(m);
    for (const v of new Float32Array(ex.memory.buffer, ex.web_out_l(), m)) { if (v !== v) nan++; pk = Math.max(pk, Math.abs(v)); }
  }
  return [pk, nan];
};
function wav(rate, seconds, seed) {                      // a click on every 8th at 120 and a hum
  const n = Math.round(rate * seconds), d = Buffer.alloc(n * 2);
  let r = seed;
  for (let i = 0; i < n; i++) {
    r = (r * 1103515245 + 12345) >>> 0;
    const beat = (i % Math.round(rate / 2)) < rate * 0.03 ? 0.8 : 0.0;
    d.writeInt16LE(Math.round(Math.max(-1, Math.min(1, beat * Math.sin(i * 0.3) + 0.1 * ((r >>> 8) / 16777216 - 0.5))) * 32767), i * 2);
  }
  const h = Buffer.alloc(44);
  h.write("RIFF", 0); h.writeUInt32LE(36 + d.length, 4); h.write("WAVE", 8); h.write("fmt ", 12);
  h.writeUInt32LE(16, 16); h.writeUInt16LE(1, 20); h.writeUInt16LE(1, 22); h.writeUInt32LE(rate, 24);
  h.writeUInt32LE(rate * 2, 28); h.writeUInt16LE(2, 32); h.writeUInt16LE(16, 34); h.write("data", 36); h.writeUInt32LE(d.length, 40);
  return Buffer.concat([h, d]);
}

let ex = await boot();
render(ex, 2000);
const NB = ex.web_break_builtin();
ok(NB === 14 && ex.web_break_loops() === NB && ex.web_break_ready(0) === 1 && ex.web_break_ready(1) === 1,
   "empty slots: the track has the 14 built-in loops, LOOP A and B built");
{   /* a set of three loops in USR1 (tests/fixtures/breaks_cc.bin: AMEN A, THINK, FUNKY, OaSyntax, CC BY 3.0) */
  const img = new Uint8Array(readFileSync(HERE + "../../tests/fixtures/breaks_cc.bin")), len = new DataView(img.buffer).getUint32(16, true);
  writeSlot(new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size()), 0, img.slice(0, 480), img.slice(512, 512 + len));
  ex.web_slot_changed(0);
  render(ex, 200);
}
ok(ex.web_break_loops() === NB + 3, "the standard set in USR1: three loops more (BR1..BR3 after the built-in ones)");
ex.web_test_break();
ok(ex.web_break_steps() === 0xB6DB, "the BREAK screen with a pattern of 16ths");
ex.web_buttons(1 << 10);
render(ex, 100);
ex.web_buttons(0);
let t0 = ex.web_break_trigs(), [pk, nan] = render(ex, 2500);
ok(ex.web_playing() === 1 && pk > 0.05 && !nan && ex.web_break_trigs() > t0, `PLAY: the track sounds, AMEN A (peak ${pk.toFixed(3)})`);
ex.web_buttons(1 << 10);
render(ex, 300);
ex.web_buttons(0);
render(ex, 100);
ok(ex.web_playing() === 0, "PLAY again: stopped");

/* loops from the page */
const loops = [wav(44100, 4.0, 5), wav(48000, 2.1, 7)].map((b, i) => { const { rate, x } = readWav(b); return { label: "f" + i, s: cut(x, rate, true) }; });
const { slots } = pack(loops, NSLOTS);
const images = slots.map((sl, k) => userSlot(`BR${k + 1}`, sl.map((l) => l.s)));
const nor = new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size());
images.forEach((im, k) => { writeSlot(nor, k + 1, im.hdr, im.data); ex.web_slot_changed(k + 1); });   // (after the standard set)
render(ex, 100);
ok(ex.web_break_loops() === NB + 3 + loops.length, `two files in USR2: ${NB + 3 + loops.length} loops (BR4, BR5 after the standard set)`);
ex.web_break_loop(NB + 3, NB + 4);                       // LOOP A = BR4, LOOP B = BR5
render(ex, 200);                                         // (the main loop builds the banks)
ok(ex.web_break_ready(0) === 1 && ex.web_break_ready(1) === 1, "LOOP A = BR4 and LOOP B = BR5 are built");
ex.web_buttons(1 << 10);
render(ex, 100);
ex.web_buttons(0);
[pk, nan] = render(ex, 3000);
ok(ex.web_playing() === 1 && pk > 0.03 && !nan, `PLAY: BR4 sounds (peak ${pk.toFixed(3)})`);
ex.web_buttons(1 << 10);
render(ex, 300);
ex.web_buttons(0);
render(ex, 25000);                                       // stopped, untouched: the autosave (project.c, 20 s after power-on)

/* the page keeps the changed sectors; after a power-off they come back */
const saved = storeSectors(new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size()));
ok(Object.keys(saved).length >= 2, `the flash sectors to keep: ${Object.keys(saved).length} (the slots, the autosave)`);
ex = await boot(saved);
render(ex, 2500);
ok(ex.web_break_loops() === NB + 3 + loops.length, "after a power-off: the loops are still there");
ok(ex.web_break_steps() === 0xB6DB && ex.web_break_param(14) === NB + 3 && ex.web_break_param(15) === NB + 4,
   "... and the BREAK track as it was left (its steps, LOOP A / B = BR4 / BR5: the autosave)");
ex.web_buttons(1 << 10);
render(ex, 100);
ex.web_buttons(0);
[pk, nan] = render(ex, 2000);
ok(ex.web_playing() === 1 && pk > 0.03 && !nan, `PLAY: BR4 sounds again (peak ${pk.toFixed(3)})`);
ex.web_buttons(1 << 10);
render(ex, 300);
ex.web_buttons(0);
render(ex, 100);

/* a SLICE loop (Load a SLICE loop: slice.js) into USR4, the CHOP track playing it */
{
  const R = 44100, n = Math.round(2 * 240 / 170 * R), x = new Float64Array(n);
  for (let i = 0; i < n; i++) x[i] = 0.5 * Math.sin(2 * Math.PI * 220 * 2 ** (Math.floor(i / (n / 8)) % 3 / 12) * i / R);
  const a = analyse(x, R), c = sliceLoop(a.y, a.bars, 8), im = sliceSlot("TUNE", c.slices, sliceMeta(a.key, c.bars, sliceNotes(a.frames, 8, c.per, a.cents)));
  const nor3 = new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size());
  writeSlot(nor3, 3, null, null);
  ex.web_slot_changed(3);
  ok(ex.web_slice_use(3) === 0, "SLICE: an empty slot is not taken");
  writeSlot(nor3, 3, im.hdr, im.data);
  ex.web_slot_changed(3);
  ok(ex.web_slice_use(3) === 1, "SLICE: the loop in USR4, the CHOP track plays it in SLICE");
  ex.web_buttons(1 << 10);
  render(ex, 100);
  ex.web_buttons(0);
  [pk, nan] = render(ex, 2000);
  ok(ex.web_playing() === 1 && pk > 0.03 && !nan, `... PLAY: it sounds (peak ${pk.toFixed(3)})`);
  ex.web_buttons(1 << 10);
  render(ex, 300);
  ex.web_buttons(0);
  render(ex, 100);
}

/* cleared */
{
  const nor2 = new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size());
  for (let k = 0; k < NSLOTS; k++) { writeSlot(nor2, k, null, null); ex.web_slot_changed(k); }
  render(ex, 100);
  ok(ex.web_break_loops() === NB, "the slots cleared: the built-in loops only");
}

/* the app's library into the emulator's pool (worklet.js poolPut: a free place, the lowest free id), as an FM-1 takes it */
{
  const nor4 = new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size());
  const card = makeCard("loop.wav", readWav(wav(44100, 2.8, 3)).x, 44100);
  Object.assign(card, { type: "BREAK", khz: 16 });
  const kit = makeCard("hit.wav", readWav(wav(44100, 0.3, 4)).x, 44100);
  Object.assign(kit, { type: "KIT" });
  const [brE, kitE] = entriesOf([card, kit]), hdr = brE.im.hdr.slice();
  hdr.fill(0, 8, 16);
  hdr.set([66, 82, 49], 8);                              // "BR1": the BREAK's
  const id = poolPut(nor4, hdr, brE.im.data);
  ex.web_slot_changed(id);
  const id2 = poolPut(nor4, kitE.im.hdr, kitE.im.data);
  ex.web_slot_changed(id2);
  render(ex, 100);
  ok(id === 0 && id2 === 1 && poolScan(nor4).length === 2 && poolPut(nor4, hdr, brE.im.data) === 0,
    "two sounds from the library in the pool: ids 0, 1; the same one again is not put twice");
  ok(ex.web_break_loops() === NB + 1, "the BREAK entry: its loop after the built-in ones");
  ex.web_break_loop(NB, NB);
  render(ex, 200);
  ok(ex.web_break_ready(0) === 1, "LOOP A = that loop (16 kHz) is built");
  ex.web_buttons(1 << 10);
  render(ex, 100);
  ex.web_buttons(0);
  [pk, nan] = render(ex, 2000);
  ok(ex.web_playing() === 1 && pk > 0.03 && !nan, `PLAY: it sounds (peak ${pk.toFixed(3)})`);
  ex.web_buttons(1 << 10);
  render(ex, 300);
  ex.web_buttons(0);
}
/* the labels (app_help.js): web_ui says the screen, the labels follow it: LIVE at power-on, HOME the DECK, SEL the BASS screen */
{
  const ex = await boot();
  const ui = () => {
    const b = new Uint8Array(ex.memory.buffer, ex.web_ui(), 256);
    let t = "";
    for (let i = 0; i < 256 && b[i]; i++) t += String.fromCharCode(b[i]);
    return controlHelp(parseUi(t));
  };
  const tap = (bit) => { ex.web_buttons(1 << bit); render(ex, 80); ex.web_buttons(0); render(ex, 250); };
  render(ex, 6000);
  let h = ui();
  ok(h.where === "LIVE" && h.knob["KNOB 1"] === "FILTER" && h.key[0] === "INTRO", `power-on: LIVE's labels (${h.where}, ${h.knob["KNOB 1"]})`);
  tap(6);
  h = ui();
  ok(h.where === "STUDIO · DECK" && /^LIVE/.test(h.btn.HOME), `HOME: the DECK's (${h.where})`);
  tap(1);
  h = ui();
  ok(h.where.startsWith("BASS") && h.knob["KNOB 1"] === "VARIANT" && /^NEW/.test(h.key[1]), `SEL: the BASS screen's (${h.where}, ${h.knob["KNOB 1"]})`);
}
console.log(bad ? "EMU TEST FAILED" : "emu test: ok");
process.exit(bad ? 1 : 0);
