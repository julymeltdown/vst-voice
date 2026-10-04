# The pitch tracker jumps an octave after a voicing gap, and most of the measured pitch error is that

Date: 2026-10-04. This is a measurement, not a fix. Nothing in the product changed.

## What was measured

The retained application comparison
`/Users/lhs/seam-corpus-pauses-2026-09-19-r1/application-combined-e9-v512-e1/comparison.json`
reports `status: MISMATCH`, `meanAbsoluteCents: 102.93`, and 447 measurable voiced pairs. Read as a
distribution rather than a mean, it says something different:

| Measure | Value |
| --- | --- |
| Median absolute frame error | **3.54 cents** |
| Mean absolute frame error | 102.93 cents |
| Mean absolute error, excluding 23 frames | **7.17 cents** |
| Share of total absolute error carried by those 23 frames | **93.4 %** |
| Of those 23, how many are exact negative multiples of 1200 cents | **20** |

The multiples are not approximate. Sorted, the 23 are -3693, -3578, -3377, -2539, -2532, -2483,
-2479, -2478, -2477, -2477, -2477, -2463, -1677, -995, -993, -977, -977, -977, -977, -977, 394,
579 and 1893 cents: that is -1200 nine times, -2400 eight times and -3600 three times, plus three
that are not.

Every one of those 23 frames carries `frameStatus: "compared"`. The tracker reported them as
confident measurements, not as uncertain ones. None of them is a frame it flagged low confidence.

## The shape the errors take

Reading the frame series around each error shows one repeated shape. Frames 95-96 read -2477 and
-2478 preceded by two unmeasurable frames. Frames 437-440 read -977 four times running, also after
a gap. Frames 313-315 read -2539, -2532 and -2479 after a gap. Frames 23-24 read -1677 then -3578
after a gap. In every cluster the error begins immediately after the track went unvoiced, and it
persists for as long as the note does.

Between the gaps the tracker is accurate to single-digit cents.

## Reproduced on the real audio, not on a synthetic tone

The mechanism is visible in `libs/seam-voicebank/src/pitch.cpp`. The correlation search finds the
highest-scoring lag (line 152), then a refinement pass takes **the earliest** lag whose correlation
clears `max(voicingThreshold, best * 0.92)` and which is a local maximum (lines 159-170). For a
voice with a strong second harmonic, the correlation at twice the true period is nearly as high as
at the true one, so the earliest qualifying peak is the double period: an octave down. There is
no continuity check against the previously accepted frame, so nothing corrects it while the note
continues.

Running the shipped extractor (`seam_voicebank_cli extract-pitch`) over the retained candidate WAV
after an arithmetic stereo downmix reproduces it:

| Frame | Reported | Held | Cents | Confidence |
| ---: | ---: | ---: | ---: | ---: |
| 98 | 264.22 Hz | 593.21 Hz | -1400.2 | 0.320 |
| 297 | 1200.00 Hz | 250.93 Hz | +2709.2 | 0.558 |
| 315 | 783.34 Hz | 195.76 Hz | +2400.6 | 0.375 |
| 416 | 331.16 Hz | 679.66 Hz | -1244.7 | 0.337 |
| 467 | 329.50 Hz | 655.02 Hz | -1189.5 | 0.497 |
| 490 | 189.52 Hz | 332.80 Hz | -974.8 | 0.325 |
| 497 | 521.47 Hz | 189.61 Hz | +1751.5 | 0.444 |

Seven post-gap octave jumps in 1172 frames, all within a few cents of an exact multiple of 1200.

## Two failed reproductions, recorded because they were informative

**A steady tone with a strong second harmonic does not reproduce it.** A 220 Hz tone with an
equally strong second harmonic tracks correctly across every frame, because with no gap there is
no moment at which the earliest-peak rule is applied to a fresh decision.

**A synthetic note-gap-note does not reproduce it either.** The gap used was long and
digital-silent, which left no energy for the refinement to lock onto at all.

What reproduces it is a real voice across a real voicing boundary, where energy continues but
periodicity does not. That is why the measurement above is on retained audio, and the two
synthetic cases are not offered as evidence.

## What this is and is not

**Established, by the sources and the runs named above.** That 93.4 % of the measured pitch error
in the retained application comparison sits in 23 frames that are exact octave multiples; that all
23 were reported as confident comparisons; that the error begins after a voicing gap and
persists; that the shipped extractor reproduces the same pattern on the same audio; and that the
earliest-peak rule at `pitch.cpp:159-170` has no continuity term.

