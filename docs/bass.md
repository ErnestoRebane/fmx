# Jungle basses on the BASS track

The BASS presets were redone on 2026-10-26. This time each one copies one of the scene's own basses, with a
known source, method and reason. The engine is `firmware/src/eng_analog.c` (WAVE RSUB, SUB, DRED). The register
is set in `firmware/src/bassline.c` (`bl_note`, `bass_floor`) and BASS FOLLOW in `seq.c`. A/B renders are in
`build/bass/` (`bass-old.wav`, `bass-new.wav`: 4 bars per preset, AMEN in JUNGLE, F minor, 172 BPM).

None of this has been heard on the device yet.

## What was wrong (measured)

Each preset held the phrase's root, played through the whole mix and measured over 0.75 s. The phrase plays its
notes as written: TRANS moves only the keys.

| old preset | F minor root | where its energy was |
|---|---|---|
| JUNGLE SUB | 87 Hz (A minor: 110 Hz) | 100 % in 60-120 Hz: a sine on the kick's body, not a sub; nothing a small speaker plays |
| 808 JUNGLE | 87 Hz | 96 % in 60-120 Hz |
| REESE SUB | saws 87 Hz, sub 44 Hz | 75-86 % in the sub an octave down; the reese itself (250-800 Hz) 1-2 % |
| DARK REESE | the same | a sub with a hint of reese; in C minor its sub was at 33 Hz |
| HOOVER | 87 Hz | the low end swung 24-30 dB as the two pulses' fundamentals cancelled |

The register was an octave too high for the subs and an octave too low for the reese's sub. The reese's own
character was nearly inaudible, and in C to D# the sub went under 40 Hz.

## The register

A jungle sub's root sits from **E1 to D#2 (41-78 Hz)**:

- Over it the bass sits on the kick's body (60-120 Hz) and fights the break.
- Under it a club system has little left (most bins roll off below 35-40 Hz), and so do headphones, the FM-1's
  out and small speakers.
- Mixing guides put sub fundamentals at 40-60 Hz, at most 40-80 Hz. In F the root is at 43.7 Hz, in A at 55 Hz.

`bl_note` puts the root there: keys C to D# at C2 to D#2, keys E to B at E1 to B1. No note goes under E1: a note
there goes an octave up, so the fifth under the root becomes the fourth over it, as producers play it. BASS
FOLLOW keeps the same floor.

Every preset now plays its note at the true pitch, so the note is the sub. TRANS -24 puts the keys' C4 at C2.

## The basses

### REESE SUB: the '94 reese (Renegade, "Terrorist")

- **Source.** Kevin Saunderson's bass on "Just Want Another Chance" (Reese, 1988) was a Casio CZ patch, found
  by trial and error. In 1994 Ray Keith sampled it for "Terrorist", and jungle took it from there. Sources
  disagree on the model (CZ-5000 or CZ-1000), but all name a CZ.
- **Why it moves.** Two saws a few cents apart. Each harmonic n beats at n times the difference, so the
  cancellation sweeps up the spectrum like a slow phaser. A sampled note played down the keys keeps its detune
  in cents, so its beating slows with the pitch. FMX keeps DTN in cents for that reason.
- **The problem it brings.** The fundamentals beat too, so the sub level rises and falls. Tutorials note
  "unpredictable sub frequencies" and a meter that "rises and falls", and producers fix it with a clean sine
  layer.
- **What FMX does (RSUB).** Each saw has its fundamental removed. A saw is -2/π sin x plus its harmonics, so
  0.6366 of the same phase's sine is added back. The clean sine at the note carries the fundamental. The
  beating stays in the mids, where it makes the reese, and the low end is steady.
  - The detune is now either side of the note, so the reese is in tune with its sub.
  - Measured on the saws alone: 0.0 % of the energy at 30-60 Hz after the change, against 10.7 % for plain
    saws.
  - REESE SUB: DTN 20 ct, SUB 72, CUT 955 Hz, slow LFO on the filter. Low swing 1-1.7 dB (the old REESE swung
    11-12 dB). Mids at -11 dB re the sub.

