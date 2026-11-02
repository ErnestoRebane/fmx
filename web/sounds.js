// SPDX-License-Identifier: GPL-3.0-only
// The companion app's sounds (web/app.html): a dropped file is told apart (a BREAK loop, a LOOP for the CHOP's SLICE, a
// KIT hit, an INSTRUMENT note), built at the quality chosen (22, 16 or 11 kHz: each zone keeps its own rate, which the
// engines read: brkpart.c, eng_slice.c, eng_sample.c) into the pool's entries (the slot format, web/breaks.js), kept in
// this browser's library (IndexedDB), and heard as the FM-1 plays them (the 4-bit ADPCM decoded at its rate).
// The cuts are the pages' that came before: breaks.js (a BREAK loop: its first bar), slice.js (a LOOP: its slices on
// the hits and its key, the lines moved, taken away or added by hand); a kit is a grid of 16 pads, a hit per key from F3
// up (tools/gen_packs.py), an empty pad a moment of silence on its key; an instrument one zone over every key.
import { SLOT_RATE, SLOT_ZONES, SLOT_MAX_DATA, readWav, cut, resample, imaEncode, imaDecode, crc32, barsOf } from "./breaks.js";
import { NOKEY, analyse, beatSlices, fitBars, placeCuts, sliceLoop, sliceNotes, sliceMeta, pitchTrack, keyName, noteName } from "./slice.js";
import { sectorsFor, place, freeId, freeSectors, withEntry } from "./pool.js";

export const TYPES = ["BREAK", "LOOP", "KIT", "INSTRUMENT"];
export const TYPE_COL = { BREAK: "--brk", LOOP: "--chop", KIT: "--kit", INSTRUMENT: "--chd", MELODY: "--mel" };   // (MELODY: the melody bank, melody.js)
export const KHZ = { 22: 22050, 16: 16000, 11: 11025 };
export const KIT_BASE = 53, KIT_PADS = 16;
const BREAK_ROOT = 36, SLICE_ROOT = 36, META_AT = 480, SILENT = 32;
const ONE_SHOT = 1.2;                                 // seconds: shorter, a hit or a note; longer, a loop

// ---- a file -> floats ----------------------------------------------------------------------------------------------
export async function decodeFile(file) {
  const buf = new Uint8Array(await file.arrayBuffer());
  try { return readWav(buf); } catch { /* not a WAV the reader takes: the browser's decoder */ }
  const ab = await new OfflineAudioContext(1, 1, 44100).decodeAudioData(buf.buffer.slice(0));
  const x = new Float64Array(ab.length);
  for (let c = 0; c < ab.numberOfChannels; c++) { const d = ab.getChannelData(c); for (let i = 0; i < d.length; i++) x[i] += d[i] / ab.numberOfChannels; }
  return { rate: ab.sampleRate, x };
}
const nameOf = (file) => file.replace(/\.[^.]*$/, "").toUpperCase().replace(/[^A-Z0-9 ]/g, "").replace(/\s+/g, " ").trim().slice(0, 8) || "SOUND";
const midiOf = (f) => 69 + 12 * Math.log2(f / 440);
const median = (a) => { const s = [...a].sort((p, q) => p - q); return s.length ? s[s.length >> 1] : 0; };

// floats at rate -> floats at 22.05 kHz, peak 0.95
function prepared(x, rate) {
  const y = resample(Array.from(x), rate, SLOT_RATE);
  let pk = 1e-9;
  for (const v of y) pk = Math.max(pk, Math.abs(v));
  return Float64Array.from(y, (v) => v * 0.95 / pk);
}
// int16 (or floats in -1..1) at 22.05 kHz -> int16 at `to`
function atRate(s, to) {
  const f = s instanceof Int16Array ? Array.from(s, (v) => v / 32768) : Array.from(s);
  const y = to === SLOT_RATE ? f : resample(f, SLOT_RATE, to);
  return Int16Array.from(y, (v) => Math.max(-32768, Math.min(32767, Math.round(v * 32767))));
}

