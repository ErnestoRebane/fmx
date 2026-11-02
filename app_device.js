// SPDX-License-Identifier: GPL-3.0-only
// The FM-1 from the companion app (web/app.html), over Web MIDI: which one it is (the update protocol's identity,
// web/fm1ota.js: FMX, the stock firmware, or a device left in update mode), FMX's own SysEx (web/EDITOR_PROTOCOL.md:
// INFO, the sample pool's directory POOL_DIR, its uploads in web/pool.js, the backup BK_LIST / BK_GET / BK_PUT), the
// songs in its project slots, a backup of the whole set and its restore (the editor's file, "sloop-backup", with the
// pool's entries beside the objects).
import { Updater } from "./fm1ota.js";
import { readDir, send, erase, place, withEntry, withoutEntry, sectorsFor, DATA_OFF } from "./pool.js";
import { pack7, crc32 } from "./breaks.js";

const HDR = [0x7D, 0x46, 0x4C];
const CMD = { INFO: 1, SMP_ERASE: 14, BK_LIST: 34, BK_GET: 35, BK_PUT: 36 };
const u35 = (v) => [0, 1, 2, 3, 4].map((k) => Math.floor(v / 2 ** (7 * k)) & 0x7F);
const r35 = (a, i) => { let v = 0; for (let k = 0; k < 5; k++) v += a[i + k] * 2 ** (7 * k); return v; };
function unpack7(a) {
  const out = [];
  for (let i = 0; i < a.length;) { const m = a[i++]; for (let j = 0; j < 7 && i < a.length; j++) out.push(a[i++] | ((m >> j) & 1) << 7); }
  return new Uint8Array(out);
}
const cstr = (a, i) => { const z = a.indexOf(0, i); return [String.fromCharCode(...a.slice(i, z < 0 ? a.length : z)), z < 0 ? a.length : z + 1]; };

// one in / out pair, one request at a time: req(cmd, args, ms) -> the reply's arguments
class Link {
  constructor(input, output) {
    this.input = input; this.output = output; this.wait = null;
    this.onMsg = (ev) => {
      const d = ev.data;
      if (d[0] !== 0xF0 || d[1] !== HDR[0] || d[2] !== HDR[1] || d[3] !== HDR[2] || !this.wait || d[4] !== this.wait.cmd) return;
      const w = this.wait;
      this.wait = null;
      clearTimeout(w.timer);
      w.resolve([...d.slice(5, d.length - 1)]);
    };
    input.onmidimessage = this.onMsg;
  }
  get lost() { return this.input.state === "disconnected" || this.output.state === "disconnected"; }
  req(cmd, args, ms = 3000) {
    return new Promise((resolve, reject) => {
      this.wait = { cmd, resolve, timer: setTimeout(() => { this.wait = null; reject(new Error("The FM-1 stopped answering.")); }, ms) };
      try { this.output.send([0xF0, ...HDR, cmd, ...args, 0xF7]); } catch (e) { clearTimeout(this.wait.timer); this.wait = null; reject(new Error("The FM-1 is not connected.")); }
    });
  }
  async retry(cmd, args, ms, tries = 3) {
    for (let k = 1; ; k++) { try { return await this.req(cmd, args, ms); } catch (e) { if (k >= tries) throw e; } }
  }
  close() { if (this.input.onmidimessage === this.onMsg) this.input.onmidimessage = null; }
}

export class Device {
  // the first FM-1 that answers -> a Device, or null. kind: "fmx" (the pool), "old" (FMX's SysEx, no pool: SLOOP, an
  // early FMX), "stock" (no FMX SysEx: the stock firmware), "update" (in update mode: an install to finish)
  static async connect(midi) {
    const found = await new Updater(midi).find();
    if (!found) return null;
    const { input, output } = found.link;
    found.link.close();
    const d = new Device(midi, input, output, found.id, found.name);
    if (/^ota-/i.test(found.id.model)) { d.kind = "update"; return d; }
    d.link = new Link(input, output);
    try {
      const a = await d.link.retry(CMD.INFO, [], 800, 2);
      d.version = cstr(a, 0)[0];
    } catch { d.kind = "stock"; d.close(); return d; }
    d.dir = await readDir(d.link);
    d.kind = d.dir ? "fmx" : "old";
    return d;
  }
  constructor(midi, input, output, id, name) { Object.assign(this, { midi, input, output, id, name, kind: "", version: "", dir: null, link: null }); }
  get lost() { return this.input.state === "disconnected" || this.output.state === "disconnected"; }
  close() { if (this.link) this.link.close(); this.link = null; }
  // what it says it is, for the header: "FMX 0.1", "stock V15" ...
  get label() {
    if (this.kind === "update") return "update mode";
    if (this.kind === "stock") return this.id.text === "FM-1_015" ? "stock V15" : this.id.text;
    return this.version || this.id.text;
  }

