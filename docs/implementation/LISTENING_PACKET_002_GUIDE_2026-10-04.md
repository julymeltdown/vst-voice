# Listening packet 002 — the three questions measurement could not answer

Date: 2026-10-04. **This packet is NOT_REVIEWED.** Nothing here is a musical judgement. It gives a
reviewer exactly three decisions and explains what each answer unblocks.

Packet 001 asked a pitch question that a long measurement campaign has since answered: the renderer is
correct and the shipped tracker is correct, at **0.176-centre median over 4232 frames across five songs**.
That question is closed. This packet asks the three that measurement genuinely cannot reach, because each
is a judgement about what the audio *should* sound like.

## What is in the packet

`out/listening/packet-2026-10-04-r2/`, 6 files, **33.00 seconds** of audio, all rendered through the
production export path at `9f6dadbc`.

| File | Seconds | Peak | RMS | Clipped |
| --- | ---: | ---: | ---: | ---: |
| `q1-legato-run.wav` | 7.00 | 0.0466 | 0.01551 | 0 |
| `q1-detached-run.wav` | 10.00 | 0.0430 | 0.01305 | 0 |
| `q2-single-weak-fundamental.wav` | 1.00 | 0.0138 | 0.00698 | 0 |
| `q2-pitch-range.wav` | 5.00 | 0.0483 | 0.02208 | 0 |
| `q3-phrase-with-missing-note.wav` | 3.00 | 0.0778 | 0.02097 | 0 |
| `q3-phrase-control.wav` | 7.00 | 0.0555 | 0.02298 | 0 |

Every artifact re-hashes to its recorded SHA-256, and every file carries real signal. **Use headphones**;
two of the three questions are about timbre and about a note that is *absent*.

The packet is regenerated from source rather than retained in git, because it is 12 MB of audio and the
repository does not carry binaries:

```sh
python3 tools/singing_quality/build_listener_packet_002.py    # prints the packet directory
python3 tools/singing_quality/verify_listener_packet_002.py <packet-directory>
```

The verifier re-hashes every artifact and re-checks that each file still contains the phenomenon its
question depends on, so a packet that has drifted from its claim fails rather than being reviewed.

---

## Question 1 — Is the note lead intentional phrasing or a timing fault?

**The measurement.** Every note in every render begins **0.67 to 16.67 ms before its written start**, never
late. This is deliberate: `libs/seam-synthesis/src/timing_solver.cpp:108` places a unit so its *vowel* lands
on the written start, which puts the unit's *preutterance* first. The measured median is 6.67 ms and the
recipe declares 10 ms bursts. So the lead is real, intended, and small.

**Why it still needs a listener.** A 19 ms lead is inaudible on an isolated note and can be either flattering
legato or a rushed attack depending on musical context. No metric distinguishes those.

**Play, in this order:**

1. `q1-legato-run.wav` — seven notes, MIDI 67 64 62 60 62 64 67, each 1 second with almost no gap.
2. `q1-detached-run.wav` — the **same seven notes**, with a 0.5 second written gap between each.

**The question.** The same lead is present in both. Does it read as phrasing in the legato run, as notes
arriving early in the detached run, or as not audible at all?

| Verdict | Meaning | What happens next |
| --- | --- | --- |
| **A** | Intentional legato in both | The timing solver is doing what it says. This closes as correct-by-design and the preutterance becomes a documented design property. |
| **B** | Reads as early or rushed, especially detached | The preutterance is too long for the requested articulation. The fix is a per-unit preutterance budget in the timing solver, and the change is measurable against these files. |
| **C** | Not audible | The lead is below the threshold of perception and should be documented as such rather than tuned. |

**Record:** `case: q1-lead-timing / verdict: A / confidence: high / notes: ...`

---

## Question 2 — Does the weak fundamental sound like a voice or a fault?

**The measurement.** The harmonic balance of the same pitch depends on **phrase context**, reproducibly:

| Context | h1 to h2 amplitude ratio |
| --- | ---: |
| MIDI 67, alone | **0.645** — second harmonic stronger |
| MIDI 67, alone, 0.25 s note | 0.652 — second harmonic stronger |
| MIDI 67, inside an ascending run | **122.1** — fundamental far stronger |
| MIDI 67, between MIDI 79 and 85 | 30.0 — fundamental stronger |

**That is the real finding here.** The same written pitch produces a thin, second-harmonic-dominant tone
alone and a full, fundamental-dominant tone in a phrase. The shipped extractor tracks both correctly, so
this is a **voice-design** question, not an analysis error.

**Play, in this order:**

