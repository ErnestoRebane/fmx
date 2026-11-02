// SPDX-License-Identifier: GPL-3.0-only
// The companion app's pictures (web/app.html), in the PSX look: a software renderer (flat
// tris, an ordering table, snapped vertices, the console's 4x4 dither to 15-bit), the hero scene, the boot gem, and the
// FM-1 as a low-poly model drawn flat (an oblique projection, each part a faceted prism, one shade a facet) after the
// emulator's two layouts (WIDE: the FM-1's own; TALL: a phone held upright). drawFM1 takes the device's lights.

export const css = (n) => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
export const hex = (s) => [1, 3, 5].map((i) => parseInt(s.slice(i, i + 2), 16));
export const mix = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);
export const rgb = (c) => `rgb(${c[0] | 0},${c[1] | 0},${c[2] | 0})`;

// ---- the PSX renderer --------------------------------------------------------------------------------------------
const DITHER = [-4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2];   // the console's 4x4 table
export function psxDither(ctx, w, h) {
  const im = ctx.getImageData(0, 0, w, h), d = im.data;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const k = DITHER[(y & 3) * 4 + (x & 3)], i = (y * w + x) * 4;
    for (let c = 0; c < 3; c++) d[i + c] = Math.max(0, Math.min(255, d[i + c] + k)) & 0xF8;
    d[i + 3] = d[i + 3] > 127 ? 255 : 0;
  }
  ctx.putImageData(im, 0, 0);
}
const rotY = ([x, y, z], a) => { const c = Math.cos(a), s = Math.sin(a); return [x * c + z * s, y, -x * s + z * c]; };
const rotX = ([x, y, z], a) => { const c = Math.cos(a), s = Math.sin(a); return [x, y * c - z * s, y * s + z * c]; };

// the boot gem: a six-sided bipyramid, its faces in the track colours
export function gemTris(cx, cy, cz, size, ang, tilt) {
  const top = [0, 1.15, 0], bot = [0, -1.35, 0], ring = [];
  for (let k = 0; k < 6; k++) ring.push([Math.cos(k * Math.PI / 3) * 0.78, 0.18, Math.sin(k * Math.PI / 3) * 0.78]);
  const cols = ["--brk", "--bass", "--chd", "--chop"].map((n) => hex(css(n)));
  const tf = (p) => { p = rotX(rotY(p, ang), tilt); return [cx + p[0] * size, cy + p[1] * size, cz + p[2] * size]; };
  const out = [];
  for (let k = 0; k < 6; k++) {
    const a = ring[k], b = ring[(k + 1) % 6];
    out.push({ v: [top, b, a].map(tf), col: cols[k % 4] });
    out.push({ v: [bot, a, b].map(tf), col: mix(cols[(k + 2) % 4], [10, 14, 40], 0.35) });
  }
  return out;
}
function shade(t, L) {
  const [a, b, c] = t.v, u = b.map((v, i) => v - a[i]), w = c.map((v, i) => v - a[i]);
  let n = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]];
  const m = Math.hypot(...n) || 1;
  n = n.map((v) => v / m);
  const l = Math.max(0, n[0] * L[0] + n[1] * L[1] + n[2] * L[2]);
  return t.col.map((v) => v * (0.32 + 0.78 * l));
}
function drawTris(ctx, tris, project, fog) {
  for (const t of tris) t.z = (t.v[0][2] + t.v[1][2] + t.v[2][2]) / 3;
  tris.sort((a, b) => b.z - a.z);                     // back to front, as the GPU's ordering table
  for (const t of tris) {
    if (t.v.some((p) => p[2] < 0.4)) continue;
    const p = t.v.map(project);
    let c = shade(t, [-0.45, 0.75, -0.5]);
    if (fog) c = mix(c, fog.col, Math.min(1, Math.max(0, (t.z - fog.near) / (fog.far - fog.near))));
    ctx.fillStyle = ctx.strokeStyle = rgb(c);
    ctx.beginPath(); ctx.moveTo(p[0][0], p[0][1]); ctx.lineTo(p[1][0], p[1][1]); ctx.lineTo(p[2][0], p[2][1]); ctx.closePath();
    ctx.fill(); ctx.lineWidth = 1; ctx.stroke();
  }
}

