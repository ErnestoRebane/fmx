#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""FMX's own pads and stabs for the SAMPLE engine, synthesised (no recording, no licence to check): short loops at
22.05 kHz in the manner of a PlayStation's sound chip, whose games played their pads as a few cycles of a sampled
tone, looped. Writes OUT/<SET>/<SET>_m<root>[_L<start>-<end>].wav, read by tools/gen_samples.py (GEN_SETS).

  PSXPAD  five detuned saws, the top rolled off: a warm analogue-ish pad
  CHOIR   three voices of a pulse through the formants of "ah"
  GLASS   a bell-like tone: a few strong upper harmonics, two voices
  ORGAN   the rave organ stab: drawbars 16' 8' 5 1/3' 4' 2', a percussive 2nd and 3rd, decaying (no loop)
  (these four at 16 kHz since 2026-10-26; the CHORDS pads of docs/pads.md, one loop each from C4, at 16 / 11.025 kHz:)
  SNDTRK  the D-50's Soundtrack: a breath's chiff, a hollow pulse through a resonance, air
  GLSVOX  the D-50's Glass Voices: a breathy "oo", glass two and three octaves up
  CRYSTL  the D-50's Fantasia: a bar's partials over soft strings
  UNIVRS  the M1's Universe: a bright saw and an octave square, fast beating, air
  TIBET   the JV's Pure Tibet: a flute's tone under its breath
  STRTCH  the S1000's time stretch: an "ah" stretched three times in 23 ms grains
  TINEEP  the Rhodes: a hammer's bark, the tine's ping, a near sine
  MIRAGE  the Mirage strings of Strings of Life: an ensemble whose bow bites at the start
  the CHOP track's kits, one hit per key from F3 up (OUT/<KIT>/<KIT>_k<nn>.wav):
  RAVEHITS  minor organ stab, m7 saw stab, orchestra hit, hoover hit, rave piano chord, sub drop, zap, noise riser,
            reversed cymbal

A looped zone: an attack of ATK s, then L samples that repeat without a seam: every voice makes a whole number of
cycles in L (the fundamental is moved by well under a cent for that, the root says by how much), so the detuning's
beating is periodic in L too.

  tools/gen_pads.py OUT_DIR"""
import math
import struct
import sys
from pathlib import Path

TR = 22050
ATK = 0.12                                       # s of attack before the loop
LOOP = (0.55, 0.85)                              # s: the loop's length is searched in this range
TAB = 2048                                       # wavetable size (one cycle)


def midi_hz(n):
    return 440.0 * 2 ** ((n - 69) / 12)


def table(amps):
    """one cycle of the harmonics amps[1..] (amps[0] unused), peak 1"""
    t = [0.0] * TAB
    for h, a in enumerate(amps):
        if h and a:
            w = 2 * math.pi * h / TAB
            for i in range(TAB):
                t[i] += a * math.sin(w * i)
    pk = max(abs(v) for v in t) or 1.0
    return [v / pk for v in t]


def saw_amps(f0, roll, rate=TR):
    """a band-limited saw below 0.45 rate, its top rolled off from `roll` x f0"""
    n = int(0.45 * rate / f0)
    return [0.0] + [(1.0 / h) / (1.0 + (h / roll) ** 2) for h in range(1, n + 1)]


def formant_amps(f0, formants, rate=TR):
    """a pulse's harmonics weighted by resonances [(Hz, bandwidth, gain)]"""
    n = int(0.45 * rate / f0)
    out = [0.0]
    for h in range(1, n + 1):
        f = h * f0
        g = sum(a / (1.0 + ((f - fc) / (bw / 2)) ** 2) for fc, bw, a in formants)
        out.append(g / h ** 0.3)
    return out


def best_loop(f0, rate=TR):
    """the loop length L (samples) whose whole number of cycles n puts n rate / L nearest f0: (L, n, f0')"""
    best = None
    for L in range(int(LOOP[0] * rate), int(LOOP[1] * rate)):
        n = round(f0 * L / rate)
        e = abs(math.log2(n * rate / L / f0))
        if best is None or e < best[0]:
            best = (e, L, n)
    _, L, n = best
    return L, n, n * rate / L