// a file's sound -> a card: {file, name, type (the guess, changeable), khz, secs, ...what each type needs}; as: "KIT"
// for a file dropped on a kit's pad (a hit, however long)
export function makeCard(file, x, rate, as = null) {
  const secs = x.length / rate, c = { file, name: nameOf(file), secs, x, rate };
  if (as === "KIT") {
    c.y = prepared(x, rate);
    c.type = "KIT";
    c.khz = 22;
  } else if (secs < ONE_SHOT) {                       // a one-shot: a note (one pitch most of it) or a hit
    c.y = prepared(x, rate);
    const fr = pitchTrack(c.y).filter((f) => f.rms >= 0.02), voiced = fr.filter((f) => f.f0 > 0 && f.clar > 0.8);
    c.root = voiced.length ? Math.max(0, Math.min(127, Math.round(median(voiced.map((f) => midiOf(f.f0)))))) : 60;
    c.type = fr.length && voiced.length / fr.length > 0.5 ? "INSTRUMENT" : "KIT";
    c.khz = 22;
  } else {                                            // a loop: drums (no key) for the BREAK, else the CHOP's SLICE
    c.a = analyse(x, rate);
    c.key = c.a.key;
    c.bars = c.a.bars;
    c.type = c.a.key === NOKEY ? "BREAK" : "LOOP";
    c.khz = c.type === "BREAK" ? 16 : 22;
  }
  return c;
}

// what a card puts in its entry at its quality: zones [{s (int16 at hz), hz, root?}], meta (a LOOP's) and a line of what
// was found; cached until the type or the quality changes
export function cardBuild(c) {
  if (c.type === "LOOP") loopCuts(c);
  const k = `${c.type}/${c.khz}/${c.key}/${c.bars}/${c.type === "LOOP" ? c.cuts.join() : ""}`;
  if (c.built && c.built.k === k) return c.built;
  const hz = KHZ[c.khz];
  let zones, meta = null, what, lines = 0;
  if (c.type === "BREAK") {                           // its first bar (breaks.js cut), as the BREAK expects of a loop
    const [n, bpm] = barsOf(c.secs);
    zones = [{ s: atRate(cut(c.x, c.rate, true), hz), hz }];
    what = `1 bar · ${bpm.toFixed(0)} BPM` + (n > 1 ? ` · cut on the first of ${n} bars` : "");
    lines = 4;
  } else if (c.type === "LOOP") {                     // its lines (loopCuts), its key for TUNE
    const a = c.a, n = c.cuts.length, cuts = sliceLoop(a.y, c.bars, n, c.cuts, c.span);
    const notes = c.key === NOKEY ? cuts.slices.map(() => NOKEY) : sliceNotes(a.frames, n, [...cuts.cuts, cuts.end], a.cents);
    zones = cuts.slices.map((s) => ({ s: atRate(s, hz), hz }));
    meta = sliceMeta(c.key, cuts.bars, notes);
    what = `${cuts.bars} bar${cuts.bars > 1 ? "s" : ""} · ${keyName(c.key)} · ${n} slice${n > 1 ? "s" : ""}, ` +
      (c.edited ? "the lines as you put them" : "on the hits") + (cuts.cut ? ` · its first ${cuts.bars} bars` : "");
    lines = n;
  } else {                                            // a hit, a note: the whole of it
    const y = c.y || (c.y = prepared(c.x, c.rate));
    if (c.root === undefined) c.root = 60;            // (a loop made an instrument: C4 until told)
    zones = [{ s: atRate(y, hz), hz, root: c.type === "INSTRUMENT" ? c.root : undefined }];
    what = c.type === "KIT" ? `a hit · ${c.secs.toFixed(2)} s` : `a note · ${noteName(c.root)} · ${c.secs.toFixed(2)} s`;
  }
  const bytes = zones.reduce((a, z) => a + ((z.s.length + 1) >> 1), 0);
  c.built = { k, zones, meta, what, lines, bytes, blocks: sectorsFor(bytes) };
  return c.built;
}