// ---- the hero: the gem over a jungle canyon at dusk ----------------------------------------------------------------
const hash = (i, j) => { let h = (i * 374761393 + j * 668265263) | 0; h = (h ^ (h >>> 13)) * 1274126177; return ((h ^ (h >>> 16)) >>> 0) / 4294967296; };
export { hash };
function ground(i, j) {
  const x = Math.abs(i);
  if (x <= 1) return -0.15 + hash(i, j) * 0.1;        // the river bed
  return Math.pow(x - 1, 1.3) * 0.38 + hash(i, j) * (x > 2 ? 1 : 0.4) - 0.2;
}
// o (a wider scene, the README banner): the horizon's row, the lens and its centre's column, the moon's place, the ground's half width, the
// walls' peak (a ridge line instead of walls that keep climbing), the gem's place
export function heroFrame(cv, t, o = {}) {
  const hx = cv.getContext("2d", { willReadFrequently: true }), HW = cv.width, HH = cv.height;
  const sky = [hex("#050819"), hex("#1b1650"), hex("#5a2a6a"), hex(css("--brk"))];
  const horizon = o.horizon ?? 52, [mx, my] = o.moon ?? [206, 22], half = o.half ?? 9;
  for (let y = 0; y < HH; y++) {
    const f = y / horizon;
    const c = f < 0.55 ? mix(sky[0], sky[1], f / 0.55) : f < 0.9 ? mix(sky[1], sky[2], (f - 0.55) / 0.35)
      : mix(sky[2], sky[3], Math.min(1, (f - 0.9) / 0.1) * 0.7);
    hx.fillStyle = rgb(c); hx.fillRect(0, y, HW, 1);
  }
  for (let k = 0; k < 40; k++) {
    const x = hash(k, 7) * HW | 0, y = hash(k, 9) * HH * 0.32 | 0, b = hash(k, 3);
    hx.fillStyle = rgb(mix([90, 100, 170], [240, 240, 255], (Math.sin(t * 2 + b * 40) + 1) / 2 * b));
    hx.fillRect(x, y, 1, 1);
  }
  hx.fillStyle = "#e9dcc4"; hx.beginPath();             // the moon: a low-poly disc
  for (let k = 0; k < 7; k++) { const a = k / 7 * Math.PI * 2; hx.lineTo(Math.round(mx + Math.cos(a) * 9), Math.round(my + Math.sin(a) * 9)); }
  hx.fill();
  const cam = [0, 2.1, 0], f = o.f ?? 132, scroll = t * 2.2, base = Math.floor(scroll), frac = scroll - base;
  const project = (p) => {
    const [x, y, z] = [p[0] - cam[0], p[1] - cam[1], p[2] - cam[2]];
    return [Math.round((o.cx ?? HW / 2) + f * x / z), Math.round(horizon - f * y / z)];   // snapped: the PSX wobble
  };
  const tris = [], jungle = hex("#1f6b55"), rock = hex("#3a4d7a"), river = hex("#2a2f78");
  for (let j = 0; j < 22; j++) for (let i = -half; i < half; i++) {
    const z0 = j - frac + 1, z1 = z0 + 1, J = j + base;
    const v = (ii, jj, z) => [ii * 1.1, Math.min(ground(ii, J + jj), (o.peak ?? Infinity) + hash(ii, J + jj + 99) * 1.6), z];
    const a = v(i, 0, z0), b = v(i + 1, 0, z0), c = v(i + 1, 1, z1), d = v(i, 1, z1);
    const ax = Math.abs(i + 0.5), col = ax < 1.5 ? river : ax > 5 ? rock : jungle;
    tris.push({ v: [a, d, c], col }, { v: [a, c, b], col: mix(col, [0, 0, 0], 0.08) });
  }
  tris.push(...gemTris(o.gemX ?? 0, (o.gemY ?? 2.25) + Math.sin(t * 1.7) * 0.18, o.gemZ ?? 5.2, 0.95, t * 0.9, 0.18));
  drawTris(hx, tris, project, { col: sky[2], near: 6, far: 21 });
  psxDither(hx, HW, HH);
}

