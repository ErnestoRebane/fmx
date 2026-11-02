// SPDX-License-Identifier: GPL-3.0-only
// The FMX sample pool from the browser (web/EDITOR_PROTOCOL.md, "Sample pool"; the firmware: eng_sample.c, pool.c).
// The pool is 80 sectors of 4 KiB in two regions (0..59 and 60..79); an entry (the slot format: header + zones, the
// SLC1 meta at 480, the data at 512) starts at any sector and takes as many as it needs inside one region. Its id
// (0..79) is what a sound keeps in SET; ids 0..3 are the old slots USR1..USR4.
//   readDir(link)                       the pool's directory (POOL_DIR, 80)
//   place(dir, sectors, ids)            where an entry of that many sectors goes (best fit), the entries `ids` free
//   freeId(dir, after, taken)           an id for a new entry
//   send(link, id, sector, im, onPart)  an image {hdr, data} into the pool (SMP_BEGIN with its place, WRITE, END)
//   erase(link, id)                     an entry gone (SMP_ERASE)
//   makePoolMock()                      a pretend FM-1 with the pool (the pages' tests): the firmware's rules in JS
// `link` is the pages' Link: req(cmd, args, ms) -> the reply's arguments, retry(cmd, args, ms).
import { pack7, crc32 } from "./breaks.js";

export const SECT = 4096, DATA_OFF = 512, HDR_LEN = 480, NSECT = 80, NIDS = 80;
export const REGIONS = [{ first: 0, n: 60 }, { first: 60, n: 20 }];
export const CMD = { INFO: 1, BEGIN: 11, WRITE: 12, END: 13, ERASE: 14, BK_LIST: 34, BK_GET: 35, BK_PUT: 36, POOL_DIR: 80 };
export const FLAG = { KIT: 1, SLICE: 2, LOOPS: 4, MEL: 8 };   // (MEL: INST's melody bank, melody.js)

// the sectors an entry of dataLen bytes of ADPCM takes
export const sectorsFor = (dataLen) => Math.ceil((DATA_OFF + dataLen) / SECT);
const u7 = (a, i, n) => { let v = 0; for (let k = 0; k < n; k++) v += a[i + k] * 2 ** (7 * k); return v; };

// one POOL_DIR reply -> {nsect, sectKiB, nids, regions, zonesLeft, entries, next}
export function parseDir(a) {
  let i = 0;
  const d = { nsect: a[i++], sectKiB: a[i++], nids: a[i++], regions: [], entries: [] };
  for (let r = a[i++]; r > 0; r--) d.regions.push({ first: a[i++], n: a[i++] });
  d.zonesLeft = u7(a, i, 2); i += 2;
  for (let n = a[i++]; n > 0; n--) {
    const e = { id: a[i++], sect: a[i++], nsect: a[i++], nz: a[i++] };
    const z = a.indexOf(0, i);
    e.name = String.fromCharCode(...a.slice(i, z)); i = z + 1;
    e.dataLen = u7(a, i, 3); i += 3;
    e.crc = u7(a, i, 5) >>> 0; i += 5;
    e.hz = u7(a, i, 3); i += 3;
    const f = a[i++];
    Object.assign(e, { kit: !!(f & FLAG.KIT), slice: !!(f & FLAG.SLICE), loops: !!(f & FLAG.LOOPS), mel: !!(f & FLAG.MEL) });
    d.entries.push(e);
  }
  d.next = a[i];
  return d;
}

// the whole directory (16 entries a reply); null = the firmware has no pool (no answer to POOL_DIR)
export async function readDir(link) {
  let d = null;
  for (let start = 0; ;) {
    let r;
    try { r = await link.retry(CMD.POOL_DIR, [start], 1500, 2); } catch (e) { if (!d) return null; throw e; }
    const p = parseDir(r);
    if (!d) d = { ...p, entries: [] };
    d.entries.push(...p.entries);
    if (p.next <= start || p.next >= p.nids) break;
    start = p.next;
  }
  delete d.next;
  return d;
}

const regionOf = (dir, s) => (dir.regions || REGIONS).find((r) => s >= r.first && s < r.first + r.n);

