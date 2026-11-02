// SPDX-License-Identifier: GPL-3.0-only
// FMX CHOP SLICE loops in the browser: a loop (an acapella, a perc loop, a riff) cut into slices on its hits, one zone a
// slice on its own key (36, 37 ...: the CHOP track's SLICE mode plays them by number, firmware/src/chopgen.c), with what
// the page found in it written after the zones (cs_meta_t "SLC1", the 32 bytes at 480: the bars, the key, each slice's
// note) for TUNE. Pure functions: web/test_slice.mjs runs them, web/slice.html is the page.
//   the hits   spectral flux onsets (detectHits); the slices as many as before (a beat each, or 4 / 8 / 16), each cut
//              moved off its grid line onto a hit not quite on it (placeCuts); the page's lines can be moved by hand.
//   the key    the spectrum's peaks (chords too) as pitch classes, then the Krumhansl-Kessler major / minor profiles:
//              the best of the 24 keys, a key or its relative by the bass. Peaks whose tunings disagree (drums, noise,
//              rap) or no profile fitting: "no key" (TUNE leaves it alone).
//   the notes  a slice of one voice: its median YIN pitch (de Cheveigné & Kawahara 2002), to the nearest semitone after
//              the loop's own tuning (a voice a little flat of A440 is still its notes); none where under a third of it
//              has one pitch (chords: KEY moves them whole, SNAP leaves them).
import { SLOT_RATE, SLOT_ZONES, SLOT_DATA_OFF, SLOT_MAX_DATA, imaEncode, crc32, resample } from "./breaks.js";

export const META_AT = 480, META_MAGIC = 0x31434C53, NOKEY = 0xFF, KEY0 = 36;
export const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const FADE_IN = 22, FADE_OUT = 132;                 // 1 / 6 ms at 22.05 kHz (the editor's CHOP)

// the bar count (1, 2, 4, 8) that puts the loop's tempo nearest 170 inside 80-180 (breaks.js barsOf, with 8: a long
// acapella phrase)
export function barsOf(seconds) {
  let best = null;
  for (const b of [1, 2, 4, 8]) {
    const bpm = b * 240.0 / seconds;
    if (bpm >= 80 && bpm <= 180 && (best === null || Math.abs(bpm - 170) < Math.abs(best[1] - 170))) best = [b, bpm];
  }
  return best || [1, 240.0 / seconds];
}

// the slices for one a beat (SLICE plays a slice a beat, its edits a beat long): 4 a bar, 16 at most (a longer loop
// is cut to its first 4 bars, sliceLoop)
export const beatSlices = (bars) => Math.min(16, 4 * bars);

// mono floats at rate -> floats at the slot's rate, peak 0.95
export function prepare(x, rate) {
  const y = resample(Array.from(x), rate, SLOT_RATE);
  let pk = 1e-9;
  for (const v of y) pk = Math.max(pk, Math.abs(v));
  return Float64Array.from(y, (v) => v * 0.95 / pk);
}

// how much of a loop of `total` samples, `bars` bars, one slot holds as n slices: its first half (quarter ...) while the
// whole is too long -> {bars, len}
export function fitBars(total, bars, n) {
  const fits = (L) => n * Math.ceil(Math.floor(L / n) / 2) <= SLOT_MAX_DATA;
  let b = bars, len = total;
  while (!fits(len) && b > 1 && 16 * (b >> 1) >= n) { b >>= 1; len = Math.round(total * b / bars); }
  return { bars: b, len };
}

// the loop (y at SLOT_RATE, `bars` bars long) cut into n slices that fit one slot (fitBars), each from its cut (the
// samples where they start, ascending; none: n equal ones) to the next, the longest slices' tails cut while they are
// still too long -> {bars, slices: [Int16Array], per (samples an equal slice), cuts, end (where the last ends), cut
// (bars dropped)}. The FM-1 still starts slice i on its own beat (chopgen.c): a cut off its grid line plays there.
// fit: the {bars, len} the cuts were placed in (fitBars for another count: the lines moved, some taken away)
export function sliceLoop(y, bars, n, cuts = null, fit = null) {
  const { bars: b, len } = fit || fitBars(y.length, bars, n), per = Math.floor(len / n);
  const at = cuts && cuts.length === n ? cuts.slice() : Array.from({ length: n }, (_, i) => i * per);
  const end = cuts && cuts.length === n ? len : n * per, lens = at.map((a, i) => Math.max(1, (i + 1 < n ? at[i + 1] : end) - a));
  const bytes = (cap) => lens.reduce((s, l) => s + Math.ceil(Math.min(l, cap) / 2), 0);
  let lo = 1, hi = Math.max(...lens);
  while (lo < hi) { const m = (lo + hi + 1) >> 1; if (bytes(m) <= SLOT_MAX_DATA) lo = m; else hi = m - 1; }
  const slices = at.map((a, i) => {
    const take = Math.min(lens[i], lo), s = new Int16Array(take);
    for (let j = 0; j < take; j++) {
      let g = 1;
      if (j < FADE_IN) g = j / FADE_IN;
      if (j >= take - FADE_OUT) g = Math.min(g, (take - j) / FADE_OUT);
      s[j] = Math.max(-32768, Math.min(32767, Math.trunc((y[a + j] || 0) * 32767 * g)));
    }
    return s;
  });
  return { bars: b, slices, per, cuts: at, end, cut: bars - b };
}

