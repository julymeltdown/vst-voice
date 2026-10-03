# The procedural voice gets louder as it climbs, by about 5 dB across an octave
and a third

Date: 2026-10-03.

## What prompted it

Rendering the listening material recorded in
`PROCEDURAL_LISTENING_MATERIAL_2026-10-03.md` surfaced something in the numbers
before anyone listened. The four-note melisma (`あ` sustained across MIDI 60, 64,
68, 72) measured peak 0.022; the five-syllable rise measured peak 0.101. That is a
4.5x difference between two phrases recorded minutes apart with the same tool.

## The measurement

The melisma phrase has one lyric shared by all four notes, so the vowel is
identical; the saved project confirms `dynamicsAutomation` and `pitchAutomation`
are both empty arrays and vibrato is disabled on every note. Nothing in the score
asks for a level change.

Per-note RMS over the middle 60 percent of each 0.5-second note:

```
note 1 (MIDI 60)  rms 0.00501  -46.01 dBFS
note 2 (MIDI 64)  rms 0.00534  -45.45 dBFS
note 3 (MIDI 68)  rms 0.00930  -40.63 dBFS
note 4 (MIDI 72)  rms 0.00900  -40.92 dBFS
```

That is a **5.41 dB** rise from note 1 to note 3 on identical input.

**It is not the phrase, and it is not the level automation.** Four notes at the
SAME pitch and the SAME vowel render flat: RMS 0.00499, 0.00506, 0.00502, 0.00504,
a spread under one percent. Isolating single notes at four pitches reproduces the
tilt on its own:

```
midi 55 (196.0 Hz)  rms 0.00421   zcr 1573 Hz
midi 60 (261.6 Hz)  rms 0.00499   zcr 1273 Hz
midi 65 (349.2 Hz)  rms 0.00594   zcr  875 Hz
midi 72 (523.3 Hz)  rms 0.00900   zcr 1045 Hz
```

**It is the source and filter interaction.** The excitation is a harmonic series
with a fixed spectral tilt whose partial count is `floor(0.45 * rate / f0)`
(`phonation_source.cpp:80`). As f0 rises, fewer partials fit below the source's own
corner, so the share of energy sitting below 1 kHz falls:

```
midi 55   91.3 percent of source energy below 1 kHz
midi 60   87.0 percent
midi 65   82.3 percent
midi 72   71.5 percent
```

The tract's band gains are normalized across the pose's formants
(`vocal_tract.cpp:164`), so the filter presents a roughly fixed peak gain rather
than compensating for how much excitation lands inside it. As more of the source
moves out of the low band and into the formant region, more of it passes. The
zero-crossing rates fall alongside the rise, which is what a brightening of the
output looks like: the same vowel, rendered with progressively more of its energy
above 1 kHz, and progressively louder.

The source's own normalization is NOT the cause. Recomputing its summed harmonic
weight across the same pitches gives 1.3407, 1.3385, 1.3356 and 1.3299, a spread
under one percent, so the `voiced /= weight` line at `phonation_source.cpp:96` moves
too little to explain a 5.4 dB difference.

## What this is

**Established.** The procedural voice is measurably louder at higher pitches, by
about 5.4 dB from MIDI 60 to 68 on a single sustained vowel with no authored
dynamics. The effect is deterministic, reproduces note-by-note and
pitch-by-pitch, and is explained by the source's fixed spectral tilt meeting a
formant filter normalized across bands rather than across excitation.

**Not established.** Whether this is wrong. It may be defensible: real voices are
loudest in their low register, so a synth that brightens and lifts with pitch is
not automatically wrong. But a 5.4 dB rise across a phrase is large enough that a
singer will hear it as the character changing as the melody goes up, and nothing
in the recipe asked for it. That judgement belongs to a listener, not to a
measurement, and **no listening observation exists**.

**Not a defect claim.** This entry records a measured behaviour and its mechanism.
It does not assert the renderer is broken, does not propose a specific loudness
curve, and does not change any code. Whether to compensate, and by how much, is a
voice-design decision that needs an ear and a reference.

## Why it matters for the plan

Every acoustic unit added over the past several entries measured pitch, timing,
articulation, dynamics and melisma, and each was correct. None of them asked what
the voice's level does as a function of pitch, because each test used one pitch or
measured level only against an authored curve. Producing the audio and simply
measuring it found in a minute what the targeted tests did not ask about.