// the free runs of sectors [{first, n}], each inside one region; the entries whose id is in `ids` count as free
export function freeRuns(dir, ids = []) {
  const used = new Uint8Array(dir.nsect || NSECT);
  for (const e of dir.entries) if (!ids.includes(e.id)) used.fill(1, e.sect, e.sect + e.nsect);
  const runs = [];
  for (const r of dir.regions || REGIONS) {
    for (let s = r.first; s < r.first + r.n;) {
      if (used[s]) { s++; continue; }
      let e = s;
      while (e < r.first + r.n && !used[e]) e++;
      runs.push({ first: s, n: e - s });
      s = e;
    }
  }
  return runs;
}
export const freeSectors = (dir, ids = []) => freeRuns(dir, ids).reduce((a, r) => a + r.n, 0);
export const largestRun = (dir, ids = []) => freeRuns(dir, ids).reduce((a, r) => Math.max(a, r.n), 0);

// the first sector of a place for `sectors` sectors: the smallest free run that takes it (the big runs stay whole for
// long loops), its start; -1 when no run does
export function place(dir, sectors, ids = []) {
  let best = null;
  for (const r of freeRuns(dir, ids)) if (r.n >= sectors && (!best || r.n < best.n)) best = r;
  return best ? best.first : -1;
}

// the lowest id above `after` that no entry has (nor one in `taken`); -1 when there is none
export function freeId(dir, after = -1, taken = []) {
  const has = new Set([...dir.entries.map((e) => e.id), ...taken]);
  for (let k = after + 1; k < (dir.nids || NIDS); k++) if (!has.has(k)) return k;
  return -1;
}

// the directory as it is after an entry is written (the page plans several in a row)
export function withEntry(dir, e) {
  return { ...dir, entries: [...dir.entries.filter((x) => x.id !== e.id), e].sort((a, b) => a.id - b.id) };
}
export const withoutEntry = (dir, id) => ({ ...dir, entries: dir.entries.filter((x) => x.id !== id) });

const RC_BEGIN = ["", "the flash failed", "that place is taken by another sound", "that place is outside the pool"];
const RC_END = ["", "a bad header size", "the device didn't accept the header", "the data arrived damaged",
  "the flash failed", "the device has no room left for its zones", "the upload was not open"];

// image {hdr (480 or 512 bytes), data} -> entry id at `sector`; the sectors it takes from its data length
export async function send(link, id, sector, im, onPart) {
  const ns = sectorsFor(im.data.length);
  const b = await link.req(CMD.BEGIN, [id, sector, ns], 10000);
  if (b[1]) throw new Error(`The FM-1 couldn't start the upload: ${RC_BEGIN[b[1]] || "rc " + b[1]}.`);
  for (let off = 0; off < im.data.length; off += 256) {
    const adr = DATA_OFF + off;
    const r = await link.retry(CMD.WRITE, [id, adr & 0x7F, (adr >> 7) & 0x7F, (adr >> 14) & 0x7F, ...pack7(im.data.subarray(off, off + 256))], 3000);
    if (r[4]) throw new Error("Writing to the FM-1 failed.");
    if (onPart) onPart(Math.min(1, (off + 256) / im.data.length));
  }
  const e = await link.req(CMD.END, [id, ...pack7(im.hdr)], 5000);
  if (e[1]) throw new Error(`The FM-1 didn't take the sound: ${RC_END[e[1]] || "rc " + e[1]}.`);
  return { id, sect: sector, nsect: ns };
}

export async function erase(link, id) {
  const r = await link.req(CMD.ERASE, [id], 10000);
  if (r[1]) throw new Error("The FM-1 couldn't erase the sound.");
}

// ---- a pretend FM-1 with the pool, for the pages' tests: a 1 MiB flash image, the firmware's scan and upload rules ----
const HDR = [0x7D, 0x46, 0x4C];
const sectOff = (s) => (s < 60 ? 0xA0000 + s * SECT : 0xE7000 + (s - 60) * SECT);
const sectEnd = (s) => (s < 60 ? 60 : 80);
const OLD_SECT = [0, 20, 40, 60];                   // the old slots USR1..USR4
const unpack7 = (a) => {
  const out = [];
  for (let i = 0; i < a.length;) { const m = a[i++]; for (let j = 0; j < 7 && i < a.length; j++) out.push(a[i++] | ((m >> j) & 1) << 7); }
  return Uint8Array.from(out);
};
const b7 = (v, n) => [...Array(n)].map((_, k) => Math.floor(v / 2 ** (7 * k)) & 0x7F);

