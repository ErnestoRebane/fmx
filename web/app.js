// SPDX-License-Identifier: GPL-3.0-only
// The FMX companion app (web/app.html): the page's parts wired together. The FM-1 over Web MIDI (app_device.js), its
// firmware (fm1ota.js, fm1pkg.js: the installer's update path), its sample pool (pool.js: the map, the list, the
// uploads; sounds.js: the files told apart and built, the library in this browser), the set's songs and its backup, the
// packs, and the emulator (app_emu.js). Nothing is sent to the FM-1 without a press of a button here.
import { Updater, OUR_LOADER } from "./fm1ota.js";
import { productOf, logicalImage, validateStockPackage } from "./fm1pkg.js";
import { freeSectors, sectorsFor, NSECT, freeId, place } from "./pool.js";
import { parseMidi, parts, pick, keyOf, keyLabel, themeName, bankImage, BARS, STEPS, MAX_THEMES } from "./melody.js";
import { Device } from "./app_device.js";
import { Emulator } from "./app_emu.js";
import { css, hex, heroFrame, gemIcon } from "./app_draw.js";
import { TYPES, TYPE_COL, KIT_BASE, decodeFile, makeCard, cardBuild, entriesOf, planSend, parseImage, typeOf, playImage, playCard,
  playKit, playSlice, stopSound, newKit, kitUsed, kitBuild, moveCut, dropCut, addCut, resetCuts, libAll, libPut, libDel } from "./sounds.js";
import { NOKEY, keyName, noteName } from "./slice.js";
import { SLOT_RATE } from "./breaks.js";

const $ = (id) => document.getElementById(id);
const META = window.FMX_META;                         // the package this site serves (web/make_site.py), null in a dev copy
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const plural = (n, w) => `${n} ${w}${n === 1 ? "" : "s"}`;
const DEMO = [{ bpm: 172, key: "F minor", col: "--brk" }, { bpm: 165, key: "A minor", col: "--chd" },
  { bpm: 174, key: "C minor", col: "--chop" }, { bpm: 168, key: "D♯ minor", col: "--bass" }];   // web/emu/sloop_web.c DEMO
const PACKS = [["PIANO", "PIANO"]];   // (web/packs: tools/gen_packs.py)
const BPS = (khz) => ({ 22: 22050, 16: 16000, 11: 11025 }[khz] / 2);   // 4-bit ADPCM: bytes a second

let midi = null, dev = null, busy = false, manualOff = false;
let library = [], cards = [], kits = [], songs = null, estKhz = 22, sel = null;
const pkg = { image: null, state: META ? "loading" : "none" };
let stock = null, writing = false;

// ---- busy: one thing at a time with the FM-1 --------------------------------------------------------------------------
async function run(statusEl, fn) {
  if (busy) return;
  busy = true;
  document.body.classList.add("busy");
  renderAll();
  try { await fn(); } catch (e) { if (statusEl) { statusEl.textContent = e.message || String(e); statusEl.className = "status bad"; } console.error(e); }
  busy = false;
  document.body.classList.remove("busy");
  renderAll();
}
const say = (el, t, cls = "") => { el.textContent = t; el.className = "status " + cls; };
function progress(boxId, barId) {
  const box = $(boxId), bar = $(barId);
  box.hidden = false;
  bar.style.width = "0";
  return { set: (f) => { bar.style.width = `${Math.round(Math.min(1, f) * 100)}%`; }, done: () => { box.hidden = true; } };
}

// ---- the FM-1 ----------------------------------------------------------------------------------------------------------
async function connect() {
  if (!navigator.requestMIDIAccess) {
    $("devText").textContent = "No Web MIDI here";
    return;
  }
  manualOff = false;
  await run($("poolStatus"), async () => {
    $("devText").textContent = "Looking for the FM-1…";
    if (!midi) {
      try { midi = await navigator.requestMIDIAccess({ sysex: true }); } catch { throw new Error("MIDI (SysEx) was not allowed. Allow MIDI in the site settings (address bar), then press Connect."); }
      let t = 0;
      midi.addEventListener("statechange", () => { clearTimeout(t); t = setTimeout(ports, 700); });
    }
    await setDev(await Device.connect(midi));
    if (!dev) $("devText").textContent = "No FM-1";
  });
}
// a port came or went: the FM-1 unplugged, or plugged in again (not while an install runs: it restarts on purpose)
async function ports() {
  if (writing || busy) return;
  if (dev && dev.lost) await setDev(null);
  else if (!dev && !manualOff) await run(null, async () => { await setDev(await Device.connect(midi)); });
}
async function setDev(d) {
  if (dev && dev !== d) dev.close();
  dev = d;
  songs = null;
  sel = null;
  renderAll();
  if (dev && dev.kind === "fmx") {
    try { songs = await dev.songs(); } catch { songs = null; }
    renderAll();
  }
}
$("connect").addEventListener("click", async () => {
  if (dev) { manualOff = true; await setDev(null); return; }
  connect();
});
// the browser already lets this site use MIDI: connect at once
if (navigator.permissions && navigator.requestMIDIAccess)
  navigator.permissions.query({ name: "midi", sysex: true }).then((p) => { if (p.state === "granted") connect(); }, () => {});

// ---- everything drawn from the state -------------------------------------------------------------------------------------
function renderAll() {
  const fmx = dev && dev.kind === "fmx";
  document.body.classList.toggle("offline", !dev);
  document.body.classList.toggle("nopool", !!dev && !fmx);
  $("devText").textContent = dev ? `FM-1 · ${dev.label} · USB` : busy ? $("devText").textContent : "No FM-1";
  $("connect").textContent = dev ? "Disconnect" : "Connect";
  $("connect").disabled = busy;
  renderFirmware();
  renderPool();
  renderFiles();
  renderSet();
  renderPacks();
  renderMelSend();
  renderDoors();
}
function renderDoors() {
  const fmx = dev && dev.kind === "fmx";
  $("doorEmuV").textContent = emu.on ? "On" : "Demo set";
  $("doorEmuS").textContent = emu.on ? "playing in this tab" : "4 songs, works on iPhone";
  $("doorFwV").textContent = META ? `FMX ${META.version}` : "FMX";
  $("doorFwV").textContent += sameFirmware() ? " on it" : pkg.image ? " ready" : "";
  $("doorFwS").textContent = !dev ? "connect the FM-1 to compare" : dev.kind === "update" ? "an install to finish" : `your FM-1 has ${dev.label}`;
  $("doorFwA").textContent = sameFirmware() ? "Details →" : dev && dev.kind === "update" ? "Finish →" : "Install →";
  if (fmx) {
    const free = freeSectors(dev.dir) * 4;
    $("doorSndV").innerHTML = `${free} KB <span class="dim" style="font-size: 16px">free</span>`;
    $("doorSndS").textContent = `${plural(dev.dir.entries.length, "sound")}` + (songs ? ` · ${plural(songs.filter((s) => s.used).length, "song")}` : "");
  } else {
    $("doorSndV").innerHTML = `${library.length} <span class="dim" style="font-size: 16px">here</span>`;
    $("doorSndS").textContent = dev ? "no sample pool on it" : "connect the FM-1";
  }
}