### DARK REESE: '96-'98 techstep (Ed Rush & Optical)

- **Source.** The reese distorted, filtered and moving: the sound of "Wormhole" and "Cutslo". Mid-range growl
  from distortion and filtering, over a focused sub.
- **What FMX does.**
  - DTN 44 ct, for faster, more nervous beating.
  - DRV 96, distortion before a lower, resonant filter (CUT 390 Hz, RES 44).
  - LFO on the filter, a little filter envelope, DIST send 16. Mids at -5 dB re the sub.

### JUNGLE SUB: the sampler's test tone

- **Source.** Producers took the Akai S-series' built-in sine test tone, played it on a low octave, and shaped
  it with the amp envelope and compression. Some say the S900/S950 sine sounds "fatter than a pure sine".
- **Why add harmonics.** A pure 40-60 Hz sine vanishes on small speakers. Its octave and twelfth let the ear
  rebuild the fundamental (the missing fundamental). This is the most likely reason the old samplers' "warm"
  sines carried better.
- **What FMX does (SUB).**
  - The sine at the note.
  - HARM adds the octave, from the sine squared: a pure 2nd harmonic, a valve's warmth.
  - ODD adds the twelfth: 3s - 4s³, a pure 3rd harmonic, a driven desk.
  - JUNGLE SUB: 2nd at about -20 dB, 3rd at about -30 dB, held long, glides. Mids at -30 dB: the cleanest of the
    set.

### 808 JUNGLE: the 808 sampled as a bass

- **Source.** 808 bass tones (sines) sampled into an Akai S950, then run through a Mackie desk for warmth and
  grit.