**Not established, and not claimed.**

- **Nothing is fixed.** No product code changed in this entry.
- **This is not shown to be the whole story.** 23 frames is 5 % of the comparison's frames.
  Removing them drops the mean to 7.17 cents, and whether the remaining frames are inside a
  50-cent tolerance has not been measured.
- **The reference track was not re-extracted**, so this compares the candidate against a stored
  error series rather than re-deriving both sides. The downmix used here is 16-bit and differs
  from the comparison's float32 one, so frame indices do not correspond; the pattern does,
  independently, in both.
- **No threshold is proposed.** A continuity check would change what the tracker reports on real
  voices, and which check is right is a listening question. The listening packet named in the
  readiness register is the input that decision needs, and no one has listened to these frames.
- This does not move SEAM-BETA-P0-08. That blocker is a pitch-accuracy verdict on an application
  render; a measurement explaining most of one number in it is progress toward diagnosis, not a
  qualification.

## A first repair attempt, measured and rejected

The obvious repair is a continuity term: score each candidate peak by its distance from the last
accepted frame rather than taking the earliest qualifying one. It was implemented and measured on
the retained audio, and **it did not work**. Three variants, all rejected:

| Attempt | Result on the retained audio |
| --- | --- |
| Anchor scored by distance, gap clears the anchor | post-gap octave jumps 7 -> 8 |
| Anchor survives a 3-frame gap | 7 -> 8 (unchanged) |
| Anchor survives an 8-frame gap, no in-window peak means unvoiced | jumps 8, but voiced frames fell 518 -> 432 |

The first two did nothing because the octave error begins at the first voiced frame *after* a gap,
which is exactly the frame whose anchor has just been cleared. Carrying the anchor across the gap
did fix frames whose gap was short -- frame 23 moved from 981.8 Hz to 497.3 Hz, the true note --
but not the ones measured here, whose gaps are four frames or longer.

The third variant is the honest failure. Discarding a frame with no peak near the anchor does
remove the octave reading, but it costs 86 real voiced frames to do it: 17 per cent of the voiced
track discarded to remove seven wrong readings. A tracker that marks a sixth of a sung note
unvoiced is worse for every downstream consumer than one that is occasionally an octave out,
because pitch marks, vibrato and unit selection all treat unvoiced as absent rather than wrong.

**What this means.** The defect is real, located and reproduced, and the obvious fix for it is
not correct. The remaining approaches all need evidence this repository does not have: whether a
listener would call the two readings the same note across a voicing boundary is a listening
judgement, and choosing between an octave low and unvoiced changes what every downstream stage
sees. The code is left exactly as it was.

**The measurement that would settle it** is a listening packet over these specific frames, naming
each one and asking whether the tracker or the source has the note right. That packet is the input
the register already names for M2.1, and no one has listened to it.


## Correction, 2026-10-04: the reproduction above used the wrong file

The reproduction in this document was run against master.wav in the same directory, downmixed
to 16-bit. **That is not the audio the comparison measured.** The comparison records
candidate.audioSha256 as b452273c... and the retained master.wav hashes to 015f386a....
They are different files, and the 16-bit conversion additionally clipped 232 samples.

What the retained candidate track itself contains, measured directly from the pitch frames the
comparison stored:

- **2** post-gap octave jumps, not 7: frame 297 (379.2 Hz to 1200.0 Hz) and frame 494
  (374.4 Hz to 93.8 Hz).
- Re-extracting master.wav as float32 -- the encoding the comparison used -- gives 150000 frames
  and **zero** post-gap octave jumps. The frame count matches the stored comparison exactly, and
  the voiced count is 542 against the stored candidate track 541.

**And the 23 large errors are not what I said they were.** Every one of the 23 has frameStatus
"compared" *and* is voiced on both sides at that frame: the reference track reports a note at
494.0 Hz where the candidate reads an octave away, at 812.8 Hz where the candidate reads lower,
and so on. These are octave disagreements about notes both signals agree are sung, not frames that
follow a voicing gap. The pattern I described -- a burst beginning after unmeasurable frames --
was a property of the 16-bit artefact, not of the audio under test.

**What survives.** The distribution is unchanged and does not depend on any file I chose: 23 of
586 frames carry 93.4 per cent of the absolute error, 20 of those 23 are exact negative multiples
of 1200 cents, the median frame is within 3.54 cents, and removing those 23 drops the mean from
102.93 to 7.17. All of that is read from the stored comparison and stands.