// ---- the hits -------------------------------------------------------------------------------------------------------
// the attacks the slices start on: the spectral flux of the log spectrum (frames of OW every OHOP, centred), its peaks
// over the local median; each moved to where its attack leaves the quiet before it (the envelope a quarter of the way
// up), so a slice starts just ahead of its hit, then back to a zero crossing.
const OW = 1024, OHOP = 128, OMED = 16, OPEAK = 3, OGAP = 0.04, OREL = 0.06;

// y (at rate) -> [{t (the attack's first sample), s (strength, 1 the loudest)}], ascending
export function detectHits(y, rate = SLOT_RATE) {
  const win = Float64Array.from({ length: OW }, (_, i) => 0.5 - 0.5 * Math.cos(2 * Math.PI * i / OW));
  const K = OW / 2, re = new Float64Array(OW), im = new Float64Array(OW), prev = new Float64Array(K + 1), flux = [];
  const L = y.length;
  for (let c = -OHOP; c < L; c += OHOP) {             // (round the loop's ends: its end runs into its start)
    for (let i = 0; i < OW; i++) { re[i] = y[(((c - K + i) % L) + L) % L] * win[i]; im[i] = 0; }
    fft(re, im);
    let f = 0;
    for (let k = 1; k <= K; k++) {
      const m = Math.log1p(100 * Math.hypot(re[k], im[k]));
      if (m > prev[k]) f += m - prev[k];
      prev[k] = m;
    }
    if (c >= 0) flux.push(f);
  }
  const top = Math.max(1e-9, ...flux), F = flux.map((v) => v / top), peaks = [];
  for (let i = 1; i < F.length; i++) {
    let isPk = true;
    for (let j = Math.max(0, i - OPEAK); j <= Math.min(F.length - 1, i + OPEAK) && isPk; j++) if (F[j] > F[i]) isPk = false;
    if (!isPk) continue;
    const w = F.slice(Math.max(0, i - OMED), i + OMED + 1).sort((p, q) => p - q), med = w[w.length >> 1];
    if (F[i] - med < OREL) continue;
    const p = peaks[peaks.length - 1];
    if (p && (i - p.i) * OHOP < OGAP * rate) { if (F[i] > p.s) { p.i = i; p.s = F[i]; } continue; }
    peaks.push({ i, s: F[i] });
  }
  const out = [];
  for (const p of peaks) {
    let t = attackAt(y, p.i * OHOP);
    if (t >= L - 0.01 * rate) continue;           // (the loop's own start, coming round: cut 0 is there)
    t = zeroBefore(y, t, rate);
    if (!out.length || t - out[out.length - 1].t >= OGAP * rate / 2) out.push({ t, s: p.s });
  }
  const sm = Math.max(1e-9, ...out.map((h) => h.s));
  return out.map((h) => ({ t: h.t, s: h.s / sm }));
}
// the start of the attack near c, 3 ms ahead of it: the peak of blocks of 160 (every 16: half a period of 70 Hz, so a
// low note's own swing is not a rise; past the end, the loop's start), the first after the quiet a quarter of the way to
// the loudest, at the block's end (where its peak came in)
function attackAt(y, c) {
  const B = 160, H = 16, L = y.length, lo = Math.max(0, c - 768), hi = c + 512, e = [];
  for (let p = lo; p <= hi; p += H) {
    let s = 0;
    for (let j = 0; j < B; j++) s = Math.max(s, Math.abs(y[(p + j) % L]));
    e.push(s);
  }
  let pk = Math.max(0, Math.floor((c - 256 - lo) / H));
  for (let k = pk; k < e.length; k++) if (e[k] > e[pk]) pk = k;
  let q = 0;
  for (let k = 1; k <= pk; k++) if (e[k] < e[q]) q = k;
  let k = q;
  while (k < pk && e[k] <= e[q] + 0.25 * (e[pk] - e[q])) k++;
  return Math.max(0, lo + k * H + B - H / 2 - 66);
}
// t moved back to a zero crossing, 2 ms at most
function zeroBefore(y, t, rate = SLOT_RATE) {
  t = Math.max(0, Math.min(y.length - 1, Math.round(t)));
  for (let j = t; j > 0 && j > t - rate * 0.002; j--) if (y[j - 1] <= 0 !== y[j] <= 0 || y[j] === 0) return j;
  return t;
}

