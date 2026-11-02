// SPDX-License-Identifier: GPL-3.0-only
// web/breaks.js against tools/upload_breaks.py (the same WAV files give the same slot images, byte for byte, whole and cut
// to one bar, at 44.1 kHz stereo 16-bit and 48 kHz mono 24-bit). The companion app's side of the break loops (entries
// named BR..., sent into the pool after the ones there) is web/test_app.mjs.
//   node web/test_breaks.mjs
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import * as B from "./breaks.js";

const { readWav, cut, pack, userSlot, NSLOTS, imaDecode, parseSlot } = B;
const HERE = fileURLToPath(new URL(".", import.meta.url));
const dir = mkdtempSync(join(tmpdir(), "sloop-breaks-"));
let bad = 0;
const ok = (cond, what) => { console.log(`  ${cond ? "ok" : "FAIL"} ${what}`); if (!cond) bad++; };

function wavBuf(rate, nch, bits, seconds, seed) {
  const n = Math.round(rate * seconds), bps = bits >> 3, data = Buffer.alloc(n * nch * bps);
  let r = seed;
  for (let i = 0; i < n; i++) {
    r = (r * 1103515245 + 12345) >>> 0;
    const beat = (i % Math.round(rate / 2)) < rate * 0.03 ? 0.8 : 0.0;   // a click on every 8th at 120
    const v = beat * Math.sin(i * 0.3) + 0.1 * ((r >>> 8) / 16777216 - 0.5) + 0.2 * Math.sin(i * 2 * Math.PI * 110 / rate);
    for (let c = 0; c < nch; c++) {
      const s = Math.max(-1, Math.min(1, c ? v * 0.7 : v));
      const o = (i * nch + c) * bps;
      if (bits === 16) data.writeInt16LE(Math.round(s * 32767), o);
      else data.writeIntLE(Math.round(s * 8388607), o, 3);
    }
  }
  const h = Buffer.alloc(44);
  h.write("RIFF", 0); h.writeUInt32LE(36 + data.length, 4); h.write("WAVE", 8); h.write("fmt ", 12);
  h.writeUInt32LE(16, 16); h.writeUInt16LE(1, 20); h.writeUInt16LE(nch, 22); h.writeUInt32LE(rate, 24);
  h.writeUInt32LE(rate * nch * bps, 28); h.writeUInt16LE(nch * bps, 32); h.writeUInt16LE(bits, 34);
  h.write("data", 36); h.writeUInt32LE(data.length, 40);
  return Buffer.concat([h, data]);
}

const files = [
  [join(dir, "amen.wav"), 44100, 2, 16, 4.0, 1],     // 2 s per bar at 120: --bars cuts it to 2 s
  [join(dir, "think.wav"), 48000, 1, 24, 2.1, 7],
  [join(dir, "funky.wav"), 44100, 1, 16, 1.6, 3],
];
for (const f of files) writeFileSync(f[0], wavBuf(...f.slice(1)));

console.log("breaks.js against tools/upload_breaks.py");
for (const bars of [false, true]) {
  const out = join(dir, bars ? "py_bars" : "py_whole");
  execFileSync("python3", ["tools/upload_breaks.py", ...(bars ? ["--bars"] : []), "--dry-run", out, ...files.map((f) => f[0])],
    { stdio: "pipe" });
  const loops = files.map((f) => {
    const { rate, x } = readWav(readFileSync(f[0]));
    return { label: f[0], s: cut(x, rate, bars) };
  });
  const { slots } = pack(loops, NSLOTS);
  slots.forEach((sl, i) => {
    const img = userSlot(`BR${i + 1}`, sl.map((l) => l.s));
    const hdr = readFileSync(join(out, `slot${i + 1}.hdr`)), bin = readFileSync(join(out, `slot${i + 1}.bin`));
    ok(Buffer.compare(Buffer.from(img.hdr), hdr) === 0 && Buffer.compare(Buffer.from(img.data), bin) === 0,
      `${bars ? "one bar" : "whole"}, slot ${i + 1}: ${sl.length} loop(s), ${img.data.length} B`);
  });
}
// the bars (FMX: nearest 170): a jungle loop of 2.82 s is 2 bars (not 1 at 85); raw breaks as they were
ok(B.barsOf(2.82)[0] === 2 && B.barsOf(2.1)[0] === 1 && B.barsOf(4.0)[0] === 2 && B.barsOf(1.76)[0] === 1 && B.barsOf(7.06)[0] === 4,
  "bars: 2.82 s 2 (170), Think 2.1 s 1, 4 s 2, Amen 1.76 s 1 / 7.06 s 4");
{   // reading back: the header parses, and the decoded loops follow what went in (ADPCM: close, not exact)
  const loops = files.map((f) => { const { rate, x } = readWav(readFileSync(f[0])); return cut(x, rate, true); });
  const img = userSlot("BR1", loops), h = parseSlot(img.hdr);
  let good = h && h.nz === loops.length && h.name === "BR1" && h.dataLen === img.data.length;
  loops.forEach((s, i) => {
    const z = h.zones[i], y = imaDecode(img.data.subarray(z.off, z.off + ((z.n + 1) >> 1)), z.n);
    let e = 0, p = 0;
    for (let k = 0; k < s.length; k++) { e += (y[k] - s[k]) ** 2; p += s[k] ** 2; }
    good = good && z.n === s.length && e / p < 0.05;
  });
  ok(good, "read back: header and decoded loops");
}

if (existsSync("tests/fixtures/kan/breaks_standard.bin")) {   // (local only) the standard set is what --standard-image makes
  const out = join(dir, "standard.bin");
  execFileSync("python3", ["tools/upload_breaks.py", "--standard-image", out], { stdio: "pipe" });
  const a = readFileSync(out), b = readFileSync("tests/fixtures/kan/breaks_standard.bin"), h = parseSlot(b.subarray(0, 480));
  ok(Buffer.compare(a, b) === 0 && h && h.nz === 3 && h.name === "BR1", "tests/fixtures/kan/breaks_standard.bin: the standard set (3 loops, slot BR1), as the tool makes it");
}

console.log(bad ? "BREAKS WEB FAILED" : "breaks web: same bytes as upload_breaks.py");
process.exit(bad ? 1 : 0);