// ---- a LOOP's lines ---------------------------------------------------------------------------------------------------
// the cuts (samples of c.a.y, at 22.05 kHz; the first at 0): a slice a beat, each line on the hit by its beat (slice.js
// placeCuts), again whenever the bars change; c.span the {bars, len} they lie in, c.edited once moved by hand
export function loopCuts(c) {
  const a = c.a || (c.a = analyse(c.x, c.rate));
  if (c.key === undefined) c.key = a.key;
  if (!c.bars) c.bars = a.bars;
  if (!c.cuts || c.cutsFor !== c.bars) {
    const n = Math.min(beatSlices(c.bars), 16 * c.bars);
    c.span = fitBars(a.y.length, c.bars, n);
    c.cuts = placeCuts(a.hits, c.span.len, n);
    c.cutsFor = c.bars;
    c.edited = false;
  }
  return c.cuts;
}
const LINE_GAP = Math.round(SLOT_RATE * 0.01);     // a slice 10 ms at least
// s moved onto the nearest hit within `magnet` samples (0: where it is)
function snapped(c, s, magnet) {
  let t = Math.round(s), d = magnet;
  for (const h of c.a.hits) if (h.t < c.span.len && Math.abs(h.t - s) < d) { d = Math.abs(h.t - s); t = h.t; }
  return t;
}
// line k (not the first: the loop's start) to s, on a hit within magnet, kept between its neighbours
export function moveCut(c, k, s, magnet = 0) {
  if (k < 1 || k >= c.cuts.length) return;
  const lo = c.cuts[k - 1] + LINE_GAP, hi = (k + 1 < c.cuts.length ? c.cuts[k + 1] : c.span.len) - LINE_GAP;
  c.cuts[k] = Math.max(lo, Math.min(hi, snapped(c, s, magnet)));
  c.edited = true;
}
// line k taken away: the slice before it runs on to the next
export function dropCut(c, k) {
  if (k < 1 || k >= c.cuts.length) return;
  c.cuts.splice(k, 1);
  c.edited = true;
}
// a line at s (on a hit within magnet), 16 at most, not on top of another -> its index, or -1
export function addCut(c, s, magnet = 0) {
  if (c.cuts.length >= SLOT_ZONES) return -1;
  const t = snapped(c, s, magnet);
  if (t < LINE_GAP || t > c.span.len - LINE_GAP || c.cuts.some((q) => Math.abs(q - t) < LINE_GAP)) return -1;
  const k = c.cuts.findIndex((q) => q > t), at = k < 0 ? c.cuts.length : k;
  c.cuts.splice(at, 0, t);
  c.edited = true;
  return at;
}
export const resetCuts = (c) => { c.cuts = null; loopCuts(c); };

// ---- kits: a grid of 16 pads -------------------------------------------------------------------------------------------
// {name, khz (the whole kit's), pads: [card | null]}: pad i plays on key F3 + i; an empty pad before the last hit is a
// moment of silence there (the FM-1 takes a kit's zones as keys in a row from the lowest, eng_sample.c smp_kit)
export const newKit = (name) => ({ name, khz: 22, pads: new Array(KIT_PADS).fill(null) });
export const kitUsed = (kit) => kit.pads.some(Boolean);
export function kitBuild(kit) {
  const hz = KHZ[kit.khz], last = kit.pads.reduce((m, c, i) => (c ? i : m), -1);
  const zones = kit.pads.slice(0, last + 1).map((c) => {
    if (!c) return { s: new Int16Array(SILENT), hz };
    c.khz = kit.khz;
    return { ...cardBuild(c).zones[0] };
  });
  const bytes = zones.reduce((a, z) => a + ((z.s.length + 1) >> 1), 0);
  return { zones, bytes, blocks: sectorsFor(bytes), hits: kit.pads.filter(Boolean).length,
    fail: bytes > SLOT_MAX_DATA ? `${kit.name} is too long for one sound (${Math.ceil(bytes / 1024)} KB, ${Math.floor(SLOT_MAX_DATA / 1024)} at most): a lower quality, or shorter hits` : "" };
}

