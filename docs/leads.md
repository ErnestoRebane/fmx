# Leads on the CHOP's INST

The INST pack was replaced on 2026-10-28, the way the basses (`docs/bass.md`) and the pads (`docs/pads.md`) were.
Each lead copies a sound the scenes used, with a known source. INST now lists the leads only. The old pack had
G-FUNK LD, SUPERSAW, FAT LEAD, PAN FLUTE, PWM LEAD, TRAP PLUCK, SYN BRASS, HORN STAB, STRING STB and SCRATCH.
- The analog ones are gone; the new leads took their places in `eng_analog.c`.
- HORN STAB, STRING STB and SCRATCH moved to the CHORDS list (stabs are chords).

Saved songs are not kept compatible (the user's call): a project that used an old lead now plays the new one in its
place.

The scenes are jungle, drum & bass, breakcore, J-core, PS1-era game music and phonk.

A/B renders are in `build/leads/`:

- `leads-old-mix` and `leads-new-mix` (`.m4a` and `.wav`): each sound plays INST's MELODY MOTIF (since replaced by MIDI) for 8 bars, F minor
  at 170, over the BREAK, the BASS phrase and the CHORDS pads.
- `-solo`: the same renders with the CHOP alone.
- The start times are in the `.txt` files next to them.
- `song_audio_test leads OUTDIR NAME..` renders any preset this way (`SOLO=1` for the CHOP alone; `MEL=2` for ARP,
  `MEL=3` for SING).

None of this has been heard on the device yet.

## What was wrong (measured, solo)

- **Level.** The old pack spread over 11 dB, from -8.8 dBFS (STRING STB) to -19.7 (HORN STAB). The new leads are
  matched to -17 dBFS. COWBELL is at -20, being short and percussive.
- **Kinds.** Three of the ten were chord stabs and one a scratch. The user had already set stabs aside for INST.
- **Air.** Six of the ten had less than 5 % of their energy over 4 kHz.

## What a lead needs here (sources)

- **The pads' band.** The melody runs G4 to D6 (392-1175 Hz), right inside the pads' 250 Hz-1 kHz. EQ on the
  fundamental cannot separate them, so a lead wins by other means:
  - harmonics in 2-5 kHz, where the ear is most sensitive (equal-loudness contours);
  - mono, while the pads are wide;
  - an articulation the pads lack: an attack, vibrato, glide.
- **Vibrato.** Singers use 5.5-6.7 Hz at about ±50-60 cents, violinists the same rate at about ±25 cents
  (mcg.uva.nl/mmm-2003, paper 35). LFO RATE 88-92 is 5.1-6.3 Hz, and LD_PIT 1 is ±19 cents. At 170 BPM an eighth
  note is 5.67 Hz.
- **Two looks of "PS1".** The famous soundtracks were streamed CD audio made on 90s studio gear. Soichi Terada (Ape
  Escape) used "AKAI samplers, synths, and computers" at 160-170 BPM, because "the hero is always running" (Red Bull
  Music Academy). The SPU's own lo-fi (4-bit ADPCM, Gaussian interpolation, psx-spx) is the FX panel's PSX CRUSH.

## The leads

| preset | where it comes from | how |
|---|---|---|
| HOOVER LD | Second Phase "Mentasm" ('91), Human Resource "Dominator" ('91). The Alpha Juno's "What The" patch: a pulse-width sawtooth, chorus, a pitch dip at the attack | new WAVE HOOV: a saw plus the same saw one pulse width later, the width swept by a 0.4 Hz LFO; a second one detuned 18 cents; a little octave under; a scoop up from ~3 semitones under; chorus 56 |
| J-CORE SAW | J-core (DJ Sharpnel, t+pazolite). The Roland JP-8000 supersaw: 7 saws detuned up to about ±34/105/180 cents (KVR reverse-engineering) | UNI voices, DETUNE 70, a short glide, delay |
| CHIP PULSE | chip breakcore. The Game Boy pulse channel at 12.5 % duty, where every 8th harmonic is missing: thin, bright, it masks least (gbdev) | PWM held at 12.5 % (ED_SHP -48 at full sustain), no filter, a delayed vibrato |
| GAME SQR | the square game lead of Ridge Racer, Wipeout and Ape Escape. A style, not a documented patch | two squares 6 cents apart, a scoop of about 2 semitones up, a glide, vibrato, echo |
| 16BIT FM | the YM2612 (Mega Drive) brass leads, Yuzo Koshiro | new WAVE FM: ratio 1:1, the modulator's feedback, index 4 rad, a delayed vibrato |
| SYNC LEAD | the hard-sync lead of 80s-90s synths | new WAVE SYNC: a saw restarted each period of the note, its own pitch 3 semitones up plus an envelope sweep of up to 12 |
| ACID LEAD | the 303 in a high register (Aphex Twin, Venetian Snares). Accent raises the level and the filter envelope; a slide spans the next step | one saw, resonance 108, a filter envelope, a legato slide |
| JUNO LEAD | the Juno-106's PWM lead and its chorus (a clone's rates ~0.5/0.83 Hz) | PWM swept at ~0.8 Hz, chorus 70 |
| LIQUID FLT | Good Looking Records' flutes and pan pipes (LTJ Bukem, "Demon's Theme", '92) | a triangle with breath noise, a short chiff from above, a vibrato that comes in late, reverb 52 |
| FM BELL | the DX7's bells: ratio 1:3.5, the index decaying with the note | WAVE FM, ratio 3.5, the index on the envelope (ED_SHP 26), POLY so bells ring over each other |
| COWBELL | drift phonk's melodic 808 cowbell: two squares a ratio of 1.48 apart (540 : 800 Hz) into a band-pass | WAVE FM at ratio 1.48, a low index, a short decay, drive. An approximation: the engine has no band-pass on the output |

