// SPDX-License-Identifier: GPL-3.0-only
// INST's MELODY MIDI (firmware chopgen.c cm_mel_midi): a MIDI file's melody, 8 bars of it, into the melody bank the
// FM-1 keeps in its sample pool (an entry with the "MEL1" meta at 480).
//   parseMidi(bytes)               a standard MIDI file -> {tpq, bars, notes: [{ch, prog, at, dur, note, vel}]} (ticks)
//   parts(song)                    its channels that carry a line (no drums, bass, pads, effects) -> [{ch, fam, ...}]
//   pick(song, part, start)        8 bars of a part as the FM-1 plays them -> {start, part, ev: [{at, len, note, vel}]}
//                                  (16ths; part -1 / start -1: the most melodic part and 8 bars, found)
//   keyOf(song)                    the file's key: the tonic 0..11 | 0x10 minor (Krumhansl-Schmuckler)
//   bankImage(themes)              [{name, key, ev}] -> the pool entry {hdr (512), data}
//   readBank(hdr, data)            back: [{name, key, ev}] or null (not a bank)
// The FM-1 moves a theme to the song's key and fits it to the song's chords; here it stays the file's.
import { crc32 } from "./breaks.js";

export const BARS = 8, STEPS = BARS * 16, MAX_EV = 160, MAX_THEMES = 100;
export const MEL_MAGIC = 0x314C454D;                // "MEL1"
const REC = 10;                                     // a theme's head: name[8], key, count

// ---- the file ------------------------------------------------------------------------------------------------------------
export function parseMidi(bytes) {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const str = (i) => String.fromCharCode(b[i], b[i + 1], b[i + 2], b[i + 3]);
  const u32 = (i) => (b[i] << 24 | b[i + 1] << 16 | b[i + 2] << 8 | b[i + 3]) >>> 0;
  if (b.length < 14 || str(0) !== "MThd") throw new Error("not a MIDI file");
  const ntrk = b[10] << 8 | b[11], div = b[12] << 8 | b[13];
  if (div & 0x8000) throw new Error("a MIDI file timed in SMPTE frames (not beats)");
  const tpq = div || 96, notes = [];
  let p = 8 + u32(4), num = 4, den = 4;
  for (let t = 0; t < ntrk && p + 8 <= b.length; t++) {
    if (str(p) !== "MTrk") break;
    const end = Math.min(b.length, p + 8 + u32(p + 4));
    let i = p + 8, tick = 0, run = 0;
    const on = new Map(), prog = new Array(16).fill(0);
    const vlq = () => { let v = 0, c; do { c = b[i++]; v = v * 128 + (c & 0x7F); } while (c & 0x80 && i < end); return v; };
    while (i < end) {
      tick += vlq();
      let st = b[i];
      if (st & 0x80) i++; else st = run;
      if (st === 0xFF) {
        const type = b[i++], len = vlq();
        if (type === 0x58 && len >= 2 && !notes.length) { num = b[i]; den = 2 ** b[i + 1]; }
        if (type === 0x2F) break;
        i += len;
        continue;
      }
      if (st === 0xF0 || st === 0xF7) { i += vlq(); continue; }
      run = st;
      const ch = st & 15, kind = st >> 4, d1 = b[i++], d2 = kind === 0xC || kind === 0xD ? 0 : b[i++];
      if (kind === 0xC) prog[ch] = d1;
      else if (kind === 0x9 && d2) {
        const k = ch * 128 + d1;
        if (on.has(k)) { const n = on.get(k); n.dur = tick - n.at; }   // (the same key again: the first ends)
        const n = { ch, prog: prog[ch], at: tick, dur: 0, note: d1, vel: d2 };
        notes.push(n);
        on.set(k, n);
      } else if (kind === 0x8 || kind === 0x9) {
        const k = ch * 128 + d1, n = on.get(k);
        if (n) { n.dur = tick - n.at; on.delete(k); }
      }
    }
    for (const n of on.values()) n.dur = Math.max(1, tick - n.at);
    p = end;
  }
  notes.sort((x, y) => x.at - y.at || y.note - x.note);
  const bar = tpq * 4;                              // (8 bars of 4/4 whatever the file's metre: the FM-1's bars)
  const last = notes.reduce((m, n) => Math.max(m, n.at + n.dur), 0);
  return { tpq, bars: Math.ceil(last / bar), notes, meter: `${num}/${den}` };
}

// ---- the parts -----------------------------------------------------------------------------------------------------------
const FAM = ["Piano", "Chromatic", "Organ", "Guitar", "Bass", "Strings", "Ensemble", "Brass", "Reed", "Pipe", "Synth lead",
  "Synth pad", "Synth FX", "Ethnic", "Percussive", "Effects"];