// the n cuts of a loop len long: the first at 0, each other on its grid line (the n equal slices), moved onto the hit
// that is not quite on it: a hit on the line (15 ms) keeps it there; else the hit within 0.3 of a slice of it, the
// loudest, the nearer counting more (a note pushed ahead of the beat, one dragging behind it); no hit near: the line
export function placeCuts(hits, len, n) {
  const cell = len / n, tol = 0.3 * cell, on = Math.min(tol, SLOT_RATE * 0.015), cuts = [0], gap = Math.min(cell / 4, SLOT_RATE * OGAP);
  for (let i = 1; i < n; i++) {
    const g = i * cell, near = hits.filter((h) => Math.abs(h.t - g) < tol && h.t < len && h.t >= cuts[i - 1] + gap);
    const at = near.filter((h) => Math.abs(h.t - g) <= on), from = at.length ? at : near;
    let best = Math.round(g), bs = 0;
    for (const h of from) {
      const sc = h.s * (1 - 0.6 * Math.abs(h.t - g) / tol);
      if (sc > bs) { bs = sc; best = h.t; }
    }
    cuts.push(best);
  }
  return cuts;
}

// ---- pitch ----------------------------------------------------------------------------------------------------------
const W = 1024, HOP = 512, FMIN = 70, FMAX = 1000, YIN_THR = 0.15;

// YIN over frames of W every HOP -> [{t (first sample), f0 (Hz, 0 = none), clar (1 - the dip), rms}]
export function pitchTrack(y, rate = SLOT_RATE) {
  const tmin = Math.floor(rate / FMAX), tmax = Math.ceil(rate / FMIN), out = [], d = new Float64Array(tmax + 2);
  for (let t = 0; t + W + tmax < y.length; t += HOP) {
    let e = 0;
    for (let j = 0; j < W; j++) e += y[t + j] * y[t + j];
    const rms = Math.sqrt(e / W);
    if (rms < 0.01) { out.push({ t, f0: 0, clar: 0, rms }); continue; }
    d[0] = 1;
    let run = 0, best = -1;
    for (let tau = 1; tau <= tmax; tau++) {         // the cumulative-mean-normalised difference
      let s = 0;
      for (let j = 0; j < W; j++) { const q = y[t + j] - y[t + j + tau]; s += q * q; }
      run += s;
      d[tau] = run ? s * tau / run : 1;
    }
    for (let tau = tmin; tau < tmax; tau++)
      if (d[tau] < YIN_THR) {
        while (tau + 1 < tmax && d[tau + 1] < d[tau]) tau++;
        best = tau;
        break;
      }
    if (best < 0) { out.push({ t, f0: 0, clar: 0, rms }); continue; }
    const a = d[best - 1], b = d[best], c = d[best + 1], den = a - 2 * b + c;   // the dip between samples
    const tt = den ? best + 0.5 * (a - c) / den : best;
    out.push({ t, f0: rate / tt, clar: Math.max(0, 1 - b), rms });
  }
  return out;
}
const midiOf = (f) => 69 + 12 * Math.log2(f / 440);

// ---- the key --------------------------------------------------------------------------------------------------------
const KK_MAJ = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];
const KK_MIN = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];
function corr(a, b) {
  const n = a.length, ma = a.reduce((s, v) => s + v, 0) / n, mb = b.reduce((s, v) => s + v, 0) / n;
  let x = 0, ya = 0, yb = 0;
  for (let i = 0; i < n; i++) { x += (a[i] - ma) * (b[i] - mb); ya += (a[i] - ma) ** 2; yb += (b[i] - mb) ** 2; }
  return ya && yb ? x / Math.sqrt(ya * yb) : 0;
}