## The new waves (eng_analog.c analog_lead)

| WAVE | DTN | MIX | NOIS | SHP (env / LFO) |
|---|---|---|---|---|
| HOOV | cents to the second pulse-saw | the two pulse-saws' balance | the octave under | the pulse width |
| SYNC | the slave's pitch, in quarter semitones | the master's pulse under it | - | adds to the slave's pitch (the sweep) |
| FM | the ratio, DTN / 8 into 0.5, 1, 1.48, 2, 2.5, 3, 3.5, 4, 5, 6, 7, 8, 9, 10, 12, 14 | the index (up to ~8 rad) | the modulator's feedback (up to ±pi) | adds to the index |

Each wave goes through the same drive and low-pass as the others. `analog_lead` is a noinline function with its own
line in `tests/target_budget.txt` (cost 700, near `analog_render`'s 664: a voice runs only one of them).

## Measured (solo, through the mix)

| preset | centroid | 1-4 kHz | over 4 kHz |
|---|---|---|---|
| HOOVER LD | 1476 Hz | -1.0 dB | -20.3 dB |
| J-CORE SAW | 1260 Hz | -4.6 dB | -13.9 dB |
| CHIP PULSE | 1240 Hz | -5.6 dB | -13.3 dB |
| GAME SQR | 997 Hz | -5.7 dB | -17.5 dB |
| 16BIT FM | 1487 Hz | -2.4 dB | -13.3 dB |
| SYNC LEAD | 1813 Hz | -0.6 dB | -13.8 dB |
| ACID LEAD | 1226 Hz | -3.2 dB | -19.6 dB |
| JUNO LEAD | 975 Hz | -6.3 dB | -19.4 dB |
| LIQUID FLT | 770 Hz | -8.0 dB | -41.1 dB |
| FM BELL | 3323 Hz | -3.0 dB | -4.4 dB |
| COWBELL | 1278 Hz | -2.5 dB | -18.7 dB |

The columns are each band's share of the energy, in dB.
- LIQUID FLT is meant to be soft: a flute lives on its fundamental.
- COWBELL has 4 % of its energy under 250 Hz: the FM's lower sideband at 0.48 of the note.

## Open

- Nothing has been heard on the device.
- COWBELL is FM, not the 808's two squares into a band-pass. If it doesn't read as phonk, the next step is a sample
  or a band-pass output.
- No primary source gives patch numbers (ms, cents) for the hoover, Terada's leads, J-core or the YM2612 leads. The
  values here are set from the documented method plus the vibrato and level rules above.
- `web/editor.html` (the legacy editor) still lists the old analog presets.