1. `q2-single-weak-fundamental.wav` — **one second**, MIDI 67 alone. This is the case in question: the
   second harmonic is above the fundamental.
2. `q2-pitch-range.wav` — five ascending notes, MIDI 55 to 79, where the same pitch sounds quite different.

**The question.** Does the solo note sound like a voice with brightness, or thin and reedy? And does the
change between the two files read as a fault or as ordinary phrasing?

| Verdict | Meaning | What happens next |
| --- | --- | --- |
| **A** | Reads as a voice; the brightness is timbre | The recipe's spectral balance is acceptable as designed. Recorded so future work does not "fix" it. |
| **B** | Reads thin or reedy on the solo note | The recipe's harmonic profile needs a fundamental-preserving constraint when a note is rendered without phrase context. That is a recipe-level change. |
| **C** | Cannot hear a difference | The variation is below audibility and should be documented rather than tuned. |

**Record:** `case: q2-harmonic-balance / verdict: A / confidence: high / notes: ...`

---

## Question 3 — Is the rest audible?

**The measurement.** In `q3-phrase-with-missing-note.wav`, the fifth written note (MIDI 74, between MIDI 64
and MIDI 72) renders **completely silent**:

| Note | MIDI | RMS over its own written span |
| ---: | ---: | ---: |
| 0 | 79 | 0.00553 |
| 1 | 67 | 0.02428 |
| 2 | 71 | 0.03632 |
| 3 | 64 | 0.02813 |
| **4** | **74** | **0.00000** |
| 5 | 72 | 0.00720 |
| 6 | 62 | 0.00797 |
| 7 | 64 | 0.01905 |

**Why it is silent is now known, and it is not a defect.** That note carries the phonetic hint `pau` with a
lyric whose surface is literally `"pau"`. `pau` maps to `PhonemeRole::Silence`, so the note is an **authored
rest** and is supposed to render as zero amplitude. A two-variant control confirms the renderer is correct
here: with the hint changed to a vowel and *everything else identical* — same pitch, timing, region, recipe —
MIDI 74 sounds at 587.40 Hz against a written 587.33 Hz, **+0.2 cents** (see the note-74 entry in
FULL_SCOPE_BETA_EXECUTION.md and `tools/singing_quality/verify_rest_note_is_authored.py`).

The question is therefore no longer "is this a bug" but "does an authored rest read convincingly". That is a
real question about the singer, not about the analyser, and it is the only part a listener can settle.

**Play, in this order:**

1. `q3-phrase-with-missing-note.wav` — the phrase as rendered, 3 seconds, with the silent note.
2. `q3-phrase-control.wav` — the same seven pitches **without** MIDI 74, for comparison.

**The question.** Can you hear that a note is missing? Does the hole read as a deliberate rest or as a dropped
note?

| Verdict | Meaning | What happens next |
| --- | --- | --- |
| **A** | Reads as a deliberate rest or breath | The rest is convincing in context. The silence is authored and correctly rendered, so there is nothing further to fix. |
| **B** | Reads as an unintended dropped note | Not a pitch or synthesis defect, but an **authoring/ergonomics** problem: a rest is indistinguishable from a failed note. The fix is at the authoring level (making rests explicit in the UI and in score export), not in the renderer. |
| **C** | Cannot hear anything missing | The rest is inaudible in context. Documented, not blocking. |

**Record:** `case: q3-missing-note / verdict: A / confidence: high / notes: ...`

---

## What this packet does not prove, whatever the answers

- **No singer qualification.** One procedural recipe, one render family. `listeningStatus` is
  `NOT_REVIEWED` and `releaseEligible` is `false` in the manifest.
- **No pitch-accuracy change.** The five-song figure of **0.176-centre median, 98.05 per cent within 50
  cents, zero hop-locked frames** is unaffected by any answer here.
- **No clearance of SEAM-BETA-P0-08.** That blocker needs a multi-voice, multi-song corpus **and** these
  listening answers. This packet supplies the second half; it does not supply the first.
- **No rights, signing, installer or platform evidence.** Nothing audible speaks to P0-01 through P0-07.

## Why these three and not others

The measurement campaign produced four retracted findings before it converged. Every retraction came from
an instrument that was not good enough for the claim, and the pattern that emerged is recorded in
`FULL_SCOPE_BETA_EXECUTION.md`: **measurements that overturned earlier ones used stronger methods than the
ones they replaced.** These three questions are the ones left where *no* instrument is strong enough,
because each asks what the audio should be rather than what it is. That is why they are here and why they
are not answerable by another week of analysis.