// ---- the spectrum: the key of chords too ----------------------------------------------------------------------------
const N = 4096, SHOP = 2048, PMIN = 55, PMAX = 2000;
function fft(re, im) {                              // in place, radix 2
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let b = n >> 1;
    for (; j & b; b >>= 1) j ^= b;
    j ^= b;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const p = i + k, q = p + len / 2, tr = re[q] * cr - im[q] * ci, ti = re[q] * ci + im[q] * cr;
        re[q] = re[p] - tr; im[q] = im[p] - ti; re[p] += tr; im[p] += ti;
        const t = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = t;
      }
    }
  }
}
// the spectral peaks of y (55 Hz..2 kHz, over a tenth of the frame's loudest) -> [{t (the frame), midi (fractional), w}]
export function spectralPeaks(y, rate = SLOT_RATE) {
  const win = Float64Array.from({ length: N }, (_, i) => 0.5 - 0.5 * Math.cos(2 * Math.PI * i / N)), out = [];
  const k0 = Math.ceil(PMIN * N / rate), k1 = Math.floor(PMAX * N / rate), re = new Float64Array(N), im = new Float64Array(N);
  const m = new Float64Array(k1 + 2);
  for (let t = 0; t + N <= y.length; t += SHOP) {
    for (let i = 0; i < N; i++) { re[i] = y[t + i] * win[i]; im[i] = 0; }
    fft(re, im);
    let top = 0;
    for (let k = 1; k <= k1 + 1; k++) { m[k] = Math.hypot(re[k], im[k]); if (k >= k0 && m[k] > top) top = m[k]; }
    if (top < 1e-3) continue;
    for (let k = k0; k <= k1; k++)
      if (m[k] > m[k - 1] && m[k] >= m[k + 1] && m[k] > 0.1 * top) {
        const a = Math.log(m[k - 1] + 1e-12), b = Math.log(m[k]), c = Math.log(m[k + 1] + 1e-12), den = a - 2 * b + c;
        out.push({ t, midi: midiOf((k + (den ? 0.5 * (a - c) / den : 0)) * rate / N), w: m[k] });
      }
  }
  return out;
}

// peaks (and the YIN frames, for how much of it is one voice) -> {key (pc | 0x10 minor, or NOKEY), cents (the loop's
// tuning off A440), conc (how alike the peaks' tunings are: 1 a tuned instrument, low for noise and drums), voiced,
// score, margin (over the next key that is not its relative), chroma}. A key and its relative are told apart by the bass
// (each frame's lowest peak): the one whose tonic it plays more.
export function detectKey(peaks, frames = []) {
  let sx = 0, sy = 0, sw = 0, all = 0, voiced = 0;
  for (const p of peaks) {                          // the tuning: the mean of the cents, on a circle
    const c = (p.midi % 1 + 1) % 1;
    sx += p.w * Math.cos(2 * Math.PI * c);
    sy += p.w * Math.sin(2 * Math.PI * c);
    sw += p.w;
  }
  let cents = Math.atan2(sy, sx) / (2 * Math.PI) * 100;
  if (cents > 50) cents -= 100;
  const conc = sw ? Math.hypot(sx, sy) / sw : 0, pc = (m) => ((Math.round(m - cents / 100) % 12) + 12) % 12;
  const chroma = new Array(12).fill(0), bass = new Array(12).fill(0), low = new Map();
  for (const p of peaks) {
    chroma[pc(p.midi)] += p.w;
    const l = low.get(p.t);
    if (!l || p.midi < l.midi) low.set(p.t, p);
  }
  for (const p of low.values()) bass[pc(p.midi)] += p.w;
  for (const f of frames) { all += f.rms; if (f.f0) voiced += f.rms * f.clar; }
  const scores = [];
  for (let k = 0; k < 12; k++) {
    const rot = chroma.slice(k).concat(chroma.slice(0, k));
    scores.push({ key: k, r: corr(rot, KK_MAJ) }, { key: k | 0x10, r: corr(rot, KK_MIN) });
  }
  scores.sort((p, q) => q.r - p.r);
  const rel = (k) => (k & 0x10 ? ((k & 15) + 3) % 12 : (((k & 15) + 9) % 12) | 0x10);
  let top = scores[0];
  const other = scores.find((s) => s.key === rel(top.key));
  if (other && top.r - other.r < 0.05 && bass[other.key & 15] > bass[top.key & 15]) top = other;
  const next = scores.find((s) => s.key !== top.key && s.key !== rel(top.key));
  const key = conc >= 0.65 && top.r >= 0.5 ? top.key : NOKEY;
  return { key, cents, conc, voiced: all ? voiced / all : 0, score: top.r, margin: top.r - (next ? next.r : 0), chroma };
}

