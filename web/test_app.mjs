// SPDX-License-Identifier: GPL-3.0-only
// The companion app's sounds and its FM-1 (web/sounds.js, web/app_device.js) against the pool's mock FM-1 (web/pool.js
// makePoolMock, made to answer the update protocol's identity as FMX does): a file told apart (a BREAK loop, a LOOP, a
// KIT hit, an INSTRUMENT note), the entries built at their quality (each zone its rate), planned into the pool (BR..
// after the BREAK entries there, ids above theirs), sent, read back, and the set backed up and restored.
//   node web/test_app.mjs
import { makePoolMock, sectorsFor, freeSectors } from "./pool.js";
import { pack7 as otaPack7 } from "./fm1ota.js";
import { NOKEY } from "./slice.js";
import { makeCard, cardBuild, entriesOf, planSend, parseImage, KHZ, newKit, kitBuild, moveCut, dropCut, addCut, resetCuts } from "./sounds.js";
import { Device } from "./app_device.js";

let bad = 0;
const ok = (c, what) => { console.log(`  ${c ? "ok" : "FAIL"} ${what}`); if (!c) bad++; };

// ---- signals ----
const R = 44100, beat = 60 / 170;
let seed = 7;
const rnd = () => { seed = (seed * 1103515245 + 12345) >>> 0; return seed / 4294967296 - 0.5; };
const perc = (bars) => Float64Array.from({ length: Math.round(R * beat * 4 * bars) }, (_, i) => {   // a kick and a hat a beat
  const t = (i % Math.round(R * beat / 2)) / R, kick = (i % Math.round(R * beat)) / R;
  return 0.8 * Math.exp(-kick * 30) * Math.sin(2 * Math.PI * 55 * kick) + 0.4 * Math.exp(-t * 60) * rnd();
});
const notes = (bars, mids) => Float64Array.from({ length: Math.round(R * beat * 4 * bars) }, (_, i) => {   // a note a beat
  const k = Math.floor(i / (R * beat)), t = (i % Math.round(R * beat)) / R, f = 440 * 2 ** ((mids[k % mids.length] - 69) / 12);
  return 0.5 * Math.exp(-t * 2) * (Math.sin(2 * Math.PI * f * t) + 0.3 * Math.sin(4 * Math.PI * f * t));
});
const tone = (sec, f) => Float64Array.from({ length: Math.round(R * sec) }, (_, i) => 0.6 * Math.sin(2 * Math.PI * f * i / R) * Math.min(1, (R * sec - i) / 2000));
const hit = (sec) => Float64Array.from({ length: Math.round(R * sec) }, (_, i) => Math.exp(-i / R * 25) * rnd());

console.log("a file told apart, built at its quality");
const brk = makeCard("amen_170.wav", perc(2), R), brk2 = makeCard("think.wav", perc(1), R);
const loop = makeCard("keys_Am.wav", notes(2, [57, 60, 64, 69, 57, 60, 64, 67]), R);
const inst = makeCard("bass_A2.wav", tone(0.8, 110), R), kit1 = makeCard("kick.wav", hit(0.3), R), kit2 = makeCard("snare.wav", hit(0.2), R);
ok(brk.type === "BREAK" && brk2.type === "BREAK", `a drum loop is a BREAK (${brk.type}, ${brk2.type})`);
ok(loop.type === "LOOP" && loop.key !== NOKEY, `a loop of notes is a LOOP with a key (${loop.type}, key ${loop.key})`);
ok(inst.type === "INSTRUMENT" && inst.root === 45, `a short tone is an INSTRUMENT on its note (${inst.type}, ${inst.root})`);
ok(kit1.type === "KIT" && kit2.type === "KIT", `a short noise hit is a KIT hit (${kit1.type}, ${kit2.type})`);
{
  const b16 = cardBuild(brk);
  brk.khz = 22;
  const b22 = cardBuild(brk);
  ok(b16.zones.length === 1 && b16.zones[0].hz === 16000 && b22.zones[0].hz === 22050 && Math.abs(b22.zones[0].s.length / b16.zones[0].s.length - 22050 / 16000) < 0.01,
    "a BREAK: its first bar, 16 kHz by default; at 22 kHz the same bar in more samples");
  ok(Math.abs(b22.zones[0].s.length / 22050 - beat * 4) < 0.02, `... one bar at 170 (${(b22.zones[0].s.length / 22050).toFixed(3)} s)`);
  brk.khz = 16;
}
ok(cardBuild(loop).zones.length === 8 && cardBuild(loop).meta && cardBuild(loop).meta.length === 32, "a LOOP of 2 bars: 8 slices, a beat each, and its SLC1 meta");