  async refresh() { this.dir = await readDir(this.link); return this.dir; }
  async send(id, sector, im, onPart) { return send(this.link, id, sector, im, onPart); }
  async erase(id) { return erase(this.link, id); }

  // ---- the backup's objects ---------------------------------------------------------------------------------------
  // BK_LIST: the objects there (and the snapshot BK_GET reads) -> [{id, len, crc}]
  async list() {
    const a = await this.link.req(CMD.BK_LIST, [], 5000);
    if (a[0]) throw new Error("The FM-1 couldn't list what it holds.");
    const out = [];
    for (let i = 0, j = 2; i < a[1]; i++, j += 11) out.push({ id: a[j], len: r35(a, j + 1), crc: r35(a, j + 6) >>> 0 });
    return out;
  }
  // an object's bytes (after a list): 32 + id is a pool entry, its header then its data from 512
  async read(obj, len, onPart) {
    const out = new Uint8Array(len);
    for (let at = 0; at < len;) {
      const want = Math.min(256, len - at);
      const r = await this.link.retry(CMD.BK_GET, [obj, ...u35(at), want & 0x7F, (want >> 7) & 0x7F], 3000);
      if (r[1]) throw new Error("The FM-1 changed while it was read. Try again.");
      const b = unpack7(r.slice(1 + 1 + 5 + 2));
      if (!b.length) throw new Error("The FM-1 sent nothing back.");
      out.set(b.subarray(0, Math.min(b.length, len - at)), at);
      at += Math.min(b.length, len - at);
      if (onPart) onPart(at / len);
    }
    return out;
  }
  // a pool entry read back -> {hdr (512), data}
  async readEntry(e, onPart) {
    await this.list();
    const all = await this.read(32 + e.id, DATA_OFF + e.dataLen, onPart);
    if (crc32(all.subarray(DATA_OFF)) !== e.crc) throw new Error("The sound came back damaged. Try again.");
    return { hdr: all.slice(0, DATA_OFF), data: all.slice(DATA_OFF) };
  }
  // the four project slots: the songs of the set -> [{slot, used, bpm}] (a project starts magic, size, then its
  // globals, G_BPM first: firmware/src/project.c)
  async songs() {
    const L = await this.list(), out = [];
    for (let s = 0; s < 4; s++) {
      const it = L.find((o) => o.id === 2 + s), song = { slot: s, used: !!(it && it.len), bpm: 0 };
      if (song.used && it.len >= 10) {
        const b = await this.read(2 + s, 10), v = new DataView(b.buffer);
        if ((v.getUint32(0, true) & 0xFFFFFF00) === 0x53504200) song.bpm = v.getInt16(8, true);   // "SPB3" / "SPB4"
      }
      out.push(song);
    }
    return out;
  }