// ---- firmware ------------------------------------------------------------------------------------------------------------
const sameFirmware = () => !!(dev && META && dev.kind !== "update" && dev.kind !== "stock" && dev.id.text === META.product);
let stepAt = -1, reported = "";                       // the install's step (1 send, 2 restart, 4 done), what came back
function renderFirmware() {
  $("fwDev").textContent = dev ? dev.label : "—";
  $("fwHere").textContent = META ? `FMX ${META.version}` : "—";
  $("fwFile").textContent = META ? `${META.pkg.split("/").pop()} · ${pkg.state === "ready" ? "SHA-256 checked" : pkg.state === "bad" ? "damaged" : "checking…"}` : "no package in this copy of the site";
  const install = $("install");
  install.disabled = busy || writing || !pkg.image || !dev;
  install.textContent = dev && dev.kind === "update" ? "Finish the update" : sameFirmware() ? `Install FMX ${META.version} again` : META ? `Install FMX ${META.version}` : "Install FMX";
  const at = stepAt >= 0 ? stepAt : pkg.image ? 1 : 0;   // (idle: the package checked, or not yet)
  $("steps").querySelectorAll("li").forEach((li, i) => {
    li.className = i < at ? "done" : i === at && (stepAt >= 0 || i === 0) ? "now" : "";
    if (li.dataset.k === "done") li.textContent = reported ? `FM-1 reports ${reported}` : META ? `FM-1 reports ${META.version}` : "FM-1 reports back";
  });
  $("stockGo").disabled = busy || writing || !stock || !$("stockSaved").checked || !dev;
}
const fwLog = (t) => { $("fwLog").textContent += t + "\n"; };
(async () => {                                         // the package: fetched, its identity and SHA-256 checked
  if (!META) return;
  try {
    const r = await fetch(META.pkg);
    if (!r.ok) throw new Error(`${META.pkg}: HTTP ${r.status}`);
    const raw = new Uint8Array(await r.arrayBuffer());
    if (productOf(raw) !== META.product) throw new Error("its identity is not the one expected");
    if (META.sha256 && self.crypto && crypto.subtle) {
      const h = [...new Uint8Array(await crypto.subtle.digest("SHA-256", raw))].map((b) => b.toString(16).padStart(2, "0")).join("");
      if (h !== META.sha256) throw new Error(`SHA-256 ${h}, expected ${META.sha256}`);
    }
    pkg.image = logicalImage(raw);
    pkg.state = "ready";
  } catch (e) {
    pkg.state = "bad";
    say($("fwStatus"), "The firmware file here is damaged. Reload the page.", "bad");
    fwLog(String(e.message || e));
  }
  renderAll();
})();
// the update path's errors, by their code (fm1ota.js)
const FW_ERR = {
  notfound: "FM-1 not found. Check the USB cable (a data cable), close other apps that use MIDI, and try again.",
  model: "The connected device is not an FM-1.",
  stopped: "Stopped. Replug the USB cable and press Install again.",
  lost: "The USB connection was lost. Replug it and press Install again.",
  noloader: "The FM-1 in update mode was not found. Replug the USB cable and press Install again (it stays in update mode until it is finished).",
  noreturn: "The FM-1 did not come back. Switch it off and on.",
  mismatch: "Written, but the FM-1 reports another version: ",
  foreign: "The FM-1 is in the update mode of another firmware. Finish that update with its own updater (M-UPGRADE), then try again: ",
  badreq: "The FM-1 asked for something outside the package. Reload the page and try again.",
};
const TOTAL_READS = 74 + 1179;                       // the device's reads: step 1, step 2 (as the installer counts them)
function stepper() {
  const bar = $("fwBar");
  return (k, a) => {
    fwLog(`${k} ${a ?? ""}`);
    if (k === "start") { stepAt = 1; say($("fwStatus"), "Starting…"); }
    if (k === "verify") { say($("fwStatus"), "The FM-1 checks the package…"); bar.style.width = `${Math.min(100, a / TOTAL_READS * 100)}%`; }
    if (k === "loader") say($("fwStatus"), "Switching to update mode…");
    if (k === "write") { say($("fwStatus"), "Writing. Keep the FM-1 on USB."); bar.style.width = `${Math.min(100, (74 + a) / TOTAL_READS * 100)}%`; }
    if (k === "reboot") { stepAt = 2; say($("fwStatus"), "The FM-1 restarts…"); }
    if (k === "done") { stepAt = 4; reported = a; bar.style.width = "100%"; say($("fwStatus"), `Done: the FM-1 reports ${a}.`, "ok"); }
    renderFirmware();
  };
}
async function update(image, product, opts = {}) {
  if (!midi) midi = await navigator.requestMIDIAccess({ sysex: true });
  writing = true;
  stepAt = 1;
  reported = "";
  $("fwBar").style.width = "0";
  $("fwLog").textContent = "";
  if (dev) dev.close();                               // (the update path has the port to itself)
  try {
    const up = new Updater(midi, fwLog), step = stepper(), ota = await up.find((id) => /^ota-/i.test(id.model));
    if (ota) {
      ota.link.close();
      if (opts.stock && OUR_LOADER(ota.id)) throw new Error("The FM-1 is in FMX's update mode. Finish the FMX install first, then go back to the stock V15.");
      const ok = await up.resume(image, step, opts.stock ? { product } : {});
      if (!ok) throw Object.assign(new Error("stopped"), { code: "stopped" });
      if (!opts.stock) { stepAt = 4; $("fwBar").style.width = "100%"; say($("fwStatus"), "Written: the FM-1 restarts with it.", "ok"); }
    } else await up.install(image, product, step);
  } catch (e) {
    stepAt = -1;
    say($("fwStatus"), e.code && FW_ERR[e.code] ? FW_ERR[e.code] + (e.detail || "") : e.message || String(e), "bad");
    fwLog(String(e && e.stack || e));
  } finally {
    writing = false;
  }
  await new Promise((r) => setTimeout(r, 1500));
  await setDev(await Device.connect(midi).catch(() => null));
}
addEventListener("beforeunload", (e) => { if (writing) { e.preventDefault(); e.returnValue = "Writing. Do not close the page."; } });
$("install").addEventListener("click", () => run($("fwStatus"), () => update(pkg.image, META.product)));
$("stockSaved").addEventListener("change", renderFirmware);
$("stockPick").addEventListener("click", () => $("stockFile").click());
$("stockFile").addEventListener("change", async () => {
  const f = $("stockFile").files[0];
  stock = null;
  if (f) {
    try { stock = await validateStockPackage(new Uint8Array(await f.arrayBuffer())); say($("stockStatus"), "The stock V15, checked.", "ok"); }
    catch (e) { say($("stockStatus"), "That is not the stock V15: " + e.message + ".", "bad"); }
  }
  $("stockFile").value = "";
  renderFirmware();
});
$("stockGo").addEventListener("click", () => {
  if (!stock || !confirm("Install the stock V15 on the FM-1? It can't open FMX's songs and sounds. Did you back up the set?")) return;
  run($("stockStatus"), () => update(stock.image, stock.product, { stock: true }));
});

