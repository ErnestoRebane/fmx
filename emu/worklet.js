// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// SLOOP in an AudioWorklet: sloop.wasm is the whole FM-1 firmware against a simulated FM-1
// (web/emu/sloop_web.c). Each render quantum asks it for 128 frames, which runs the device's clock forward;
// between quanta the page's input goes in and, about 30 times a second (4 when the page is hidden), the screen,
// the lights and any changed flash go out. After X0X's web/emu/worklet.js (charlesvestal/fm1-x0x, GPL-3.0).

// The flash the page keeps: storage.c's areas (projects 0x97000.., the user sample slots USR1..USR3 0xA0000.., the
// user preset banks ..0xDFFFF; the FM6 patch bank 0xE5000 and the sample slot USR4 0xE7000..0xFAFFF (SLOOP 2.4); the
// settings and the autosave's second copy 0xFC000..0xFFFFF), only the sectors that hold something (an erased sector
// is all 0xFF). {offset: bytes}
export const STORE_AREAS = [[0x97000, 0xE0000], [0xE5000, 0xFB000], [0xFC000, 0x100000]];
export function storeSectors(nor) {
  const out = {};
  for (const [a, b] of STORE_AREAS)
    for (let off = a; off < b; off += 4096) {
      const s = nor.subarray(off, off + 4096);
      if (s.some((v) => v !== 0xFF)) out[off] = s.slice();
    }
  return out;
}
export function restoreSectors(nor, sectors) {
  for (const [k, v] of Object.entries(sectors || {})) {
    const off = +k, b = v instanceof Uint8Array ? v : new Uint8Array(v);
    if (STORE_AREAS.some(([a, e]) => off >= a && off + 4096 <= e && off % 4096 === 0) && b.length === 4096)
      nor.set(b, off);
  }
}

export const HOLD_MS = 24;

// a user sample slot: USR1..USR3 at 0xA0000, 0xB4000, 0xC8000, USR4 at 0xE7000, 0x14000 bytes each (the header, 512 bytes of
// it, then the data)
export const SLOT_BASE = [0xA0000, 0xB4000, 0xC8000, 0xE7000], SLOT_BYTES = 0x14000;
export function writeSlot(nor, k, hdr, data) {
  const off = SLOT_BASE[k];
  nor.fill(0xFF, off, off + SLOT_BYTES);
  if (hdr) {
    nor.set(hdr, off);
    nor.set(data, off + 512);
  }
}

