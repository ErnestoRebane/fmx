# Pads on the CHORDS track

The CHORDS pads were redone on 2026-10-26, the same way as the basses (`docs/bass.md`): each new preset copies a
sound the scene used, with a known source, method and reason. The samples are made by `tools/gen_pads.py` and built
by `tools/gen_samples.py`. The engine is `firmware/src/eng_sample.c`, apart from BLUE PAD, which is in
`eng_analog.c`.

A/B renders are in `build/pads/`:

- `pads-old.wav`: the CHORDS list before, solo.
- `pads-new.wav`: the PSX pads at 16 kHz and the new presets, solo.
- `pads-mix.wav`: each pad over AMEN in JUNGLE with REESE SUB on the BASS phrase. The start times are in
  `pads-mix.txt`.

Each preset plays 4 bars at 172 BPM: Fm9 (Ab3 C4 Eb4 G4), then Dbmaj9 (F3 Ab3 C4 Eb4), 2 bars each.

None of this has been heard on the device yet.

## What was wrong (measured)

Each preset held the chords on the CHORDS track solo, through the whole mix (sends, master). The steady part is
the second bar.

| old preset | centroid | 250 Hz-1 kHz | over 4 kHz | attack peak | movement |
|---|---|---|---|---|---|
| WARM PAD | 298 Hz | -1.2 dB | -54 dB | +3.8 dB | 2.6 dB |
| DARK STR | 296 Hz | -2.0 dB | -54 dB | +1.7 dB | 2.0 dB |
| ATMOS PAD | 318 Hz | -1.3 dB | -51 dB | -6.7 dB | 1.9 dB |
| SINE PAD | 295 Hz | -1.0 dB | -54 dB | -0.9 dB | 0.6 dB |
| PSX PAD | 461 Hz | -1.0 dB | -37 dB | +1.4 dB | 1.8 dB |
| PSX CHOIR | 1047 Hz | -2.0 dB | -19 dB | +3.6 dB | 1.2 dB |
| PSX GLASS | 612 Hz | -1.7 dB | -40 dB | +1.7 dB | 1.1 dB |
| PSX DARK | 469 Hz | -1.0 dB | -38 dB | -7.3 dB | 1.9 dB |

Key to the columns:

- **250 Hz-1 kHz**: that band's share of the energy. -1 dB means about 80 %.
- **Attack peak**: the loudest 20 ms of the first 400 ms, relative to the steady level.
- **Movement**: the spread of the level over 1 kHz, in 46 ms frames.

Three things stand out:

- **One place in the spectrum.** Seven of the eight have about 80 % of their energy at 250 Hz-1 kHz. That is
  where the reese's mids are (`docs/bass.md`: REESE SUB's mids at 250-800 Hz), so the pad and the bass mask each
  other. Only PSX CHOIR had anything over 4 kHz. The breath and air of the scene's pads were missing.
- **One shape.** Every pad faded in, held and rang out under a lot of reverb: ATK 60-90 (WARM PAD the exception),
  release 0.6-1 s, REVERB 55-76. None had an attack of its own: no bell, no breath, no bow.
- **No life.** The level moved 1-2.6 dB, and every sound was a static loop of detuned saws or a pulse through
  formants.

## Sources: what the scene's pads were

- **The romplers' presets.** Tim Cant's breakdown of atmospheric drum & bass pads (Sonicstate) names:
  - Roland D-50: "Soundtrack" and "Glass Voices";
  - Korg M1: "Universe";
  - Roland JV-1080: "Spaced Voxx" and "Pure Tibet";
  - a "Blade Runner Blue" pad.

  Attack Magazine notes that the producers of the time had samplers, but leaned on sample-player modules (the
  JV-1080 and JV-2080) for more sounds.
- **How LA synthesis builds them.** A D-50 partial is either a PCM sample or a synthesised wave:
  - the PCM samples are attack transients or looped upper spectra;
  - a pad pairs a sampled attack (a breath, a mallet) with a sustained synthesised wave.

  "Fantasia" is soft string tones with metallic, crystal-like attacks.
- **The Rhodes.** By 1995-96 the "Fender Rhodes pads" of atmospheric drum & bass had become a cliché (A Bass
  Chronicle), which shows how common they were. Bukem's palette ran "from swooping synth pads to pan pipe samples".
- **Darkside, 1993.** The strings and pads of euphoric hardcore turned into "doom-laden moans of despair" (A Bass
  Chronicle).
