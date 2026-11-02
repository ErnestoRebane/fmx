// SPDX-License-Identifier: GPL-3.0-only
// web/slice.js: the hits and the lines on them (as many slices as beats, each moved onto a hit a little off its beat), the key and the slices' notes of made-up loops (a tune in A minor, the same 30 cents sharp, one in
// E flat major, a noise perc loop: no key), the slicing (bars nearest 170, a loop too long for a slot), and the slot
// image (zones on keys 36.., one each; the SLC1 meta at 480 as firmware/src/chopgen.c cs_meta_t reads it).
//   node web/test_slice.mjs [slot.bin]   (writes the A minor tune's slot image there)
import * as S from "./slice.js";
import { writeFileSync } from "node:fs";
import { readFileSync } from "node:fs";
import { SLOT_MAX_DATA, SLOT_DATA_OFF, parseSlot, crc32, imaDecode } from "./breaks.js";

let bad = 0;
const ok = (cond, what) => { console.log(`  ${cond ? "ok" : "FAIL"} ${what}`); if (!cond) bad++; };
const RATE = 44100;

// a tune: notes (MIDI) one per slice over a loop of `secs`, a voice-like tone (a few harmonics, a little vibrato)
function tune(notes, secs, cents = 0) {
  const n = Math.round(secs * RATE), per = n / notes.length, x = new Float64Array(n);
  let ph = 0;
  for (let i = 0; i < n; i++) {
    const k = Math.floor(i / per), t = (i - k * per) / per;
    const f = 440 * 2 ** ((notes[k] - 69 + cents / 100) / 12) * (1 + 0.004 * Math.sin(i * 2 * Math.PI * 5 / RATE));
    ph += 2 * Math.PI * f / RATE;
    const env = Math.min(1, t * 20) * Math.min(1, (1 - t) * 10);
    x[i] = env * (0.6 * Math.sin(ph) + 0.25 * Math.sin(2 * ph) + 0.12 * Math.sin(3 * ph));
  }
  return x;
}
function perc(secs) {
  const n = Math.round(secs * RATE), x = new Float64Array(n), beat = Math.round(RATE * 60 / 170 / 2);
  let r = 7;
  for (let i = 0; i < n; i++) {
    r = (r * 1103515245 + 12345) >>> 0;
    const t = i % beat;
    x[i] = Math.exp(-t / 900) * ((r >>> 8) / 8388608 - 1);
  }
  return x;
}

// chords: per bar a triad over its root an octave down, a piano-ish tone (a few partials, each note decaying)
function chords(ch, secs) {
  const n = Math.round(secs * RATE), per = n / ch.length, x = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    const k = Math.floor(i / per), t = (i - k * per) / RATE;
    for (const m of ch[k]) {
      const f = 440 * 2 ** ((m - 69) / 12), ph = 2 * Math.PI * f * t;
      x[i] += Math.exp(-t * 1.5) * (0.5 * Math.sin(ph) + 0.2 * Math.sin(2 * ph) + 0.1 * Math.sin(3 * ph)) / ch[k].length;
    }
  }
  return x;
}