// ---- entries ---------------------------------------------------------------------------------------------------------
// zones [{s, hz, root, lo, hi}] -> {hdr (512 bytes: the header, then the SLICE meta or 0), data}
export function buildImage(name, zones, meta = null) {
  if (zones.length < 1 || zones.length > SLOT_ZONES) throw new Error(`1..${SLOT_ZONES} zones`);
  const parts = zones.map((z) => imaEncode(z.s)), total = parts.reduce((a, p) => a + p.length, 0);
  const data = new Uint8Array(total), hdr = new Uint8Array(512), v = new DataView(hdr.buffer);
  v.setUint32(0, 0x504D5346, true);
  v.setUint16(4, 1, true);
  hdr[6] = zones.length;
  const nm = String(name).toUpperCase().replace(/[^\x20-\x7E]/g, "").slice(0, 8);
  for (let i = 0; i < nm.length; i++) hdr[8 + i] = nm.charCodeAt(i);
  let off = 0;
  zones.forEach((z, i) => {
    data.set(parts[i], off);
    const o = 32 + i * 28, n = z.s.length;
    v.setUint32(o, off, true);
    v.setUint32(o + 4, n, true);
    v.setUint32(o + 8, 0, true);                      // loop start, end: none (the whole, once)
    v.setUint32(o + 12, n - 1, true);
    v.setUint32(o + 16, Math.round(z.hz / 44100 * 65536), true);
    v.setInt16(o + 20, z.root * 16, true);
    v.setInt16(o + 22, 0, true);                      // predictor and index at the loop start: from 0
    hdr[o + 24] = 0;
    hdr[o + 25] = z.lo;
    hdr[o + 26] = z.hi;
    off += parts[i].length;
  });
  v.setUint32(16, total, true);
  v.setUint32(20, crc32(data), true);
  if (meta) hdr.set(meta, META_AT);
  return { hdr, data };
}
const keySplit = (roots) => roots.map((r, j) => [j === 0 ? 0 : ((roots[j - 1] + r) >> 1) + 1, j === roots.length - 1 ? 127 : (r + roots[j + 1]) >> 1]);

// the cards and the kits -> the entries they make, in order: BREAK loops packed into entries of up to 16 (80 KB; named
// BR.. by the plan), KIT hits not on a kit's grid into kits of up to 16 (a key each from F3), each kit (kitBuild), each
// LOOP and INSTRUMENT its own -> [{type, name, im, cards}]
export function entriesOf(cards, kits = []) {
  const out = [], packed = (type) => {
    let cur = null;
    for (const c of cards.filter((q) => q.type === type)) {
      const b = cardBuild(c);
      if (!cur || cur.cards.length >= SLOT_ZONES || cur.bytes + b.bytes > SLOT_MAX_DATA) out.push(cur = { type, cards: [], bytes: 0 });
      cur.cards.push(c);
      cur.bytes += b.bytes;
    }
  };
  packed("BREAK");
  packed("KIT");
  for (const k of kits) if (kitUsed(k)) out.push({ type: "KIT", kit: k, cards: k.pads.filter(Boolean), zones: kitBuild(k).zones });
  for (const c of cards) if (c.type === "LOOP" || c.type === "INSTRUMENT") out.push({ type: c.type, cards: [c] });
  for (const e of out) {
    const zs = e.zones || e.cards.flatMap((c) => cardBuild(c).zones);
    if (e.type === "BREAK") {
      const sp = keySplit(zs.map((_, i) => BREAK_ROOT + i));
      zs.forEach((z, i) => Object.assign(z, { root: BREAK_ROOT + i, lo: sp[i][0], hi: sp[i][1] }));
    } else if (e.type === "KIT") zs.forEach((z, i) => Object.assign(z, { root: KIT_BASE + i, lo: KIT_BASE + i, hi: KIT_BASE + i }));
    else if (e.type === "LOOP") zs.forEach((z, i) => Object.assign(z, { root: SLICE_ROOT + i, lo: SLICE_ROOT + i, hi: SLICE_ROOT + i }));
    else Object.assign(zs[0], { lo: 0, hi: 127 });
    e.name = e.type === "BREAK" ? "BR" : (e.kit ? e.kit.name : e.cards[0].name).replace(/^BR/, "S");   // (a name BR.. is the BREAK's)
    delete e.zones;
    e.im = buildImage(e.name, zs, e.type === "LOOP" ? cardBuild(e.cards[0]).meta : null);
  }
  return out;
}
export const renamed = (im, name) => {                // the same image under another name (the CRC is the data's)
  const hdr = im.hdr.slice();
  hdr.fill(0, 8, 16);
  for (let i = 0; i < Math.min(8, name.length); i++) hdr[8 + i] = name.charCodeAt(i);
  return { hdr, data: im.data };
};