**What does not survive.** The claim that the errors begin after a voicing gap, the count of seven
reproduced jumps, and the attribution to the earliest-peak rule at pitch.cpp:159-170. The
mechanism remains a plausible cause of *some* octave error -- the rule still has no continuity
term -- but this document no longer claims it is the cause of these 23.

**And the rejected repair stands as rejected**, for a reason that is now clearer: it was evaluated
against an artefact. It is recorded as untested against the real candidate rather than as refuted,
because the audio it was measured on was the wrong one. The code is unchanged either way.


## What the stored comparison does establish, read from the pitch frames themselves

This section reads the two pitch tracks the comparison stored, and does not depend on any file
chosen here. Both tracks are in the comparison at `pitch.candidateTrack.pitchFrames` and
`pitch.referenceTrack.pitchFrames`.

| Measure | Candidate | Reference |
| --- | ---: | ---: |
| Voiced frames | 541 | 528 |
| Median voiced F0 | 335.7 Hz | 333.3 Hz |
| Distinct voiced F0 values | 231 | 121 |
| Frames at exactly 187.5 Hz (lag 256, the hop size) | 14 | 0 |
| Frames at exactly 1200.0 Hz (the search ceiling) | 1 | 0 |

The medians agree, so both tracks are singing the same note for most of the phrase. What differs
is the candidate has roughly twice as many distinct pitch values, and has 14 frames sitting at
exactly the hop size as a lag while the reference has none.

**The 14 frames are the sharpest thing in the data.** 187.5 Hz at 48 kHz is a lag of exactly 256,
which is this analysis hop size, so those frames report the frame period as its own pitch. Five
of them (frames 305 to 311) sit where the reference reports **unvoiced** -- the source has no
note there -- and the candidate confidence *rises* across that stretch, from 0.61 to 0.92. A
tracker growing more confident where the audio has stopped singing is not measuring pitch.

**What was tested against that.** Three probes were run through the shipped extractor:

- A plain 300 Hz tone: reads 300.0 Hz on all 375 frames. Correct.
- A 300 Hz tone followed by a second of digital silence: **0** voiced frames in the silence. Correct.
- A 300 Hz tone followed by a second of low-level noise: **0** voiced frames in the noise. Correct.

So the tracker does not invent pitch from silence or from noise in isolation, and the hop-locked
frames are not a general failure of the estimator. They need the candidate audio itself, which is
not retained: the comparison stores its SHA-256, and the file beside it is a different render.

**One earlier probe in this session was malformed and is withdrawn rather than reported.** It read
1200 Hz everywhere, which looked like the search ceiling latching. The generator was wrong -- it
never produced a real tone -- and the two clean probes above are the result that stands.

**What this does and does not change.** The concentration finding is unchanged: 23 of 586 frames
carry 93.4 per cent of the absolute error, and removing them drops the mean from 102.93 to 7.17.
What is new is that the candidate side is where the anomaly is, and that it has a specific shape --
14 frames locked to the hop size, five of them where the source is silent, with rising confidence.
Confirming it needs the candidate audio, which is not in this repository.


## U16 corpus render: the measured pitch of the dry vocal mostly sits above the written notes

The U16 quality tooling was run end to end for the first time in this session, on the checked-in
corpus, and the render is real audio:

| Case | Duration | Peak | RMS | Clipped | DC offset |
| --- | ---: | ---: | ---: | ---: | ---: |
| original-melody, bank renderers | 41.00 s | 0.5026 | 0.0691 | 0 | -6.0e-05 |
| original-melody, forced raw | 41.00 s | 0.5026 | 0.1056 | 0 | -2.0e-04 |
| unequal-rests, bank renderers | 9.12 s | 0.4925 | 0.0803 | 0 | -4.1e-04 |
| unequal-rests, forced raw | 9.12 s | 0.4933 | 0.1087 | 0 | -2.4e-04 |

No silent file, no clipping, negligible DC offset. This is listenable material.

**What measuring it found.** The project writes MIDI 62 to 72, which is 293.7 to 523.3 Hz. Running the
shipped extractor over the rendered dry vocal:

| Render | Voiced frames | Inside the written range | Median F0 |
| --- | ---: | ---: | ---: |
| bank renderers | 5745 | 200 (3.5 %) | 751.1 Hz |
| forced raw | 5719 | 260 (4.5 %) | 958.3 Hz |