console.log("the entries");
const ents = entriesOf([brk, kit1, loop, brk2, inst, kit2]);
ok(ents.map((e) => e.type).join() === "BREAK,KIT,LOOP,INSTRUMENT", `two breaks in one entry, two hits in one kit, a loop, an instrument (${ents.map((e) => e.type)})`);
{
  const [B, K, L, I] = ents.map((e) => parseImage(e.im.hdr));
  ok(B.nz === 2 && B.zones.map((z) => `${z.root}:${z.lo}-${z.hi}`).join() === "36:0-36,37:37-127" && B.zones.every((z) => z.hz === 16000),
    "the BREAK entry: roots 36, 37 split over the keys, at 16 kHz");
  ok(K.nz === 2 && K.zones.every((z, i) => z.root === 53 + i && z.lo === 53 + i && z.hi === 53 + i) && K.type === "KIT", "the kit: a hit a key from F3 (53)");
  ok(L.type === "LOOP" && L.nz === 8 && L.zones.every((z, i) => z.lo === 36 + i && z.hi === 36 + i) && L.zones[0].hz === KHZ[loop.khz], "the LOOP: a slice a key from 36, SLC1 at 480");
  ok(I.type === "INSTRUMENT" && I.nz === 1 && I.zones[0].lo === 0 && I.zones[0].hi === 127 && I.zones[0].root === 45, "the instrument: one zone over every key, its root A2");
}

console.log("a LOOP's lines, by hand");
{
  const c = makeCard("keys2.wav", notes(1, [57, 60, 64, 69]), R);
  c.type = "LOOP";
  const at = cardBuild(c), cuts0 = c.cuts.slice();
  ok(at.zones.length === 4 && cuts0[0] === 0 && !c.edited, `1 bar: 4 lines on the hits (${cuts0})`);
  moveCut(c, 1, cuts0[1] + 50, 200);
  ok(c.edited && c.a.hits.some((h) => h.t === c.cuts[1]), `a line moved lands on a hit near it (${c.cuts[1]})`);
  moveCut(c, 1, 1e9);
  ok(c.cuts[1] < c.cuts[2], "... and stays before the next line");
  dropCut(c, 2);
  const b3 = cardBuild(c), m = parseImage(entriesOf([c])[0].im.hdr);
  ok(b3.zones.length === 3 && m.nz === 3 && entriesOf([c])[0].im.hdr[480 + 6] === 3, "a line taken away: 3 slices, the meta's n 3");
  dropCut(c, 0);
  ok(c.cuts.length === 3 && c.cuts[0] === 0, "the first line (the loop's start) stays");
  const k = addCut(c, c.span.len / 2, 0);
  ok(k > 0 && c.cuts.length === 4 && c.cuts.every((q, i) => !i || q > c.cuts[i - 1]), `a line added in order (${c.cuts})`);
  ok(addCut(c, c.cuts[k] + 10, 0) < 0, "... not on top of another");
  resetCuts(c);
  ok(c.cuts.join() === cuts0.join() && !c.edited, "the lines on the hits again");
  c.bars = 2;
  cardBuild(c);
  ok(c.cuts.length === 8, "other bars: the lines placed again (8)");
}

console.log("a kit's grid");
{
  const k = newKit("MYKIT");
  k.pads[0] = makeCard("k.wav", hit(0.3), R);
  k.pads[2] = makeCard("s.wav", hit(0.2), R);
  k.pads[5] = makeCard("long.wav", perc(1), R, "KIT");
  const b = kitBuild(k), [e] = entriesOf([], [k]), h = parseImage(e.im.hdr);
  ok(k.pads[5].type === "KIT" && b.hits === 3 && !b.fail, "a loop dropped on a pad is a hit there");
  ok(e.name === "MYKIT" && h.type === "KIT" && h.nz === 6 && h.zones.every((z, i) => z.root === 53 + i && z.lo === 53 + i && z.hi === 53 + i),
    "the kit: pads 1..6 on F3..A#3, its own name");
  ok(h.zones[1].n === 32 && h.zones[3].n === 32 && h.zones[0].n > 1000, "... an empty pad a moment of silence on its key");
  k.khz = 11;
  ok(parseImage(entriesOf([], [k])[0].im.hdr).zones.every((z) => z.hz === 11025), "the kit's quality for every pad");
  for (let i = 0; i < 16; i++) k.pads[i] = makeCard("l.wav", perc(1), R, "KIT");
  k.khz = 22;
  ok(!!kitBuild(k).fail, `16 long hits at 22 kHz: too long for one sound (${kitBuild(k).fail})`);
}