def render(tab, voices, f0, L, atk_n, total, env=None, rate=TR):
    """voices: [(cycles offset in L, gain)]; f0 such that f0 L / rate is whole: each voice makes f0 L / rate + m cycles"""
    out = [0.0] * total
    for m, g in voices:
        inc = (f0 + m * rate / L) * TAB / rate
        ph = (hash((m, g)) % TAB) * 1.0            # (a fixed start phase per voice: no comb at the attack)
        for i in range(total):
            k = int(ph)
            fr = ph - k
            out[i] += g * (tab[k % TAB] * (1 - fr) + tab[(k + 1) % TAB] * fr)
            ph += inc
            if ph >= TAB:
                ph -= TAB
    for i in range(min(atk_n, total)):            # the attack: a smooth rise
        out[i] *= math.sin(0.5 * math.pi * i / atk_n) ** 2
    if env:
        for i in range(total):
            out[i] *= env(i / rate)
    pk = max(abs(v) for v in out) or 1.0
    return [v / pk for v in out]


_fresh = set()


def write_wav(path, x, rate=TR):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.parent not in _fresh:                   # (a set's files of an earlier run go: their names say how they
        _fresh.add(path.parent)                     # were made, and gen_samples.py reads every file in the folder)
        for old in path.parent.glob("*.wav"):
            old.unlink()
    data = b"".join(struct.pack("<h", max(-32767, min(32767, int(v * 30000)))) for v in x)
    hdr = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    path.write_bytes(hdr + b"data" + struct.pack("<I", len(data)) + data)


def looped(out, name, root, amps_of, voices, rate=TR):
    f_target = midi_hz(root)
    L, n, f0 = best_loop(f_target, rate)
    atk = int(ATK * rate)
    x = render(table(amps_of(f0, rate)), voices, f0, L, atk, atk + L, rate=rate)
    cents = 1200 * math.log2(f0 / f_target)
    tag = f"{root * 100 + round(cents):d}"         # (the root in cents: gen_samples.py reads _c)
    write_wav(out / name / f"{name}_c{tag}_L{atk}-{atk + L - 1}.wav", x, rate)


def env_ad(t, a, d):
    """a quick rise over a s, then an exponential fall (d s to 1/e)"""
    return min(1.0, t / a) * math.exp(-max(0.0, t - a) / d) if a > 0 else math.exp(-t / d)


def additive(freqs_amps, n, env, bend=None):
    """sines (freq Hz, amp) for n samples, the amplitude env(t), the pitch x bend(t)"""
    out = [0.0] * n
    phs = [0.0] * len(freqs_amps)
    for i in range(n):
        t = i / TR
        k = bend(t) if bend else 1.0
        v = 0.0
        for j, (f, a) in enumerate(freqs_amps):
            phs[j] += 2 * math.pi * f * k / TR
            v += a * math.sin(phs[j])
        out[i] = v * env(t)
    return out


def saw_partials(f, roll=12.0, top=0.45):
    n = int(top * TR / f)
    return [(h * f, (1.0 / h) / (1.0 + (h / roll) ** 2)) for h in range(1, n + 1)]


class Noise:
    def __init__(self, seed):
        self.x = seed

    def __call__(self):
        self.x = (self.x * 1103515245 + 12345) & 0x7FFFFFFF
        return self.x / 0x3FFFFFFF - 1.0


def norm(x):
    pk = max(abs(v) for v in x) or 1.0
    return [v / pk for v in x]


# ---- the pads of docs/pads.md: each one a sound of the scene's (the source, why it sounds so, what is made here)
# A pad is an attack of A samples, then a loop of N (a power of two) that is a sum of the loop's own bins: every
# partial makes a whole number of cycles in N, the noise too (made in its spectrum), so the loop has no seam. The
# attack is the same partials, at the same phases, their levels moving (and transients that are gone by its end):
# it runs into the loop without a join. The fundamental is the bin nearest the root; the root in cents says how far.
PAD_RATE = 16000                                 # (FMX's pads: the PlayStation's few kHz, not the CD's)
LO_RATE = 11025                                  # (the darker ones: a quarter of 44.1 kHz, as many PS1 banks were)