Only about one frame in twenty falls inside the notes the project actually writes. In the bank
render 59 per cent of voiced frames are above the highest written note and 37 per cent are below the
lowest. Both renderers show it, so it is not the bank selection: it is in the shared path.

**Two claims of mine that were wrong on checking, recorded because both were stated with
confidence.** I first read the 1200 Hz readings as the search ceiling latching on silence. They are
not: a plain tone followed by digital silence gives zero voiced frames, and the lag search range is
correctly 60 to 1200 Hz for this frame size. I then claimed `maximumLag` was capped below the
declared minimum pitch. It is not: the cap is frameSize/2 = 1024, and 60 Hz needs lag 800. Both were
assertions from reading rather than from computing, and both were wrong.

**What this is not.** It is not a musical judgement: nobody has listened to these renders, and the
bank gives eight phoneme labels the same 0.55-second spoken recording by design, so the packet
cannot demonstrate intelligible singing. It is a measurement discrepancy between what the score
writes and what the analyser reads from the render, on a corpus whose own notice says it exists to
exercise timing and fallback rather than musical quality. It may be the analyser, the renderer, or
the fixture. Locating which needs the packet to be listened to and the per-phrase reports read
alongside the pitch track, neither of which has happened.

## Resolved 2026-10-04: it is the fixture. Neither the renderer nor the extractor is at fault.

The entry above correctly refused to attribute the U16 discrepancy. It is now explained, and both of
the suspects named there are cleared by measurement.

**The extractor is accurate.** Running the shipped `seam_voicebank_cli extract-pitch` over synthetic
float32 tones at exactly the pitches this project writes:

| Written | Expected | Reported | Confidence |
| --- | ---: | ---: | ---: |
| MIDI 60 | 261.626 Hz | 261.6255 Hz | 0.99987 |
| MIDI 64 | 329.628 Hz | 329.6269 Hz | 0.99986 |
| MIDI 72 | 523.251 Hz | 523.2503 Hz | 0.99984 |

Sub-millicent across the whole written range. The earliest-peak rule at `pitch.cpp:159-170` does not
misreport clean periodic audio.

**The bank declares itself a fixture.** `production-bank/manifest.json` sets `displayName: "Public-
domain Human Production Pipeline Fixture"`, and all **8 units** reference **one** file,
`audio/human-vowel-demo.wav`, all at **rootMidi 67**. The run proves it: every phrase's `resources`
list carries the same `audio_sha256` of `caf8ceb04b864c7501371a2adae46697495e617b9ade178560ba2ea1aa6b8cb9`
for all eight units. The README states it outright — *"It is deliberately labelled `official=false`
and `contractedSinger=false`. It is not a complete phoneme bank"* — and the notice traces the audio
to a single public-domain spoken recording rather than sung syllables.

**The consequence is arithmetic, not mysterious.** That source recording measures at a **median
990.07 Hz (MIDI 83.0)**, while the manifest declares **rootMidi 67 (392 Hz)** and the score asks for
MIDI 62-72. A renderer asked to move one sustained ~990 Hz voice down into 293-523 Hz cannot present
a clean single-cycle autocorrelation peak in range; the extractor then reports the harmonic
structure that genuinely exists. An independent re-run reproduces the packet's figures almost
exactly: **5745 voiced frames, 196 inside the written range (3.4 %), 3410 above it**, against the
packet's 5745 and 200 (3.5 %).

**What this settles, and what it does not.** It settles the question the earlier entry declined to
answer: the U16 corpus **cannot** demonstrate pitch accuracy, because its bank is one spoken
recording re-labelled eight ways and scored against a written melody. `analyzer_ceiling_hz: 1200.0`
and the packet's own `singerQualified=false` agree with that reading. It does **not** clear the
extractor defect measured on the retained application comparison earlier in this document: that
audio is a real sung render and still carries post-gap octave jumps. Two separate things, now
separated. **The fixture is not a pitch-accuracy test; the real-render octave bug remains open**, and
closing it still needs the listening judgement described above.

## Re-measured 2026-10-04: the defect is real, but it is not an octave error, and four repairs are now ruled out

The retained evidence is still on disk, so this was re-measured from it rather than from the summary
above. Two of the earlier conclusions did not survive contact with it.