console.log("the FM-1 (the pool's mock, answering as FMX)");
const mock = makePoolMock({ name: "FMX" });
{   // the update protocol's identity, as FMX answers it (web/fm1ota.js parseIdentity), and ports that open
  const out = mock.access.outputs.get("mock-out"), inp = mock.access.inputs.get("mock-in"), send = out.send.bind(out);
  out.open = inp.open = async () => {};
  out.send = (d) => {
    if (d[0] === 0xF0 && d[1] === 0x00 && d[2] === 0x32) {
      const body = [0, 0x59, 0x11, 0, 0, 0, ...[..."FM-1_900".padEnd(27, "\0")].map((c) => c.charCodeAt(0)), 0];
      setTimeout(() => inp.onmidimessage && inp.onmidimessage({ data: Uint8Array.from([0xF0, ...otaPack7(body), 0xF7]) }), 0);
    } else send(d);
  };
}
{   // already there: a kit, and a BREAK entry of the old page (id 5)
  const pre = entriesOf([makeCard("old.wav", perc(1), R)])[0];
  mock.put(0, entriesOf([makeCard("hat.wav", hit(0.2), R)])[0].im, 1);
  const name = new TextEncoder().encode("BR1");
  const hdr = pre.im.hdr.slice(0, 480);
  hdr.fill(0, 8, 16); hdr.set(name, 8);
  mock.put(10, { hdr, data: pre.im.data }, 6);
}
const dev = await Device.connect(mock.access);
ok(dev && dev.kind === "fmx" && dev.version === "FMX MOCK" && dev.id.text === "FM-1_900", `connect: FMX, its version, its identity (${dev && dev.kind} ${dev && dev.version})`);
ok(dev.dir.entries.length === 2 && dev.dir.entries.find((e) => e.id === 5).loops, "the pool read: a kit and BR1 (id 5)");
const p = planSend(dev.dir, ents);
ok(!p.fail && p.writes.length === 4, `the plan: four entries placed (${p.fail || p.left + " blocks left"})`);
ok(p.writes[0].name === "BR2" && p.writes[0].id > 5, `the new BREAK entry is BR2, its id above BR1's (${p.writes[0].id})`);
for (const w of p.writes) {
  const slc = new DataView(w.im.hdr.buffer, w.im.hdr.byteOffset).getUint32(480, true) === 0x31434C53;
  await dev.send(w.id, w.at, { hdr: w.im.hdr.subarray(0, slc ? 512 : 480), data: w.im.data });
}
await dev.refresh();
const names = dev.dir.entries.map((e) => e.name).sort().join();
ok(dev.dir.entries.length === 6 && names === ["BR1", "BR2", ...ents.slice(1).map((e) => e.name)].concat([dev.dir.entries.find((e) => e.id === 0).name]).sort().join(),
  `sent: six sounds in the pool (${names})`);
ok(p.writes.every((w) => { const e = dev.dir.entries.find((x) => x.id === w.id); return e && e.crc === parseImage(w.im.hdr).crc && e.nsect === sectorsFor(w.im.data.length); }),
  "each as planned: its id, its CRC, its blocks");
ok(dev.dir.entries.find((e) => e.name === ents[2].name).slice && dev.dir.entries.find((e) => e.name === "BR2").loops, "the directory says which: the LOOP's slices, BR2 loops");
ok(Math.abs(dev.dir.entries.find((e) => e.id === p.writes[0].id).hz - 16000) <= 1, "the directory's rate: BR2 at 16 kHz (to the Hz: Q16 of 44.1 kHz)");
{
  const w = p.writes[3], e = dev.dir.entries.find((x) => x.id === w.id), back = await dev.readEntry(e);
  ok(Buffer.compare(Buffer.from(back.data), Buffer.from(w.im.data)) === 0 && parseImage(back.hdr).name === w.name, "read back: the INSTRUMENT, byte for byte");
}
const songs = await dev.songs();
ok(songs.length === 4 && songs.every((s) => !s.used), "the set: four song slots, empty on the mock");
{
  const full = { ...dev.dir, entries: [...dev.dir.entries, { id: 70, sect: 0, nsect: 0 }] };
  for (let s = 0, k = 71; s < 80; s++) full.entries.push({ id: k++ % 80, sect: s, nsect: 1 });
  ok(/Not enough room|as many sounds/.test(planSend(full, ents).fail || ""), "a full pool: the plan says why it can't");
}

console.log("the set backed up and restored");
const before = dev.dir.entries.map((e) => `${e.id}:${e.name}:${e.crc}`).join();
const file = await dev.backup();
ok(file.format === "sloop-backup" && file.pool.length === 6 && file.objects.some((o) => o.id === 0), `the backup: the editor's objects and the six sounds (${file.pool.length})`);
await dev.erase(p.writes[1].id);
await dev.erase(p.writes[2].id);
await dev.refresh();
ok(dev.dir.entries.length === 4, "two sounds erased");
await dev.restore(JSON.parse(JSON.stringify(file)));
ok(dev.dir.entries.map((e) => `${e.id}:${e.name}:${e.crc}`).join() === before && mock.puts.includes(0) && mock.puts.includes(1),
  "restored: the same sounds under the same ids, the working project and the settings written");
ok(freeSectors(dev.dir) === 80 - dev.dir.entries.reduce((a, e) => a + e.nsect, 0), "... and the pool's room adds up");
dev.close();

console.log(bad ? "APP FAILED" : "app: files told apart, entries at their rates, planned, sent, read back, the set backed up and restored");
process.exit(bad ? 1 : 0);