// the sample pool in the flash image (FMX, web/EDITOR_PROTOCOL.md "Sample pool"): 80 sectors of 4 KiB, 0..59 at 0xA0000
// and 60..79 at 0xE7000; an entry (the slot format) from the start of any sector, inside one region; its id in byte 7
// (id + 1; 0: an old slot's, the id of its place 0, 20, 40, 60, else the first free)
export const poolSect = (s) => (s < 60 ? 0xA0000 + s * 4096 : 0xE7000 + (s - 60) * 4096);
const poolEnd = (s) => (s < 60 ? 60 : 80), OLD_SECT = [0, 20, 40, 60];
// the entries as the firmware finds them at power-on -> [{id, sect, nsect, crc}]
export function poolScan(nor) {
  const dv = new DataView(nor.buffer, nor.byteOffset, nor.byteLength), found = [];
  for (let s = 0; s < 80;) {
    const o = poolSect(s), nz = nor[o + 6], len = dv.getUint32(o + 16, true);
    if (dv.getUint32(o, true) !== 0x504D5346 || !nz || nz > 16 || len > (poolEnd(s) - s) * 4096 - 512) { s++; continue; }
    const n = Math.ceil((512 + len) / 4096);
    found.push({ sect: s, nsect: n, rsv: nor[o + 7], crc: dv.getUint32(o + 20, true), id: -1 });
    s += n;
  }
  const taken = new Set();
  for (const f of found) if (f.rsv && f.rsv <= 80 && !taken.has(f.rsv - 1)) taken.add(f.id = f.rsv - 1);
  for (const f of found) if (f.id < 0 && !f.rsv) { const k = OLD_SECT.indexOf(f.sect); if (k >= 0 && !taken.has(k)) taken.add(f.id = k); }
  for (const f of found) if (f.id < 0) { let k = 0; while (taken.has(k)) k++; taken.add(f.id = k); }
  return found.map(({ id, sect, nsect, crc }) => ({ id, sect, nsect, crc }));
}
// an image (hdr: 480 or 512 bytes, data) into the pool: in the smallest free piece that takes it, the lowest free id
// -> its id; the same data (CRC) already there: that entry's id; -1: no room
export function poolPut(nor, hdr, data) {
  const ents = poolScan(nor), crc = new DataView(hdr.buffer, hdr.byteOffset).getUint32(20, true);
  const same = ents.find((e) => e.crc === crc);
  if (same) return same.id;
  const n = Math.ceil((512 + data.length) / 4096), used = new Uint8Array(80);
  for (const e of ents) used.fill(1, e.sect, e.sect + e.nsect);
  let best = null;
  for (const [a, z] of [[0, 60], [60, 80]])
    for (let s = a; s < z;) {
      if (used[s]) { s++; continue; }
      let e = s;
      while (e < z && !used[e]) e++;
      if (e - s >= n && (!best || e - s < best.n)) best = { first: s, n: e - s };
      s = e;
    }
  let id = 0;
  while (ents.some((e) => e.id === id)) id++;
  if (!best || id >= 80) return -1;
  const o = poolSect(best.first);
  nor.fill(0xFF, o, o + n * 4096);
  nor.set(hdr.subarray(0, 512), o);
  nor[o + 7] = id + 1;
  nor.set(data, o + 512);
  return id;
}