export function makePoolMock(opt = {}) {
  const nor = new Uint8Array(0x100000).fill(0xFF), dv = new DataView(nor.buffer);
  let ents = [], up = null;                         // [{id, sect, nsect, nz, name}] in id order; the upload open
  const puts = [];                                  // the backup objects written (BK_PUT commits): their ids
  const hdrOk = (o, cap) => {
    if (dv.getUint32(o, true) !== 0x504D5346 || dv.getUint16(o + 4, true) !== 1) return false;
    const nz = nor[o + 6], len = dv.getUint32(o + 16, true);
    if (!nz || nz > 16 || len > cap - DATA_OFF) return false;
    for (let i = 0; i < nz; i++) {
      const z = o + 32 + i * 28, off = dv.getUint32(z, true), n = dv.getUint32(z + 4, true);
      if (off > len || off + Math.ceil(n / 2) > len || nor[z + 24] > 88 || !dv.getUint32(z + 16, true) || nor[z + 25] > nor[z + 26]) return false;
    }
    return true;
  };
  function scan() {
    const found = [];
    for (let s = 0; s < NSECT;) {
      const o = sectOff(s);
      if (!hdrOk(o, (sectEnd(s) - s) * SECT)) { s++; continue; }
      const ns = sectorsFor(dv.getUint32(o + 16, true));
      found.push({ sect: s, nsect: ns, rsv: nor[o + 7], id: -1 });
      s += ns;
    }
    const taken = new Set();
    for (const f of found) if (f.rsv && f.rsv <= NIDS && !taken.has(f.rsv - 1)) taken.add(f.id = f.rsv - 1);
    for (const f of found) if (f.id < 0 && !f.rsv) { const k = OLD_SECT.indexOf(f.sect); if (k >= 0 && !taken.has(k)) taken.add(f.id = k); }
    for (const f of found) if (f.id < 0) { let k = 0; while (taken.has(k)) k++; taken.add(f.id = k); }
    ents = found.map((f) => {
      const o = sectOff(f.sect);
      let name = "";
      for (let i = 8; i < 16 && nor[o + i] >= 32 && nor[o + i] < 127; i++) name += String.fromCharCode(nor[o + i]);
      return { id: f.id, sect: f.sect, nsect: f.nsect, nz: nor[o + 6], name };
    }).sort((a, b) => a.id - b.id);
  }
  const ent = (k) => ents.find((e) => e.id === k);
  const eraseSect = (s) => nor.fill(0xFF, sectOff(s), sectOff(s) + SECT);
  const drop = (k) => { const e = ent(k); if (up && up.id === k) up = null; if (e) { eraseSect(e.sect); scan(); } };
  function begin(k, s, n) {
    up = null;
    if (k >= NIDS || !n || s >= NSECT || n > sectEnd(s) - s) return 3;
    if (ents.some((e) => e.id !== k && e.sect < s + n && s < e.sect + e.nsect)) return 2;
    drop(k);
    eraseSect(s);
    up = { id: k, sect: s, nsect: n };
    return 0;
  }
  function write(k, off, d) {
    if (!up || up.id !== k) return 4;
    if (off < DATA_OFF || off & 0xFF || !d.length || d.length > 256 || off + d.length > up.nsect * SECT) return 1;
    const at = sectOff(up.sect) + off;
    if (!(off & 0xFFF)) nor.fill(0xFF, at, at + SECT);
    for (let i = 0; i < d.length; i++) nor[at + i] &= d[i];
    return 0;
  }
  function end(k, h) {
    if (!up || up.id !== k) return 6;
    if (h.length !== HDR_LEN && h.length !== DATA_OFF) return 1;
    const o = sectOff(up.sect), v = new DataView(h.buffer, h.byteOffset), len = v.getUint32(16, true);
    if (v.getUint32(0, true) !== 0x504D5346 || !h[6] || h[6] > 16 || len > up.nsect * SECT - DATA_OFF) return 2;
    h = Uint8Array.from(h);
    h[7] = k + 1;
    up = null;
    if (crc32(nor.subarray(o + DATA_OFF, o + DATA_OFF + len)) !== v.getUint32(20, true)) return 3;
    for (let i = 0; i < h.length; i++) nor[o + i] &= h[i];
    scan();
    return ent(k) ? 0 : 5;
  }
  function entryBytes(k) {                          // BK_GET's object 32 + k: the header, then the data from 512
    const e = ent(k);
    if (!e) return new Uint8Array(0);
    const o = sectOff(e.sect);
    return nor.slice(o, o + DATA_OFF + dv.getUint32(o + 16, true));
  }
  function handle(cmd, a) {
    switch (cmd) {
      case CMD.INFO: return [...[..."FMX MOCK"].map((c) => c.charCodeAt(0)), 0, 11];
      case CMD.BEGIN: return [a[0], a.length >= 3 ? begin(a[0], a[1], a[2]) : a[0] < 4 ? begin(a[0], OLD_SECT[a[0]], 20) : 3];
      case CMD.WRITE: return [a[0], a[1], a[2], a[3], write(a[0], a[1] | a[2] << 7 | a[3] << 14, unpack7(a.slice(4)))];
      case CMD.END: return [a[0], end(a[0], unpack7(a.slice(1)))];
      case CMD.ERASE: drop(a[0]); return [a[0], 0];
      case CMD.BK_LIST: {
        const ids = [0, 1, 2, 3, 4, 5, 6, 7, 32, 33, 34, 35], out = [0, ids.length];
        for (const id of ids) { const b = id >= 32 ? entryBytes(id - 32) : new Uint8Array(0); out.push(id, ...b7(b.length, 5), ...b7(crc32(b), 5)); }
        return out;
      }
      case CMD.BK_GET: {
        const b = a[0] >= 32 ? entryBytes(a[0] - 32) : new Uint8Array(0), off = u7(a, 1, 5), count = a[6] | a[7] << 7;
        const rc = !count || count > 256 || off + count > b.length ? 1 : 0;
        return [a[0], rc, ...b7(off, 5), rc ? 0 : count & 0x7F, rc ? 0 : count >> 7, ...(rc ? [] : pack7(b.subarray(off, off + count)))];
      }
      case CMD.BK_PUT:                              // (a restore: taken, not kept; the commit noted)
        if (a[0] === 2) puts.push(a[1]);
        return [a[0], a[1], 0];
      case CMD.POOL_DIR: {
        if (opt.noPool) return null;
        const start = a.length ? a[0] : 0, list = ents.filter((e) => e.id >= start).slice(0, 16);
        const zones = ents.reduce((s, e) => s + e.nz, 0), out = [NSECT, 4, NIDS, 2, 0, 60, 60, 20, ...b7(384 - zones, 2), list.length];
        for (const e of list) {
          const o = sectOff(e.sect), z = o + 32;
          let kit = true;
          for (let i = 0; i < e.nz; i++) kit = kit && nor[z + i * 28 + 25] === nor[z + i * 28 + 26];
          out.push(e.id, e.sect, e.nsect, e.nz, ...[...e.name].map((c) => c.charCodeAt(0)), 0, ...b7(dv.getUint32(o + 16, true), 3),
            ...b7(dv.getUint32(o + 20, true), 5), ...b7(Math.floor(dv.getUint32(z + 16, true) * 44100 / 65536), 3),
            (kit ? 1 : 0) | (dv.getUint32(o + 480, true) === 0x31434C53 ? 2 : 0) | (/^BR/.test(e.name) ? 4 : 0) |
            (dv.getUint32(o + 480, true) === 0x314C454D ? 8 : 0));
        }
        out.push(list.length === 16 ? list[15].id + 1 : NIDS);
        return out;
      }
    }
    return null;
  }
  const input = { id: "mock-in", name: opt.name || "FMX", type: "input", state: "connected", onmidimessage: null };
  const output = {
    id: "mock-out", name: opt.name || "FMX", type: "output", state: "connected",
    send(d) {
      if (d[0] !== 0xF0 || d[1] !== HDR[0] || d[2] !== HDR[1] || d[3] !== HDR[2]) return;
      const cmd = d[4], r = handle(cmd, [...d.slice(5, d.length - 1)]);
      if (r) setTimeout(() => input.onmidimessage && input.onmidimessage({ data: Uint8Array.from([0xF0, ...HDR, cmd, ...r, 0xF7]) }), 0);
    },
  };
  // an image put straight into the flash at sector s (rsv as given: 0 = an old slot's), then scanned: the test's setup
  function put(s, im, rsv = 0) {
    const o = sectOff(s);
    nor.fill(0xFF, o, o + sectorsFor(im.data.length) * SECT);
    nor.set(im.hdr, o);
    nor[o + 7] = rsv;
    nor.set(im.data, o + DATA_OFF);
    scan();
  }
  scan();
  return {
    nor, put, scan, puts, entries: () => ents.map((e) => ({ ...e })), bytes: entryBytes, sectOff,
    access: { sysexEnabled: true, inputs: new Map([[input.id, input]]), outputs: new Map([[output.id, output]]), onstatechange: null },
  };
}