// ---- the pool: its map and its list ----------------------------------------------------------------------------------------
let planCache = null;
function plan() {
  const used = kits.filter(kitUsed);
  if (!cards.length && !used.length) return null;
  if (planCache) return planCache;
  const big = used.map(kitBuild).find((b) => b.fail);
  if (big) return (planCache = { entries: [], writes: [], fail: big.fail + "." });
  const entries = entriesOf(cards, used);
  planCache = dev && dev.kind === "fmx" ? { ...planSend(dev.dir, entries), entries } : { entries, writes: [] };
  return planCache;
}
const libByCrc = () => new Map(library.map((r) => [r.crc, r]));
function renderPool() {
  const fmx = dev && dev.kind === "fmx", dir = fmx ? dev.dir : null, map = $("map"), tbody = $("poolTable").querySelector("tbody");
  const p = plan(), ghost = new Set();
  if (p) for (const w of p.writes) for (let s = w.at; s < w.at + w.ns; s++) ghost.add(s);
  const owner = new Array(NSECT).fill(null);
  if (dir) for (const e of dir.entries) for (let s = e.sect; s < e.sect + e.nsect; s++) owner[s] = e;
  map.textContent = "";
  const second = dir && dir.regions && dir.regions[1] ? dir.regions[1].first : 60;
  for (let k = 0; k < NSECT; k++) {
    if (k === second) { const g = document.createElement("div"); g.className = "gapline"; g.textContent = "the firmware sits here"; map.append(g); }
    const c = document.createElement("button"), o = owner[k];
    c.type = "button";
    c.className = "cell";
    if (o) { const t = typeOf(o); c.dataset.t = t; c.dataset.id = o.id; c.style.background = `var(${TYPE_COL[t]})`; c.title = `${o.name || "U" + (o.id + 1)} · block ${k}`; }
    else if (ghost.has(k)) { c.classList.add("ghost"); c.title = `block ${k} · about to send`; }
    else c.title = `block ${k} · free`;
    if (sel && o && sel === "d" + o.id) c.classList.add("hi");
    c.setAttribute("aria-label", c.title);
    map.append(c);
  }
  const used = dir ? (NSECT - freeSectors(dir)) * 4 : 0, freeKB = dir ? freeSectors(dir) * 4 : 0;
  $("usedKB").textContent = used;
  $("freeTime").textContent = `${(freeKB * 1024 / BPS(estKhz)).toFixed(1)} s`;
  // the list: what's on the FM-1 (matched to the library by its data's CRC), then what's only in this browser
  const lib = libByCrc(), rows = [], onDev = new Set();
  const row = (key, name, type, hz, kb, acts, cls = "") =>
    `<tr data-k="${key}" class="${cls}${sel === key ? " hi" : ""}"><td><span class="sw" style="background: var(${TYPE_COL[type]})"></span>${esc(name)}</td>` +
    `<td class="dim">${type}</td><td class="r">${hz ? Math.round(hz / 1000) : "–"}</td><td class="r">${kb}</td><td class="acts">${acts}</td></tr>`;
  if (dir) {
    for (const e of dir.entries) {
      const r = lib.get(e.crc), type = typeOf(e), name = e.name || `U${e.id + 1}`;
      onDev.add(e.crc);
      const listen = type === "MELODY" ? "" : r ? `<button class="btn small" type="button" data-play="${e.crc}" aria-label="Listen to ${esc(name)}">▶</button>`
        : `<button class="btn small wait" type="button" data-back="${e.id}">Read back</button>`;
      rows.push(row("d" + e.id, name, type, e.hz, e.nsect * 4, listen + `<button class="btn small" type="button" data-erase="${e.id}" aria-label="Remove ${esc(name)} from the FM-1" title="Remove from the FM-1">✕</button>`, r ? "" : "only"));
    }
    if (!dir.entries.length) rows.push(`<tr class="sub"><td colspan="5">Nothing in the pool yet.</td></tr>`);
  }
  const here = library.filter((r) => !onDev.has(r.crc));
  if (here.length) {
    rows.push(`<tr class="sub"><td colspan="5">${dir ? "Only in this browser" : "In this browser"}</td></tr>`);
    for (const r of here)                             // (a BREAK kept here is named BR.. only when it is sent: its file)
      rows.push(row("l" + r.crc, r.name === "BR" && r.from ? r.from.split(",")[0].replace(/\.[^.]*$/, "") : r.name, r.type, r.hz, sectorsFor(r.data.length) * 4,
        (r.type === "MELODY" ? "" : `<button class="btn small" type="button" data-play="${r.crc}" aria-label="Listen to ${esc(r.name)}">▶</button>`) +
        (fmx ? `<button class="btn small" type="button" data-send="${r.crc}">Send</button>` : "") +
        `<button class="btn small" type="button" data-forget="${r.crc}" aria-label="Forget ${esc(r.name)}" title="Forget it in this browser">✕</button>`));
  } else if (!dir) rows.push(`<tr class="sub"><td colspan="5">Nothing here yet: add sounds below.</td></tr>`);
  tbody.innerHTML = rows.join("");
  tbody.querySelectorAll("button").forEach((b) => { b.disabled = busy; });
  $("poolNote").hidden = !dir || !dir.entries.some((e) => !lib.has(e.crc));
}
function highlight(key) {
  sel = sel === key ? null : key;
  $("map").querySelectorAll(".cell").forEach((c) => c.classList.toggle("hi", !!sel && sel === "d" + c.dataset.id));
  $("poolTable").querySelectorAll("tbody tr[data-k]").forEach((tr) => tr.classList.toggle("hi", tr.dataset.k === sel));
}
$("map").addEventListener("click", (e) => { const c = e.target.closest(".cell"); if (c && c.dataset.id) highlight("d" + c.dataset.id); });
$("poolTable").addEventListener("click", (e) => {
  const b = e.target.closest("button");
  if (!b) { const tr = e.target.closest("tr[data-k]"); if (tr) highlight(tr.dataset.k); return; }
  if (b.dataset.play) { const r = library.find((x) => x.crc === +b.dataset.play); if (r) playImage(r, r.type); }
  else if (b.dataset.back) readBack(+b.dataset.back);
  else if (b.dataset.erase) removeEntry(+b.dataset.erase);
  else if (b.dataset.send) sendLibrary(+b.dataset.send);
  else if (b.dataset.forget) forget(+b.dataset.forget);
});
$("estSeg").addEventListener("click", (e) => {
  const b = e.target.closest("[data-khz]");
  if (!b) return;
  estKhz = +b.dataset.khz;
  $("estSeg").querySelectorAll("[data-khz]").forEach((x) => x.setAttribute("aria-pressed", x === b));
  renderPool();
});
async function refreshLibrary() {
  library = (await libAll()).sort((a, b) => a.added - b.added);
  if (emu.on && $("mine").checked) loadMine();
}
function readBack(id) {
  const e = dev.dir.entries.find((x) => x.id === id);
  run($("poolStatus"), async () => {
    say($("poolStatus"), `Reading ${e.name || "it"} back…`);
    const im = await dev.readEntry(e, (f) => say($("poolStatus"), `Reading ${e.name || "it"} back… ${Math.round(f * 100)} %`));
    await libPut(im, { from: "read back from the FM-1" });
    await refreshLibrary();
    say($("poolStatus"), `${e.name || "It"} is kept in this browser now.`, "ok");
  });
}
function removeEntry(id) {
  const e = dev.dir.entries.find((x) => x.id === id), name = e.name || `U${id + 1}`, kept = library.some((r) => r.crc === e.crc);
  if (!confirm(`Remove ${name} from the FM-1?` + (kept ? " A copy stays in this browser." : " There is no copy of it in this browser.") +
    (e.loops ? " The BREAK's loops after it move up one place." : ""))) return;
  run($("poolStatus"), async () => {
    await dev.erase(id);
    await dev.refresh();
    planCache = null;
    say($("poolStatus"), `${name} is gone from the FM-1.`);
  });
}
function sendLibrary(crc) {
  const r = library.find((x) => x.crc === crc);
  run($("poolStatus"), async () => {
    const p = planSend(dev.dir, [{ type: r.type, name: r.name, im: { hdr: r.hdr, data: r.data } }]);
    if (p.fail) throw new Error(p.fail);
    await sendWrites(p.writes, (t) => say($("poolStatus"), t));
    say($("poolStatus"), `${p.writes[0].name} is on the FM-1.`, "ok");
  });
}
async function forget(crc) {
  const r = library.find((x) => x.crc === crc);
  if (!confirm(`Forget ${r.name} in this browser?` + (dev && dev.dir && dev.dir.entries.some((e) => e.crc === crc) ? " It stays on the FM-1." : ""))) return;
  await libDel(crc);
  await refreshLibrary();
  renderAll();
}
// the writes of a plan sent in order (the header 512 bytes with a meta, SLICE's or the melody bank's, else 480), then the
// pool read again
async function sendWrites(writes, onText) {
  const total = writes.reduce((a, w) => a + w.im.data.length, 0) || 1, pr = progress("sendBarBox", "sendBar");
  let sent = 0;
  try {
    for (const w of writes) {
      onText(`Sending ${w.name}…`);
      const meta = new DataView(w.im.hdr.buffer, w.im.hdr.byteOffset).getUint32(480, true);
      const slc = meta === 0x31434C53 || meta === 0x314C454D;
      await dev.send(w.id, w.at, { hdr: slc ? w.im.hdr.subarray(0, 512) : w.im.hdr.subarray(0, 480), data: w.im.data },
        (f) => pr.set((sent + f * w.im.data.length) / total));
      sent += w.im.data.length;
    }
  } finally {
    pr.done();
    await dev.refresh().catch(() => {});
    planCache = null;
  }
}