if (typeof registerProcessor === "function") {
  const clock = globalThis.performance ? () => globalThis.performance.now() : () => Date.now();
  class Sloop extends AudioWorkletProcessor {
    constructor() {
      super();
      this.ex = null;
      this.sentDraws = -1;
      this.sentWrites = -1;
      this.sentLeds = "";
      this.sentUi = "";
      this.lastFrame = 0;
      this.period = 1 / 30;
      this.busy = 0;
      this.frames = 0;
      this.queue = [];                                // button / key changes, each held >= HOLD_MS of device time
      this.nextOk = 0;
      this.port.onmessage = (e) => this.onMessage(e.data);
    }

    async onMessage(m) {
      if (m.type === "load") {
        try {
          const { instance } = await WebAssembly.instantiate(m.wasm, {});
          const ex = instance.exports;
          ex._initialize();
          ex.web_nor_erase();
          restoreSectors(new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size()), m.sectors);
          ex.web_master(m.master ?? 700);
          ex.web_boot();
          this.ex = ex;
          this.sentWrites = ex.web_flash_writes_count();
          this.port.postMessage({ type: "ready", rate: ex.web_sample_rate() });
        } catch (err) {
          this.port.postMessage({ type: "error", message: String(err && err.message || err) });
        }
        return;
      }
      const ex = this.ex;
      if (!ex) return;
      if (m.type === "buttons" || m.type === "keys") this.queue.push(m);
      else if (m.type === "demo") this.port.postMessage({ type: "demo-done", ok: ex.web_demo_set() });   // the demo set
      else if (m.type === "slots") {                 // break loops from the page: the slot images into the flash, read again
        const nor = new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size());
        for (const s of m.slots) {
          writeSlot(nor, s.k, s.hdr ? new Uint8Array(s.hdr) : null, s.data ? new Uint8Array(s.data) : null);
          ex.web_slot_changed(s.k);
        }
        if (m.slice != null) ex.web_slice_use(m.slice);   // a SLICE loop: the CHOP track plays it
        this.port.postMessage({ type: "slots-done", n: m.slots.length });
      } else if (m.type === "pool") {                // sounds from the page's library: each into a free place of the pool
        const nor = new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size()), ids = [];
        for (const s of m.sounds) {
          const id = poolPut(nor, new Uint8Array(s.hdr), new Uint8Array(s.data));
          if (id >= 0) ex.web_slot_changed(id);
          ids.push(id);
        }
        this.port.postMessage({ type: "pool-done", ids });
      } else if (m.type === "enc") ex.web_enc(m.role, m.n | 0);
      else if (m.type === "master") ex.web_master(m.value | 0);
      else if (m.type === "midi") ex.web_midi(m.data[0] | 0, m.data[1] | 0, m.data[2] | 0);
      else if (m.type === "visible") this.period = m.on ? 1 / 30 : 1 / 4;
    }

    publish() {
      const ex = this.ex, mem = ex.memory.buffer;
      const msg = { type: "frame" };
      const leds = [ex.web_lit_buttons(), ex.web_lit_keys(), ex.web_dim_buttons(), ex.web_dim_keys(), ex.web_dim_level(),
                    ex.web_breath_buttons(), ex.web_breath_keys(), ex.web_mid_keys ? ex.web_mid_keys() : 0];
      const an = ex.web_anim_levels ? ex.web_anim_levels() : 0;   // the power-on sweep: each LED's level
      const anim = an ? Array.from(new Uint8Array(mem, an, 41)) : null;
      const key = leds.join(",") + (anim ? ";" + anim.join(",") : "");
      if (key !== this.sentLeds) {
        this.sentLeds = key;
        msg.leds = leds;
        msg.anim = anim;
      }
      if (this.frames >= 44100) {                     // the share of real time spent running the device
        msg.load = this.busy / (this.frames / 44.1);
        this.busy = this.frames = 0;
      }
      const transfer = [];
      const draws = ex.web_screen_draws();
      if (draws !== this.sentDraws) {
        this.sentDraws = draws;
        msg.fb = new Uint16Array(mem, ex.web_screen(), 240 * 240).slice();
        transfer.push(msg.fb.buffer);
      }
      if (ex.web_ui) {                                // what the panel does now (app_help.js): when it changes
        const b = new Uint8Array(mem, ex.web_ui(), 256);
        let t = "";
        for (let i = 0; i < 256 && b[i]; i++) t += String.fromCharCode(b[i]);
        if (t !== this.sentUi) msg.ui = this.sentUi = t;
      }
      const writes = ex.web_flash_writes_count();
      if (writes !== this.sentWrites) {               // a save is done within one main-loop pass: whole here
        this.sentWrites = writes;
        msg.sectors = storeSectors(new Uint8Array(mem, ex.web_nor(), ex.web_nor_size()));
      }
      if (msg.leds || msg.fb || msg.sectors || msg.ui || msg.load !== undefined) this.port.postMessage(msg, transfer);
    }

    // A click's down and up can arrive within one render quantum, but the FM-1's contacts never close for less than
    // its debounce, and the UI reads them once a frame: each change of the buttons or the keys stays HOLD_MS of the
    // device's time before the next one goes in (a tap: down for a frame and more, then up).
    input() {
      const ex = this.ex, now = ex.web_now_ms();
      while (this.queue.length && now - this.nextOk >= 0) {
        const m = this.queue.shift();
        if (m.type === "buttons") ex.web_buttons(m.mask >>> 0);
        else ex.web_keys(m.mask >>> 0);
        this.nextOk = now + HOLD_MS;
      }
    }

    process(inputs, outputs) {
      const ex = this.ex;
      if (!ex) return true;
      if (this.queue.length) this.input();
      const out = outputs[0], n = out[0].length;
      const t0 = clock();
      ex.web_render(n);
      this.busy += clock() - t0;
      this.frames += n;
      out[0].set(new Float32Array(ex.memory.buffer, ex.web_out_l(), n));
      if (out[1]) out[1].set(new Float32Array(ex.memory.buffer, ex.web_out_r(), n));
      if (currentTime - this.lastFrame >= this.period) {
        this.lastFrame = currentTime;
        this.publish();
      }
      return true;
    }
  }
  registerProcessor("sloop", Sloop);
}