- **Time-stretch.** From OS 2.0 the Akai S1000 could stretch a sample. With extreme settings and a short cycle, a
  voice turns into a "stretched high string sound": a texture of its own, far from seamless.
- **Detroit strings.** The strings on Derrick May's "Strings of Life" came from an Ensoniq Mirage, an 8-bit
  sampler, layered with a synth for more attack. They lean on the first beat with a strong attack, then fill
  with a quieter sustain (Attack Magazine's breakdown; May names the Mirage himself).
- **Goldie.** "Inner City Life" has "stretched orchestral fragments", and its strings bend away from one note to
  another.

## The new presets

Every new sample is one loop at 11.025 or 16 kHz, played across the keys from C4. The PS1's SPU played its
instruments the same way: few samples, low rates, short loops in 512 KB.

How each sample is built:

- **The loop.** It is N samples long, N a power of two, and is made in its own spectrum. Every partial and the
  noise fall on whole bins, so the loop has no seam and needs no crossfade.
- **The attack.** It uses the same partials at the same phases, with their levels moving. Transients such as a
  bell, a chiff or a bark fade out before the loop starts, so the attack runs into the loop without a join.

### SOUNDTRK: the D-50's "Soundtrack"

- **Source.** LA synthesis: a sampled breath for the attack, a synthesised pad after it.
- **What FMX does (SNDTRK, 11 kHz).**
  - Two voices of a hollow pulse, odd harmonics first, a bin apart, so they beat slowly.
  - A resonance at 1.8 kHz, where the D-50's TVF would sit.
  - Air at 2-5 kHz, 17 dB under the tone, and a breath chiff 15 dB up at the start that falls away in 50 ms.
  - A little LFO on the cutoff.

### GLASS VOX: the D-50's "Glass Voices" and the JV's "Spaced Voxx"

- **Source.** A breathy voice with glassy upper partials.
- **What FMX does (GLSVOX, 16 kHz).**
  - An "oo": formants at 380, 850, 2500 and 3400 Hz on a glottal source, two voices a bin apart.
  - Sines two and three octaves up, a bin either side of the note, so the glass shimmers.
  - Breath shaped by the same formants, 17 dB under the tone, stronger at the attack.

### CRYSTAL: the D-50's "Fantasia"

- **Source.** A mallet's metallic attack over soft strings.
- **What FMX does (CRYSTL, 11 kHz).**
  - The attack is a bar's partials (1, 2.76, 5.40 and 8.93 times) an octave above the note, gone in 0.35 s.
  - Under it, a three-voice string pad with glass that stays: an octave and two octaves up, each beating against
    the pad.
  - Measured attack peak: +11.5 dB, the only pad with a real hit.

### UNIVERSE: the M1's "Universe"

- **Source.** The M1's bright, wide digital pad.
- **What FMX does (UNIVRS, 16 kHz).**
  - A saw with a steeper top (h^-0.7), three voices 1-3 bins apart, so the beating is fast.
  - A square an octave up, two voices.
  - Air at 6 kHz.
  - The brightest pad of the set: 1 kHz centroid, -18 dB over 4 kHz.

### TIBET: the JV's "Pure Tibet" and Bukem's pan pipes

- **Source.** A breathy flute, the pan pipe samples of atmospheric drum & bass.
- **What FMX does (TIBET, 16 kHz).**
  - A flute's tone: a strong fundamental, a weak 2nd and 3rd.
  - The breath only 5 dB under the tone, with peaks at the harmonics (what makes breath sound blown, not hiss).
  - At the attack, a chiff and the octave overblown for a moment.
  - The preset adds a vibrato that comes in late.
  - The most noise-like pad: spectral flatness -4 dB, against -14 to -22 for the old ones.

### STRETCH: the S1000's time-stretch

- **Source.** A voice stretched with a short cycle: the "stretched high string sound".
- **What FMX does (STRTCH, 11 kHz).**
  - A sung "ah" stretched three times: each 23 ms grain starts where the voice was, not where it would be.
  - So the phase jumps 43 times a second, with the sampler's short crossfades between grains.
  - The buzz is the sound. The loop has a whole number of grains, and the read position comes back round.

### DARKSIDE: 1993's doom-laden moans

- **Source.** Darkcore's strings and choirs turned into moans.
- **What FMX does (on CHOIR, no new sample).**
  - The choir an octave down: TUNE -12 moves its formants with it, so the voices turn huge and wrong.
  - BITS 30 and DRV 20 for grit, CUT 96.
  - A slow, wide pitch LFO: the moan.
  - The longest swell and tail of the set, and a lot of reverb.

### DETROIT: the Mirage strings of "Strings of Life"

- **Source.** An 8-bit sampler's string section, a strong attack then a quieter sustain.
- **What FMX does (MIRAGE, 11 kHz).**
  - A three-voice ensemble through a violin's body resonances (300 Hz, 1 kHz, 2.8 kHz).
  - The bow bites at the start: louder, and the upper harmonics brighter, for 50-70 ms.
  - The preset removes 8 bits (BITS 80) and plays it with LIN, not PSX: the Mirage had no Gaussian.
  - Measured attack peak +5.7 dB, brighter at the attack (760 Hz against 560 Hz steady).

### BLUE PAD: the CS-80 of "Blade Runner"

- **Source.** Vangelis's brass pad: detuned saws whose filter opens slowly, with vibrato from the ribbon and
  aftertouch.
- **What FMX does (ANALOG).**
  - Saws DTN 16.
  - A resonant low-pass (CUT 34, RES 22) that the slow envelope opens (fenv 44).
  - A vibrato that fades in late (LFADE 90).

### JAZZ EP: the Rhodes

- **Source.** The Fender Rhodes chords of atmospheric drum & bass. The DX's tine piano is the same family.
- **What FMX does (TINEEP, 11 kHz, KEYS).**
  - A hammer's bark: harmonics 2-7, gone in 0.04-0.15 s.
  - The tine's ping at 14 times the note.
  - Then a near sine with some 2nd and 3rd.
  - The envelope lets it fade (DEC 112, SUS 36), and the LFO on AMP makes the Suitcase's tremolo.

## The set (measured)

The same measures as the table above: chords held solo through the whole mix. Key to the extra columns:

- **Attack centroid**: the first 120 ms.
- **Flatness**: 1-6 kHz in the steady part. 0 dB is noise, very negative is lines.

| preset | centroid | over 4 kHz | attack peak | attack centroid | flatness | role |
|---|---|---|---|---|---|---|
| PSX PAD | 446 Hz | -40 dB | +1.3 dB | 620 Hz | -15 | the warm bed |
| PSX DARK | 452 Hz | -42 dB | -5.2 dB | 603 Hz | -17 | a slow swell |
| DARKSIDE | 537 Hz | -34 dB | +0.7 dB | 883 Hz | -11 | moans, darkside sections |
| CRYSTAL | 460 Hz | -43 dB | +11.5 dB | 735 Hz | -20 | a hit on the chord change |
| DETROIT | 563 Hz | -24 dB | +5.7 dB | 758 Hz | -9 | string stabs, riffs |
| BLUE PAD | 431 Hz | -40 dB | +2.0 dB | 467 Hz | -15 | a brass swell |
| GLASS VOX | 919 Hz | -29 dB | +0.7 dB | 684 Hz | -18 | voices over the break |
| SOUNDTRK | 928 Hz | -28 dB | +1.1 dB | 894 Hz | -14 | the atmospheric pad |
| STRETCH | 974 Hz | -32 dB | +2.2 dB | 940 Hz | -19 | the buzz, the '94 texture |
| UNIVERSE | 1037 Hz | -18 dB | +1.1 dB | 1028 Hz | -9 | bright, wide, drops |
| TIBET | 658 Hz | -20 dB | +0.9 dB | 1039 Hz | -4 | breath, intros |
| JAZZ EP | 329 Hz | -47 dB | +12.4 dB | 354 Hz | -17 | Rhodes chords |

What changed:

- Six pads now sit over 900 Hz, and four have real content over 4 kHz (-18 to -29 dB), against one before.
- Three have an attack of their own (CRYSTAL, DETROIT, JAZZ EP).
- TIBET, UNIVERSE, DETROIT and DARKSIDE carry breath or grit (flatness -4 to -11).

In the mix (`pads-mix.wav`), every preset comes out at -12.4 to -13.4 dB RMS, with no clipping.

## Flash, and the user's sounds

- **The old PSX pads at 16 kHz.** PSX PAD, PSX CHOIR, PSX GLASS and the RAVE ORGAN are made the same way as
  before, at 16 kHz instead of 22.05. Nothing of theirs was over 7 kHz worth keeping (PSX PAD -40 dB over
  4 kHz). That gives back 11.2 KB (66.4 KB down to 55.0).
- **The new samples** (eight sets) take 31.0 KB together, 2.3-5.1 KB each. The image is 510,368 B of 581,564 (it was 489,948).
- **SET numbering.** The new sets come after the pool's ids in SET, at `SMP_LATE0` = 84 and up. Sounds from the
  pool keep their SET (`SMP_NSETS + id`, NSETS still 4). Without this, a saved project's CHOP kit or a user
  sound on CHORDS would have played one of the new pads. `smp_builtin()` maps a SET to its built-in set.
- **Preset numbers.** The new SAMPLE presets come after CHOP KIT (12-20) and BLUE PAD is ANALOG 30, so the
  numbers before them do not move (`TRK_DEF`, projects).

## Levels

- **The trims are set by hand.** `tools/level_presets.py` re-measures every preset of an engine, and run now it
  moves the old ones too (the piano packs by 4-5 dB): they were measured before the new master. Those moves were
  left out.
- **Each new pad's trim** is its measured trim, shifted by the amount the same run moved its engine's own pads:
  - SAMPLE: half a dB, from PSX PAD and PSX CHOIR;
  - ANALOG: 4 dB, from WARM PAD, DARK STR, ATMOS PAD and SINE PAD.
- Re-levelling the whole bank against the new master is still open (below).

## Not done / open

- Not heard on the device. Listen to `pads-new.wav` and `pads-mix.wav` first.
- **The whole bank's trims against the new master.** The piano packs and the old analog pads measure 4-5 dB
  off.
- **Goldie's bend.** A string that bends away from one note to another needs a per-voice pitch envelope at the
  release, which the SAMPLE engine does not have.
- **A reversed pad**, the jungle swell that stops dead. The SAMPLE engine's attack is linear, not a reversed
  decay; the ANALOG engine's DRED shape could be lent to it.
- **One sample per pad.** Played far from C4, the voices' formants move (GLASS VOX, DARKSIDE). The PS1 did the
  same, and the chords sit around C4, but a second zone would be 3-5 KB more each.

## Sources

- [Sonicstate: atmospheric DnB pads deconstructed](https://sonicstate.com/news/2024/07/17/atmospheric-dnb-pads-deconstructed/):
  D-50 Soundtrack and Glass Voices, M1 Universe, JV-1080 Spaced Voxx and Pure Tibet, Blade Runner Blue
- [Attack Magazine: a '90s drum & bass lead on the XV-5080](https://www.attackmagazine.com/technique/synth-secrets/programming-a-90s-style-drum-and-bass-lead-with-rolands-xv-5080/):
  samplers plus the JV-1080 and JV-2080 sample players
- [Roland D-50, Wikipedia](https://en.wikipedia.org/wiki/Roland_D-50);
  [Linear arithmetic synthesis, Wikipedia](https://en.wikipedia.org/wiki/Linear_arithmetic_synthesis): PCM
  attacks and looped spectra, partials
- [Image-Line forum: the Fantasia preset](https://forum.image-line.com/viewtopic.php?p=1206117);
  [Future Music: LA synthesis](https://www.pressreader.com/australia/future-music-9629/20200630/283914525132832)
- [A Bass Chronicle: atmospheric D&B, 1995-1996](https://abasschronicle.co.uk/?p=18759): the Rhodes and sax
  pads; Photek, Peshay
- [A Bass Chronicle: early 1993](https://abasschronicle.co.uk/?p=6043): darkside's "doom-laden moans"
- [The Vinyl Factory: LTJ Bukem in 10 records](https://thevinylfactory.com/?p=106337): synth pads and pan pipes
- [Akai S1000, Wikipedia](https://wikipedia.com/wiki/Akai_S1000);
  [Computer Music: old-school time-stretching for an atmospheric intro](https://www.pressreader.com/australia/computer-music/20160518/282849370198549):
  the stretched high string sound
- [Attack Magazine: Strings of Life deconstructed](https://www.attackmagazine.com/technique/deconstructed/rhythim-is-rhythim-strings-of-life-derrick-may/);
  [Roland: the sound behind Strings of Life](https://articles.roland.com/strings-of-life-derrick-may/)
- [Inner City Life, Wikipedia](https://en.wikipedia.org/wiki/Inner_City_Life); [DJ Mag: the making of Inner City Life](https://djmag.com/watch/goldie-inner-city-life-making-of-drum-bass-classic)
- [PlayStation technical specifications, Wikipedia](https://en.wikipedia.org/wiki/PlayStation_technical_specifications):
  the SPU's 24 ADPCM voices, 512 KB