- **What FMX does.**
  - SUB with HARM 48, ODD 16, a little GRIT (the desk).
  - The pitch falls ~4.5 semitones into the note (ENV > PIT 24, the 808's sweep).
  - SUS 0 with a long decay: the boom.
  - Glides on slides.

### VALVE SUB: the sound system's sub with mid distortion

- **Source.** Dillinja's basses ("Valve" is the name of his label and his sound system): extreme sub weight and
  midrange distortion that cuts through a system. He pushed levels hard and used saturation and layered
  resampling.
- **Why parallel.** Distortion through the sub makes the low end mush, and difference tones pile under it. The
  usual way is a clean sub plus a distorted copy that is filtered away from the low end (multiband).
- **What FMX does (SUB GRIT).**
  - The same sine driven up to 16x (DRV) goes through the filter as a band-pass (BAND, RES) and is added beside
    the clean sine.
  - VALVE SUB: GRIT 100, band 390 Hz, wide. Mids at -6/-7 dB re the sub; the low end stays steady (0.6-1.4 dB).

### DUB BASS: ragga jungle's roots bass

- **Source.** The reggae sound-system bass that ragga jungle sampled and replayed: round, plucked, a valve amp's
  bark.
- **What FMX does.**
  - SUB with a strong octave (HARM 50) and twelfth.
  - The grit band at 300 Hz, opened by the filter envelope on each note (the bark).
  - A pluck that decays to two-thirds, then long glides.

### HOOVER: the Alpha Juno's "What the"

- **Source.** Eric Persing's Alpha Juno patch "What the", via Joey Beltram's "Mentasm":
  - pulse-width-modulated saw;
  - detuned layers;
  - the Juno's chorus;
  - a multi-stage pitch envelope.
- **What FMX does.**
  - PWM with the LFO on the width (it was static before), DTN 34.
  - The pitch scooping up into the note (ENV > PIT -20).
  - CHORUS send 48. It is a riff and a stab, not a sub, and its low end still swings (15-18 dB): the two pulses
    keep their fundamentals.

### DRED BASS: the reversed bass (Dead Dred, "Dred Bass", 1994)

- **Source.** An acid line from a Future Music cover CD, reversed and warped on an Amiga in Music-X. It led to
  speed garage, bassline and grime basses.
- **Why it sounds so.** A reversed decay is a swell that rushes to the top and stops dead. The reversed filter
  closes into the start, so played forward it opens late.
- **What FMX does (DRED).**
  - The saws through a resonant low-pass.
  - The envelope cubed, so the attack is a decay played backwards. The filter envelope follows it.
  - SUS at full and a short release: the cut.
  - Measured: -68, -41, -25, -18, -14 dB over the first 200 ms.

## The set as a ladder

From clean to dirty: how much of each preset is above the sub, the mids (120-3000 Hz) re the low end (30-120 Hz),
at A minor.

| preset | mids re sub | low swing | role |
|---|---|---|---|
| JUNGLE SUB | -30 dB | 0.8 dB | the roller under a busy break |
| 808 JUNGLE | -17 dB | decay | booms, slides |
| REESE SUB | -11 dB | 1.0 dB | the '94 roll |
| DUB BASS | -8 dB | 0.7 dB | ragga, dub sections |
| VALVE SUB | -6 dB | 0.6 dB | drops on a big system |
| DARK REESE | -5 dB | 1.6 dB | techstep, DROP 2 |
| HOOVER | -5 dB | 15.6 dB | riffs, stabs |
| DRED BASS | -3 dB | 2.6 dB | swells, breakdowns |

The trims (`preset_trim.h`) are set by hand to the same RMS, about -13.5 dBFS through the master (the decaying
808 -14.7, the swelling DRED -15.5).

## Not done / open

- Not heard on the device. Check the subs on headphones and a system, and whether E1 is the right floor.
- The S1000 time-stretch "warp" texture (grains at the stretch rate) is not modelled.
- The HOOVER's PWM is a pulse, not the Alpha Juno's PWM saw ("a saw with two holes cut in it").

## Sources

- [Attack Magazine: Reese bass redux](https://www.attackmagazine.com/technique/tutorials/reese-bass-redux/):
  CZ origin per Saunderson, detuned-sine reese, the level swing
- [Reese bass, Wikipedia](https://en.wikipedia.org/wiki/Reese_bass)
- [MusicRadar: classic reese like Renegade's Terrorist](https://www.musicradar.com/how-to/classic-reese-bass-renegade-terrorist):
  ±55 ct, noise, unpredictable sub
- [Bonedo: Origins of Sound, der Reese-Bass](https://www.bonedo.de/artikel/origins-of-sound-der-reese-bass): CZ-1000,
  the bassapella intro sampled
- [DJ Mag: how Renegade's Terrorist created a blueprint for jungle](https://djmag.com/longreads/how-renegades-terrorist-created-blueprint-jungle)
- [Reason Studios: Jungle 101, let's talk bass](https://www.reasonstudios.com/news/post/jungle-101-lets-talk-bass): dred bass,
  reese, 808 through the S950 and a Mackie, test tones
- [Attack Magazine: old school jungle bass](https://www.attackmagazine.com/technique/synth-secrets/learn-how-to-make-an-old-school-jungle-bass-with-baby-audios-ba-1/):
  PWM + triangle sub, overdrive, cheap rack effects
- [MusicRadar: ultimate guide to sub bass](https://musicradar.com/news/ultimate-guide-to-sub-bass): 40-60 Hz,
  the Akai test tone
- [Line Noise: Dillinja is a jungle king](https://linenoise.substack.com/p/dillinja-is-a-jungle-king)
- [MusicRadar: classic 90s hoover](https://musicradar.com/how-to/how-to-create-a-classic-90s-hoover-sound);
  [Elektronauts: Alpha Juno hoover](https://elektronauts.com/t/syntakt-patch-requests-alpha-juno-2-hoover/198696)
- [Sonicstate: make the Cutslo reese bass](https://sonicstate.com/news/2025/12/19/make-the-cutslo-reese-bass)
- [Akai S1000, Wikipedia](https://en.wikipedia.org/wiki/Akai_S1000): time-stretch in OS 2.0
