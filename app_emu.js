// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The emulator in the companion app (web/app.html): emu/sloop.wasm, the whole firmware against a simulated FM-1, in an
// AudioWorklet (web/emu/worklet.js), on the FM-1 the page draws (app_draw.js drawFM1) with the device's lights; HTML
// targets over the drawing take the pointer, the computer's keyboard plays it while it is in view. The input, the
// screen, the flash kept in this browser and the first switch-on (the packs, the demo set) are web/emu/index.html's,
// after X0X's browser page (charlesvestal/fm1-x0x, GPL-3.0).
import { LAYOUTS, BTN, ENC, WHITE, ROW1, ROW2, spriteSize, controlRects, screenRect, drawFM1 } from "./app_draw.js";
import { controlHelp, parseUi, BOOT } from "./app_help.js";

const STORE = "sloop-emu-flash";                      // (the old emulator page's: the same site, the same saves)
const FIRST_PACKS = [["PIANO", 0]];   // the demo set's sounds: USR1

// the computer's keyboard, every key one control: F3..A#4 on the Z (white) and A (black) rows, B4..G5 on Q..O in
// semitones, the twelve buttons on the number row in the panel's order, [ ] OCT, the arrows SELECT and ALGORITHM
const KEYMAP = {};
["KeyZ", "KeyX", "KeyC", "KeyV", "KeyB", "KeyN", "KeyM", "Comma", "Period", "Slash"].forEach((c, i) => { KEYMAP[c] = { key: WHITE[i] }; });
[["KeyS", 1], ["KeyD", 3], ["KeyF", 5], ["KeyH", 8], ["KeyJ", 10], ["KeyL", 13], ["Semicolon", 15], ["Quote", 17]].forEach(([c, k]) => { KEYMAP[c] = { key: k }; });
["KeyQ", "KeyW", "KeyE", "KeyR", "KeyT", "KeyY", "KeyU", "KeyI", "KeyO"].forEach((c, i) => { KEYMAP[c] = { key: 18 + i }; });
["Digit1", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6", "Digit7", "Digit8", "Digit9", "Digit0", "Minus", "Equal"]
  .forEach((c, i) => { KEYMAP[c] = { btn: BTN[[...ROW1, ...ROW2][i]] }; });
Object.assign(KEYMAP, { BracketLeft: { btn: BTN["OCT-"] }, BracketRight: { btn: BTN["OCT+"] } });
const ARROWS = { ArrowUp: [ENC.SELECT, 1], ArrowDown: [ENC.SELECT, -1], ArrowRight: [ENC.ALGORITHM, 1], ArrowLeft: [ENC.ALGORITHM, -1] };

export class Emulator {
  // el: {wrap (the .fm1 box), sprite, screen (canvases), on (the switch-on layer), go (its button), status, tipLine (the
  // screen's name and hint line while the labels show)}
  constructor(el, { base = "emu/", packs = "packs/", onState = () => {} } = {}) {
    Object.assign(this, { el, base, packs, onState });
    this.node = null;
    this.starting = false;
    this.btns = new Map();                            // pointer / key code -> a button's bit
    this.keys = new Map();                            // pointer / key code -> a key
    this.btnMask = this.keyMask = 0;
    this.latched = 0;                                 // Shift-click: a button held until clicked again
    this.master = 700;
    this.leds = [0, 0, 0, 0, 0, 0, 0, 0];
    this.anim = null;
    this.inView = false;
    this.pending = false;
    this.tips = false;                                // the labels: what each control does now (app_help.js)
    this.ui = BOOT;
    this.img = el.screen.getContext("2d").createImageData(240, 240);
    const tall = matchMedia("(orientation: portrait) and (max-width: 700px)");
    this.layout(tall.matches ? "tall" : "wide");
    tall.addEventListener("change", () => this.layout(tall.matches ? "tall" : "wide"));
    new ResizeObserver(() => el.wrap.classList.toggle("narrow", el.wrap.clientWidth < 760)).observe(el.wrap);   // (keys' labels upright)
    new IntersectionObserver((es) => { this.inView = es[es.length - 1].intersectionRatio > 0.25; if (this.inView) this.redraw(); },
      { threshold: [0, 0.25, 0.5] }).observe(el.wrap);
    el.go.addEventListener("click", () => this.switchOn());
    for (const t of ["pointerup", "pointercancel", "lostpointercapture"]) el.wrap.addEventListener(t, (e) => this.release(e));
    addEventListener("keydown", (e) => this.keyDown(e));
    addEventListener("keyup", (e) => this.keyUp(e));
    addEventListener("blur", () => {
      for (const id of [...this.btns.keys()]) if (id[0] !== "p") this.btns.delete(id);
      for (const id of [...this.keys.keys()]) if (id[0] !== "p") this.keys.delete(id);
      this.sync();
    });
    document.addEventListener("visibilitychange", () => this.send({ type: "visible", on: !document.hidden }));
  }

  get on() { return !!this.node; }
  send(m, transfer) { if (this.node) this.node.port.postMessage(m, transfer || []); }

  // ---- the drawing and its targets --------------------------------------------------------------------------------
  layout(name) {
    const L = LAYOUTS[name], el = this.el, [W, H] = spriteSize(L);
    this.L = L;
    el.sprite.width = W;
    el.sprite.height = H;
    const [x, y, s] = screenRect(L);
    for (const e of [el.screen, el.on])
      Object.assign(e.style, { left: `${x / W * 100}%`, top: `${y / H * 100}%`, width: `${s / W * 100}%`, height: `${s / H * 100}%` });
    for (const d of el.wrap.querySelectorAll(".hit")) d.remove();
    for (const c of controlRects(L)) {
      const d = document.createElement("div"), [rx, ry, rw, rh] = c.rect;
      d.className = "hit hit-" + c.kind;               // (not .btn: the page's buttons' look)
      Object.assign(d.style, { left: `${rx / W * 100}%`, top: `${ry / H * 100}%`, width: `${rw / W * 100}%`, height: `${rh / H * 100}%` });
      d.setAttribute("role", c.kind === "knob" ? "slider" : "button");
      d.setAttribute("aria-label", c.name);
      d.title = c.name;
      d.dataset.id = c.kind === "btn" ? Object.keys(BTN).find((n) => BTN[n] === c.bit) : c.kind === "key" ? c.k : c.name;
      d.appendChild(document.createElement("span")).className = "tip";
      d.tabIndex = -1;                                // (reachable once switched on)
      d.addEventListener("touchstart", (e) => e.preventDefault(), { passive: false });
      d.addEventListener("contextmenu", (e) => e.preventDefault());
      if (c.kind === "knob") this.bindKnob(d, c);
      else if (c.kind === "btn") this.bindButton(d, c.bit);
      else this.bindKey(d, c.k);
      el.wrap.insertBefore(d, el.on);
    }
    this.syncTabs();
    this.redraw();
    this.drawTips();
  }
  syncTabs() { for (const d of this.el.wrap.querySelectorAll(".hit")) d.tabIndex = this.node ? 0 : -1; }
  redraw() {
    if (this.pending) return;
    this.pending = true;
    requestAnimationFrame(() => {
      this.pending = false;
      const breathing = drawFM1(this.el.sprite, this.L, { on: !!this.node, t: performance.now() / 1000, leds: this.leds,
        anim: this.anim, held: this.btnMask, keys: this.keyMask, master: this.master });
      if (breathing && this.inView && !document.hidden) setTimeout(() => this.redraw(), 50);   // ~20 frames a second
    });
  }

  // ---- the labels: each control's job in the screen shown (off: the FM-1 as it boots) ---------------------------------
  setTips(on) {
    this.tips = !!on;
    this.el.wrap.classList.toggle("tips", this.tips);
    this.drawTips();
  }
  drawTips() {
    if (this.el.tipLine) this.el.tipLine.hidden = !this.tips;
    if (!this.tips) return;
    const h = controlHelp(this.node ? this.ui : BOOT);
    for (const d of this.el.wrap.querySelectorAll(".hit")) {
      const id = d.dataset.id, t = (d.classList.contains("hit-knob") ? h.knob[id] : /^\d+$/.test(id) ? h.key[+id] : h.btn[id]) || "";
      const tip = d.querySelector(".tip");
      if (tip.dataset.t === t) continue;
      tip.dataset.t = t;
      d.classList.toggle("has", !!t);
      const [first, ...rest] = t.split("\n");
      tip.textContent = first;
      if (rest.length) tip.appendChild(document.createElement("small")).textContent = rest.join(" ");
    }
    if (this.el.tipLine) {
      const hint = this.node && this.ui.fresh ? this.ui.hint : "";
      this.el.tipLine.textContent = h.where + (hint ? ` · the screen says: ${hint}` : "");
    }
  }

  // ---- input ------------------------------------------------------------------------------------------------------
  sync() {
    let b = this.latched, k = 0;
    for (const v of this.btns.values()) b |= 1 << v;
    for (const v of this.keys.values()) k |= 1 << v;
    if (b !== this.btnMask) { this.btnMask = b; this.send({ type: "buttons", mask: b }); this.redraw(); }
    if (k !== this.keyMask) { this.keyMask = k; this.send({ type: "keys", mask: k }); this.redraw(); }
  }
  bindButton(d, bit) {
    d.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      if (!this.node) return;
      if (e.shiftKey || e.button === 2) { this.latched ^= 1 << bit; this.sync(); return; }
      if (this.latched & (1 << bit)) { this.latched &= ~(1 << bit); this.sync(); return; }
      d.setPointerCapture(e.pointerId);
      this.btns.set("p" + e.pointerId, bit);
      this.sync();
    });
    d.addEventListener("keydown", (e) => {
      if ((e.key === "Enter" || e.key === " ") && !e.repeat && this.node) { this.btns.set("f" + bit, bit); this.sync(); e.preventDefault(); e.stopPropagation(); }
    });
    d.addEventListener("keyup", () => { if (this.btns.delete("f" + bit)) this.sync(); });
  }
  bindKey(d, k) {
    d.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      if (!this.node) return;
      d.setPointerCapture(e.pointerId);
      this.keys.set("p" + e.pointerId, k);
      this.sync();
    });
  }
  release(e) {
    const id = "p" + e.pointerId;
    if (this.btns.delete(id) | this.keys.delete(id)) this.sync();
  }
  turn(role, n) {
    if (!n || !this.node) return;
    if (role === "master") {
      this.master = Math.max(0, Math.min(1023, this.master + n * 16));
      this.send({ type: "master", value: this.master });
      this.redraw();
    } else this.send({ type: "enc", role, n });
  }
  bindKnob(d, c) {
    let drag = null, wheel = 0;
    d.addEventListener("pointerdown", (e) => { e.preventDefault(); d.setPointerCapture(e.pointerId); drag = { y: e.clientY, acc: 0 }; });
    d.addEventListener("pointermove", (e) => {
      if (!drag) return;
      drag.acc += drag.y - e.clientY;
      drag.y = e.clientY;
      const step = c.role === "master" ? 3 : 8, n = Math.trunc(drag.acc / step);
      if (n) { drag.acc -= n * step; this.turn(c.role, n); }
    });
    const end = () => { drag = null; };
    d.addEventListener("pointerup", end);
    d.addEventListener("pointercancel", end);
    d.addEventListener("wheel", (e) => {
      if (!this.node) return;
      e.preventDefault();
      const dy = e.deltaMode === 1 ? e.deltaY * 33 : e.deltaY;
      if (Math.abs(dy) >= 50) { this.turn(c.role, dy < 0 ? 1 : -1); wheel = 0; return; }   // a wheel notch: one detent
      wheel -= dy;                                                                          // a trackpad: by distance
      const n = Math.trunc(wheel / 25);
      if (n) { wheel -= n * 25; this.turn(c.role, n); }
    }, { passive: false });
    d.addEventListener("keydown", (e) => {
      const n = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 }[e.key];
      if (n) { this.turn(c.role, n); e.preventDefault(); e.stopPropagation(); }
    });
  }
  // the keyboard plays the FM-1 while it is on and in view, and the page's own fields don't have it
  keyDown(e) {
    if (!this.node || !this.inView || e.metaKey || e.ctrlKey || e.altKey) return;
    if (e.target.closest?.("input:not([type=checkbox]):not([type=radio]), select, textarea, [contenteditable], .hit-knob")) return;
    if (ARROWS[e.code]) { const [r, n] = ARROWS[e.code]; this.turn(r, n); e.preventDefault(); return; }
    const m = KEYMAP[e.code];
    if (!m) return;
    e.preventDefault();
    if (e.repeat) return;
    if (m.key !== undefined) this.keys.set("k" + e.code, m.key);
    else this.btns.set("k" + e.code, m.btn);
    this.sync();
  }
  keyUp(e) {
    const id = "k" + e.code;
    if (this.keys.delete(id) | this.btns.delete(id)) this.sync();
  }

  // ---- the screen, the flash kept here ------------------------------------------------------------------------------
  drawScreen(fb) {
    const d = this.img.data;
    for (let i = 0; i < 240 * 240; i++) {
      const v = fb[i], p = ((v >> 8) | (v << 8)) & 0xFFFF;          // the panel's byte order -> RGB565
      d[i * 4] = ((p >> 11) & 31) * 255 / 31;
      d[i * 4 + 1] = ((p >> 5) & 63) * 255 / 63;
      d[i * 4 + 2] = (p & 31) * 255 / 31;
      d[i * 4 + 3] = 255;
    }
    this.el.screen.getContext("2d").putImageData(this.img, 0, 0);
  }
  static loadFlash() {
    try {
      const raw = JSON.parse(localStorage.getItem(STORE) || "{}"), out = {};
      for (const [off, b64] of Object.entries(raw)) out[off] = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
      return out;
    } catch { return {}; }
  }
  static saveFlash(sectors) {
    try {
      const raw = {};
      for (const [off, u8] of Object.entries(sectors)) {
        let s = "";
        for (let i = 0; i < u8.length; i += 4096) s += String.fromCharCode.apply(null, u8.subarray(i, i + 4096));
        raw[off] = btoa(s);
      }
      localStorage.setItem(STORE, JSON.stringify(raw));
      return true;
    } catch { return false; }                         // private mode, full: this session only
  }

  // ---- switching on ---------------------------------------------------------------------------------------------------
  say(t) { this.el.status.textContent = t; this.el.status.hidden = !t; }
  async switchOn() {
    if (this.node || this.starting) return;
    const go = this.el.go;
    this.starting = true;
    go.disabled = true;
    go.textContent = "Starting…";
    this.say("");
    try {
      const ac = new AudioContext({ sampleRate: 44100, latencyHint: "interactive" });
      const [wasm] = await Promise.all([
        fetch(this.base + "sloop.wasm").then((r) => { if (!r.ok) throw new Error(`sloop.wasm: ${r.status}`); return r.arrayBuffer(); }),
        ac.audioWorklet.addModule(this.base + "worklet.js")]);
      const node = new AudioWorkletNode(ac, "sloop", { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });
      node.connect(ac.destination);
      const sectors = Emulator.loadFlash(), first = !Object.keys(sectors).length;   // nothing kept here: the packs go in
      let warned = false;
      this.waiters = new Map();
      await new Promise((res, rej) => {
        node.port.onmessage = (e) => {
          const m = e.data;
          if (m.type === "ready") res();
          else if (m.type === "error") rej(new Error(m.message));
          else if (m.type === "frame") {
            if (m.fb) this.drawScreen(m.fb);
            if (m.leds) { this.leds = m.leds; this.anim = m.anim; this.redraw(); }
            if (m.ui) { this.ui = parseUi(m.ui); this.drawTips(); }
            if (m.sectors && !Emulator.saveFlash(m.sectors) && !warned) {
              warned = true;
              this.onState("nosave");
            }
          } else if (this.waiters.has(m.type)) { const w = this.waiters.get(m.type); this.waiters.delete(m.type); w(m); }
        };
        node.port.postMessage({ type: "load", wasm, sectors, master: this.master }, [wasm]);
      });
      await ac.resume();
      this.node = node;
      this.el.on.hidden = true;
      this.drawTips();
      this.syncTabs();
      this.sync();
      this.send({ type: "buttons", mask: this.btnMask });
      this.send({ type: "visible", on: !document.hidden });
      this.midiIn();
      if (first) await this.demoSet().catch(() => {});
      this.onState("on");
    } catch (err) {
      this.node = null;
      go.disabled = false;
      go.textContent = "Switch on";
      this.say("Could not start: " + (err && err.message || err) + ". A current Chrome, Edge, Firefox or Safari should work.");
    }
    this.starting = false;
  }
  reply(type) { return new Promise((res) => this.waiters.set(type, res)); }

  // the demo set: the PIANO pack into USR1, then four songs over the project slots (web_demo_set: stopped first)
  async demoSet() {
    if (!this.node) throw new Error("Switch it on first.");
    const slots = await Promise.all(FIRST_PACKS.map(async ([name, k]) => {
      const r = await fetch(`${this.packs}${name}.bin`);
      if (!r.ok) throw new Error(`${name}: ${r.status}`);
      const img = new Uint8Array(await r.arrayBuffer()), len = new DataView(img.buffer).getUint32(16, true);
      return { k, hdr: img.slice(0, 480).buffer, data: img.slice(512, 512 + len).buffer };
    }));
    this.send({ type: "slots", slots });
    const done = this.reply("demo-done");
    this.send({ type: "demo" });
    if (!(await done).ok) throw new Error("The demo set could not be saved.");
  }

  // sounds from the library ([{hdr, data}]: images as the FM-1 keeps them) into the emulator's pool -> their ids (-1:
  // no room); one already there (the same data) is not put twice
  async loadSounds(images) {
    if (!this.node || !images.length) return [];
    const done = this.reply("pool-done");
    this.send({ type: "pool", sounds: images.map((im) => ({ hdr: im.hdr.slice().buffer, data: im.data.slice().buffer })) });
    return (await done).ids;
  }

  // a MIDI keyboard plays it (channel messages), when the browser already lets the page use MIDI (no prompt from here);
  // never the FM-1's own port
  async midiIn() {
    try {
      if (!navigator.requestMIDIAccess || (await navigator.permissions.query({ name: "midi" })).state !== "granted") return;
      const access = await navigator.requestMIDIAccess();
      const bind = () => {
        for (const input of access.inputs.values()) {
          if (input.fmxBound || /felucca|fmx|sloop|fm-1/i.test(input.name || "")) continue;
          input.fmxBound = true;
          input.addEventListener("midimessage", (e) => {
            const d = e.data;
            if (d.length >= 2 && d[0] >= 0x80 && d[0] < 0xF0) this.send({ type: "midi", data: [d[0], d[1], d[2] || 0] });
          });
        }
      };
      access.addEventListener("statechange", bind);
      bind();
    } catch { /* no MIDI: the panel and the keyboard */ }
  }
}