  // ---- the set backed up and back -----------------------------------------------------------------------------------
  // everything: the editor's objects (projects, settings, banks, the old slots) and every pool entry -> the file
  async backup(onPart = () => {}) {
    const L = await this.list(), entries = this.dir ? this.dir.entries : [];
    const total = L.reduce((s, o) => s + o.len, 0) + entries.reduce((s, e) => s + DATA_OFF + e.dataLen, 0) || 1;
    const b64 = (u8) => { let s = ""; for (let i = 0; i < u8.length; i += 4096) s += String.fromCharCode.apply(null, u8.subarray(i, i + 4096)); return btoa(s); };
    let done = 0;
    const objects = [], pool = [];
    for (const o of L) {
      const d = o.len ? await this.read(o.id, o.len, (f) => onPart((done + f * o.len) / total)) : new Uint8Array(0);
      if (crc32(d) !== o.crc) throw new Error("The FM-1 changed while it was read. Try again.");
      done += o.len;
      objects.push({ id: o.id, len: o.len, crc: o.crc, data: b64(d) });
    }
    for (const e of entries) {
      const len = DATA_OFF + e.dataLen, d = await this.read(32 + e.id, len, (f) => onPart((done + f * len) / total));
      done += len;
      pool.push({ id: e.id, name: e.name, len, crc: crc32(d), data: b64(d) });
    }
    return { format: "sloop-backup", version: 1, firmware: this.version, date: new Date().toISOString(), objects, pool };
  }
  // a backup file back on the FM-1: the banks, the projects, the working project and the settings (BK_PUT, as the
  // editor restores them: the song stopped), then the pool as it was (the entries there now erased first). An editor's
  // file has the old slots 32..35 for the pool.
  async restore(file, onPart = () => {}) {
    const unb64 = (s) => Uint8Array.from(atob(s || ""), (c) => c.charCodeAt(0));
    if (!file || file.format !== "sloop-backup" || !Array.isArray(file.objects)) throw new Error("That is not a backup of an FM-1.");
    const objs = new Map(), pool = [];
    for (const o of file.objects) {
      const d = unb64(o.data);
      if (d.length !== o.len || crc32(d) !== (o.crc >>> 0)) throw new Error(`The backup is damaged (object ${o.id}).`);
      if (o.id >= 32) { if (!file.pool && d.length) pool.push({ id: o.id - 32, d }); } else objs.set(o.id, d);
    }
    for (const p of file.pool || []) {
      const d = unb64(p.data);
      if (d.length !== p.len || crc32(d) !== (p.crc >>> 0)) throw new Error(`The backup is damaged (${p.name || "a sound"}).`);
      pool.push({ id: p.id, d });
    }
    if (!objs.has(0) || !objs.has(1)) throw new Error("The backup is incomplete.");
    const total = [...objs.values(), ...pool.map((p) => p.d)].reduce((s, d) => s + d.length, 0) || 1;
    let done = 0;
    for (const id of [6, 7, 8, 9, 2, 3, 4, 5, 0, 1]) {
      if (!objs.has(id)) continue;
      const d = objs.get(id), put = async (args, ms) => { const r = await this.link.retry(CMD.BK_PUT, args, ms, 2); return r[2]; };
      let rc = await put([0, id, ...u35(d.length), ...u35(crc32(d))], 1500);
      if ((rc === 1 || rc === 5) && (id === 8 || id === 9)) continue;   // (a firmware without that bank)
      if (rc === 3) throw new Error("Stop the song on the FM-1 first, then restore again.");
      if (rc) throw new Error(`The FM-1 refused the backup (object ${id}, rc ${rc}).`);
      for (let off = 0; off < d.length; off += 256) {
        rc = await put([1, id, ...u35(off), ...pack7(d.subarray(off, off + 256))], 1500);
        if (rc) throw new Error(`Writing the backup failed (object ${id}, rc ${rc}).`);
        done += Math.min(256, d.length - off);
        onPart(done / total);
      }
      rc = (await this.link.req(CMD.BK_PUT, [2, id], 5000))[2];
      if (rc === 3) throw new Error("Stop the song on the FM-1 first, then restore again.");
      if (rc) throw new Error(`The FM-1 refused the backup (object ${id}, rc ${rc}).`);
    }
    let dir = await this.refresh();
    for (const e of dir.entries) { await this.erase(e.id); dir = withoutEntry(dir, e.id); }
    pool.sort((a, b) => b.d.length - a.d.length);     // the big ones first: they need the long free pieces
    for (const p of pool) {
      const im = { hdr: p.d.slice(0, DATA_OFF), data: p.d.slice(DATA_OFF) }, ns = sectorsFor(im.data.length), at = place(dir, ns);
      if (at < 0) throw new Error("The backup's sounds don't fit this FM-1's pool.");
      const slc = new DataView(im.hdr.buffer).getUint32(480, true) === 0x31434C53;   // (FMX SLICE's meta: kept)
      await this.send(p.id, at, { hdr: slc ? im.hdr : im.hdr.slice(0, 480), data: im.data }, (f) => onPart((done + f * p.d.length) / total));
      dir = withEntry(dir, { id: p.id, sect: at, nsect: ns });
      done += p.d.length;
    }
    await this.refresh();
  }
}
