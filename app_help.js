// SPDX-License-Identifier: GPL-3.0-only
// What each control of the emulator's FM-1 does right now (web/app.html "Show what the controls do"): the firmware's
// screen as the emulator reports it (web/emu/sloop_web.c web_ui: the screen as numbers, its title, the four knobs'
// labels and the hint line it drew) -> a label a control, after the maps of the firmware's screens (ui_live.c,
// ui_input.c layer_tap, ui_break.c, ui_bass.c, ui_chord.c, ui_chop.c, ui_song.c, ui_menu.c, ui_layers.c) and
// docs/manual. A label's first line is what a tap does; a second line, smaller, says how (held) or what to. No label:
// the control does nothing here.
import { WHITE, BLACK, keyName } from "./app_draw.js";

const FIELDS = ["menu", "live", "fx", "show", "layer", "home", "scope", "fam", "sel", "brk", "cpView", "cpPage", "sgPage",
  "bsPage", "chPage", "bkPage", "rec", "playing", "auto", "q", "fresh"];
const LY = { FX: 1, ERASE: 2, ROLL: 3, STEP: 4, SCALE: 5, MIX: 6 };           // seq.c LY_*
const SC = { TRK: 4, SONG: 5, BREAK: 6, CHORD: 7, CHOP: 8, BASS: 9 };          // params.c SC_*
const LANES = ["BREAK", "BASS", "CHORDS", "CHOP"];                            // LIVE's and GLO's lanes, top down
const TRACK = ["BASS", "CHORDS", "CHOP"];                                     // song.sel 0..2; the BREAK: brk
const SECTIONS = ["INTRO", "BUILD", "DROP", "ROLL", "BREAK\ndown", "DROP 2", "OUTRO"];
const FX_KEYS = ["DUB\nverb", "ECHO", "SPARKLE", "VOID", "FREEZE", "SHARDS", "TAPE", "REVERSE", "DUB\nfilter", "PSX\ncrush",
  "VOWEL", "RESO-\nnator"];
const CHOP_TABS = ["PADS", "SLICE", "INST"];
const SONG_PAGES = ["SET", "CHAIN", "CARD", "TRACKS"];

// web_ui's text -> the state ({menu, live, ...: numbers, title, knobs: [4 labels], hint})
export function parseUi(text) {
  const [nums = "", title = "", k1 = "", k2 = "", k3 = "", k4 = "", hint = ""] = (text || "").split("\n");
  const st = { title, knobs: [k1, k2, k3, k4], hint };
  nums.split(" ").forEach((v, i) => { if (FIELDS[i]) st[FIELDS[i]] = +v || 0; });
  return st;
}
// the state before the switch-on: LIVE, stopped, AUTO, BASS in focus (as the FM-1 boots)
export const BOOT = parseUi("0 1 0 0 0 0 4 10 0 0 0 0 0 0 0 0 0 0 1 0 0\nTRACKS\nfilter\nenergy\nspace\nlevel\n");