// ---- adding sounds ---------------------------------------------------------------------------------------------------------
// the files told apart (a hit to a kit's grid: the last kit's first free pad, else a new kit), or put on a kit's pads
// from `to` {kit, pad} on (each file the next free pad after it)
async function addFiles(files, to = null) {
  files = [...files].filter((f) => f.size);
  if (!files.length) return;
  const st = $("sendStatus"), bad = [];
  for (const f of files) {
    say(st, `Listening to ${f.name}…`);
    await new Promise((r) => setTimeout(r, 0));      // (the line drawn before the work)
    try {
      const { rate, x } = await decodeFile(f);
      if (!x.length) throw new Error("no audio");
      const c = makeCard(f.name, x, rate, to ? "KIT" : null);
      if (to) {                                       // (the first on its pad, in place of what was there)
        const i = to.next ? to.kit.pads.indexOf(null, to.pad) : to.pad;
        if (i < 0) { bad.push(`${f.name} (the kit is full)`); continue; }
        to.kit.pads[i] = c;
        to = { kit: to.kit, pad: i + 1, next: true };
      } else if (c.type === "KIT") toKit(c);
      else cards.push(c);
    } catch { bad.push(f.name); }
    planCache = null;
    renderFiles();
    renderPool();
  }
  say(st, bad.length ? `Couldn't read ${bad.join(", ")}.` : "", bad.length ? "bad" : "");
  renderAll();
}
// a hit onto a kit's grid: the last kit's first free pad, else a new kit
function toKit(c) {
  let k = kits[kits.length - 1];
  if (!k || k.pads.every(Boolean)) kits.push(k = newKit(`KIT${kits.length + 1}`));
  k.pads[k.pads.indexOf(null)] = c;
}
function wave(cv, c) {
  const x = cv.getContext("2d"), W = cv.width, H = cv.height, b = cardBuild(c), zs = b.zones;
  const n = zs.reduce((a, z) => a + z.s.length, 0);
  x.fillStyle = css("--navy-2"); x.fillRect(0, 0, W, H);
  x.fillStyle = css(TYPE_COL[c.type]);
  let at = 0;
  const starts = [];
  for (const z of zs) { starts.push(at); at += z.s.length; }
  for (let i = 0; i < W; i++) {
    const a = Math.floor(i * n / W), e = Math.max(a + 1, Math.floor((i + 1) * n / W));
    let pk = 0, k = 0, off = 0;
    for (let j = a; j < e; j++) {
      while (k + 1 < zs.length && j >= starts[k + 1]) k++;
      off = j - starts[k];
      pk = Math.max(pk, Math.abs(zs[k].s[off] || 0));
    }
    const h = Math.max(1, Math.round(pk / 32768 * (H - 2)));
    x.fillRect(i, Math.round((H - h) / 2), 1, h);
  }
  x.fillStyle = css("--ink");
  if (c.type === "LOOP") for (const s of starts.slice(1)) x.fillRect(Math.round(s * W / n), 0, 1, H);
  else if (c.type === "BREAK") for (let k = 1; k < b.lines; k++) x.fillRect(Math.round(k * W / b.lines), 0, 1, H);
}
function renderFiles() {
  const box = $("files");
  box.textContent = "";
  kits.forEach((k, i) => box.append(kitBox(k, i)));
  cards.forEach((c, i) => {
    const b = cardBuild(c), el = document.createElement("div");
    el.className = "file";
    const fix = c.type === "LOOP" ? `<label>Key <select data-fix="key">${[NOKEY, ...Array.from({ length: 24 }, (_, k) => (k % 12) | (k >= 12 ? 16 : 0))]
      .map((k) => `<option value="${k}"${k === c.key ? " selected" : ""}>${keyName(k)}</option>`).join("")}</select></label>
      <label>Bars <select data-fix="bars">${[1, 2, 4, 8].map((n) => `<option${n === c.bars ? " selected" : ""}>${n}</option>`).join("")}</select></label>`
      : c.type === "INSTRUMENT" ? `<label>Its note <select data-fix="root">${Array.from({ length: 61 }, (_, k) => k + 36)
        .map((m) => `<option value="${m}"${m === c.root ? " selected" : ""}>${noteName(m)}</option>`).join("")}</select></label>` : "";
    el.innerHTML = `<div class="name"><b title="${esc(c.file)}">${esc(c.file)}</b>
        <select class="chip" style="color: var(${TYPE_COL[c.type]})" aria-label="Type of ${esc(c.file)}" data-fix="type">${TYPES.map((t) => `<option${t === c.type ? " selected" : ""}>${t}</option>`).join("")}</select>
        <button class="btn small" type="button" data-drop aria-label="Leave ${esc(c.file)} out">✕</button></div>
      ${c.type === "LOOP" ? `<div class="slicer"><div class="xs"></div><canvas height="96" tabindex="0" aria-label="The slice lines of ${esc(c.file)}"></canvas></div>
      <div class="dim small">Drag a line: it snaps to the hits (the ticks along the top; Alt: free) · ✕ takes it away · double-click adds one · click a slice to hear it</div>`
        : `<canvas width="240" height="24" aria-hidden="true"></canvas>`}
      <div class="dim">${esc(b.what)}</div>
      ${fix ? `<div class="row">${fix}${c.type === "LOOP" && c.edited ? `<button class="btn small" type="button" data-reset>Lines on the hits again</button>` : ""}</div>` : ""}
      <div class="row" style="justify-content: space-between">
        <span class="seg" role="group" aria-label="Quality for ${esc(c.file)}">${[22, 16, 11].map((k) => `<button type="button" data-k="${k}" aria-pressed="${k === c.khz}">${k} kHz</button>`).join("")}</span>
        <span class="num"><b>${b.blocks}</b> <span class="dim">block${b.blocks === 1 ? "" : "s"}</span></span>
        <button class="btn small" type="button" data-listen>▶ Listen as the FM-1 plays it</button>
      </div>`;
    el.querySelectorAll("[data-k]").forEach((x) => x.addEventListener("click", () => { c.khz = +x.dataset.k; filesChanged(); }));
    el.querySelectorAll("select[data-fix]").forEach((s) => s.addEventListener("change", () => {
      const v = s.value, k = s.dataset.fix;
      if (k === "type") c.type = v; else c[k] = +v;
      if (c.type === "KIT") { cards.splice(i, 1); toKit(c); }   // (a hit goes to a kit's grid)
      filesChanged();
    }));
    el.querySelector("[data-drop]").addEventListener("click", () => { cards.splice(i, 1); filesChanged(); });
    el.querySelector("[data-listen]").addEventListener("click", () => playCard(c));
    el.querySelector("[data-reset]")?.addEventListener("click", () => { resetCuts(c); filesChanged(); });
    box.append(el);
    if (c.type === "LOOP") slicer(el.querySelector(".slicer"), c);
    else wave(el.querySelector("canvas"), c);
  });
  renderSendRow();
}
function filesChanged() { planCache = null; renderFiles(); renderPool(); renderSendRow(); }