// each slice's note (MIDI, or NOKEY): the median over its voiced frames, after the loop's tuning. per: the samples of an
// equal slice, or the slices' bounds (the cuts, then the end)
export function sliceNotes(frames, nSlices, per, cents = 0) {
  const out = [];
  for (let i = 0; i < nSlices; i++) {
    const a = Array.isArray(per) ? per[i] : i * per, b = Array.isArray(per) ? per[i + 1] : a + per, ms = [];
    let all = 0, v = 0;
    for (const f of frames) {
      const mid = f.t + W / 2;
      if (mid < a || mid >= b) continue;
      all++;
      if (f.f0 && f.clar > 0.5) { v++; ms.push(midiOf(f.f0) - cents / 100); }
    }
    if (!all || v / all < 1 / 3) { out.push(NOKEY); continue; }
    ms.sort((p, q) => p - q);
    out.push(Math.max(0, Math.min(127, Math.round(ms[ms.length >> 1]))));
  }
  return out;
}

export const keyName = (k) => (k === NOKEY || k == null ? "no key" : NOTE_NAMES[k & 15] + (k & 0x10 ? "m" : ""));
export const noteName = (m) => (m === NOKEY ? "–" : NOTE_NAMES[m % 12] + (Math.floor(m / 12) - 1));

// ---- the slot -------------------------------------------------------------------------------------------------------
// cs_meta_t: magic, key, bars, n, 0, note[16], 8 bytes 0
export function sliceMeta(key, bars, notes) {
  const m = new Uint8Array(32), v = new DataView(m.buffer);
  v.setUint32(0, META_MAGIC, true);
  m[4] = key; m[5] = bars; m[6] = notes.length; m[7] = 0;
  for (let i = 0; i < 16; i++) m[8 + i] = i < notes.length ? notes[i] : NOKEY;
  return m;
}
export function parseMeta(hdr) {
  if (!hdr || hdr.length < META_AT + 32) return null;
  const v = new DataView(hdr.buffer, hdr.byteOffset, hdr.byteLength);
  if (v.getUint32(META_AT, true) !== META_MAGIC) return null;
  const n = hdr[META_AT + 6];
  return { key: hdr[META_AT + 4], bars: hdr[META_AT + 5], n, notes: Array.from(hdr.subarray(META_AT + 8, META_AT + 8 + n)) };
}

// slices -> {hdr (512 B: the 480 of the header, then the meta), data}: zone i on key KEY0 + i alone (lo = hi = root), the slot's
// name not "BR..." (the break loops page would take it for its own)
export function sliceSlot(name, slices, meta) {
  if (slices.length < 1 || slices.length > SLOT_ZONES) throw new Error(`1..${SLOT_ZONES} slices`);
  const parts = slices.map((s) => imaEncode(s)), total = parts.reduce((a, p) => a + p.length, 0);
  if (total > SLOT_MAX_DATA) throw new Error(`too long: ${total} B of ADPCM, a slot holds ${SLOT_MAX_DATA} B`);
  const data = new Uint8Array(total), hdr = new Uint8Array(SLOT_DATA_OFF), v = new DataView(hdr.buffer);
  const rate = Math.round(SLOT_RATE / 44100 * 65536);
  let nm = String(name).toUpperCase().replace(/[^\x20-\x7E]/g, "").trim() || "SLICES";
  if (/^BR/.test(nm)) nm = "S" + nm;
  nm = nm.slice(0, 8);
  v.setUint32(0, 0x504D5346, true);
  v.setUint16(4, 1, true);
  hdr[6] = slices.length;
  for (let i = 0; i < nm.length; i++) hdr[8 + i] = nm.charCodeAt(i);
  let off = 0;
  parts.forEach((p, i) => {
    data.set(p, off);
    const o = 32 + i * 28, n = slices[i].length;
    v.setUint32(o, off, true);
    v.setUint32(o + 4, n, true);
    v.setUint32(o + 8, 0, true);
    v.setUint32(o + 12, n - 1, true);
    v.setUint32(o + 16, rate, true);
    v.setInt16(o + 20, (KEY0 + i) * 16, true);
    v.setInt16(o + 22, 0, true);
    hdr[o + 24] = 0;
    hdr[o + 25] = hdr[o + 26] = KEY0 + i;
    off += p.length;
  });
  v.setUint32(16, total, true);
  v.setUint32(20, crc32(data), true);
  hdr.set(meta, META_AT);
  return { hdr, data };
}

// the whole analysis of a loop: mono floats at rate -> {y, bars, bpm, frames, hits, key...}
export function analyse(x, rate) {
  const y = prepare(x, rate), [bars, bpm] = barsOf(y.length / SLOT_RATE), frames = pitchTrack(y);
  return { y, bars, bpm, frames, hits: detectHits(y), ...detectKey(spectralPeaks(y), frames) };
}
