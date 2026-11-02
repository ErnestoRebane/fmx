// SPDX-License-Identifier: GPL-3.0-only
// INST's MELODY MIDI, the browser's side (web/melody.js): a MIDI file read, its line found, its key, the bank built and
// read back, the pack's tunes, the bank in the pool's directory. With a path: the pack's bank written there for the
// firmware's side (tests/chopgen_test.c reads it: node web/test_melody.mjs OUT.bin, then chopgen_test --mel OUT.bin N).
import fs from "fs";
import { parseMidi, parts, pick, keyOf, bankImage, readBank, themeName, STEPS } from "./melody.js";
import { makePoolMock, readDir } from "./pool.js";
import { parseImage } from "./sounds.js";

let bad = 0;
const ok = (c, what) => { console.log(`  ${c ? "ok" : "FAIL"} ${what}`); if (!c) bad++; };
const HERE = decodeURIComponent(new URL(".", import.meta.url).pathname);

// a MIDI file: 120 tpq, a C major tune on a flute (ch 1), a bass (ch 2), chords on a pad (ch 3), drums (ch 10), 16 bars
function smf() {
  const ev = [], vlq = (v) => { const o = [v & 0x7F]; while ((v >>= 7)) o.unshift(0x80 | (v & 0x7F)); return o; };
  const add = (t, d) => ev.push([t, d]), note = (ch, t, len, n, v = 96) => { add(t, [0x90 | ch, n, v]); add(t + len, [0x80 | ch, n, 0]); };
  add(0, [0xC0, 73]); add(0, [0xC1, 33]); add(0, [0xC2, 89]);
  const tune = [60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60, 67];
  for (let b = 0; b < 16; b++) {
    tune.slice(0, 4).forEach((_, k) => note(0, b * 480 + k * 120, 110, tune[(b * 4 + k) % 16] + (b >= 8 ? 0 : 0)));
    note(1, b * 480, 470, 36 + [0, 5, 7, 0][b % 4]);
    [48, 52, 55].forEach((n) => note(2, b * 480, 470, n + [0, 5, 7, 0][b % 4]));
    for (let k = 0; k < 4; k++) note(9, b * 480 + k * 120, 30, k % 2 ? 38 : 36);
  }
  ev.sort((a, b) => a[0] - b[0]);
  const trk = [];
  let t = 0;
  for (const [at, d] of ev) { trk.push(...vlq(at - t), ...d); t = at; }
  trk.push(0, 0xFF, 0x2F, 0);
  const len = trk.length;
  return Uint8Array.from([..."MThd"].map((c) => c.charCodeAt(0)).concat([0, 0, 0, 6, 0, 0, 0, 1, 0, 120],
    [..."MTrk"].map((c) => c.charCodeAt(0)), [len >>> 24, (len >> 16) & 255, (len >> 8) & 255, len & 255], trk));
}

console.log("a MIDI file read");
const song = parseMidi(smf());
ok(song.tpq === 120 && song.bars === 16 && song.notes.length === 16 * (4 + 1 + 3 + 4), `16 bars, every note (${song.notes.length})`);
const ps = parts(song);
ok(ps.length === 1 && ps[0].ch === 0 && ps[0].fam === "Pipe", `the line's part only: the flute, not the bass, the pad, the drums (${ps.map((p) => p.ch)})`);
const p = pick(song);
ok(p.ev.length === 32 && p.ev.every((e, i) => e.at === i * 4 && e.len === 4), `8 bars of quarters (${p.ev.length})`);
ok(p.ev[0].note === [60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60, 67][(p.start * 4) % 16], `from its bar ${p.start + 1}: the tune's notes`);
ok(pick(song, 0, 3).start === 3 && pick(song, 0, 3).ev.length === 32, "a bar chosen: 8 bars from it");
ok(keyOf(song) === 0, `the key: C major (${keyOf(song)})`);
ok(themeName("wayward bit.mid") === "WAYWARD " && themeName("x.mid") === "X", "names: the file's, 8 characters");
let threw = false;
try { parseMidi(new Uint8Array(20)); } catch { threw = true; }
ok(threw, "not a MIDI file: said so");

console.log("the bank");
const th = [{ name: "flute", key: 0, ev: p.ev }, { name: "minor tune", key: 9 | 0x10, ev: p.ev.slice(0, 5) }];
const im = bankImage(th), back = readBank(im.hdr, im.data);
ok(back && back.length === 2 && back[0].name === "FLUTE" && back[1].name === "MINOR TU" && back[1].key === 25 &&
   JSON.stringify(back[0].ev) === JSON.stringify(p.ev), "built and read back: the names, the keys, the notes");
ok(parseImage(im.hdr).type === "MELODY" && parseImage(im.hdr).name === "MELODIES", "the entry: MELODY, named MELODIES");
const mock = makePoolMock(), out = mock.access.outputs.get("mock-out"), inp = mock.access.inputs.get("mock-in");
const link = { retry: (cmd, args) => new Promise((res) => {   // (the pool's commands straight to the mock)
  inp.onmidimessage = (m) => res([...m.data.slice(5, -1)]);
  out.send(Uint8Array.from([0xF0, 0x7D, 0x46, 0x4C, cmd, ...args, 0xF7]));
}) };
mock.put(3, im, 8);
const e = (await readDir(link)).entries.find((x) => x.id === 7);
ok(e && e.mel && e.nz === 1, "in the pool: its one silent zone passes, the directory says it's the melody bank");

console.log("the pack (web/packs/melodies)");
const dir = HERE + "packs/melodies/", idx = JSON.parse(fs.readFileSync(dir + "index.json", "utf8"));
const themes = [];
let few = 0;
for (const f of idx.files) {
  const s = parseMidi(fs.readFileSync(dir + f)), q = pick(s);
  few += q.ev.length < 12;
  ok(q.ev.every((x) => x.at < STEPS && x.len >= 1 && x.note < 128), `${f}: ${q.ev.length} notes from bar ${q.start + 1}`);
  themes.push({ name: themeName(f), key: keyOf(s), ev: q.ev });
}
ok(idx.files.length === 56 && idx.license === "CC0 1.0", "56 tunes, CC0");
ok(few <= 3, `a tune found in (nearly) every file (${few} thin)`);
const pack = bankImage(themes);
ok(readBank(pack.hdr, pack.data).length === 56, `one bank of all 56: ${Math.ceil((512 + pack.data.length) / 4096) * 4} KB`);
if (process.argv[2]) {
  fs.writeFileSync(process.argv[2], Buffer.concat([pack.hdr, pack.data]));
  console.log(`  wrote ${process.argv[2]} for chopgen_test --mel`);
}

console.log(bad ? "MELODY FAILED" : "melody: a MIDI file's line, its key, the bank, the pack");
process.exit(bad ? 1 : 0);