// ---- a LOOP's lines: the loop with its hits (ticks along the top), the beats the FM-1 starts the slices on (faint) and
// its lines, each with a ✕ over it. A line dragged lands on the nearest hit (Alt: free); ✕ (or Delete) takes it away; a
// double click adds one (on a hit); a click plays the slice under it; ← → nudge the line picked 1 ms (Shift: 10 ms).
const MAGNET = 12;                                    // CSS pixels a line jumps to a hit from
function slicer(box, c) {
  const cv = box.querySelector("canvas"), xs = box.querySelector(".xs"), y = c.a.y, len = c.span.len;
  let drag = -1, moved = false, pick = -1;
  const pxS = () => len / cv.clientWidth;             // samples a CSS pixel
  const at = (ev) => (ev.clientX - cv.getBoundingClientRect().left) * pxS();
  const near = (s) => {
    let k = -1, d = 8 * pxS();
    c.cuts.forEach((q, i) => { if (i && Math.abs(q - s) <= d) { d = Math.abs(q - s); k = i; } });
    return k;
  };
  const sliceAt = (s) => { let i = 0; while (i + 1 < c.cuts.length && c.cuts[i + 1] <= s) i++; return i; };
  function draw() {
    const W = cv.width = Math.max(1, Math.round(cv.clientWidth)), H = cv.height, g = cv.getContext("2d"), X = (s) => Math.round(s * W / len);
    const n = c.cuts.length;
    g.fillStyle = css("--navy-2"); g.fillRect(0, 0, W, H);
    g.fillStyle = css("--rule");                      // the beats the FM-1 starts the slices on
    for (let i = 1; i < n; i++) g.fillRect(Math.round(i * W / n), 0, 1, H);
    g.fillStyle = css("--chop");
    for (let x = 0; x < W; x++) {
      let pk = 0;
      for (let i = Math.floor(x * len / W), e = Math.floor((x + 1) * len / W); i < e; i++) pk = Math.max(pk, Math.abs(y[i]));
      const h = Math.max(1, Math.round(pk * (H - 24)));
      g.fillRect(x, Math.round((H - h) / 2) + 6, 1, h);
    }
    g.fillStyle = css("--dim");                       // the hits found
    for (const h of c.a.hits) if (h.t < len) g.fillRect(X(h.t) - 1, 0, 3, 3 + Math.round(6 * h.s));
    g.font = "12px 'Terminus Small', monospace";
    c.cuts.forEach((q, i) => {
      const x = X(q), on = i === pick || i === drag;
      g.fillStyle = css(on ? "--wait" : "--ink");
      g.fillRect(x - (on ? 1 : 0), 0, on ? 3 : 1, H);
      g.fillText(String.fromCharCode(65 + i), x + 4, H - 4);
    });
    xs.innerHTML = c.cuts.map((q, i) => (i ? `<button type="button" data-x="${i}" style="left: ${X(q)}px" aria-label="Take away line ${String.fromCharCode(65 + i)}" title="Take away this line">✕</button>` : "")).join("");
  }
  xs.addEventListener("click", (ev) => {
    const b = ev.target.closest("[data-x]");
    if (!b) return;
    dropCut(c, +b.dataset.x);
    filesChanged();
  });
  cv.addEventListener("pointerdown", (ev) => {
    const s = at(ev), k = near(s);
    if (k > 0) { drag = k; moved = false; pick = k; cv.setPointerCapture(ev.pointerId); draw(); return; }
    pick = -1;
    draw();
    playSlice(c, sliceAt(s));
  });
  cv.addEventListener("pointermove", (ev) => {
    const s = at(ev);
    if (drag < 0) { cv.style.cursor = near(s) > 0 ? "ew-resize" : "pointer"; return; }
    moveCut(c, drag, s, ev.altKey ? 0 : MAGNET * pxS());
    moved = true;
    draw();
  });
  const up = () => {
    if (drag < 0) return;
    const k = drag;
    drag = -1;
    if (moved) filesChanged(); else draw();
    playSlice(c, k);
  };
  cv.addEventListener("pointerup", up);
  cv.addEventListener("pointercancel", up);
  cv.addEventListener("dblclick", (ev) => {
    const k = addCut(c, at(ev), ev.altKey ? 0 : MAGNET * pxS());
    if (k >= 0) { filesChanged(); playSlice(c, k); }
  });
  cv.addEventListener("keydown", (ev) => {
    if (pick < 1) return;
    if (ev.key === "Delete" || ev.key === "Backspace") { ev.preventDefault(); dropCut(c, pick); filesChanged(); return; }
    if (ev.key !== "ArrowLeft" && ev.key !== "ArrowRight") return;
    ev.preventDefault();
    moveCut(c, pick, c.cuts[pick] + (ev.key === "ArrowLeft" ? -1 : 1) * Math.round(SLOT_RATE * (ev.shiftKey ? 0.01 : 0.001)));
    draw();
    clearTimeout(cv._t);
    cv._t = setTimeout(() => { const k = pick; filesChanged(); playSlice(c, k); }, 400);
  });
  draw();
}

// ---- a kit: 16 pads, a key each from F3. A file dropped on a pad goes there (more: the next free pads); a pad dragged
// onto another swaps them; a click plays a hit, or on an empty pad opens files for it.
let padFor = null;                                     // the pad the file picker fills
function kitBox(k, ki) {
  const b = kitBuild(k), el = document.createElement("div"), last = k.pads.reduce((m, c, i) => (c ? i : m), -1);
  el.className = "file";
  el.innerHTML = `<div class="name"><label class="kitname">Kit <input type="text" maxlength="8" value="${esc(k.name)}" aria-label="The kit's name" spellcheck="false"></label>
      <span class="chip" style="color: var(--kit)">KIT</span>
      <button class="btn small" type="button" data-drop aria-label="Leave the kit ${esc(k.name)} out">✕</button></div>
    <div class="pads">${k.pads.map((c, i) => `<div class="pad${c ? "" : " empty"}${i > last ? " past" : ""}" data-pad="${i}" draggable="${!!c}" tabindex="0" role="button"
        aria-label="${c ? `Pad ${i + 1}, ${noteName(KIT_BASE + i)}: ${esc(c.file)}. Play` : `Pad ${i + 1}, ${noteName(KIT_BASE + i)}: empty. Add a sound`}">
        <span class="key">${noteName(KIT_BASE + i)}</span><b title="${c ? esc(c.file) : ""}">${c ? esc(c.name) : "+"}</b>
        ${c ? `<button type="button" data-clear="${i}" aria-label="Empty pad ${i + 1}" title="Empty this pad">✕</button>` : ""}</div>`).join("")}</div>
    <div class="dim">${b.hits ? `${plural(b.hits, "hit")} on keys ${noteName(KIT_BASE)}–${noteName(KIT_BASE + last)}` + (b.hits <= last ? " · an empty pad is silent" : "") : "Drop sounds on the pads"}
      ${b.fail ? `<br><span class="bad">${esc(b.fail)}.</span>` : ""}</div>
    <div class="row" style="justify-content: space-between">
      <span class="seg" role="group" aria-label="Quality for the kit">${[22, 16, 11].map((q) => `<button type="button" data-k="${q}" aria-pressed="${q === k.khz}">${q} kHz</button>`).join("")}</span>
      <span class="num"><b>${b.hits ? b.blocks : 0}</b> <span class="dim">block${b.blocks === 1 ? "" : "s"}</span></span>
      <button class="btn small" type="button" data-listen${b.hits ? "" : " disabled"}>▶ Listen to the kit</button>
    </div>`;
  const name = el.querySelector("input");
  name.addEventListener("input", () => {
    const v = name.value.toUpperCase().replace(/[^A-Z0-9 ]/g, "").slice(0, 8);
    if (v !== name.value) name.value = v;
    k.name = v.trim() || `KIT${ki + 1}`;
    planCache = null;
  });
  el.querySelector("[data-drop]").addEventListener("click", () => { kits.splice(ki, 1); filesChanged(); });
  el.querySelectorAll("[data-k]").forEach((x) => x.addEventListener("click", () => { k.khz = +x.dataset.k; filesChanged(); }));
  el.querySelector("[data-listen]").addEventListener("click", () => playKit(k));
  const pads = el.querySelector(".pads");
  const padOf = (ev) => ev.target.closest(".pad");
  const press = (p) => {
    const i = +p.dataset.pad;
    if (k.pads[i]) { k.pads[i].khz = k.khz; playCard(k.pads[i]); }
    else { padFor = { kit: k, pad: i }; $("fileIn").click(); }
  };
  pads.addEventListener("click", (ev) => {
    const x = ev.target.closest("[data-clear]");
    if (x) { k.pads[+x.dataset.clear] = null; filesChanged(); return; }
    const p = padOf(ev);
    if (p) press(p);
  });
  pads.addEventListener("keydown", (ev) => {
    const p = padOf(ev);
    if (!p || (ev.key !== "Enter" && ev.key !== " " && ev.key !== "Delete" && ev.key !== "Backspace")) return;
    ev.preventDefault();
    if (ev.key === "Delete" || ev.key === "Backspace") { k.pads[+p.dataset.pad] = null; filesChanged(); }
    else press(p);
  });
  pads.addEventListener("dragstart", (ev) => {
    const p = padOf(ev);
    if (!p) return;
    ev.dataTransfer.setData("application/x-fmx-pad", `${ki}/${p.dataset.pad}`);
    ev.dataTransfer.effectAllowed = "move";
  });
  pads.addEventListener("dragover", (ev) => {
    const p = padOf(ev);
    if (!p) return;
    ev.preventDefault();
    pads.querySelectorAll(".pad.over").forEach((q) => q !== p && q.classList.remove("over"));
    p.classList.add("over");
  });
  pads.addEventListener("dragleave", (ev) => { const p = padOf(ev); if (p && !p.contains(ev.relatedTarget)) p.classList.remove("over"); });
  pads.addEventListener("drop", (ev) => {
    const p = padOf(ev);
    if (!p) return;
    ev.preventDefault();
    const i = +p.dataset.pad, from = ev.dataTransfer.getData("application/x-fmx-pad");
    if (from) {                                       // a pad onto a pad: swapped (from another kit too)
      const [fk, fi] = from.split("/").map(Number), src = kits[fk];
      if (src) { [src.pads[fi], k.pads[i]] = [k.pads[i], src.pads[fi]]; filesChanged(); }
    } else if (ev.dataTransfer.files?.length) addFiles(ev.dataTransfer.files, { kit: k, pad: i });
  });
  return el;
}
function renderSendRow() {
  const p = plan(), fmx = dev && dev.kind === "fmx", btn = $("sendBtn"), n = p ? p.entries.length : 0;
  btn.disabled = busy || !p || !!p.fail;
  btn.textContent = !p ? "Send to the FM-1" : fmx ? `Send ${plural(n, "sound")} to the FM-1` : `Keep ${plural(n, "sound")} in this browser`;
  $("afterText").textContent = !p ? "" : p.fail ? p.fail : !fmx ? (dev ? "this FM-1 has no sample pool" : "send them when the FM-1 is connected")
    : `leaves ${plural(p.left, "block")} free`;
  $("afterText").className = "num " + (p && p.fail ? "bad" : "dim");
}
$("open").addEventListener("click", () => { padFor = null; $("fileIn").click(); });
$("newKit").addEventListener("click", () => { kits.push(newKit(`KIT${kits.length + 1}`)); filesChanged(); });
$("fileIn").addEventListener("change", (e) => { addFiles(e.target.files, padFor); padFor = null; e.target.value = ""; });
{
  let t = 0;
  addEventListener("resize", () => { clearTimeout(t); t = setTimeout(() => { if (cards.some((c) => c.type === "LOOP")) renderFiles(); }, 150); });
}
{
  const drop = $("drop");
  let depth = 0;
  addEventListener("dragenter", (e) => { if (e.dataTransfer?.types?.includes("Files")) { depth++; drop.classList.add("over"); } });
  addEventListener("dragleave", () => { if (--depth <= 0) { depth = 0; drop.classList.remove("over"); } });
  addEventListener("dragover", (e) => e.preventDefault());
  addEventListener("drop", (e) => {
    e.preventDefault();
    depth = 0;
    drop.classList.remove("over");
    if (e.target.closest?.(".pad")) return;           // (a kit's pad takes it: kitBox)
    if (e.dataTransfer?.files?.length) {
      const all = [...e.dataTransfer.files], mid = all.filter((f) => /\.(mid|midi|kar)$/i.test(f.name));
      if (!e.target.closest?.("#sounds")) (mid.length ? $("melBox") : $("sounds")).scrollIntoView();
      if (mid.length) addMidi(mid);
      if (mid.length < all.length) addFiles(all.filter((f) => !mid.includes(f)));
    }
  });
}
$("sendBtn").addEventListener("click", () => run($("sendStatus"), async () => {
  const p = plan(), fmx = dev && dev.kind === "fmx";
  if (!p) return;
  stopSound();
  if (fmx) {
    if (p.fail) throw new Error(p.fail);
    await sendWrites(p.writes, (t) => say($("sendStatus"), t));
    for (const w of p.writes) await libPut(w.im, { from: w.cards.map((c) => c.file).join(", ") });
    say($("sendStatus"), `On the FM-1: ${p.writes.map((w) => w.name).join(", ")}. ` + onTheFM1(p.writes), "ok");
  } else {
    for (const e of p.entries) await libPut(e.im, { from: e.cards.map((c) => c.file).join(", ") });
    say($("sendStatus"), `Kept in this browser: ${plural(p.entries.length, "sound")}. Send them when the FM-1 is connected.`, "ok");
  }
  cards = [];
  kits = [];
  planCache = null;
  await refreshLibrary();
}));
// where a sound shows up on the FM-1
function onTheFM1(writes) {
  const t = new Set(writes.map((w) => w.type)), out = [];
  if (t.has("BREAK")) out.push("Breaks: the BREAK screen, EDIT, then LOOP A..D.");
  if (t.has("LOOP")) out.push("Loops: the CHOP track, KNOB 1 to its name, SEL for SLICE.");
  if (t.has("KIT")) out.push("Kits: the CHOP track's CHOP KIT, KNOB 1.");
  if (t.has("INSTRUMENT")) out.push("Instruments: a SAMPLE preset, SET to its name.");
  if (t.has("MELODY")) out.push("Melodies: the CHOP track in INST, MELODY to MIDI, THEME picks one.");
  return out.join(" ");
}