**The candidate is `master.wav`.** `comparison.json` records `candidateSha256` as
`015f386a59f826904e58b0f09b065185b50428cc5effc9aca0ff5c635c0e05a3`, which is exactly the digest of
`master.wav` beside it. The earlier claim that the candidate was `b452273c...` and that `master.wav` was
therefore the wrong file **was itself wrong** — the two are the same file. The whole corpus is 24-bit
stereo at 48 kHz, so the reproduction requires a downmix before the bounded extractor will accept it.

**The errors are not octaves.** Extracting the pitch track and comparing against the stored reference at
the same frames:

| Frame | Candidate | Reference | Error |
| ---: | ---: | ---: | ---: |
| 5632 | 93.8 Hz | 493.9 Hz | **−2876 cents (−2.40 octaves)** |
| 6144 | 62.5 Hz | 493.8 Hz | **−3578 cents (−2.98 octaves)** |
| 15872 | 93.8 Hz | 659.2 Hz | **−3376 cents (−2.81 octaves)** |
| 24064 | 62.5 Hz | 261.5 Hz | **−2478 cents (−2.06 octaves)** |
| 100608 | 93.8 Hz | 791.5 Hz | **−3692 cents (−3.08 octaves)** |
| 100864 | 93.8 Hz | 389.0 Hz | **−2463 cents (−2.05 octaves)** |

These are octave errors: divided by 1200 they are -2.98, -2.81, -2.07, -3.08 and -2.05 octaves, which is
within a fraction of an octave of exact -3, -3, -2, -3 and -2 multiples. The chosen lags are 512 and 768
samples; against each frame's true period those are ratios of roughly **2 to 3**, which is the harmonic
relationship an octave-down reading consists of. **This paragraph's original claim that they are "not
multiples of 1200 cents" was an arithmetic error and is corrected by the section at the end of this
document.** So the
"the earliest qualifying peak picks the double period" story in the sections above **does not describe
this audio**, and a fix built on it would have been built on a misreading.

**What the errors actually are.** Ten of **541** voiced frames (1.8 per cent) report 62.5 or 93.75 Hz —
**(counted within the comparison's measurable subset only; the true figure is 46 frames, see the final
section of this document)** —
lag 768 or 512, at the very bottom of the 60–1200 Hz search range. The voiced histogram has a clean gap:
ten frames below 100 Hz, **nothing** between 100 and 125 Hz, then the real content from 175 Hz up. They
are a separate low-frequency cluster, not wrong readings inside the melody.

**Four candidate repairs, all measured and all rejected.**

| Attempt | Result |
| --- | --- |
| Prefer the **strongest** qualifying peak instead of the earliest (the change this section seemed to call for) | **Made it worse**: misreported frames 10 → 29, because the strongest peak is more often the low-frequency one. Reverted rather than kept. |
| Frames adjacent to a voicing gap | **Does not apply**: only **1 of 10** bad frames touches an unvoiced frame, against **7 of 531** good ones — the base rate is higher for good frames. The post-gap theory is unsupported here. |
| An RMS floor to reject quiet frames | **Unusable**: every bad frame is quiet, but the threshold that catches all ten drops **465 of 531** good frames — **87.6 per cent**. |
| A confidence threshold | **Does not separate**: bad frames run 0.41–0.88 against good frames at 0.33–1.00, with the bad median *below* the good median but heavily overlapping. |

**What this establishes.** The defect is **10 frames out of 541**, confined to the lowest lag range, and
**no property available inside the frame separates them from correct readings.** Energy and confidence
both overlap almost completely, and the one structural signal that seemed to explain them — adjacency to
a gap — is a base-rate artefact. This is now the fourth and fifth repair ruled out by measurement, after
the three in the section above.

**It also lowers the stakes honestly.** *(Superseded: the real figure is 8.5 per cent of voiced frames, not
1.8 per cent, and the frames are inside sung notes rather than outside them. See the final section.)* At
1.8 per cent of voiced frames, concentrated outside the sung
range, this is a bounded defect rather than the dominant error term the earlier section described. The
listening packet named above is still the right input, but the question for it has changed: it is no
longer "is this an octave?" but **"is the audio at these ten frames a note at all, or a breath, a room
tone, or silence?"** — because if those frames are not singing, the correct answer is not a better
period estimate but to report them unvoiced, and that is a judgement about the audio, not the code.

**Nothing in the product was changed by this measurement.** The one code change attempted was reverted
and the tree is clean.

## Root cause found 2026-10-04: the audio itself contains 256-sample-period energy, and it is real