const NOT_LINE = new Set([4, 11, 12, 14, 15]);      // bass, pads, synth FX, percussive, effects
const s16 = (song, tick) => Math.round(tick * 4 / song.tpq);
// how much of a line a channel's notes are: single notes (not chords), how high, how many
function lineStats(song, ns) {
  const at = ns.map((n) => s16(song, n.at)), groups = new Map();
  at.forEach((a) => groups.set(a, (groups.get(a) || 0) + 1));
  const single = at.filter((a) => groups.get(a) === 1).length;
  const mean = ns.reduce((s, n) => s + n.note, 0) / (ns.length || 1);
  return { n: ns.length, mono: ns.length ? single / ns.length : 0, mean, distinct: new Set(ns.map((n) => n.note)).size, onsets: groups.size };
}
export function parts(song) {
  const by = new Map();
  for (const n of song.notes) if (n.ch !== 9) (by.get(n.ch) || by.set(n.ch, []).get(n.ch)).push(n);
  const out = [];
  for (const [ch, ns] of by) {
    const fams = new Map();
    for (const n of ns) fams.set(n.prog >> 3, (fams.get(n.prog >> 3) || 0) + 1);
    const fam = [...fams].sort((a, b) => b[1] - a[1])[0][0], st = lineStats(song, ns);
    if (NOT_LINE.has(fam) || st.n < 8 || st.distinct < 4 || st.mean < 50) continue;
    out.push({ ch, fam: FAM[fam], ...st });
  }
  return out.sort((a, b) => a.ch - b.ch);
}

// ---- 8 bars of a line ------------------------------------------------------------------------------------------------------
// a part's notes from 16th `start * 16` for 8 bars as one line: the top note of each onset, held up to the next
function line(song, chs, start) {
  const t0 = start * 16, top = new Map();
  for (const n of song.notes) {
    if (!chs.includes(n.ch)) continue;
    const a = s16(song, n.at) - t0;
    if (a < 0 || a >= STEPS) continue;
    const o = top.get(a);
    if (!o || n.note > o.note) top.set(a, n);
  }
  const at = [...top.keys()].sort((x, y) => x - y);
  return at.slice(0, MAX_EV).map((a, i) => {
    const n = top.get(a), next = i + 1 < at.length ? at[i + 1] : STEPS;
    const len = Math.max(1, Math.min(next - a, Math.round(n.dur * 4 / song.tpq) || 1, 128));
    return { at: a, len, note: n.note, vel: Math.max(40, Math.min(127, n.vel)) };
  });
}
// how good a theme 8 bars of a line make: enough notes but not a run of 16ths (a riff, an arpeggio), many pitches, no bar
// empty, a line not chords, up where a tune sings (a low line is a riff or a bass)
function score(ev, st) {
  if (ev.length < 12) return -9 + ev.length / 12;
  const bars = new Set(ev.map((e) => e.at >> 4)).size, d = new Set(ev.map((e) => e.note)).size;
  const mean = ev.reduce((s, e) => s + e.note, 0) / ev.length, lens = new Set(ev.map((e) => e.len)).size;
  return Math.min(ev.length, 48) / 48 - Math.max(0, ev.length - 72) / 48 + Math.min(d, 8) / 8 + bars / BARS * 1.5 +
    st.mono + (Math.min(mean, 79) - 55) / 12 + Math.min(lens, 4) / 8;
}
// part: an index into parts(song), -2 all of them (the top of each onset), -1 found; start: a bar, -1 found
export function pick(song, part = -1, start = -1) {
  const ps = parts(song), last = Math.max(0, song.bars - BARS);
  const cands = part >= 0 && ps[part] ? [part] : part === -2 || !ps.length ? [-2] : ps.map((_, i) => i);
  const chsOf = (i) => (i === -2 ? (ps.length ? ps.map((q) => q.ch) : [...new Set(song.notes.filter((n) => n.ch !== 9).map((n) => n.ch))]) : [ps[i].ch]);
  let best = null;
  for (const i of cands) {
    const st = i >= 0 ? ps[i] : { mono: 0.5, mean: 60 };
    for (let s = start >= 0 ? start : 0; s <= (start >= 0 ? start : last); s++) {
      const ev = line(song, chsOf(i), s), sc = score(ev, st);
      if (!best || sc > best.sc) best = { sc, part: i, start: s, ev };
    }
  }
  return best ? { part: best.part, start: best.start, ev: best.ev } : { part: -2, start: Math.max(0, start), ev: [] };
}

