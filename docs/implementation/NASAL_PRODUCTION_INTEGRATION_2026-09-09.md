# Nasal recipe production integration

## Outcome

The schema-3 nasal model is now covered through scheduled song rendering,
cache replay/invalidation, mixed frication/vowel Final export, candidate baking
and the existing producer import/generation workflow. This turn strengthened
integration evidence and retained real generated comparison audio; it did not
introduce another DSP model or accept an additional Full-Scope unit.

## Authoritative checks

`tests/test_performance_snapshot.cpp` adds an oral-to-nasal pose transition split
100 frames into the second note. Sequential scheduler chunks reconstruct the
whole Final render exactly. Replaying cached chunks performs no additional
procedural DSP. Editing antiresonance changes the render content identity,
requires fresh DSP and changes the second note while preserving the first note.

`tests/test_export_service.cpp` now exercises nasal and frication together in its
existing comprehensive articulated-candidate workflow. The oral comparison uses
schema/resource version 2; the nasal recipe uses version 3. Their first 100 ms
of stereo consonant-noise output are identical, while total PCM differs after
the vowel starts. Actual Float32 export matches the Final renderer. Candidate
loading preserves typed planned gestures and matches phrase-pipeline PCM; its
existing malformed-input, lineage, resumable generation and producer-import
checks continue running against the nasal recipe. Imported takes remain
MarkerReview, not automatically approved. Recipe schema 3 and candidate schema
2 are distinct contracts and must not be conflated.

During test implementation, two assumed field names did not compile. They were
corrected against the actual snapshot `contentHash` and completion `pcm` API.
The unchanged-consonant assertion counts interleaved channels explicitly rather
than accidentally checking only half the intended stereo duration.

## Verification result

The affected targets rebuilt successfully. Final focused CTest: **3/3 PASS,
24.18 seconds**. Snapshot cases: **43/43**; export cases: **23/23**;
core: **840/840**. `git diff --check` passed. No production implementation files
changed in this turn; the preceding full 119/120 result remains historical,
not a newly executed full run. Source closure is still unresolved.

Raw final execution is retained under `evidence/nasal-integration-2026-09-09/`.

## Generated comparison

- [Oral reference](evidence/nasal-integration-2026-09-09/oral.wav)
- [Nasal coloration at coupling 0.8](evidence/nasal-integration-2026-09-09/nasal.wav)

Both were produced by the actual Final export service, not an external TTS or
post-processed substitute. Each is 0.5 seconds, stereo Float32, 48 kHz, 24,000
frames, finite and non-silent, with zero clipped samples. Measured peak is
0.0901484862 in both; RMS is 0.0116003733 oral and 0.0120402243 nasal. No loudness
normalization was added to the comparison. Original recipe inputs are retained
beside the WAVs for reproducibility.

This is a short synthetic target-/sa/ engineering fixture, not native-speaker
intelligibility evidence, a finished female singer or an approved voicebank.

## Preservation and next capability

Comparison against the 1,942-file `session-preservation-Sdna9A` snapshot found no
missing captured files; only the two intended test files changed before evidence
collection. No staging, commits, pushes or real source/reviewer approvals occurred.

The next voice-development gap remains actual consonant articulation rather than
more coloration: nasal consonants, closures and bursts require explicit timing,
excitation/tract behavior and their own render/production proof. U19/U20 and all
remaining Full-Scope Beta GO requirements remain open.