// a small gem: the logo, the songs' save icons (colBase: one colour instead of the tracks')
export function gemIcon(cv, ang, colBase) {
  const x = cv.getContext("2d"), W = cv.width;
  x.clearRect(0, 0, W, W);
  const tris = gemTris(0, 0, 4, 1, ang, 0.25);
  if (colBase) tris.forEach((t, k) => { t.col = mix(colBase, [10, 14, 40], k % 2 ? 0.45 : 0); });
  drawTris(x, tris, (p) => [Math.round(W / 2 + W * 0.9 * p[0] / p[2]), Math.round(W / 2 - W * 0.9 * p[1] / p[2])]);
}

// ---- the FM-1 --------------------------------------------------------------------------------------------------------
// the panel's labels (bit = the firmware's button bit, panel.c) and the encoders' roles (web/emu/sloop_web.c web_enc)
export const BTN = { FX: 0, SEL: 1, ENV: 2, LFO: 3, EDIT: 4, GLO: 5, HOME: 6, SAVE: 7, ARP: 8, SEQ: 9, PLAY: 10, REC: 11, "OCT-": 12, "OCT+": 13 };
export const ENC = { SELECT: 0, ALGORITHM: 1, PRESETS: 2, "KNOB 1": 3, "KNOB 2": 4, "KNOB 3": 5, "KNOB 4": 6 };
export const PLAY_GREEN = 14;
export const ROW1 = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"], ROW2 = ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"];
export const KNOBS = ["MASTER", "SELECT", "PRESETS", "ALGORITHM", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4"];
// the 27 keys F3..G5: key k = k semitones above F3; the black ones each after a white one
export const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];
export const BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25];
const BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14];
export const LAYOUTS = {
  wide: {
    w: 1000, h: 624, screen: [292, 44, 256],
    knobs: [[105, 100, 24], [215, 100, 24], [105, 205, 24], [215, 205, 24], [610, 100, 22], [714, 100, 22], [818, 100, 22], [922, 100, 22]],
    trays: [[50, 250, 220, 50], [568, 160, 400, 140]],
    oct: [[60, 258, 90, 34], [170, 258, 90, 34]],
    grid: [582, 174, 54, 48, 63, 62], keybed: [36, 332, 928, 256],
  },
  tall: {
    w: 600, h: 1070, screen: [130, 104, 340],
    knobs: [[66, 64, 24], [186, 64, 24], [414, 64, 24], [534, 64, 24], [90, 512, 26], [230, 512, 26], [370, 512, 26], [510, 512, 26]],
    trays: [[14, 572, 572, 146], [150, 728, 300, 62]],
    oct: [[164, 737, 128, 44], [308, 737, 128, 44]],
    grid: [24, 582, 86, 60, 94, 70], keybed: [14, 800, 572, 256],
  },
};
const DS = 0.6, DK = 0.5, BODY = 34;                  // sprite pixels a layout unit, the depth's slant, the body's height
export const spriteSize = (L) => [Math.round((L.w + 24) * DS), Math.round((L.h + BODY * DK + 28) * DS)];
const Pf = (x, y, z) => [x * DS, (y + 6 + (BODY - z) * DK) * DS];
const P = (x, y, z) => Pf(x, y, z).map(Math.round);  // snapped to the sprite's pixels
const nrm = (v) => { const m = Math.hypot(...v) || 1; return v.map((a) => a / m); };
const SUN = nrm([-0.45, -0.6, 0.75]);
const lit = (col, n) => { n = nrm(n); const d = Math.max(0, n[0] * SUN[0] + n[1] * SUN[1] + n[2] * SUN[2]); return rgb(col.map((v) => v * (0.34 + 0.8 * d))); };
function rr(x, y, w, h, r, seg = 2) {                // a rounded rectangle cut into facets, clockwise
  const out = [];
  for (const [cx, cy, a0] of [[x + w - r, y + r, -90], [x + w - r, y + h - r, 0], [x + r, y + h - r, 90], [x + r, y + r, 180]])
    for (let s = 0; s <= seg; s++) { const a = (a0 + 90 * s / seg) * Math.PI / 180; out.push([cx + Math.cos(a) * r, cy + Math.sin(a) * r]); }
  return out;
}
const ngon = (cx, cy, r, n) => Array.from({ length: n }, (_, i) => { const a = (-90 + 180 / n + 360 * i / n) * Math.PI / 180; return [cx + Math.cos(a) * r, cy + Math.sin(a) * r]; });

