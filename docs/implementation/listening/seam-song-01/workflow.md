---
title: M1 evidence record — original singer song 01
date: 2026-09-16
status: engineering demonstrated; creator workflow and musical review open
baseline_commit: 15a304bd6db0caa596cbc6f28603ccdb350a361e
implementation_performed_by_this_document: false
---

# M1 evidence record — original singer song 01

This record states what the M1 work demonstrated and what it did not. It is the status boundary for the first integrated deliverable, not an acceptance decision.

```text
Engineering:       DEMONSTRATED
Creator workflow:  NOT_OBSERVED
Musical review:    NOT_REVIEWED
```

## What was demonstrated

An installed, signed original procedural singer is selected through the application's own command, sings a 24-note Japanese lyric song written through the real add-note command, is tuned through the expression lane, and survives undo, redo, save, a fresh-session reopen and export. The export is decoded and required to carry real signal rather than a header, and the reopened export is required to equal the tuned export, so a re-render that silently lost the installed singer would fail rather than pass.

| Evidence | Command | Result |
|---|---|---|
| Whole-song journey | `ctest -R '^seam_original_singer_song_journey_tests$'` | 2/2 |
| Per-carrier capability matrix | `ctest -R '^seam_renderer_capability_tests$'` | 6/6 |
| Singing-route resolution | `ctest -R '^seam_singer_route_tests$'` | 8/8 |
| Regression across the project | `ctest -j 8` | 172/172 |
| Source closure | `python3 scripts/verify_tracked_source_closure.py` | `SOURCE_CLOSURE=PASS` |

The song fixture is checked in at `assets/pilots/seam-song-01/recipe.json` and is the production encoder's own output. The journey refuses to run when that checked-in definition and the code recipe disagree, so the retained material cannot come to describe a singer nobody rendered. Regeneration is deliberate and documented in that directory's README.

## Where to listen

The first retained M1 material is at `/Users/lhs/Downloads/seam-listening-artifacts/2026-09-16-song-01/`. Start with `baseline.mp3` for the untuned song, then `tuned.mp3`, which carries one drawn formant curve. The `.wav` masters beside them are what the manifest hashes and what any measurement must use; the MP3 files are lossy auditions only. The manifest records the route, the note count, the master digests, `listening: NOT_REVIEWED` and `creatorWorkflow: NOT_OBSERVED`, so the material cannot be mistaken for an approved result.

This material is the workflow song. It is deliberately not the short perceptual comparison set: the retained 16-second diagnostic packet under `docs/implementation/listening/2026-09-15-d1-02/` remains the right size for an intelligibility and identity judgement, and a 40-second song is kept out of repeated listening comparisons so a long melody does not confound which failure class a listener is hearing.

## Working length

The song is 48 notes over 40.5 seconds at the default tempo, which is inside the 30-to-60-second range the plan asks for. The earlier draft was 15.5 seconds and was extended deliberately rather than left short: a song shorter than the working length would let a tuning session finish before the creator had to loop, save and resume, which is exactly the workflow this milestone exists to exercise.

The journey also found and closed a real defect in the owning layer: every committed export pushed a metadata-only renderer-provenance record onto the creator's undo stack, so the creator's next undo appeared to do nothing and repeated exports stacked invisible entries. The record is now written only when it differs.

## What is not observed

**No person has performed this session.** Every result above is a replay of application commands driven by a test. That is engineering evidence: it proves the operations compose and that their observable contracts hold. It is not the creator workflow observation, which requires a person to open the application, enter notes and lyrics, hear the result, tune, save, reopen and export without a developer beside them.

The first intended observer is the project owner. That observation is recorded here as `NOT_OBSERVED` because it has not happened, and it does not count toward the five independent pre-GO creators M6 requires; that needs separate participation meeting the canonical independence and task protocol.

Workflow metrics to capture when it does happen, and the reason each is a workflow measure rather than an audio-quality measure: task completion, the number and identity of blocking interactions, time to first sound, and edit-to-audible latency. None of these says whether the voice sounds good, and none may be reported as one.

## What is not reviewed

**No listener has judged this song.** No transcript, musician vote or reviewer signature exists, and none has been produced on anyone's behalf. The recipe's resonance, frication, plosive and approximant parameters are development screening values, not phonetic qualification for Japanese. The lyric material is a development screening phrase, not a language-reviewed corpus.

The retained diagnostic packet at `docs/implementation/listening/2026-09-15-d1-02/` remains `NOT_REVIEWED` and is unchanged. Its short comparison material is the right size for a perceptual judgment; the full 30-to-60-second workflow song is deliberately kept out of repeated diagnostic comparisons, because a longer song multiplies listening cost and confounds which failure class a listener is hearing.

Audition-only MP3 conversions under `/Users/lhs/Downloads/seam-listening-artifacts/` exist for convenient playback. They are lossy, so spectral, level, pitch and qualification measurements must use the bound WAVs instead.

## What happens next

1. A person performs the session and this record gains an observed result or an actionable failure. A blocking interaction found here precedes any DSP work, because a voice nobody can tune is not rescued by better consonants.
2. Once that observation exists, the short perceptual material is reviewed and the two-cycle acoustic repair budget in the R3 plan applies to whatever failure class the listeners name.
3. The neural input path advances independently. The generated-teacher adapter now lets the pipeline be exercised from this project's own renders, and it records its phone spans as renderer intent rather than acoustic truth.

Neural singing remains mandatory under R9 and is untouched by M1. No claim of intelligibility, identity, musical quality, release readiness or Beta GO is made here.