// ---- the set: its songs, its backup ---------------------------------------------------------------------------------------
function renderSet() {
  const box = $("set");
  if (box.childElementCount !== 4) {
    box.innerHTML = [0, 1, 2, 3].map(() => `<div class="song num"><canvas width="16" height="16" aria-hidden="true"></canvas><span class="bpm"></span><span class="dim"></span></div>`).join("");
  }
  [...box.children].forEach((d, i) => {
    const s = songs && songs[i];
    d.classList.toggle("empty", !!songs && !s.used);
    d.querySelector(".bpm").textContent = !songs ? "—" : s.used ? (s.bpm || "—") : "—";
    d.querySelector(".dim").textContent = `SONG ${i + 1}` + (songs && !s.used ? " · empty" : "");
  });
}
$("backup").addEventListener("click", () => run($("setStatus"), async () => {
  const pr = progress("setBarBox", "setBar");
  say($("setStatus"), "Reading the set…");
  try {
    const file = await dev.backup((f) => pr.set(f));
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([JSON.stringify(file)], { type: "application/json" }));
    a.download = `fmx-set-${new Date().toISOString().slice(0, 10)}.json`;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
    say($("setStatus"), `Saved ${a.download}: the songs, the settings and ${plural(file.pool.length, "sound")}.`, "ok");
  } finally { pr.done(); }
}));
$("restore").addEventListener("click", () => $("restoreFile").click());
$("restoreFile").addEventListener("change", async (e) => {
  const f = e.target.files[0];
  e.target.value = "";
  if (!f) return;
  let file;
  try { file = JSON.parse(await f.text()); } catch { say($("setStatus"), "That file is not a backup.", "bad"); return; }
  if (!confirm(`Restore ${f.name} on the FM-1? Its songs, settings and sounds replace the ones there now. Stop the song first.`)) return;
  run($("setStatus"), async () => {
    const pr = progress("setBarBox", "setBar");
    say($("setStatus"), "Restoring…");
    try { await dev.restore(file, (x) => pr.set(x)); } finally { pr.done(); }
    planCache = null;
    songs = await dev.songs().catch(() => null);
    say($("setStatus"), "Restored.", "ok");
  });
});