// the state -> {btn: {FX: text ..}, knob: {SELECT: text ..}, key: [27 texts or ""], where: the screen's name}
export function controlHelp(st) {
  const btn = {}, knob = { MASTER: "VOLUME" }, key = Array(27).fill("");
  const white = (fn) => WHITE.forEach((k, i) => { key[k] = fn(i, k) || ""; });
  const black = (fn) => BLACK.forEach((k, j) => { key[k] = fn(j, k) || ""; });
  const knobs = (fallback) => st.knobs.forEach((l, i) => {
    knob[`KNOB ${i + 1}`] = (st.fresh && l ? l.toUpperCase() : fallback?.[i]) || "";
  });
  const note = (k) => "♪ " + keyName(k);
  const slice = (k) => {                                     // brkpart.c brk_slice_of_key: the white key's (a black: the one before)
    const W = [0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6];
    let i = W[k % 12];
    if (i < 0) i = W[(k - 1) % 12];
    return `SLICE ${(Math.floor(k / 12) * 7 + i) % 8 + 1}`;
  };
  const focus = st.brk ? "BREAK" : TRACK[st.sel] || "";
  btn.PLAY = st.playing ? "STOP" : st.live ? "PLAY\nthe song" : "PLAY\nthe loop";

  if (st.menu) {                                             // ui_menu.c
    Object.assign(btn, { HOME: "hold:\nclose menu", "OCT+": "OPEN\nor step", "OCT-": "CLOSE" });
    Object.assign(knob, { SELECT: "SECTION", PRESETS: "CURSOR", "KNOB 1": "ROW 1", "KNOB 2": "ROW 2", "KNOB 3": "ROW 3", "KNOB 4": "ROW 4" });
    return { btn, knob, key, where: "MENU" };
  }
  if (st.live && st.fx || !st.live && st.layer === LY.FX) {  // the FX panel (lfx.c), held or locked open
    white((i) => FX_KEYS[i]);
    black((j) => j < 4 ? `TO\n${LANES[j]}` : j === 4 ? "LATCH" : j === 5 ? "CLEAR" : "");
    Object.assign(btn, { FX: "let go:\ncloses", HOME: "LOCK\nopen" });
    knob.SELECT = "TEMPO";
    knobs(["AMOUNT", "TIME", "TONE", "FDBK"]);
    return { btn, knob, key, where: "FX PANEL" };
  }
  if (st.live) {                                             // ui_live.c: one map, whatever the track
    white((i) => i < 7 ? SECTIONS[i] : i === 7 ? "NEXT\nsong way" : i < 12 ? `${LANES[i - 8]}\nin / out` :
      ["GAP", "WASH", "ROLL", "REWIND"][i - 12] + "\nhold");
    black((j, k) => st.brk ? (j < 4 ? `LOOP ${"ABCD"[j]}\nhold` : `SLICE ${j % 8 + 1}`) :
      st.sel === 2 && j < 3 ? CHOP_TABS[j] : note(k));
    const q = ["NOW", "BAR", "4 BARS"][st.q] || "";
    Object.assign(btn, {
      FX: "FX PANEL\nhold", SEL: `${focus}\nscreen`, ENV: st.show ? "SHOW\noff" : "SHOW", EDIT: "CLEAR\nwaiting",
      GLO: `Q ${q}\nnext`, HOME: "STUDIO\nhold: menu", SAVE: "SONGS", SEQ: st.auto ? "AUTO\n→ MAN" : "MAN\n→ AUTO",
      "OCT-": st.playing ? "SONG −\nnext" : "SONG −\nload", "OCT+": st.playing ? "SONG +\nnext" : "SONG +\nload",
    });
    Object.assign(knob, { SELECT: "TEMPO", ALGORITHM: "FOCUS\ntrack", PRESETS: st.brk ? "LOOP A" : `${focus}\nsound` });
    knobs(["FILTER", "ENERGY", "SPACE", "LEVEL"]);
    return { btn, knob, key, where: st.show ? "LIVE · SHOW" : "LIVE" };
  }

  // STUDIO: what holds on every screen (ui_input.c), then the screen's own
  const deck = st.scope === SC.TRK;
  Object.assign(btn, {
    HOME: deck ? "LIVE\nhold: menu" : "BACK\nhold: menu", SAVE: "SONG\nscreen", REC: "REC\nhold: clear",
    FX: "PAGES\nhold: FX panel", ENV: "ENV\npages", LFO: "LFO\npages", EDIT: "PAGES\nhold: erase",
    ARP: "PAGES\nhold: roll", SEQ: "PAGES\nhold: steps", GLO: "GLOBAL\nhold: mix", SEL: `${focus}\nscreen`,
    "OCT-": st.brk ? "GHOST\nhold" : "OCT −", "OCT+": st.brk ? "HARD\nhold" : "OCT +",
  });
  if (st.brk) Object.assign(btn, { SEQ: "STYLE", ARP: "RETRIG", EDIT: "LOOPS", SEL: "PHRASE" });
  Object.assign(knob, { SELECT: "TEMPO", ALGORITHM: "TRACK", PRESETS: st.brk ? "LOOP A" : `${focus}\nsound` });
  if (!deck) knob.PRESETS = "";
  const keysPlay = () => { white((i, k) => st.brk ? slice(k) : note(k)); black((j, k) => st.brk ? slice(k) : note(k)); };
  keysPlay();
  knobs();
  let where = st.title || "STUDIO";

  if (st.layer && st.layer !== LY.FX) {                      // a layer: its button held (or locked open: HOME)
    for (const b of ["FX", "ENV", "LFO", "EDIT", "ARP", "SEQ", "GLO", "SEL", "SAVE", "REC", "OCT-", "OCT+"]) btn[b] = "";
    btn.HOME = "LOCK\nopen";
    btn[{ 2: "EDIT", 3: "ARP", 4: "SEQ", 5: "SEL", 6: "GLO" }[st.layer]] = "HELD\nlet go: back";
    knob.ALGORITHM = knob.PRESETS = "";
    if (st.layer === LY.ERASE) {
      white(() => "ERASE"); black(() => "ERASE");
      Object.assign(btn, { "OCT-": "UNDO", "OCT+": "REDO" });
      where = "EDIT HELD · ERASE";
    } else if (st.layer === LY.ROLL) {
      white(() => "ROLL"); black(() => "ROLL");
      where = "ARP HELD · ROLL";
    } else if (st.layer === LY.STEP) {
      white((i) => `STEP ${i + 1}`); black(() => "");
      if (/OCT- CLEAR/.test(st.hint)) Object.assign(btn, { "OCT-": "CLEAR\nthe step", "OCT+": "FILL\nonly / not" });
      else if (/OCT: PAGE/.test(st.hint)) Object.assign(btn, { "OCT-": "STEPS\n◀ page", "OCT+": "STEPS\npage ▶" });
      where = "SEQ HELD · STEPS";
    } else if (st.layer === LY.SCALE) {
      white((i, k) => `KEY\n${keyName(k).replace(/\d/, "")}`); black((j, k) => `KEY\n${keyName(k).replace(/\d/, "")}`);
      where = "SEL HELD · THE SONG'S KEY";
    } else if (st.layer === LY.MIX) {
      white((i) => i < 4 ? `${LANES[i]}\nin / out` : i < 8 ? `SOLO\n${LANES[i - 4]}` : i === 8 ? "FILL\nhold" :
        i === 9 ? "FILL\nnext bar" : i === 15 ? "TAP\ntempo" : "");
      black(() => "");
      where = "GLO HELD · MIX";
    }
    return { btn, knob, key, where };
  }

  switch (st.scope) {
    case SC.TRK:                                             // the DECK (ui_deck.c)
      where = "STUDIO · DECK";
      break;
    case SC.BREAK:                                           // ui_break.c
      white((i) => `STEP ${i + 1}`);
      black((j) => j < 8 ? `SLICE ${j + 1}\nhold` : ["REVERSE", "HALF", "STUTTER"][j - 8] + "\nhold");
      Object.assign(btn, { SEQ: "STYLE\n/ groove", ARP: "RETRIG", EDIT: "LOOPS\n/ bars / layer", SEL: "PHRASE", GLO: "MIX",
        ENV: "TUNE", LFO: "PATTERN", "OCT-": "MUTE\nbreak", "OCT+": "NEXT\n16 steps" });
      Object.assign(knob, { SELECT: "PAGE", PRESETS: "LOOP A", ALGORITHM: "TRACK" });
      where = "BREAK";
      break;
    case SC.BASS:                                            // ui_bass.c
      black((j, k) => ["NEW\nphrase", "ANSWER", "BACK\nas written"][j] || note(k));
      Object.assign(btn, { SEL: st.bsPage ? "PHRASE\npage" : "SHAPE\npage" });
      Object.assign(knob, { SELECT: "PAGE", PRESETS: "BASS\nsound" });
      where = `BASS · ${st.bsPage ? "SHAPE" : "PHRASE"}`;
      break;
    case SC.CHORD:                                           // ui_chord.c
      white((i) => `STEP ${i + 1}`);
      black((j) => j < 8 ? `CHORD ${j + 1}\nhold` : "");
      Object.assign(btn, { SEL: st.chPage ? "GEN\npage" : "PLAY\npage" });
      Object.assign(knob, { SELECT: "PAGE", PRESETS: "CHORDS\nsound" });
      where = `CHORDS · ${st.chPage ? "PLAY" : "GEN"}`;
      break;
    case SC.CHOP: {                                          // ui_chop.c: the tab seen
      const tab = CHOP_TABS[st.cpView] || "";
      white((i, k) => tab === "PADS" ? `PAD ${i + 1}` : tab === "SLICE" ? `SLICE ${i + 1}` : `NOTE ${i + 1}`);
      black((j) => ["STUTTER\nhold", "GAP\nhold", "DOWN\nhold", tab === "INST" ? "NEW\ntheme" : ""][j] || "");
      Object.assign(btn, { "OCT-": "◀ TAB", "OCT+": "TAB ▶", SEL: `PLAY\n${tab}` });
      Object.assign(knob, { SELECT: "PAGE\nsteps · time", PRESETS: `${tab}\nsound` });
      where = `CHOP · ${tab}` + (st.cpPage ? ` · ${["", "STEPS", "TIME"][st.cpPage]}` : "");
      break;
    }
    case SC.SONG:                                            // ui_song.c
      Object.assign(btn, { SAVE: "SET\npage" });
      Object.assign(knob, { SELECT: "PAGE", ALGORITHM: st.sgPage ? "SECTION" : "" });
      where = `SONG · ${SONG_PAGES[st.sgPage] || ""}`;
      break;
    default: {                                               // a sound page: its button again (or SELECT) the next
      const fam = { 1: "ENV", 2: "LFO", 3: "FX", 4: "SEL", 5: "EDIT", 6: "GLO", 8: "ARP", 9: "SEQ" }[st.fam];
      if (fam) btn[fam] = "NEXT\npage";
      knob.SELECT = "PAGE";
    }
  }
  if (st.rec) where += " · REC";
  return { btn, knob, key, where };
}
