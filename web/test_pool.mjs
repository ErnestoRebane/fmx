// SPDX-License-Identifier: GPL-3.0-only
// web/pool.js (the FMX sample pool from the browser) against its mock FM-1: the directory read in pages of 16, the
// places (best fit, inside one region, the entries to replace counted free), the ids, an upload and an erase; the old
// slots' ids. The companion app sending into it: web/test_app.mjs.
//   node web/test_pool.mjs
import * as B from "./breaks.js";
import * as P from "./pool.js";

let bad = 0;
const ok = (cond, what) => { console.log(`  ${cond ? "ok" : "FAIL"} ${what}`); if (!cond) bad++; };
const { makePoolMock, readDir, place, freeId, send, erase, sectorsFor, freeRuns, largestRun, SECT } = P;

// a Link over a WebMIDI access (as web/app_device.js has it)
async function linkOf(access) {
  const input = [...access.inputs.values()][0], output = [...access.outputs.values()][0];
  let wait = null;
  input.onmidimessage = (ev) => { const d = ev.data; if (wait && d[4] === wait.cmd) { const w = wait; wait = null; w.resolve([...d.slice(5, d.length - 1)]); } };
  const req = (cmd, args, ms = 3000) => new Promise((resolve, reject) => {
    wait = { cmd, resolve };
    setTimeout(() => { if (wait && wait.cmd === cmd) { wait = null; reject(new Error("no answer")); } }, ms);
    output.send([0xF0, 0x7D, 0x46, 0x4C, cmd, ...args, 0xF7]);
  });
  return { req, retry: async (cmd, args, ms, tries = 3) => { for (let k = 1; ; k++) { try { return await req(cmd, args, ms); } catch (e) { if (k >= tries) throw e; } } } };
}
const img = (name, nz, n, fill = 0x33) => {                 // an image: nz zones of n samples, a kit from key 36
  const hdr = new Uint8Array(480), v = new DataView(hdr.buffer), per = (n + 1) >> 1, data = new Uint8Array(per * nz).fill(fill);
  v.setUint32(0, 0x504D5346, true); v.setUint16(4, 1, true); hdr[6] = nz;
  for (let i = 0; i < name.length; i++) hdr[8 + i] = name.charCodeAt(i);
  v.setUint32(16, data.length, true); v.setUint32(20, B.crc32(data), true);
  for (let i = 0; i < nz; i++) {
    const o = 32 + i * 28;
    v.setUint32(o, i * per, true); v.setUint32(o + 4, n, true); v.setUint32(o + 12, n - 1, true); v.setUint32(o + 16, 32768, true);
    v.setInt16(o + 20, (36 + i) * 16, true); hdr[o + 25] = hdr[o + 26] = 36 + i;
  }
  return { hdr, data };
};

console.log("pool.js against the pool's mock FM-1");
{
  const mock = makePoolMock(), link = await linkOf(mock.access);
  let dir = await readDir(link);
  ok(dir && dir.nsect === 80 && dir.nids === 80 && dir.regions.length === 2 && dir.entries.length === 0 && dir.zonesLeft === 384,
    "an empty pool: 80 sectors in two regions (60 + 20), 80 ids, no entry, 384 zones left");
  ok(freeRuns(dir).map((r) => `${r.first}+${r.n}`).join() === "0+60,60+20", "... its free runs: the two regions");
  for (let k = 0; k < 30; k++) await send(link, k, k * 2, img(`S${k}`, 2, 1000 + k));
  dir = await readDir(link);
  ok(dir.entries.length === 30 && dir.entries.every((e, k) => e.id === k && e.sect === k * 2 && e.nsect === 1 && e.nz === 2 && e.name === `S${k}` && e.kit),
    "30 sounds sent (more than 16: the directory in two pages), each its id, place, name; kits");
  ok(dir.entries[7].dataLen === ((1007 + 1) >> 1) * 2, "... its data length (2 zones of 1007 samples)");
  ok(dir.entries[0].crc === new DataView(img("S0", 2, 1000).hdr.buffer).getUint32(20, true) && dir.entries[0].hz === 22050, "... its CRC, its rate");
  ok(place(dir, 1) === 1 && place(dir, 2) === 60 && place(dir, 21) === -1 && place(dir, 20) === 60, "a place: the smallest free piece that takes it (1 sector: 1; 2: 60; 20: 60; 21: none)");
  ok(place(dir, 3, [5]) === 9, "... an entry to replace counts free (id 5 at 10: 9..11)");
  ok(freeId(dir) === 30 && freeId(dir, 40) === 41 && freeId(dir, -1, [30, 31]) === 32, "an id: the lowest free one (after one given, not taken)");
  ok(largestRun(dir) === 20, "the largest free piece: 20 sectors (0xE7000..)");
  let threw = "";
  try { await send(link, 40, 2, img("X", 1, 100)); } catch (e) { threw = e.message; }
  ok(/taken/.test(threw), "a place over another sound: refused (\"" + threw + "\")");
  await erase(link, 3);
  dir = await readDir(link);
  ok(dir.entries.length === 29 && !dir.entries.some((e) => e.id === 3) && place(dir, 1) === 1, "an erase: id 3 gone");
  const big = img("BIG", 1, 2 * (19 * SECT - 512));
  ok(sectorsFor(big.data.length) === 19, "a sound of 19 sectors");
  await send(link, 50, place(dir, 19), big);
  dir = await readDir(link);
  ok(dir.entries.some((e) => e.id === 50 && e.sect === 60 && e.nsect === 19), "... at 0xE7000 (the only piece that takes it)");
  const mk = makePoolMock({ noPool: true }), l2 = await linkOf(mk.access);
  ok(await readDir(l2) === null, "a firmware with no pool: no directory (null)");
}
{
  const mock = makePoolMock();
  mock.put(20, img("OLD2", 1, 100));                  // USR2's place, rsv 0
  mock.put(60, img("OLD4", 1, 100));                  // USR4's
  mock.put(7, img("STRAY", 1, 100));
  mock.put(30, img("NEW", 1, 100), 1);                // id 0 by its header
  const e = mock.entries();
  ok(e.map((x) => `${x.id}:${x.name}`).join() === "0:NEW,1:OLD2,2:STRAY,3:OLD4", "old slots (rsv 0): USR2 / USR4's places ids 1, 3; elsewhere the first free; rsv: its own (as eng_sample.c)");
}

console.log(bad ? "POOL WEB FAILED" : "pool web: the directory, places, ids, uploads; the companion app's uploads: web/test_app.mjs");
process.exit(bad ? 1 : 0);