// ---- melodies: INST's MELODY MIDI (melody.js). Each MIDI file a row: on/off, its name on the FM-1, its 8 bars (found, or
// the part and the bar chosen here), its key; the ones on go to the FM-1 as one bank (a pool entry, replaced each time).
// Kept in this browser: the user's files themselves, the pack's by name (web/packs/melodies).
let mels = [], melSeq = 0;
const MEL_KEY = "fmx-melodies", MEL_PACK = "packs/melodies";
const b64 = (u8) => { let s = ""; for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode(...u8.subarray(i, i + 0x8000)); return btoa(s); };
const unb64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
function melSave() {
  try {
    localStorage.setItem(MEL_KEY, JSON.stringify(mels.map((m) => ({ name: m.name, file: m.file, pack: m.pack, bytes: m.pack ? undefined : b64(m.bytes),
      part: m.part, start: m.start, on: m.on }))));
  } catch { $("melStatus").textContent = "This browser didn't keep the list (too big, or private mode)."; }
}
// a MIDI file's bytes -> a row (null: not a MIDI file we can read)
function melRow(file, bytes, o = {}) {
  const song = parseMidi(bytes), ps = parts(song);
  const m = { id: ++melSeq, file, bytes, song, ps, name: o.name || themeName(file), pack: o.pack || null, on: o.on !== false,
    part: o.part ?? -1, start: o.start ?? -1, key: keyOf(song) };
  melPick(m);
  return m;
}
function melPick(m) {
  const p = pick(m.song, m.part, m.start);
  m.ev = p.ev;
  m.at = p.start;                                   // (what is shown and sent: the found ones when auto)
  m.pat = p.part;
}
async function addMidi(files) {
  const bad = [];
  for (const f of files) {
    try { mels.push(melRow(f.name, new Uint8Array(await f.arrayBuffer()))); } catch (e) { bad.push(`${f.name} (${e.message})`); }
  }
  say($("melStatus"), bad.length ? `Not read: ${bad.join(", ")}.` : "", bad.length ? "bad" : "");
  melSave();
  renderMels();
}
async function addPack() {
  const st = $("melStatus");
  say(st, "Fetching the pack…");
  try {
    const list = await (await fetch(`${MEL_PACK}/index.json`)).json(), have = new Set(mels.map((m) => m.pack));
    for (const f of list.files) {
      if (have.has(f)) continue;
      mels.push(melRow(f, new Uint8Array(await (await fetch(`${MEL_PACK}/${encodeURIComponent(f)}`)).arrayBuffer()), { pack: f }));
    }
    say(st, `${list.title}: ${list.credit}. Licence: ${list.license}.`, "ok");
  } catch (e) { say(st, `The pack didn't load: ${e.message}`, "bad"); }
  melSave();
  renderMels();
}
async function loadMels() {
  let saved = [];
  try { saved = JSON.parse(localStorage.getItem(MEL_KEY) || "[]"); } catch { /* none */ }
  for (const s of saved) {
    try {
      const bytes = s.pack ? new Uint8Array(await (await fetch(`${MEL_PACK}/${encodeURIComponent(s.pack)}`)).arrayBuffer()) : unb64(s.bytes);
      mels.push(melRow(s.file, bytes, s));
    } catch { /* gone: the pack moved on, or the file broke */ }
  }
  renderMels();
}
// 8 bars as a roll: a line a bar, the notes in the line's own range
function melRoll(cv, m) {
  const w = cv.width = 256, h = cv.height = 40, g = cv.getContext("2d");
  g.clearRect(0, 0, w, h);
  g.fillStyle = css("--rule");
  for (let b = 1; b < 8; b++) g.fillRect(b * 32, 0, 1, h);
  if (!m.ev.length) return;
  const lo = Math.min(...m.ev.map((e) => e.note)), hi = Math.max(...m.ev.map((e) => e.note)), span = Math.max(12, hi - lo + 1);
  g.fillStyle = css("--mel");
  for (const e of m.ev) g.fillRect(e.at * 2, Math.round((h - 3) * (1 - (e.note - lo + (span - (hi - lo + 1)) / 2) / span)), Math.max(1, e.len * 2 - 1), 3);
}
function renderMels() {
  const box = $("melList");
  box.innerHTML = mels.map((m) => {
    const opts = [`<option value="-1"${m.part === -1 ? " selected" : ""}>auto${m.part === -1 && m.pat >= 0 ? `: ${esc(m.ps[m.pat].fam.toLowerCase())}` : ""}</option>`,
      ...m.ps.map((p, i) => `<option value="${i}"${m.part === i ? " selected" : ""}>ch ${p.ch + 1} ${esc(p.fam.toLowerCase())}</option>`),
      `<option value="-2"${m.part === -2 ? " selected" : ""}>all, the top</option>`].join("");
    return `<div class="mel num${m.on ? "" : " off"}" data-m="${m.id}">` +
      `<input type="checkbox" data-a="on"${m.on ? " checked" : ""} aria-label="Send ${esc(m.name)}">` +
      `<input type="text" data-a="name" maxlength="8" value="${esc(m.name)}" aria-label="Name on the FM-1" title="${esc(m.file)}">` +
      `<canvas aria-label="${esc(m.name)}: 8 bars from bar ${m.at + 1}"></canvas>` +
      `<select class="chip" data-a="part" aria-label="Part">${opts}</select>` +
      `<span class="bars"><button class="btn small" type="button" data-a="prev" aria-label="8 bars earlier">◀</button> bar ${m.at + 1}/${m.song.bars} ` +
      `<button class="btn small" type="button" data-a="next" aria-label="8 bars later">▶</button></span>` +
      `<span class="dim">${keyLabel(m.key)}</span>` +
      `<span><button class="btn small" type="button" data-a="play" aria-label="Listen to ${esc(m.name)}">♪</button>` +
      `<button class="btn small" type="button" data-a="del" aria-label="Remove ${esc(m.name)}">✕</button></span></div>`;
  }).join("") || `<p class="dim">No melodies yet.</p>`;
  box.querySelectorAll(".mel").forEach((el) => melRoll(el.querySelector("canvas"), mels.find((m) => m.id === +el.dataset.m)));
  renderMelSend();
}
function renderMelSend() {
  const on = mels.filter((m) => m.on && m.ev.length), fmx = dev && dev.kind === "fmx", btn = $("melSend");
  $("melCount").textContent = mels.length ? `${on.length} of ${mels.length} on` : "";
  btn.disabled = busy || !on.length || on.length > MAX_THEMES;
  btn.textContent = fmx ? `Send ${plural(on.length, "melody")} to the FM-1`.replace("melodys", "melodies") : `Keep ${plural(on.length, "melody")} in this browser`.replace("melodys", "melodies");
  $("melAfter").textContent = on.length > MAX_THEMES ? `${MAX_THEMES} at most` : !on.length ? "" :
    `${sectorsFor(bankImage(on).data.length) * 4} KB` + (fmx ? "" : dev ? " · this FM-1 has no sample pool" : " · and in the emulator");
}
let melVoice = null;
function melPlay(m) {
  if (melVoice) { melVoice.close(); melVoice = null; return; }
  const ac = new AudioContext(), q = 60 / 140 / 4, t0 = ac.currentTime + 0.05, out = ac.createGain();
  out.gain.value = 0.15;
  out.connect(ac.destination);
  for (const e of m.ev) {
    const o = ac.createOscillator(), g = ac.createGain(), t = t0 + e.at * q, d = e.len * q;
    o.type = "triangle";
    o.frequency.value = 440 * 2 ** ((e.note - 69) / 12);
    g.gain.setValueAtTime(e.vel / 127, t);
    g.gain.exponentialRampToValueAtTime(0.01, t + Math.max(0.08, d));
    o.connect(g).connect(out);
    o.start(t);
    o.stop(t + Math.max(0.08, d) + 0.02);
  }
  melVoice = ac;
  setTimeout(() => { if (melVoice === ac) { ac.close(); melVoice = null; } }, (STEPS * q + 1) * 1000);
}
$("melList").addEventListener("click", (e) => {
  const b = e.target.closest("[data-a]"), row = e.target.closest(".mel");
  if (!b || !row) return;
  const m = mels.find((x) => x.id === +row.dataset.m), a = b.dataset.a;
  if (a === "play") return melPlay(m);
  if (a === "del") mels = mels.filter((x) => x !== m);
  else if (a === "prev" || a === "next") { m.start = Math.max(0, Math.min(Math.max(0, m.song.bars - BARS), m.at + (a === "prev" ? -BARS : BARS))); melPick(m); }
  else return;
  melSave();
  renderMels();
});
$("melList").addEventListener("change", (e) => {
  const b = e.target.closest("[data-a]"), row = e.target.closest(".mel");
  if (!b || !row) return;
  const m = mels.find((x) => x.id === +row.dataset.m);
  if (b.dataset.a === "on") { m.on = b.checked; row.classList.toggle("off", !m.on); renderMelSend(); }
  else if (b.dataset.a === "name") { m.name = b.value.toUpperCase().replace(/[^\x20-\x7E]/g, "").slice(0, 8) || themeName(m.file); b.value = m.name; }
  else if (b.dataset.a === "part") { m.part = +b.value; m.start = -1; melPick(m); renderMels(); }
  melSave();
});
$("melOpen").addEventListener("click", () => $("melIn").click());
$("melIn").addEventListener("change", (e) => { addMidi([...e.target.files]); e.target.value = ""; });
$("melPack").addEventListener("click", addPack);
$("melAll").addEventListener("click", () => { mels.forEach((m) => { m.on = true; }); melSave(); renderMels(); });
$("melNone").addEventListener("click", () => { mels.forEach((m) => { m.on = false; }); melSave(); renderMels(); });
$("melSend").addEventListener("click", () => run($("melStatus"), async () => {
  const on = mels.filter((m) => m.on && m.ev.length), im = bankImage(on.map((m) => ({ name: m.name, key: m.key, ev: m.ev })));
  if (dev && dev.kind === "fmx") {                  // (one bank on the FM-1: the one there replaced, in its place if it fits)
    const old = dev.dir.entries.find((e) => e.mel), ns = sectorsFor(im.data.length);
    const id = old ? old.id : freeId(dev.dir), at = place(dev.dir, ns, old ? [old.id] : []);
    if (id < 0) throw new Error("The FM-1 holds as many sounds as it can (80). Remove one first.");
    if (at < 0) throw new Error(`Not enough room in one piece for the melodies (${ns * 4} KB). Remove a sound, or send fewer.`);
    await sendWrites([{ id, at, ns, im, type: "MELODY", name: "MELODIES" }], (t) => say($("melStatus"), t));
  }
  for (const r of library.filter((x) => x.type === "MELODY")) await libDel(r.crc);
  await libPut(im, { from: `${on.length} MIDI files` });
  await refreshLibrary();
  say($("melStatus"), dev && dev.kind === "fmx"
    ? `On the FM-1: ${plural(on.length, "melody")}. CHOP track, SEL to INST, MELODY to MIDI, THEME picks one.`.replace("melodys", "melodies")
    : `Kept in this browser, and in the emulator when it's on. Send them when the FM-1 is connected.`, "ok");
}));

