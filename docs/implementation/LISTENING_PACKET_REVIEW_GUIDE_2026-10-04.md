# Listening packet 001 — reviewer guide and the pitch decision it unblocks

Date: 2026-10-04. This packet is **generated and retained but NOT_REVIEWED**. Nothing in this
document is a musical judgement. It tells a reviewer what to play, what to decide, and what their
answer unblocks.

## What exists now

`tools/singing_quality/listening_packet.py` had never been run. It was run on 2026-10-04 from a
clean committed tree at `9307bbf5` and produced `packet-001`:

| Field | Value |
| --- | --- |
| Source commit | `9307bbf5cd8d92077f20cb9b0f83fbaea4fa00b5` |
| Cases | 11 |
| WAV outputs | 66 (6 per case: baseline plus candidate variants) |
| Total audio | **199.50 s** |
| Retained size | 68 MiB across **335 artifacts** |
| Hash verification | **335 of 335 artifacts re-hash to their recorded SHA-256** |
| `listeningStatus` | `NOT_REVIEWED` |
| `releaseEligible` | `false` |

Signal is present in every output: minimum peak **0.0087**, median peak **0.0777**, maximum peak
**0.1573**, minimum RMS **0.0028**. **Zero** near-silent outputs. This is audio worth listening to,
not a packet of empty files.

The manifest already states the limit of the evidence in its own words:
`bankComparison: "NOT_RUN: candidates are generated audio, not installed sample-bank renders"`.

## The decision this packet exists to settle

The pitch tracker has a known defect: on real sung audio, frames immediately after a voicing gap
are frequently reported an exact octave away from the true note (see
`PITCH_TRACKER_OCTAVE_ERROR_2026-10-04.md`). Three repairs were implemented and measured, and
**all three were rejected**: two did nothing, and the third discarded 17 per cent of the voiced
track to remove 7 wrong readings. Choosing between "an octave low" and "unvoiced" changes what every
downstream stage sees, and **that choice is a listening question, not a code question.** No automated
metric can settle it.

## How to review, in priority order

### 1. First, the pitch question (this is the one that unblocks code)

Play, in this order:

1. `melisma/baseline/master.wav` — one sustained note across several beats. This is the cleanest
   reference for what the tracker should say on a held note.
2. `events/baseline/master.wav` — note-to-note movement with rests. **This is the case that matters**:
   the defect appears in the frames right after a gap.
3. `rhythm/baseline/master.wav` — a phrase with a held syllable, a short note and a nasal.
4. `unfamiliar-song/baseline/master.wav` — the full 96-second melody. Listen for any note that jumps
   to a doubled or halved frequency exactly at the start of a note, then stays wrong for the rest of
   that note.

**The question to answer, per note transition:** after a rest, does the sung note continue at the
same pitch the ear expects, or does the audio itself jump an octave? Record which of these you
observe:

| Verdict | Meaning | Consequence |
| --- | --- | --- |
| **A — the audio jumps** | The renderer is genuinely changing pitch at the boundary | Fix the renderer, not the tracker |
| **B — the audio is correct, the tracker is wrong** | The defect is in the extractor only | The octave-reporting fix is safe to pursue |
| **C — cannot tell / ambiguous** | The boundary is inaudible or the note is too short | Keep the current behaviour; the case is not decidable and no fix should be made on it |

Answer **B** only if you are confident the sung pitch is continuous across the gap. If the audio is
genuinely ambiguous, say **C** — that answer is as useful as A or B, because it tells us not to
spend more effort on this path.

### 2. Then, intelligibility and vocal behaviour

| Case | What to check |
| --- | --- |
| `vowels` | Can you distinguish the five vowels? Is any of them the same sound? |
| `articulation` | Are the 14 syllables distinguishable, or do several collapse to the same sound? |
| `stops` | Do the stops actually cut (k, t, p) rather than running into the vowel? |
| `affricates` | Is there a clear closure-then-frication, or one blended gesture? |
| `glides` | Do the glides move in pitch, or stay flat? |
| `nasals` | Is `ん` nasal in context, or silent/missing? |
| `range` | Does the pitch actually rise and fall across `ま み む め も`? |
| `unfamiliar-song` | Overall: does this sound like someone singing the kana you can read? |

**Expect failure in at least one place.** The bank these renders use is
`demo.public-domain.human.production`, which is **one spoken public-domain recording reused for all
eight phoneme labels** — the manifest names it a *pipeline fixture*, not a complete phoneme bank. If
several syllables sound alike, that is the bank, not a tracker bug. Record it as a **bank** finding
and do not attribute it to the DSP.

## Recording your answer

For each numbered case, record:

```
case:            events
verdict:         B
note (time, Hz): 1.85s, expected ~330 Hz, heard ~165 Hz
confidence:      high / medium / low
notes:           the note after the rest is clearly a fourth above, not an octave below
```

## What each answer unblocks

| Answer | What happens next |
| --- | --- |
| **B** on the pitch question | The tracker fix becomes safe. A continuity term can be implemented and measured against real audio instead of a synthetic case, and the 17 per cent voiced-frame loss is avoidable by choosing a different rule than the rejected third attempt. |
| **A** | The renderer is the defect. The extractor is vindicated and the octave errors are honest measurements, which moves the work to pitch marking at note boundaries. |
| **C** | The pitch decision stays open and unblocked-by-evidence; no change is made. The intelligibility answers below still apply. |

## What this packet does not prove, regardless of the answers

- **No singer qualification.** The bank is a fixture, and `listeningStatus` is `NOT_REVIEWED`.
- **No neural singing.** These renders come from the procedural path; the neural path uses arithmetic
  fixture graphs, and there is no trained model in the repository.
- **No beta readiness.** This is a diagnostic artifact retained under a named commit with a recorded
  binary hash. It is evidence for review, not a qualification record.

