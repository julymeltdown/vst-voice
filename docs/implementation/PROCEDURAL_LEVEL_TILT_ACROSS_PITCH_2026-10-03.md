# The procedural voice's level depends on where a note sits relative to the
voice's formants, with a 21 dB resonant peak inside the usable range

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

## Correction: the first version of this entry understated the effect

This entry originally reported "about 5.4 dB across an octave and a third" and
called it a gradual tilt. That was measured only over MIDI 60 to 72, and it is
wrong as a description of the behaviour. Widening the sweep to a realistic
singing range shows the effect is not gradual and not monotonic:

```
midi 48   rms 0.00225
midi 55   rms 0.00421
midi 60   rms 0.00499
midi 65   rms 0.00594
midi 72   rms 0.00900
midi 74   rms 0.01228
midi 76   rms 0.01847
midi 77   rms 0.02406
midi 78   rms 0.03505
midi 79   rms 0.05327   <- peak, the fundamental sits on the first formant
midi 80   rms 0.04360
midi 81   rms 0.02386
midi 84   rms 0.00474
```

That is **21.2 dB** between MIDI 48 and MIDI 79 and back down, on one vowel with no
authored dynamics. The earlier 5.4 dB figure is real but local: it describes the
monotonic part below the resonance, not the behaviour.

## The mechanism, which the wider sweep identifies exactly

The tract runs parallel bandpass filters and sums `band.weight * value` across
them (`vocal_tract.cpp:253`), with band gains normalized across the pose
(`vocal_tract.cpp:164`). The output therefore depends on how much excitation energy
lands inside a formant at all, and the excitation is a harmonic series whose
partials sit at exact multiples of f0. The `あ` pose's first formant is 800 Hz with
a 90 Hz bandwidth. Computing what fraction of source energy falls inside it:

```
midi 60 (f0 261.6)   4.9 percent inside the 800 Hz formant
midi 72 (f0 523.3)   0.0 percent
midi 79 (f0 784.0)  72.0 percent   <- f0 has landed on the formant
midi 84 (f0 1046.5)  0.0 percent
```

At MIDI 79 the fundamental is 35 cents below the formant centre, so nearly all the
source energy is resonant. One semitone away on either side almost none of it is.
The measured peak sits exactly there, and rises and falls smoothly around it
(0.02405, 0.03507, 0.05329, 0.04360, 0.02386 for MIDI 77 through 81). This is a
formant resonance spike in level, not a spectral-balance effect.

The code's stated intent argues this is unintended. The breathiness channel is
documented as "a balance, not an addition, so a breathy phrase is not a louder
phrase" (`phonation_source.cpp:101`), and the source's own `voiced /= weight`
(`phonation_source.cpp:96`) normalizes the periodic sum. A pitch-dependent 21 dB
level swing is the same class of surprise those lines exist to prevent.

## What this is

**Established.** The procedural voice's level depends on where a note sits
relative to the voice's formants. On one sustained vowel with no authored
dynamics it swings **21.2 dB** across MIDI 48 to 79 and back, peaking where the
fundamental lands on the first formant. The effect is deterministic, reproduces
pitch-by-pitch, and is explained by a harmonic source whose partials sit at exact
multiples of f0 meeting a parallel formant filter that sums band energy.

**Not established.** Whether the correct repair is a gain compensation, a wider
first-formant bandwidth, or accepting a real singer's formant resonance. Real
voices DO get louder on vowels whose formants align with the pitch, which is part
of why a singer's note can seem to jump. But 21 dB is far beyond the resonance a
singer would hear as expression, and a melody that wanders across the range would
carry a 21 dB level contour nobody scored. **How much to correct, and in what
direction, is a voice-design decision that needs an ear**, and **no listening
observation exists**.

**Not a defect claim.** This entry records a measured behaviour and its mechanism.
It does not assert the renderer is broken, does not propose a specific loudness
curve, and does not change any code. Whether to compensate, and by how much, is a
voice-design decision that needs an ear and a reference.

## The control that already exists: this is a recipe parameter, not a code defect

The mechanism above makes a specific prediction. If the level swing is the
fundamental landing inside a narrow first formant, then **widening that formant's
bandwidth must compress the swing**, because more of the harmonic series falls
inside the passband at every pitch. The schema already exposes this:
`bandwidthHz` is a per-band recipe field bounded to 10 through 5000 Hz
(`voice_recipe.cpp:141`), and the pilot's `あ` pose ships it at 90 Hz.

Rendering the same vowel at the same pitches with only that one number changed:

| F1 bandwidth | MIDI 72 | MIDI 79 | MIDI 84 | peak-to-min spread |
| --- | --- | --- | --- | --- |
| 90 Hz (shipped) | 0.00899 | 0.05327 | 0.00474 | **21.01 dB** |
| 120 Hz | 0.01126 | 0.05433 | 0.00760 | 17.08 dB |
| 150 Hz | 0.01350 | 0.05480 | 0.01090 | 14.03 dB |
| 200 Hz | 0.01710 | 0.05515 | 0.01631 | 10.58 dB |
| 250 Hz | 0.02049 | 0.05530 | 0.02126 | 8.62 dB |
| 300 Hz | 0.02365 | 0.05537 | 0.02563 | 7.39 dB |
| 400 Hz | 0.02923 | 0.05545 | 0.03272 | 5.56 dB |
| 600 Hz | 0.03758 | 0.05553 | 0.04180 | 3.39 dB |

The prediction holds and the relationship is monotonic: the swing falls from
**21.0 dB to 3.4 dB** purely by widening one recipe field. The peak at MIDI 79
barely moves (0.05327 to 0.05553) while the surrounding pitches rise to meet it,
which is what a resonance being filled in looks like rather than a gain being
applied.

**Why this changes the disposition of the finding.** An earlier revision of this
entry ended by saying the fix needed an ear. That is true of the final tuning
value, but not of the existence of a control: the renderer already exposes exactly
the parameter that governs this, and its shipped value sits at the narrow end of its
own legal range. **No code change is proposed and none is needed to make the
behaviour adjustable.** What is missing is a decision about where in the 90 to
600 Hz range the shipped vowel should sit, and that decision needs a listener.

## Why it matters for the plan

Every acoustic unit added over the past several entries measured pitch, timing,
articulation, dynamics and melisma, and each was correct. None of them asked what
the voice's level does as a function of pitch, because each test used one pitch or
measured level only against an authored curve. Producing the audio and simply
measuring it found in a minute what the targeted tests did not ask about.