// a control's place on the sprite in its pixels, [x, y, w, h] (its footprint at the top of its part): the page's targets
export function controlRects(L) {
  const box = (pts, z) => {
    const p = pts.map(([x, y]) => Pf(x, y, z)), xs = p.map((q) => q[0]), ys = p.map((q) => q[1]);
    const x0 = Math.min(...xs), y0 = Math.min(...ys);
    return [x0, y0, Math.max(...xs) - x0, Math.max(...ys) - y0];
  };
  const out = [];
  KNOBS.forEach((name, i) => {
    const [x, y, r] = L.knobs[i];
    out.push({ kind: "knob", name, role: name === "MASTER" ? "master" : ENC[name], rect: box(ngon(x, y, r + 10, 10), BODY + 20) });
  });
  const btn = ([x, y, w, h], name, sub) => out.push({ kind: "btn", name: sub ? `${name} / ${sub}` : name, bit: BTN[name], rect: box(rr(x, y, w, h, 8, 1), BODY + 8) });
  btn(L.oct[0], "OCT-");
  btn(L.oct[1], "OCT+");
  const [gx, gy, gw, gh, dx, dy] = L.grid;
  for (let i = 0; i < 6; i++) { btn([gx + i * dx, gy, gw, gh], ROW1[i]); btn([gx + i * dx, gy + dy, gw, gh], ROW2[i], ROW2[i] === "PLAY" ? "STOP" : ""); }
  for (const k of keyPlaces(L)) out.push({ kind: "key", k: k.k, name: keyName(k.k), rect: box([[k.cx - k.pitch / 2, k.cy - k.reach], [k.cx + k.pitch / 2, k.cy + k.reach]], BODY + 2) });
  return out;
}
const KEYNAMES = ["F", "F♯", "G", "G♯", "A", "A♯", "B", "C", "C♯", "D", "D♯", "E"];
export const keyName = (k) => KEYNAMES[k % 12] + (3 + Math.floor((k + 5) / 12));
function keyPlaces(L) {
  const [kx, ky, kw0, kh0] = L.keybed, pitch = (kw0 - 24) / 16, out = [];
  WHITE.forEach((k, i) => out.push({ k, cx: kx + 12 + (i + 0.5) * pitch, cy: ky + kh0 * 0.75, pitch, reach: kh0 * 0.24 }));
  BLACK_AFTER.forEach((a, j) => out.push({ k: BLACK[j], cx: kx + 12 + (a + 1) * pitch, cy: ky + kh0 * 0.25, pitch, reach: kh0 * 0.24 }));
  return out;
}
// where the 240 x 240 screen sits on the sprite: [x, y, size] in its pixels
export function screenRect(L) {
  const [sx, sy, ss] = L.screen, inset = ss / 32, [x, y] = Pf(sx + inset, sy + inset, BODY - 7);
  return [x, y, (ss - 2 * inset) * DS];
}

// the lights as the emulator sends them (web/emu/worklet.js publish: lit, dim, breath, mid; anim: the power-on sweep's
// levels a LED, 14 buttons then 27 keys) -> a colour a button / a key slit
const lin = (h) => hex(h).map((v) => (v / 255) ** 2.2);
const ANIM = { btn: [lin("#3d4152"), lin("#4a4840"), lin("#fff4d6")], slit: [lin("#2e3040"), lin("#77777b"), lin("#ececee")] };
const animCol = (kind, q) => {
  const [dark, glow, on] = ANIM[kind], s = Math.min(q & 127, 64) / 64, base = q & 128 ? glow : dark;
  return base.map((v, i) => 255 * (v + (on[i] - v) * s) ** (1 / 2.2));
};
const breath = (t) => (1 - Math.cos(t * 2 * Math.PI / 1.13)) / 2;   // hal/fm1_input.h fm1_led_breath, ~1.1 s