def ifft(X):
    """the inverse DFT of X (a power of two long), real part, unscaled"""
    n = len(X)
    a = list(X)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    m = 2
    while m <= n:
        w = complex(math.cos(2 * math.pi / m), math.sin(2 * math.pi / m))
        for k0 in range(0, n, m):
            wk = 1.0
            for k in range(m // 2):
                u, v = a[k0 + k], a[k0 + k + m // 2] * wk
                a[k0 + k], a[k0 + k + m // 2] = u + v, u - v
                wk *= w
        m <<= 1
    return [v.real for v in a]


def lp(f, fc, order=1):
    return 1.0 / math.sqrt(1.0 + (f / fc) ** (2 * order))


def bump(f, fc, bw):
    """a resonance at fc, bw wide (Hz)"""
    return 1.0 / (1.0 + ((f - fc) / (bw / 2)) ** 2)


class Pad:
    """partials (bin, amp, phase, env) on whole bins of the loop, noise (a spectral shape, env), transients (Hz, amp,
    tau: only in the attack, faded out by its end). env(t) over the attack ends at 1"""

    def __init__(self, rate, n, root, atk):
        self.rate, self.n, self.atk = rate, n, int(atk * rate)
        self.df = rate / n
        self.c = round(midi_hz(root) / self.df)    # the fundamental's bin
        self.root = root
        self.parts, self.noise, self.trans = [], [], []
        self.rnd = Noise(root * 7 + n)

    def harmonics(self, amp_of, det=0, gain=1.0, env=None):
        """a voice: harmonics h of bin c + det, amp_of(h, Hz); below 0.45 rate"""
        k0 = self.c + det
        h = 1
        while h * k0 * self.df < 0.45 * self.rate:
            a = amp_of(h, h * k0 * self.df)
            if a:
                self.parts.append((h * k0, gain * a, math.pi * self.rnd(), env))
            h += 1

    def sine(self, hz, amp, env=None):
        self.parts.append((max(1, round(hz / self.df)), amp, math.pi * self.rnd(), env))

    def add_noise(self, shape, db, env=None):
        """noise of a spectral shape, its level db re the partials' in the loop"""
        self.noise.append((shape, db, env))

    def transient(self, hz, amp, tau):
        self.trans.append((hz, amp, tau))

    def render(self):
        n, A, rate = self.n, self.atk, self.rate
        groups = {}                                 # (one loop per envelope: they move apart in the attack)
        for k, a, ph, env in self.parts:
            X = groups.setdefault(env, [0j] * n)
            if 0 < k < n // 2:
                X[k] += 0.5 * a * complex(math.cos(ph), math.sin(ph))
                X[n - k] += 0.5 * a * complex(math.cos(ph), -math.sin(ph))
        ref = math.sqrt(sum(abs(X[k]) ** 2 for X in groups.values() for k in range(n)))   # (Parseval, the DFT
        loops = [(ifft(X), env) for env, X in groups.items()]                          # unscaled: their RMS)
        for shape, db, env in self.noise:
            X = [0j] * n
            for k in range(1, n // 2):
                a = shape(k * self.df)
                if a > 1e-6:
                    ph = math.pi * self.rnd()
                    X[k] = 0.5 * a * complex(math.cos(ph), math.sin(ph))
                    X[n - k] = X[k].conjugate()
            x = ifft(X)
            r = math.sqrt(sum(v * v for v in x) / n) or 1.0
            g = ref * 10 ** (db / 20) / r
            loops.append(([v * g for v in x], env))
        loop = [sum(lp_[i] for lp_, _ in loops) for i in range(n)]
        att = [0.0] * A
        for lp_, env in loops:                      # the attack: the loop's own, run backwards from its start
            for i in range(A):
                att[i] += lp_[(i - A) % n] * (env(i / rate) if env else 1.0)
        fade = min(A, int(0.08 * rate))
        for hz, amp, tau in self.trans:
            w = 2 * math.pi * hz / rate
            for i in range(A):
                g = amp * math.exp(-i / rate / tau)
                if i >= A - fade:
                    g *= math.cos(0.5 * math.pi * (i - (A - fade)) / fade) ** 2
                att[i] += g * math.sin(w * i)
        x = att + loop
        ramp = int(0.003 * rate)                    # (no click at the very start)
        for i in range(min(ramp, len(x))):
            x[i] *= i / ramp
        return norm(x)

    def write(self, out, name, x=None):
        x = x if x is not None else self.render()
        cents = 1200 * math.log2(self.c * self.df / midi_hz(self.root))
        tag = f"{self.root * 100 + round(cents):d}"
        write_wav(out / name / f"{name}_c{tag}_L{self.atk}-{self.atk + self.n - 1}.wav", x, self.rate)


def rise(t0):
    """an envelope: 0 at the start, 1 from t0 on (sin^2)"""
    return lambda t: 1.0 if t >= t0 else math.sin(0.5 * math.pi * t / t0) ** 2


def burst(peak, tau):
    """an envelope: peak x at the start, falling to 1 (tau s to 1/e of the difference)"""
    return lambda t: 1.0 + (peak - 1.0) * math.exp(-t / tau)


def from_(a0, t0):
    """an envelope: a0 at the start, rising to 1 at t0"""
    return lambda t: 1.0 if t >= t0 else a0 + (1.0 - a0) * math.sin(0.5 * math.pi * t / t0) ** 2


def soundtrack():
    """the D-50's Soundtrack (LA: a sampled breath for the attack, a synthesised pad after it): two voices of a
    hollow pulse (the odd harmonics first) through the TVF's resonance at 1.8 kHz; the breath's chiff at the start;
    air over it"""
    p = Pad(LO_RATE, 4096, 60, 0.2)
    body = lambda h, f: (1.0 / h) * (0.35 + 0.65 * (h % 2)) * (0.5 if h == 1 else 1.0) * (lp(f, 2600, 2) + 1.6 * bump(f, 1800, 700))
    env = from_(0.35, 0.15)
    p.harmonics(body, 0, 1.0, env)
    p.harmonics(body, 1, 0.75, env)
    p.add_noise(lambda f: bump(f, 3300, 2600), -17, burst(6.0, 0.05))
    return p


def glass_voices():
    """the D-50's Glass Voices / the JV's Spaced Voxx: a breathy 'oo' (the formants of a voice) with glass over it:
    sines two and three octaves up, each a bin either side, beating slowly"""
    p = Pad(PAD_RATE, 8192, 60, 0.1)
    oo = [(380, 120, 1.0), (850, 160, 0.45), (2500, 300, 0.10), (3400, 400, 0.06)]
    voice = lambda h, f: sum(g * bump(f, fc, bw) for fc, bw, g in oo) / h ** 0.6
    p.harmonics(voice, 0, 1.0)
    p.harmonics(voice, 1, 0.6)
    f0 = p.c * p.df
    for m, a in ((4, 0.14), (6, 0.09), (8, 0.07)):
        p.sine(m * f0 - p.df, a)
        p.sine(m * f0 + p.df, a)
    p.add_noise(lambda f: 0.5 * sum(g * bump(f, fc, bw * 3) for fc, bw, g in oo) + 0.25 * bump(f, 6000, 3000), -17,
                burst(3.0, 0.04))
    return p


def crystal():
    """the D-50's Fantasia: a mallet's metallic attack (a bar's partials, an octave up, gone in a third of a second)
    over a soft string pad with a shimmer an octave up"""
    p = Pad(LO_RATE, 4096, 60, 0.35)
    pad = lambda h, f: (1.0 / h) * lp(f, 1100, 2)
    env = from_(0.15, 0.3)
    for d, g in ((-1, 0.6), (0, 1.0), (1, 0.6)):
        p.harmonics(pad, d, 0.5 * g, env)
    f0 = p.c * p.df
    p.sine(2 * f0 + p.df, 0.16, env)              # (the glass that stays: an octave and two up, beating)
    p.sine(4 * f0 - p.df, 0.07, env)
    for r, a, tau in ((1.0, 1.0, 0.22), (2.756, 0.6, 0.12), (5.404, 0.35, 0.07), (8.933, 0.2, 0.04)):
        if 2 * f0 * r < 0.45 * LO_RATE:
            p.transient(2 * f0 * r, 2.4 * a, tau)
    return p


def universe():
    """the M1's Universe: bright and wide, a digital shimmer: a saw and a square an octave up, three voices a few
    bins apart (the fast beating of the M1's doubled waves), air on top"""
    p = Pad(PAD_RATE, 8192, 60, 0.08)
    saw = lambda h, f: (1.0 / h ** 0.7) * lp(f, 4500) * (0.6 if h == 1 else 1.0)
    sq = lambda h, f: (0.5 / h ** 0.8 if h % 2 else 0.0) * lp(f, 5500)
    for d, g in ((-1, 0.8), (0, 1.0), (2, 0.8)):
        p.harmonics(saw, d, g)
    p.c *= 2
    p.harmonics(sq, 1, 1.0)
    p.harmonics(sq, -1, 0.7)
    p.c //= 2
    p.add_noise(lambda f: bump(f, 6200, 2400), -26)
    return p


def tibet():
    """the JV's Pure Tibet, the pan pipes of Bukem's records: a flute's tone (a strong fundamental, weak overtones)
    under its breath (noise with peaks at the harmonics); the attack's chiff, the octave overblown for an instant"""
    p = Pad(PAD_RATE, 8192, 60, 0.12)
    tone = {1: 1.0, 2: 0.22, 3: 0.10, 4: 0.04, 5: 0.025}
    p.harmonics(lambda h, f: tone.get(h, 0.0), 0, 1.0, from_(0.0, 0.07))
    p.harmonics(lambda h, f: 0.25 if h == 1 else 0.0, 1, 1.0)
    f0 = p.c * p.df
    p.sine(2 * f0, 0.0001, burst(1500.0, 0.035))  # (the octave at 0.15 at the start, gone after)
    comb = lambda f: (0.25 + sum(bump(f, h * f0, 60) for h in range(1, 12))) * lp(f, 5000) * (1 - lp(f, 600, 2))
    p.add_noise(comb, -5, burst(3.0, 0.03))
    p.add_noise(lambda f: bump(f, 6500, 3000), -18)
    return p


def stretch():
    """the S1000's time stretch: a sung 'ah' stretched three times with a short cycle; each 23 ms grain starts where
    the voice was, not where it would be, so the phase jumps at 43 Hz: the 'stretched high string' buzz"""
    rate, n, g = LO_RATE, 4096, 256
    src = Pad(rate, n, 60, 0.0)
    ah = [(730, 160, 1.0), (1090, 200, 0.55), (2440, 300, 0.3), (3400, 400, 0.12)]
    src.harmonics(lambda h, f: sum(a * bump(f, fc, bw) for fc, bw, a in ah) / h ** 0.3, 0, 1.0)
    src.harmonics(lambda h, f: sum(a * bump(f, fc, bw) for fc, bw, a in ah) / h ** 0.3, 1, 0.5)
    s = src.render()
    speed = round(src.c / 3) / src.c                # (whole cycles over the loop: it repeats in n)
    xf = 48                                         # (the crossfade between grains, as the sampler's)
    loop = [0.0] * n
    for j in range(n // g):
        r = j * g * speed
        for i in range(g + xf):
            a = 1.0 if i < g else 1.0 - (i - g) / xf
            if i < xf:
                a *= i / xf
            pos = r + i
            k = int(pos)
            fr = pos - k
            v = s[k % n] * (1 - fr) + s[(k + 1) % n] * fr
            loop[(j * g + i) % n] += a * v
    A = int(0.05 * rate)
    x = [loop[(i - A) % n] * (i / A) for i in range(A)] + loop
    src.atk = A
    return src, norm(x)


def mirage():
    """the Ensoniq Mirage's strings of Strings of Life (8 bits: the preset's BITS): an ensemble (three voices, a
    violin's body resonances) whose bow bites at the start, louder and brighter, then a quieter sustain"""
    p = Pad(LO_RATE, 4096, 60, 0.15)
    body = [(300, 200, 1.0), (1000, 500, 0.8), (2800, 1200, 0.5)]
    string = lambda h, f: (1.0 / h ** 0.8) * (0.4 + sum(g * bump(f, fc, bw) for fc, bw, g in body)) * lp(f, 4000)
    top = lambda h, f: string(h, f) if h >= 4 else 0.0
    for d, g in ((-1, 0.7), (0, 1.0), (1, 0.7)):
        p.harmonics(lambda h, f: string(h, f) if h < 4 else 0.0, d, g, burst(1.8, 0.07))
        p.harmonics(top, d, g, burst(3.0, 0.05))
    p.add_noise(lambda f: bump(f, 2500, 2000), -24, burst(4.0, 0.03))   # (the rosin)
    return p


def tine_ep():
    """the Rhodes of atmospheric jungle (and the DX's tine piano): a hammer's bark (the upper harmonics, gone in a
    few tenths), the tine's ping, then a near sine the envelope lets fade (LFO on AMP: the Suitcase's tremolo)"""
    p = Pad(LO_RATE, 4096, 60, 0.35)
    steady = {1: 1.0, 2: 0.25, 3: 0.08, 4: 0.02}
    p.harmonics(lambda h, f: steady.get(h, 0.0), 0, 1.0)
    f0 = p.c * p.df
    for h, a, tau in ((2, 0.55, 0.15), (3, 0.35, 0.10), (4, 0.25, 0.08), (5, 0.15, 0.06), (6, 0.10, 0.05), (7, 0.07, 0.04)):
        p.transient(h * f0, a, tau)
    p.transient(14.0 * f0 if 14.0 * f0 < 0.45 * LO_RATE else 11.0 * f0, 0.12, 0.015)
    p.transient(f0, 0.5, 0.12)
    return p


def pads(out):
    for f in (soundtrack, glass_voices, crystal, universe, tibet, tine_ep, mirage):
        f().write(out, {"soundtrack": "SNDTRK", "glass_voices": "GLSVOX", "crystal": "CRYSTL", "universe": "UNIVRS",
                        "tibet": "TIBET", "tine_ep": "TINEEP", "mirage": "MIRAGE"}[f.__name__])
    p, x = stretch()
    p.write(out, "STRTCH", x)


def rave_hits():
    """the RAVEHITS kit: [(name, samples)]"""
    hz = midi_hz
    out = []
    cm = [hz(60), hz(63), hz(67)]
    org = []
    for f in cm:
        org += [(f * h, a) for h, a in ((0.5, 0.6), (1, 1.0), (1.5, 0.5), (2, 0.6), (4, 0.3)) if f * h < 0.45 * TR]
    out.append(("minstab", additive(org, int(0.4 * TR), lambda t: env_ad(t, 0.003, 0.16))))
    sw = []
    for f in cm + [hz(70)]:
        sw += [(fh * (1 + d), a * 0.5) for fh, a in saw_partials(f, 6.0) for d in (-0.004, 0.004)]
    out.append(("m7stab", additive(sw, int(0.4 * TR), lambda t: env_ad(t, 0.002, 0.14))))
    nz = Noise(11)
    orch = additive([(fh, a) for f in (hz(48), hz(60), hz(64), hz(67)) for fh, a in saw_partials(f, 9.0)], int(0.45 * TR),
                    lambda t: env_ad(t, 0.002, 0.12), bend=lambda t: 1.0 - 0.02 * min(1.0, t / 0.3))
    orch = [v + 0.6 * nz() * math.exp(-i / TR / 0.04) for i, v in enumerate(norm(orch))]
    out.append(("orchhit", orch))
    hoov = []
    for d in (-0.012, -0.006, 0.0, 0.006, 0.012):
        hoov += [(fh * (1 + d), a * 0.3) for fh, a in saw_partials(hz(48), 5.0)]
    out.append(("hoovhit", additive(hoov, int(0.55 * TR), lambda t: env_ad(t, 0.004, 0.25),
                                    bend=lambda t: 2 ** ((7 - 9 * min(1.0, t / 0.35)) / 12))))
    pno = []
    for f in (hz(51), hz(55), hz(58), hz(62)):
        pno += [(f * h * (1 + 0.0004 * h * h), 1.0 / h ** 1.3) for h in range(1, 14) if f * h < 0.45 * TR]
    out.append(("pianochd", additive(pno, int(0.65 * TR), lambda t: env_ad(t, 0.002, 0.3))))
    n = int(0.6 * TR)
    ph, sub = 0.0, []
    for i in range(n):
        t = i / TR
        ph += 2 * math.pi * (35 + 55 * math.exp(-t / 0.12)) / TR
        sub.append(math.sin(ph) * env_ad(t, 0.004, 0.3))
    out.append(("subdrop", sub))
    n = int(0.16 * TR)
    ph, zap = 0.0, []
    for i in range(n):
        t = i / TR
        ph += 2 * math.pi * (120 + 3000 * math.exp(-t / 0.025)) / TR
        zap.append(math.sin(ph) * env_ad(t, 0.001, 0.06))
    out.append(("zap", zap))
    nz = Noise(23)
    n = int(0.7 * TR)
    lp, rise = 0.0, []
    for i in range(n):
        t = i / TR
        c = 0.02 + 0.6 * (t / 0.7) ** 2            # the low-pass opening: a riser
        lp += (nz() - lp) * c
        rise.append(lp * (t / 0.7) ** 1.5 * (1.0 if t < 0.66 else (0.7 - t) / 0.04))
    out.append(("riser", rise))
    nz = Noise(37)
    n = int(0.6 * TR)
    hp, last, cym = 0.0, 0.0, []
    for i in range(n):
        v = nz()
        hp = 0.7 * (hp + v - last)                 # a high-pass: the metal
        last = v
        t = i / TR
        cym.append(hp * math.exp(-(0.6 - t) / 0.18) * (1.0 if t < 0.585 else (0.6 - t) / 0.015))
    out.append(("revcym", cym))
    return out


def main(out):
    out = Path(out)
    ah = [(730, 160, 1.0), (1090, 200, 0.55), (2440, 300, 0.3), (3400, 400, 0.12)]   # the formants of "ah"
    det5 = [(-2, 0.7), (-1, 0.85), (0, 1.0), (1, 0.85), (2, 0.7)]                     # cycles off in the loop
    r = PAD_RATE                                     # (2026-10-26: at 16 kHz, as made at 22.05 before: 18 KB back)
    for root in (48, 72):
        looped(out, "PSXPAD", root, lambda f, rt: saw_amps(f, 7.0, rt), det5, r)
        looped(out, "CHOIR", root, lambda f, rt: formant_amps(f, ah, rt), [(-1, 0.8), (0, 1.0), (1, 0.8)], r)
        looped(out, "GLASS", root, lambda f, rt: [0.0, 1.0, 0.25, 0.5, 0.15, 0.35, 0, 0.2, 0, 0.12][:max(2, int(0.45 * rt / f) + 1)],
               [(0, 1.0), (1, 0.6)], r)
    for root in (48, 72):                            # the organ stab: one shot, ~0.6 s
        f0 = midi_hz(root)
        dra = [0.0] * 17
        for h, a in ((1, 0.8), (2, 1.0), (3, 0.6), (4, 0.7), (8, 0.4), (6, 0.2)):
            if h * f0 < 0.45 * r:
                dra[h] = a
        tot = int(0.65 * r)
        x = render(table(dra), [(0, 1.0)], f0, r, int(0.004 * r), tot,
                   env=lambda t: math.exp(-t / 0.22) * 0.8 + 0.2 * math.exp(-t / 0.05), rate=r)
        ph2 = [math.sin(2 * math.pi * 2 * f0 * i / r) * math.exp(-i / r / 0.06) * 0.35 +
               math.sin(2 * math.pi * 3 * f0 * i / r) * math.exp(-i / r / 0.04) * 0.25 for i in range(tot)]
        x = [a + b for a, b in zip(x, ph2)]
        pk = max(abs(v) for v in x)
        write_wav(out / "ORGAN" / f"ORGAN_c{root * 100}.wav", [v / pk for v in x], r)
    pads(out)
    for kit, hits in (("RAVEHITS", rave_hits()),):  # the CHOP kit: one per key, in order
        for k, (name, x) in enumerate(hits):
            write_wav(out / kit / f"{kit}_k{k:02d}_{name}.wav", norm(x))


if __name__ == "__main__":
    main(sys.argv[1])