The section above concluded that "no property available inside the frame separates them from correct
readings". **That conclusion was wrong, and so were two claims in the sections above it.** This section
supersedes them. The defect was located, its cause identified, and six repairs measured.

### The claim that the errors are not octaves was an arithmetic error

The re-measurement section printed six rows and read "-2.05 to -3.08 octaves, not exact multiples of
1200 cents". Recomputing from the stored `frameErrorsCents`:

| Frame offset | Candidate | Reference | Error | In octaves | Nearest 1200 multiple | Residual |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 6144 | 62.5 Hz | 493.8 Hz | -3578.4 | -2.982 | -3600 | 22 c |
| 15872 | 93.8 Hz | 659.2 Hz | -3376.6 | -2.814 | -3600 | 223 c |
| 24320 | 62.5 Hz | 261.4 Hz | -2477.5 | -2.065 | -2400 | 78 c |
| 100608 | 93.8 Hz | 791.5 Hz | -3693.2 | -3.078 | -3600 | 93 c |
| 100864 | 93.8 Hz | 389.0 Hz | -2463.3 | -2.053 | -2400 | 63 c |

These are octave errors to within a fraction of an octave. The section read the ratio of candidate to
reference frequency instead of the cents column it had already computed, which is why a lag of 512 and
768 against true periods of 97 and 183 was described as ratios of "2.8 to 12.7, not a harmonic
relationship". **A 2x or 3x lag IS the harmonic relationship an octave error consists of**; the
arithmetic that dismissed it was the mistake.

### The second false claim: the errors are not confined to ten frames

"Ten of 541 voiced frames (1.8 per cent)" counted only the frames the comparison marked measurable. The
full picture from the stored comparison:

| Measure | Value |
| --- | ---: |
| Voiced candidate frames | 541 |
| Frames whose reported period is an exact multiple of the 256-sample hop | **46** |
| Same test applied to the reference track | **0** |
| Frames carrying more than 200 cents of error | 23 |
| Of those 23, hop-locked | **20** |
| Correctly tracked frames that are hop-locked | **0 of 424** |

At a threshold of 0.001 hops the separation is **perfect**: every large-error frame is hop-locked, and
not one correctly tracked frame is. The defect is 46 frames, 8.5 per cent of the voiced track, not ten.
The "ten frames" figure came from intersecting with the comparison's measurable subset, which silently
discarded the frames the same artefact had made unmeasurable.

### The cause: isolated impulses spaced exactly one hop apart

Per-window energy maps of the defective frames show the signal is not periodic but **impulsive**. Frame
23 (offset 5888) carries 45 per cent of its energy in one 32-sample cell at position 1824, 18 per cent in
another at 800, and 11 per cent at 1568: six cells holding 86 per cent of the energy, at positions 544,
800, 1056, 1312, 1568 and 1824, which are exactly **256 apart**. Correctly tracked frames are flat: the
top six cells hold 14 per cent and are not evenly spaced.

Those isolated spikes produce the needle-sharp correlation peaks seen in the profile. At frame 23 the
normalized correlation reads -0.367 at lag 252, **+0.634 at 256**, and -0.239 at 260: a spike three
samples wide. Autocorrelation of a periodic waveform is smooth, so a genuine period cannot make a peak
that sharp, and a spike that narrow cannot be one.

### The readings are real audio, not an artefact of the analysis grid

The analyser and the renderer share a 256-sample grid: `kProducerHopSize` in
`libs/seam-voicebank/include/seam/voicebank/acoustic_analysis.hpp:56` is 256, and
`SpectralRenderParameters::hopSize` in `libs/seam-synthesis/include/seam/synthesis/spectral_classic.hpp:20`
is 256. That raised the possibility that the analysis grid was manufacturing these readings. **It is
not.** Re-running the estimator over the identical audio with only the hop changed:

| Analysis hop | 5632 | 6144 | 15872 | 24320 | 24576 | 80128 | 100608 | 100864 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 128 | 93.8 | 62.5 | 93.8 | 62.5 | 62.5 | 187.5 | 93.8 | 93.8 |
| 256 (shipped) | 93.8 | 62.5 | 93.8 | 62.5 | 62.5 | 187.5 | 93.8 | 93.8 |
| 512 | 93.8 | 62.5 | 93.8 | 62.5 | 62.5 | 187.5 | 93.8 | 93.8 |
| 1024 | 93.8 | 62.5 | 93.8 | 62.5 | 62.5 | 187.5 | 93.8 | 93.8 |

