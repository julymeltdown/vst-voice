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