// st: {on, t, leds: [litB, litK, dimB, dimK, dimLo, brB, brK, midK], anim, held (button bits), keys (key bits), master 0..1023}
// -> true while something on it breathes (the page draws it again soon)
export function drawFM1(cv, L, st) {
  const fx = cv.getContext("2d", { willReadFrequently: true });
  const poly = (pts, style) => { fx.fillStyle = style; fx.beginPath(); pts.forEach(([x, y], i) => (i ? fx.lineTo(x, y) : fx.moveTo(x, y))); fx.closePath(); fx.fill(); };
  const prism = (out, inn, z0, z1, col, chamfer = 4) => {   // the walls facing us, a chamfer ring, the flat top
    const n = out.length, zc = inn ? z1 - chamfer : z1;
    for (let i = 0; i < n; i++) {
      const a = out[i], b = out[(i + 1) % n], nx = b[1] - a[1], ny = a[0] - b[0];
      if (ny > 0) poly([P(a[0], a[1], z0), P(b[0], b[1], z0), P(b[0], b[1], zc), P(a[0], a[1], zc)], lit(col, [nx, ny, 0]));
    }
    if (inn) for (let i = 0; i < n; i++) {
      const a = out[i], b = out[(i + 1) % n], c = inn[(i + 1) % n], d = inn[i], e = nrm([b[1] - a[1], a[0] - b[0], 0]);
      poly([P(a[0], a[1], zc), P(b[0], b[1], zc), P(c[0], c[1], z1), P(d[0], d[1], z1)], lit(col, [e[0], e[1], 1.1]));
    }
    poly((inn || out).map((p) => P(p[0], p[1], z1)), lit(col, [0, 0, 1]));
  };
  const pit = (out, z, depth, col) => {               // a recess: its far walls show, the floor sits lower
    fx.save(); fx.beginPath(); out.map((p) => P(p[0], p[1], z)).forEach(([x, y], i) => (i ? fx.lineTo(x, y) : fx.moveTo(x, y))); fx.closePath(); fx.clip();
    poly(out.map((p) => P(p[0], p[1], z)), lit(col, [0, 1, 0.2]));
    poly(out.map((p) => P(p[0], p[1], z - depth)), lit(col, [0, 0, 1]));
    const n = out.length;
    for (let i = 0; i < n; i++) {
      const a = out[i], b = out[(i + 1) % n], nx = b[1] - a[1], ny = a[0] - b[0];
      if (ny < 0) poly([P(a[0], a[1], z), P(b[0], b[1], z), P(b[0], b[1], z - depth), P(a[0], a[1], z - depth)], lit(col, [-nx, -ny, 0]));
    }
    fx.restore();
  };
  const tag = (s, x, y, z, ink = "#c9cde0") => {
    const [px, py] = P(x, y, z);
    fx.font = "12px 'Terminus Small', 'Terminus', monospace"; fx.textAlign = "center"; fx.fillStyle = ink; fx.fillText(s, px, py);
  };
  const body = hex("#2b2e3c"), tray = hex("#161824"), part = hex("#3d4152"), keyc = hex("#14151d"), lcd = hex("#06070c");
  const [litB, litK, dimB, dimK, dimLo, brB, brK, midK] = st.on ? st.leds : [0, 0, 0, 0, 0, 0, 0, 0];
  const anim = st.on ? st.anim : null, b = breath(st.t);
  let breathing = false;
  fx.clearRect(0, 0, cv.width, cv.height);
  poly(rr(16, 22, L.w - 8, L.h - 8, 40, 2).map((p) => P(p[0], p[1], 0)), "#02030a");   // its shadow
  prism(rr(4, 4, L.w - 8, L.h - 8, 40, 2), rr(16, 16, L.w - 32, L.h - 32, 30, 2), 0, BODY, body, 8);
  for (const [x, y, w, h] of L.trays) pit(rr(x, y, w, h, 14, 1), BODY, 6, tray);
  const [sx, sy, ss] = L.screen;
  pit(rr(sx - 6, sy - 6, ss + 12, ss + 12, 20, 2), BODY, 4, tray);
  pit(rr(sx, sy, ss, ss, 16, 1), BODY - 1, 6, lcd);
  KNOBS.forEach((name, i) => {
    const [x, y, r] = L.knobs[i];
    pit(ngon(x, y, r + 6, 10), BODY, 3, tray);
    prism(ngon(x, y, r, 10), ngon(x, y, r - 6, 10), BODY - 3, BODY + 20, part, 6);
    if (name === "MASTER") {                          // only MASTER has a notch: the others are endless encoders
      const a = (-135 + 270 * (st.master ?? 700) / 1023) * Math.PI / 180, s = Math.sin(a), c = -Math.cos(a);
      fx.strokeStyle = "#e6e8f2"; fx.lineWidth = 2; fx.beginPath();
      fx.moveTo(...P(x + s * (r - 9), y + c * (r - 9), BODY + 20)); fx.lineTo(...P(x + s * (r - 2), y + c * (r - 2), BODY + 20)); fx.stroke();
    }
    tag(name, x, y - r - 24, BODY);
  });
  const button = (x, y, w, h, name, sub) => {
    const bit = 1 << BTN[name], held = st.held & bit;
    let col = part, bright = false;
    if (anim) { col = animCol("btn", anim[BTN[name]]); bright = (anim[BTN[name]] & 127) > 32; }
    else if (BTN[name] === BTN.PLAY && litB & (1 << PLAY_GREEN)) { col = hex(css("--go")); bright = true; }
    else if (litB & bit) { col = hex("#fff4d6"); bright = true; }
    else if (brB & bit) { col = mix(hex("#4a4840"), hex(dimLo ? "#aca592" : "#d8cfb6"), b); bright = b > 0.5; breathing = true; }
    else if (dimB & bit) col = hex("#4a4840");
    const top = held ? BODY + 4 : BODY + 8;           // a button held down sits lower
    prism(rr(x, y, w, h, 8, 1), rr(x + 4, y + 4, w - 8, h - 8, 5, 1), BODY, top, col, 4);
    const ink = name === "REC" && !bright ? "#ff5a4a" : bright ? "#0b0d18" : "#d6d8e4";
    tag(name, x + w / 2, y + h / 2 + (sub ? -4 : 6), top, ink);
    if (sub) tag(sub, x + w / 2, y + h / 2 + 15, top, ink);
  };
  button(...L.oct[0], "OCT-"); button(...L.oct[1], "OCT+");
  const [gx, gy, gw, gh, dx, dy] = L.grid;
  for (let i = 0; i < 6; i++) { button(gx + i * dx, gy, gw, gh, ROW1[i]); button(gx + i * dx, gy + dy, gw, gh, ROW2[i], ROW2[i] === "PLAY" ? "STOP" : ""); }
  const [kx, ky, kw0, kh0] = L.keybed;
  pit(rr(kx, ky, kw0, kh0, 18, 2), BODY, 10, tray);
  const kw = (kw0 - 24) / 16 * 0.78, kh = kh0 * 0.425;
  for (const { k, cx, cy } of keyPlaces(L)) {
    const bit = 1 << k, down = st.keys & bit ? 3 : 0, x = cx - kw / 2, y = cy - kh / 2;
    let slit = hex("#2e3040");
    if (anim) slit = animCol("slit", anim[14 + k]);
    else if (litK & bit) slit = hex("#ececee");
    else if (brK & bit) { slit = mix(hex("#3a3a3c"), hex(dimLo ? "#a1a1a3" : "#c8c8ca"), b); breathing = true; }
    else if (midK & bit) slit = hex(dimLo ? "#8a8a8e" : "#9a9a9e");
    else if (dimK & bit) slit = hex("#77777b");
    prism(rr(x, y, kw, kh, kw / 2, 2), rr(x + 3, y + 3, kw - 6, kh - 6, kw / 2 - 3, 2), BODY - 10, BODY + 2 - down, keyc, 3);
    const sw = Math.max(4, kw * 0.16), [ax, ay] = P(cx - sw / 2, y + kw * 0.45, BODY + 2 - down), [bx, by] = P(cx + sw / 2, y + kh - kw * 0.45, BODY + 2 - down);
    fx.fillStyle = rgb(slit); fx.fillRect(ax, ay, Math.max(2, bx - ax), by - ay);
  }
  psxDither(fx, cv.width, cv.height);
  return breathing;
}