// ---- the packs ---------------------------------------------------------------------------------------------------------------
const packs = new Map();                              // file name -> {hdr, data} (web/packs: tools/gen_packs.py)
async function loadPacks() {
  await Promise.all(PACKS.map(async ([f]) => {
    try {
      const r = await fetch(`packs/${f}.bin`);
      if (!r.ok) return;
      const img = new Uint8Array(await r.arrayBuffer()), len = new DataView(img.buffer).getUint32(16, true);
      packs.set(f, { hdr: img.slice(0, 512), data: img.slice(512, 512 + len) });
    } catch { /* not in this copy */ }
  }));
  renderPacks();
}
function renderPacks() {
  const fmx = dev && dev.kind === "fmx", crcs = new Set(fmx ? dev.dir.entries.map((e) => e.crc) : []);
  $("packs").innerHTML = PACKS.filter(([f]) => packs.has(f)).map(([f, label]) => {
    const im = packs.get(f), h = parseImage(im.hdr), on = crcs.has(h.crc);
    return `<div class="pack num"><b>${label}</b><span class="dim">${sectorsFor(im.data.length) * 4} KB · ${h.type.toLowerCase()}</span>` +
      (on ? `<span style="color: var(--go)">on the FM-1</span>` : `<button class="btn small" type="button" data-pack="${f}" data-pool${busy ? " disabled" : ""}>Install</button>`) + `</div>`;
  }).join("");
}
$("packs").addEventListener("click", (e) => {
  const b = e.target.closest("[data-pack]");
  if (!b) return;
  const im = packs.get(b.dataset.pack), h = parseImage(im.hdr);
  run($("setStatus"), async () => {
    const p = planSend(dev.dir, [{ type: h.type, name: h.name, im }]);
    if (p.fail) throw new Error(p.fail);
    await sendWrites(p.writes, (t) => say($("setStatus"), t));
    say($("setStatus"), `${h.name} is on the FM-1.`, "ok");
  });
});

// ---- the emulator ----------------------------------------------------------------------------------------------------------
const emu = new Emulator({ wrap: $("fm1wrap"), sprite: $("fm1"), screen: $("live"), on: $("onLayer"), go: $("switchOn"), status: $("emuStatus"),
  tipLine: $("tipLine") },
  { onState: (s) => {
    if (s === "on") { renderDoors(); if ($("mine").checked) loadMine(); }
    if (s === "nosave") $("mineNote").textContent = "(this browser doesn't keep the emulator's saves)";
  } });
$("songs").innerHTML = DEMO.map((s, i) => `<li><span>${i + 1}</span><span>${s.bpm}</span><span>${s.key}</span></li>`).join("");
async function loadMine() {
  if (!library.length) { $("mineNote").textContent = "(nothing in it yet)"; return; }
  const ids = await emu.loadSounds(library.map((r) => ({ hdr: r.hdr, data: r.data })));
  const miss = ids.filter((k) => k < 0).length;
  $("mineNote").textContent = miss ? `(${miss} didn't fit)` : `(${plural(library.length, "sound")} in it)`;
}
try { $("tips").checked = localStorage.getItem("fmx-tips") === "1"; } catch { /* off */ }
emu.setTips($("tips").checked);
$("tips").addEventListener("change", () => {
  emu.setTips($("tips").checked);
  try { localStorage.setItem("fmx-tips", $("tips").checked ? "1" : "0"); } catch { /* not kept */ }
});
try { $("mine").checked = localStorage.getItem("fmx-mine") !== "0"; } catch { $("mine").checked = true; }
$("mine").addEventListener("change", () => {
  try { localStorage.setItem("fmx-mine", $("mine").checked ? "1" : "0"); } catch { /* not kept */ }
  if ($("mine").checked && emu.on) loadMine();
});
$("demoAgain").addEventListener("click", async () => {
  const st = $("demoStatus");
  if (!emu.on) { st.textContent = "Switch it on first."; return; }
  st.textContent = "Putting it back…";
  try { await emu.demoSet(); st.textContent = "The demo set is in: song 1 loaded."; }
  catch (e) { st.textContent = e.message; }
});

// ---- the pictures: the hero, the logo, the songs' gems (only while they're in view) ---------------------------------------------
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
let heroSeen = true, setSeen = false;
new IntersectionObserver((es) => { for (const e of es) { if (e.target.id === "hero") heroSeen = e.isIntersecting; else setSeen = e.isIntersecting; } })
  .observe($("hero"));
new IntersectionObserver((es) => { setSeen = es[es.length - 1].isIntersecting; }).observe($("set"));
function frame(ms) {
  const t = ms / 1000;
  if (heroSeen || reduce) heroFrame($("hero"), t);
  gemIcon($("logoGem"), t * 1.4);
  if (setSeen || reduce) $("set").querySelectorAll("canvas").forEach((c, i) => gemIcon(c, t * 0.8 + i, hex(css(DEMO[i].col))));
  if (!reduce && !document.hidden) requestAnimationFrame(frame);
}
document.addEventListener("visibilitychange", () => { if (!document.hidden && !reduce) requestAnimationFrame(frame); });

// ---- nav: the section in view -------------------------------------------------------------------------------------------------
const links = [...document.querySelectorAll("nav.tabs a")];
new IntersectionObserver((es) => es.forEach((en) => {
  if (!en.isIntersecting) return;
  links.forEach((a) => a.toggleAttribute("aria-current", a.getAttribute("href") === "#" + en.target.id));
}), { rootMargin: "-40% 0px -55% 0px" }).observe($("top"));
document.querySelectorAll("section.panel").forEach((s) => new IntersectionObserver((es) => es.forEach((en) => {
  if (en.isIntersecting) links.forEach((a) => a.toggleAttribute("aria-current", a.getAttribute("href") === "#" + s.id));
}), { rootMargin: "-40% 0px -55% 0px" }).observe(s));

// ---- start ---------------------------------------------------------------------------------------------------------------------
$("version").textContent = META ? `FMX ${META.version} (${META.product})` : "a development copy";
renderAll();
refreshLibrary().then(renderAll);
loadPacks();
loadMels();
Promise.all([document.fonts.load("16px Terminus"), document.fonts.load("bold 32px 'Terminus Big'"), document.fonts.load("12px 'Terminus Small'")])
  .catch(() => {}).then(() => { emu.redraw(); requestAnimationFrame(frame); });