// entries [{type, name, im, cards?}] placed in the pool `dir` (pool.js readDir): each in the smallest free piece that
// takes it, a BREAK entry named BR.. after the ones there and an id above theirs (the BREAK numbers its loops by the
// entries' ids) -> {writes: [{id, at, ns, im, type, name, cards}], left (free sectors after), fail (why not, if not)}
export function planSend(dir, entries) {
  let sim = dir, last = -1, n = dir.entries.filter((e) => e.loops).length;
  for (const e of dir.entries) if (e.loops) last = Math.max(last, e.id);
  const writes = [], taken = [];
  for (const e of entries) {
    let im = e.im, name = e.name;
    if (e.type === "BREAK") { name = `BR${++n}`; im = renamed(im, name); }
    const ns = sectorsFor(im.data.length);
    let id = e.type === "BREAK" ? freeId(sim, last, taken) : -1;
    if (id < 0) id = freeId(sim, -1, taken);
    if (id < 0) return { writes, fail: "The FM-1 holds as many sounds as it can (80). Remove one first." };
    const at = place(sim, ns);
    if (at < 0) return { writes, fail: `Not enough room in one piece for ${name} (${ns * 4} KB). Remove a sound, or pick a lower quality.` };
    sim = withEntry(sim, { id, sect: at, nsect: ns });
    if (e.type === "BREAK") last = id;
    taken.push(id);
    writes.push({ id, at, ns, im, type: e.type, name, cards: e.cards });
  }
  return { writes, left: freeSectors(sim) };
}

// an image's header -> {name, nz, dataLen, crc, hz, type, zones: [{off, n, hz, root, lo, hi}]}, or null
export function parseImage(hdr) {
  const v = new DataView(hdr.buffer, hdr.byteOffset, hdr.byteLength);
  if (hdr.byteLength < 480 || v.getUint32(0, true) !== 0x504D5346 || !hdr[6] || hdr[6] > SLOT_ZONES) return null;
  let name = "";
  for (let i = 8; i < 16 && hdr[i]; i++) name += String.fromCharCode(hdr[i]);
  const zones = [];
  for (let i = 0; i < hdr[6]; i++) {
    const o = 32 + i * 28;
    zones.push({ off: v.getUint32(o, true), n: v.getUint32(o + 4, true), hz: Math.round(v.getUint32(o + 16, true) * 44100 / 65536),
      root: v.getInt16(o + 20, true) / 16, lo: hdr[o + 25], hi: hdr[o + 26] });
  }
  const slice = hdr.byteLength >= 484 && v.getUint32(META_AT, true) === 0x31434C53;
  const mel = hdr.byteLength >= 484 && v.getUint32(META_AT, true) === 0x314C454D;
  const type = typeOf({ name, slice, mel, kit: zones.every((z) => z.lo === z.hi), loops: /^BR/.test(name) });
  return { name, nz: hdr[6], dataLen: v.getUint32(16, true), crc: v.getUint32(20, true), hz: zones[0].hz, type, zones };
}
// the type of a pool entry from its directory flags (pool.js parseDir) or its header
export const typeOf = (e) => (e.mel ? "MELODY" : e.loops ? "BREAK" : e.slice ? "LOOP" : e.kit ? "KIT" : "INSTRUMENT");