The readings do not move. The 256-sample periodicity is in the rendered audio, and it survives any change
to the analysis grid. The sub-150 Hz defect rate is stable at 1.4 to 2.6 per cent across every hop from
128 to 1024, so it is a property of the signal rather than of the windowing.

### The audio really is singing at those frames

The listening question raised above, "is the audio at these frames a note at all, or a breath?", is
answerable without a listener. An independent spectral estimator was run over every voiced frame and
checked against the stored reference:

| Group | Frames | Spectral peak agrees with reference within 100 cents |
| --- | ---: | ---: |
| Correctly tracked (control) | 488 | **474 (97.1 %)** |
| Hop-locked (disputed) | 36 | **30 (83.3 %)** |

The oracle is trustworthy because it is accurate on the control group it was not tuned for. On the
disputed frames it finds a genuine note within a quarter octave of the reference at 30 of 36 frames,
including near-exact matches such as frame 96 (263.7 Hz against a 261.6 Hz reference, +14 cents) and
frame 436 (328.1 Hz against 329.7 Hz, -8 cents). **The singer is producing the right note and the tracker
is reporting an octave below it, because the impulse train out-votes the note.**

### Six repairs measured, none shippable

| Repair | Result on the retained audio | Why it was rejected |
| --- | --- | --- |
| Reject a frame when every qualifying peak is a hop multiple | errors **46 to 10**, all 464 good frames preserved | **Destroys real notes.** A true 187.5 Hz tone (MIDI ~55.7) has period 256 and is reported unvoiced on every frame. The defect and a genuine pitch are geometrically identical. |
| Prefer the strongest peak instead of the earliest | misreported frames 46 to 107 here | The strongest peak is more often the impulse. |
| Reject a peak whose lag cannot repeat inside the window | errors 46 to 8 at a limit of 384, but 524 paired frames fall to 486 | Rejects 196 Hz and MIDI 36, which have 10.4 and 31 cycles. Cycles-in-window **overlaps**: the defect reaches 8.00, correct frames go as low as 5.61. |
| Reject on peak sharpness | errors 46 to 46 at any margin below 0.10 | A true 187.5 Hz tone also gets a needle peak, because the earliest-peak rule selects lag 768 where the correlation is flat at exactly 1.000. Sharpness appeared to separate only because it was measured at the estimator's arbitrary choice. |
| Continuity anchor across gaps | 7 to 8 post-gap jumps; carrying it across an 8-frame gap cost 86 of 518 voiced frames | Measured against a 16-bit artefact in an earlier session, and re-tested here it does not separate either. |
| RMS floor, confidence threshold, gap adjacency | no separation; the threshold catching all bad frames drops 87.6 per cent of good ones | Already recorded above. |

### What this establishes, and what it does not

**Established by the runs named above.** The defect is **46 of 541 voiced frames (8.5 per cent)**, all
with a period that is an exact multiple of 256 samples, against **zero** such frames in the reference. The
audio at those frames genuinely contains the correct note. The cause is an impulse train spaced one hop
apart, present in the rendered audio and not created by the analysis. The errors are octave errors. The
earliest-peak rule at `pitch.cpp:159-170` has no continuity term, no parity term and no sharpness term,
and none of the six available in-frame signals separates the defect from a legitimate pitch.

**Not established, and not claimed.**

- **Nothing is fixed.** No product code changed. Every candidate above was measured outside the tree and
  rejected there; the working tree is clean.
- **The upstream cause is not yet located in code.** That the rendered audio contains these impulses is a
  measurement about a render produced by build `741ae2f2`. Which synthesis stage introduces them is not
  yet identified, and the impulse spacing matching the shared 256-sample analysis grid makes the render
  and analysis paths the place to look, not the tracker.
- **This is not a singer-qualification result.** SEAM-BETA-P0-08 asks for a pitch-accuracy verdict on an
  application render. A located defect with six rejected repairs is progress toward that, not the verdict.
- The six earlier rejected repairs are now partly re-characterised: the strongest-peak and continuity
  results were measured against a 16-bit artefact and stand as untested rather than refuted, and the
  "no property separates them" conclusion is withdrawn in favour of the perfect hop-parity separation
  above, which separates the defect from good frames but not from a genuine hop-multiple pitch.