const BAR170 = 240 / 170;
{
  const AM = [57, 60, 64, 62, 57, 55, 53, 52];                 // A3 C4 E4 D4 A3 G3 F3 E3
  const t0 = Date.now(), a = S.analyse(tune(AM, 2 * BAR170), RATE), ms = Date.now() - t0;
  ok(a.bars === 2 && Math.abs(a.bpm - 170) < 0.5, `A minor tune, 2 bars at 170: bars ${a.bars} at ${a.bpm.toFixed(1)}`);
  ok(a.key === (9 | 0x10), `... its key ${S.keyName(a.key)} (A minor; fit ${a.score.toFixed(2)})`);
  const c = S.sliceLoop(a.y, a.bars, 8), notes = S.sliceNotes(a.frames, 8, c.per, a.cents);
  ok(notes.join() === AM.join(), `... the slices' notes ${notes.map(S.noteName).join(" ")}`);
  ok(ms < 3000, `... the analysis took ${ms} ms`);
  if (process.argv[2]) {                                       // the slot image, for tests/chopgen_test.c to read
    const im = S.sliceSlot("AMTUNE", c.slices, S.sliceMeta(a.key, c.bars, notes)), out = new Uint8Array(SLOT_DATA_OFF + im.data.length);
    out.set(im.hdr, 0);
    out.set(im.data, SLOT_DATA_OFF);
    writeFileSync(process.argv[2], out);
  }
  const b = S.analyse(tune(AM, 2 * BAR170, 30), RATE), cb = S.sliceLoop(b.y, b.bars, 8);
  const nb = S.sliceNotes(b.frames, 8, cb.per, b.cents);
  ok(b.key === (9 | 0x10) && Math.abs(b.cents - 30) < 8 && nb.join() === AM.join(),
    `30 cents sharp: still A minor, the same notes (tuning ${b.cents.toFixed(0)} cents)`);
  const EB = [63, 67, 70, 68, 67, 65, 63, 58, 63, 70, 67, 63, 65, 62, 63, 63];   // E flat major, 16 slices over 4 bars
  const e = S.analyse(tune(EB, 4 * BAR170), RATE), ce = S.sliceLoop(e.y, e.bars, 16);
  const ne = S.sliceNotes(e.frames, 16, ce.per, e.cents);
  ok(e.key === 3 && e.bars === 4, `E flat major tune, 4 bars: ${S.keyName(e.key)}, ${e.bars} bars`);
  ok(ne.join() === EB.join(), `... 16 slices' notes ${ne.map(S.noteName).join(" ")}`);
}
{
  const FM = [[41, 53, 56, 60], [37, 49, 53, 56], [39, 51, 55, 58], [41, 53, 56, 60]];   // Fm Db Eb Fm (i VI VII i, the bass under each)
  const a = S.analyse(chords(FM, 4 * BAR170), RATE), c = S.sliceLoop(a.y, a.bars, 8), notes = S.sliceNotes(a.frames, 8, c.per, a.cents);
  ok(a.key === (5 | 0x10), `chords Fm Db Eb Fm: ${S.keyName(a.key)} (F minor, not its relative A flat; fit ${a.score.toFixed(2)}, tuning ${a.conc.toFixed(2)})`);
  ok(notes.every((v) => v === S.NOKEY), "... chords: no slice has a note (KEY moves them whole, SNAP leaves them)");
}
{
  const img = new Uint8Array(readFileSync(new URL("../tests/fixtures/breaks_cc.bin", import.meta.url))), h = parseSlot(img.subarray(0, 480));
  const keys = h.zones.map((z) => S.analyse(Float64Array.from(imaDecode(img.subarray(SLOT_DATA_OFF + z.off), z.n), (v) => v / 32768), 22050));
  ok(keys.every((k) => k.key === S.NOKEY), `the breaks (AMEN A, THINK, FUNKY): no key (tuning ${keys.map((k) => k.conc.toFixed(2)).join(" ")})`);
}
{
  const p = S.analyse(perc(2 * BAR170), RATE), c = S.sliceLoop(p.y, p.bars, 8);
  const notes = S.sliceNotes(p.frames, 8, c.per, p.cents);
  ok(p.key === S.NOKEY, `noise perc loop: ${S.keyName(p.key)} (${Math.round(p.voiced * 100)} % pitched)`);
  ok(notes.every((v) => v === S.NOKEY), "... no slice has a note");
}
{
  // the hits: a perc loop of a bar with its hits off the beat (16ths 3, 6, 10, 13: syncopated, offbeat), a riff of 2 bars
  // whose notes come in a 16th ahead of the beats
  const hitLoop = (pos, bars, tone) => {
    const n = Math.round(bars * BAR170 * RATE), x = new Float64Array(n), s16 = n / (16 * bars);
    let r = 3;
    pos.forEach(([p, amp, note], k) => {
      const a = Math.round(p * s16), e = k + 1 < pos.length ? Math.round(pos[k + 1][0] * s16) : n;
      for (let i = a; i < e; i++) {
        const t = (i - a) / RATE, f = 440 * 2 ** (((note || 0) - 69) / 12);
        r = (r * 1103515245 + 12345) >>> 0;
        x[i] = tone ? amp * Math.min(1, t * 200) * Math.exp(-t * 3) * (0.6 * Math.sin(2 * Math.PI * f * t) + 0.3 * Math.sin(4 * Math.PI * f * t))
          : amp * Math.exp(-t * 30) * ((r >>> 8) / 8388608 - 1 + Math.sin(2 * Math.PI * 80 * t));
      }
    });
    return x;
  };
  const near = (hits, want, s16) => want.every((w) => hits.some((h) => Math.abs(h.t - w * s16) < 0.008 * 22050));
  const PERC = [[0, 1], [3, 0.7], [6, 0.9], [8, 0.5], [10, 0.8], [13, 0.6], [14, 0.4]];
  const p = S.analyse(hitLoop(PERC, 1, false), RATE), ps = p.y.length / 16;
  ok(near(p.hits, [3, 6, 8, 10, 13, 14], ps) && p.hits.length === 6,
    `perc hits off the beat: ${p.hits.map((h) => (h.t / ps).toFixed(2)).join(" ")} (16ths; 0 is the loop's start)`);
  ok(p.hits.every((h) => h.t <= Math.round(Math.round(h.t / ps) * ps) && h.t > Math.round(h.t / ps) * ps - 0.008 * 22050),
    "... each a little ahead of its attack, never after it");
  const pb = S.placeCuts(p.hits, p.y.length, 4);
  ok(pb.length === 4 && near(pb.map((t) => ({ t })), [0, 3, 8, 13], ps),
    `... a beat each, still 4 slices: the lines move onto the hits a 16th off their beats, the one on its beat stays (${pb.map((t) => (t / ps).toFixed(2)).join(" ")})`);
  const hc = S.placeCuts([{ t: 8050, s: 0.3 }, { t: 6000, s: 1 }, { t: 17500, s: 0.5 }, { t: 27000, s: 1 }], 32000, 4);
  ok(hc.join() === "0,8050,17500,24000",
    `a hit on the beat keeps the line (not the louder one off it); one a little off moves it; one too far leaves it on the beat (${hc.join(" ")})`);
  const RIFF = [[0, 1, 57], [3, 0.8, 60], [7, 0.8, 64], [11, 0.9, 62], [16, 1, 57], [19, 0.7, 55], [23, 0.8, 53], [27, 0.9, 52], [30, 0.6, 50]];
  const q = S.analyse(hitLoop(RIFF, 2, true), RATE), qs = q.y.length / 32;
  ok(near(q.hits, [3, 7, 11, 16, 19, 23, 27, 30], qs) && q.hits.every((h) => h.t < q.y.length - 0.03 * 22050),
    `riff hits: ${q.hits.map((h) => (h.t / qs).toFixed(2)).join(" ")} (none at the loop's end: its start coming round)`);
  const qc = S.placeCuts(q.hits, q.y.length, 8);
  ok(near(qc.slice(1).map((t) => ({ t })), [3, 7, 11, 16, 19, 23, 27], qs), `... a beat each: the notes pushed ahead of the beats start the slices (${qc.map((t) => (t / qs).toFixed(2)).join(" ")})`);
  const c = S.sliceLoop(q.y, 2, 8, qc), lens = qc.map((a, i) => (i < 7 ? qc[i + 1] : c.end) - a);
  ok(c.cuts.join() === qc.join() && c.end === q.y.length && c.slices.every((s, i) => s.length === lens[i]),
    "... sliceLoop cuts there: each slice from its line to the next");
  const ns = S.sliceNotes(q.frames, 8, [...c.cuts, c.end], q.cents);
  ok(ns.join() === [57, 60, 64, 62, 57, 55, 53, 52].join(), `... each slice's note from its own stretch: ${ns.map(S.noteName).join(" ")}`);
  const big = new Float64Array(300000).fill(0.5), bc = [0, 1000, 2000, 150000];
  const bs = S.sliceLoop(big, 1, 4, bc), bb = bs.slices.reduce((a, s) => a + ((s.length + 1) >> 1), 0);
  ok(bb <= SLOT_MAX_DATA && bs.slices[0].length === 1000 && bs.slices[1].length === 1000 && bs.slices[3].length === bs.slices[2].length,
    `lines too long for a slot: only the longest slices' tails cut (${bs.slices.map((s) => s.length).join(" ")}, ${bb} B)`);
}
{
  ok(S.beatSlices(1) === 4&& S.beatSlices(2) === 8 && S.beatSlices(4) === 16 && S.beatSlices(8) === 16, "a slice a beat: 4, 8, 16, 16 (8 bars: its first 4)");
  ok(S.barsOf(4 * BAR170)[0] === 4 && S.barsOf(8 * BAR170)[0] === 8 && S.barsOf(2.8)[0] === 2 && S.barsOf(240 / 100)[0] === 1,
    "bars: the reading nearest 170 (4, 8 at 170; 2 bars of 2.8 s; one bar at 100)");
  const y = new Float64Array(Math.round(8 * BAR170 * 22050)).fill(0.5), c = S.sliceLoop(y, 8, 8);
  const bytes = c.slices.reduce((a, s) => a + ((s.length + 1) >> 1), 0);
  ok(c.bars === 4 && c.cut === 4 && bytes <= SLOT_MAX_DATA, `8 bars at 170 (11.3 s): its first 4 bars fit (${bytes} B)`);
  const z = S.sliceLoop(new Float64Array(300000).fill(0.5), 1, 16), zb = z.slices.reduce((a, s) => a + ((s.length + 1) >> 1), 0);
  ok(z.bars === 1 && zb <= SLOT_MAX_DATA, `one bar too long for a slot: each slice's tail cut (${zb} B)`);
  ok(c.slices[0][0] === 0 && c.slices[0][c.slices[0].length - 1] < 300, "slices fade in and out");
}
{
  const notes = [57, 60, S.NOKEY, 62, 57, 55, 53, 52];
  const sl = notes.map((_, i) => new Int16Array(1000 + i).fill(1000 * i));
  const im = S.sliceSlot("brkvox", sl, S.sliceMeta(9 | 0x10, 2, notes)), h = parseSlot(im.hdr);
  const v = new DataView(im.hdr.buffer);
  ok(im.hdr.length === SLOT_DATA_OFF && h && h.nz === 8, "the slot: a 512-byte header with 8 zones");
  ok(h.name === "SBRKVOX", `its name kept off the break page's "BR..." (${h.name})`);
  let zs = true;
  for (let i = 0; i < 8; i++) {
    const o = 32 + i * 28;
    zs &&= im.hdr[o + 25] === 36 + i && im.hdr[o + 26] === 36 + i && v.getInt16(o + 20, true) === (36 + i) * 16 && v.getUint32(o + 4, true) === 1000 + i;
  }
  ok(zs, "zone i: on key 36 + i alone (lo = hi = root), its length");
  ok(v.getUint32(16, true) === im.data.length && v.getUint32(20, true) === crc32(im.data), "the data's length and CRC");
  ok(v.getUint32(480, true) === 0x31434C53 && im.hdr[484] === 0x19 && im.hdr[485] === 2 && im.hdr[486] === 8 &&
    im.hdr[488] === 57 && im.hdr[490] === 0xFF && im.hdr[495] === 52 && im.hdr[496] === 0xFF && im.hdr[503] === 0xFF,
    "SLC1 at 480: key A minor 0x19, bars 2, n 8, the notes, 0xFF past them");
  const m = S.parseMeta(im.hdr);
  ok(m && m.key === 0x19 && m.bars === 2 && m.notes.join() === notes.join(), "parseMeta reads it back");
  let threw = false;
  try { S.sliceSlot("X", Array.from({ length: 17 }, () => new Int16Array(10)), S.sliceMeta(0, 1, [])); } catch (_) { threw = true; }
  ok(threw, "17 slices: refused");
}
console.log(bad ? `slice: ${bad} FAILED` : "slice: all ok");
process.exit(bad ? 1 : 0);