// ---- listening: as the FM-1 plays it --------------------------------------------------------------------------------
let actx = null, playing = [];
export function stopSound() { for (const s of playing) try { s.stop(); } catch { /* done */ } playing = []; }
// an image -> its zones played: a BREAK's loops one after another, a LOOP's slices a beat apart (the slice a beat at
// the loop's own tempo), a KIT's hits a quarter second apart, an INSTRUMENT its note
export function playImage(im, type) {
  stopSound();
  if (!actx) actx = new AudioContext();
  actx.resume();
  const h = parseImage(im.hdr);
  if (!h) return 0;
  let t = actx.currentTime + 0.05;
  const t0 = t, beat = type === "LOOP" ? Math.max(...h.zones.map((z) => z.n / z.hz)) : 0;
  const zones = type === "INSTRUMENT" ? h.zones.slice(0, 1) : h.zones;
  for (const z of zones) {
    const pcm = imaDecode(im.data.subarray(z.off, z.off + ((z.n + 1) >> 1)), z.n);
    const b = actx.createBuffer(1, Math.max(1, z.n), Math.max(3000, z.hz));
    const ch = b.getChannelData(0);
    for (let i = 0; i < z.n; i++) ch[i] = pcm[i] / 32768;
    const src = actx.createBufferSource();
    src.buffer = b;
    src.connect(actx.destination);
    src.start(t);
    playing.push(src);
    t += type === "LOOP" ? beat : type === "KIT" ? Math.max(0.25, Math.min(z.n / z.hz, 0.5)) : z.n / z.hz;
  }
  return t - t0;
}
// a card heard alone, at its quality
export function playCard(c) {
  const [e] = entriesOf([c]);
  return playImage(e.im, c.type);
}
// a kit's hits, one after another
export function playKit(kit) {
  const [e] = entriesOf([], [kit]);
  return e ? playImage(e.im, "KIT") : 0;
}
// a LOOP's slice i alone, as the FM-1 plays it (through the ADPCM)
export function playSlice(c, i) {
  const z = cardBuild(c).zones[i];
  if (!z) return 0;
  return playImage(buildImage("SLICE", [{ ...z, root: 60, lo: 0, hi: 127 }]), "INSTRUMENT");
}

// ---- the library: the sounds kept in this browser (IndexedDB "fmx-library"), by their data's CRC ------------------------
// {crc, name, type, hz, hdr, data, from (the files), added}
let dbp = null;
function db() {
  if (!dbp) dbp = new Promise((res, rej) => {
    const r = indexedDB.open("fmx-library", 1);
    r.onupgradeneeded = () => r.result.createObjectStore("sounds", { keyPath: "crc" });
    r.onsuccess = () => res(r.result);
    r.onerror = () => rej(r.error);
  });
  return dbp;
}
async function store(mode, fn) {
  const d = await db();
  return new Promise((res, rej) => {
    const tx = d.transaction("sounds", mode), r = fn(tx.objectStore("sounds"));
    tx.oncomplete = () => res(r && r.result);
    tx.onerror = () => rej(tx.error);
  });
}
export async function libAll() { try { return (await store("readonly", (s) => s.getAll())) || []; } catch { return []; } }
export async function libPut(im, extra = {}) {
  const h = parseImage(im.hdr);
  const rec = { crc: h.crc, name: h.name, type: h.type, hz: h.hz, hdr: im.hdr.slice(0, 512), data: im.data.slice(), added: Date.now(), ...extra };
  try { await store("readwrite", (s) => s.put(rec)); } catch { /* private mode: not kept */ }
  return rec;
}
export async function libDel(crc) { try { await store("readwrite", (s) => s.delete(crc)); } catch { /* not kept anyway */ } }