**The measurement that would settle the remaining question** is the location of the impulse train in the
render path: instrument the synth stages to find which one emits energy on the 256-sample grid, and
re-render. A fix there would remove the defect at its source and would leave the tracker's honest
limitation, that it cannot distinguish a real 187.5 Hz note from a 256-sample impulse train, as a
documented property rather than a silent failure.

## Re-measured 2026-10-04: the defect does not reproduce at HEAD

The section above ends by asking whether a re-render still shows the impulse train. It does not, and that
is the most important result in this document, because it changes what the remaining work is.

**How it was measured.** `tests/test_original_singer_song_journey.cpp` renders an installed procedural
singer singing an authored Japanese lyric end to end through the real authoring and export stack. Run at
HEAD with `SEAM_SONG_ARTIFACT_ROOT` set, it retains the masters, and it passed all 7 cases. The retained
`baseline-master.wav` is 41.0 s of real audio at 48 kHz: peak 0.0918, RMS 0.0152, no clipped samples, DC
offset 1.0e-06. Downmixed to float32 mono and cut into 17 segments under the extractor's 64 MiB input cap,
then analyzed with the shipped extractor:

| Render | Frames | Voiced | Frames with a lag-exact period | Share | Frames below 150 Hz |
| --- | ---: | ---: | ---: | ---: | ---: |
| **HEAD** (`9c2f892e`) | 7692 | 7456 | **0** | **0.000 %** | 3 (0.040 %) |
| Retained comparison candidate (build `741ae2f2`) | 586 | 541 | **46** | **8.503 %** | 11 (2.033 %) |

Per-segment medians sit at 293.6 to 392.1 Hz, which is the written melody, so this is a populated track
and not a silent file being counted as clean. Re-tested at hops of 128, 256 and 512 the HEAD render has
**zero** lag-exact frames at every hop, so it is not merely displaced onto a different grid.

**The strongest candidate cause is a committed fix.** `1c6d57c6` ("fix: refine spectral hop for retimed
voiced re-entry") halves the spectral analysis hop to 128 when a retimed source map crosses a
voiced-to-unvoiced-to-voiced transition, and applies that same hop to formant planning and reconstruction.
All ten bad frames in the retained render fall inside authored sung notes immediately after such a
transition. The commit is dated 2026-09-28, after the `741ae2f2` build that produced the retained audio.
**This is a strong correlation, not a proven cause**: the retained project cannot be re-rendered, so the
specific render that showed the defect was never run against the fixed code.

**Why the retained project cannot be re-rendered, precisely.** Its track `16379` carries no voicebank and
no `proceduralRecipe`; it resolves its voice through `neuralResource` `seam.pause-experiment` version 3
with content hash `d4dd7737eedd54638dd66c09b25f0024cceb052daca447be75ac525e2c750f3d`. A filesystem search
for that hash and for the resource id found nothing. `receipt.json` agrees: `voicebanks: []`,
`proceduralRecipes: []`, `includesProceduralCandidates: false`. **The resource that produced the retained
audio is not on disk, so that exact render is not reproducible and the comparison is against a different
score and a different voice.**

**What this establishes.** The 256-sample impulse train is **not present in current output**. The
render path that produces it still runs at a 256-sample hop
(`SpectralRenderParameters::hopSize`, `spectral_classic.hpp:20`) and is still reached, so this is not the
path having been removed. The defect is bounded to audio produced before `1c6d57c6`.

**What this does not establish, and is not claimed.**

- **`1c6d57c6` is not proven to be the fix.** A different song, a different voice and a different region
  were rendered. It is the best-evidenced candidate and nothing more.
- **This is not a pitch-accuracy pass.** Zero hop-locked frames means the *specific* artefact is gone. It
  says nothing about cent-level accuracy, and the HEAD render's medians were not scored against its score.
- **SEAM-BETA-P0-08 is not closed.** It asks for a pitch-accuracy verdict on an application render. What
  exists now is: a located defect, a bounded-to-old-build negative result, and one clean end-to-end
  render that nobody has listened to.
- **The tracker's blind spot is still real.** It cannot distinguish a genuine 187.5 Hz note from a
  256-sample impulse train, and every in-frame repair tested here breaks that case. Whether that matters
  depends on whether any future render can produce such a train.

**The measurement that would close this properly** is to re-render the *retained project* with HEAD, which
requires recovering neural resource `d4dd7737...`, and to score cent-level error against the written score
on both the old and new audio. Until that resource exists, the honest statement is: **the defect is real,
located, and does not reproduce in current output on a different song.**