// ---- the key ---------------------------------------------------------------------------------------------------------------
const MAJ = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];
const MIN = [6.33, 2.68, 3.52, 5.38, 2.6, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];
function corr(a, b) {
  const ma = a.reduce((s, x) => s + x, 0) / 12, mb = b.reduce((s, x) => s + x, 0) / 12;
  let n = 0, da = 0, db = 0;
  for (let i = 0; i < 12; i++) { n += (a[i] - ma) * (b[i] - mb); da += (a[i] - ma) ** 2; db += (b[i] - mb) ** 2; }
  return da && db ? n / Math.sqrt(da * db) : 0;
}
// the key of the notes given (the whole file's, not only the line's: the chords tell it), by how long each pitch sounds
export function keyOf(song, notes = song.notes) {
  const h = new Array(12).fill(0);
  for (const n of notes) if (n.ch !== 9) h[n.note % 12] += Math.min(n.dur, song.tpq * 4) + song.tpq / 8;
  let best = { r: -2, key: 0 };
  for (let t = 0; t < 12; t++) {
    const rot = (p) => p.map((_, i) => p[(i - t + 12) % 12]);
    const rM = corr(h, rot(MAJ)), rm = corr(h, rot(MIN));
    if (rM > best.r) best = { r: rM, key: t };
    if (rm > best.r) best = { r: rm, key: t | 0x10 };
  }
  return best.key;
}
const NAMES = ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"];
export const keyLabel = (k) => `${NAMES[k & 15]} ${k & 0x10 ? "minor" : "major"}`;
export const themeName = (file) => String(file).replace(/\.[^.]*$/, "").toUpperCase().replace(/[^A-Z0-9 _-]/g, "").slice(0, 8) || "MIDI";

// ---- the bank ----------------------------------------------------------------------------------------------------------------
// data: 4 bytes the FM-1's silent zone reads, n u16 offsets (from the data), each theme {name[8], key, count, count x
// {at, len, note, vel}}; the header: one zone (2 samples, silent), "MELODIES", the MEL1 meta {magic, n} at 480
export function bankImage(themes) {
  themes = themes.filter((t) => t.ev.length).slice(0, MAX_THEMES);
  if (!themes.length) throw new Error("no melody to send");
  const size = 4 + 2 * themes.length + themes.reduce((s, t) => s + REC + 4 * Math.min(t.ev.length, MAX_EV), 0);
  if (size > 0xFFFF) throw new Error("too many melodies for one bank");
  const data = new Uint8Array(size), hdr = new Uint8Array(512), v = new DataView(hdr.buffer);
  let o = 4 + 2 * themes.length;
  themes.forEach((t, i) => {
    data[4 + 2 * i] = o & 0xFF;
    data[5 + 2 * i] = o >> 8;
    const nm = String(t.name).toUpperCase().replace(/[^\x20-\x7E]/g, "").slice(0, 8);
    for (let k = 0; k < nm.length; k++) data[o + k] = nm.charCodeAt(k);
    const ev = t.ev.slice(0, MAX_EV);
    data[o + 8] = t.key & 0x1F;
    data[o + 9] = ev.length;
    ev.forEach((e, k) => data.set([e.at, e.len, e.note, e.vel], o + REC + 4 * k));
    o += REC + 4 * ev.length;
  });
  v.setUint32(0, 0x504D5346, true);                 // "FSMP", version 1, one zone
  v.setUint16(4, 1, true);
  hdr[6] = 1;
  for (let k = 0; k < 8; k++) hdr[8 + k] = "MELODIES".charCodeAt(k);
  v.setUint32(16, size, true);
  v.setUint32(20, crc32(data), true);
  v.setUint32(32 + 4, 2, true);                     // the zone: 2 samples at 0, a loop of them, 44.1 kHz, C4
  v.setUint32(32 + 12, 1, true);
  v.setUint32(32 + 16, 65536, true);
  v.setInt16(32 + 20, 60 * 16, true);
  v.setUint32(480, MEL_MAGIC, true);
  hdr[484] = themes.length;
  return { hdr, data };
}
export function readBank(hdr, data) {
  const v = new DataView(hdr.buffer, hdr.byteOffset, hdr.byteLength);
  if (hdr.byteLength < 485 || v.getUint32(480, true) !== MEL_MAGIC) return null;
  const out = [];
  for (let i = 0; i < hdr[484]; i++) {
    const o = data[4 + 2 * i] | data[5 + 2 * i] << 8;
    let name = "";
    for (let k = 0; k < 8 && data[o + k] >= 32 && data[o + k] < 127; k++) name += String.fromCharCode(data[o + k]);
    const ev = [];
    for (let k = 0; k < data[o + 9]; k++) {
      const q = o + REC + 4 * k;
      ev.push({ at: data[q], len: data[q + 1], note: data[q + 2], vel: data[q + 3] });
    }
    out.push({ name, key: data[o + 8], ev });
  }
  return out;
}
