# Singer stage comparison — September 20, 2026

The existing combined-corpus acoustic checkpoint materially improves the application
song with the 512-channel vocoder. The small pause-only acoustic candidate's G5
failure is not an unconditional pitch-range limit of that vocoder. Neither candidate
is qualified, and Windows remains deferred in the README.

## Fixed inputs and changes

All audio artifacts are under `/Users/lhs/seam-corpus-pauses-2026-09-19-r1/`.
The reference is `prepared/song-023/source.wav`, 150,000 samples at 48 kHz,
SHA-256 `72442f51b9c9b2b91f956f23717394ded45af83ed4293a6c26a940cccf2ea8d3`.
The application project is `phrase-00423/baseline/project.seam`.

Both application candidates use the identical 512-channel epoch-1 vocoder graph,
SHA-256 `66d847f3763453c5a82a4c9d1194db98447a45ec5b5c92e295ad11d795dee21e`,
and the same vocabulary, trained `pau` alias, configuration and score. Only the
acoustic graph/resource identity changes. The combined candidate's acoustic graph
is `4170b7c801347bd440f063a8e79124111e8b427bb35a532fc74f4e2f98666d49`, from
`export-combined-e9`; checkpoint receipt is
`ce175925e4ae433c2e5ccc96771a78353f482d9fee40214bcffe5a1c95564b2c`.
Its bundle is `bundle-combined-e9-v512-e1`, resource version 3, manifest
`d4dd7737eedd54638dd66c09b25f0024cceb052daca447be75ac525e2c750f3d`.
No installed user resource was replaced.

## Actual results

| Signal path | Spectral distance | Measurable voiced pairs | Within 50 cents | Mean absolute cents | Unmeasurable frames |
|---|---:|---:|---:|---:|---:|
| Pause-only acoustic e9 + vocoder 512 e1, application | 2.03145 | 364 | 227 | 675.69 | 166 |
| Combined acoustic e9 + same vocoder, application | 1.62676 | 447 | 421 | 102.93 | 82 |
| Source mel + measured source F0 + same vocoder | 0.92164 | 504 | 474 | 40.75 | 28 |

Every row retains the strict pitch status `MISMATCH`. The combined candidate's
94.18% is **421/447 measurable voiced pairs**, not all 586 analysis frames and not
product acceptance. It still has 16 voicing-mismatch frames. Output RMS is 0.004935
versus source RMS 0.032452 without gain fitting, so pitch improvement is not a
complete audio-quality result.

For note-interior inspection, include only native pitch windows wholly inside
each score note (`start <= sourceFrame` and `sourceFrame + 2048 <= end`), with
voiced=true and confidence >= 0.6. On G5 (MIDI 79, samples 78,000..102,000):

- Reference median: 784.05 Hz.
- Pause-only acoustic application: 187.59 Hz, 66 eligible windows.
- Combined acoustic application: 782.79 Hz, 84 eligible windows.
- Source-driven vocoder: 783.91 Hz, 83 eligible windows.

This supports an acoustic-conditioning contribution to the earlier G5 collapse;
it does not prove the vocoder has no defects. In the source-driven control's rest,
39 interior windows are spuriously voiced with median 187.5 Hz. Application paths
have zero voiced interior rest windows because score-silence handling remains in
effect. The control deliberately does not apply that application envelope.

## Execution and artifact identities

The actual production-worker harness committed master, stems and project, then
reopened the combined candidate's saved project. It reported 300,000 interleaved
samples, 276,000 nonzero, and passed in 14.9613 seconds. This is local execution
evidence; it is not an installed DAW test or an inference-only benchmark.

- Combined master `application-combined-e9-v512-e1/master.wav`:
  `015f386a59f826904e58b0f09b065185b50428cc5effc9aca0ff5c635c0e05a3`.
- Application comparison `application-combined-e9-v512-e1/comparison.json`:
  `88b015537abc33ed7d0a7eb2249c40bc39cebe0e5558b0193ede8538d0dc734f`.
- Source-control receipt `source-vocoder-512-e1/diagnostic.json`:
  `4f289e8d06679efdb145556c1c77211d7c3e58954023846deded82926240c045`.

The source-control tool is `tools.voice_model_training.reconstruct_source_vocoder`.
It verifies source/graph/profile identities, derives full-hop mel and native F0,
runs CPU ONNX inference with one thread, validates the complete padded waveform,
then trims only final-hop padding. It retains both WAVs and native pitch tracks.
The application comparison uses `tools.voice_model_training.compare_application_export`.

Verification: full training-tool discovery ran 242 tests with one skip and no
failures (45.137 seconds). Five new tests cover exact padded geometry, invalid
padded-tail samples, retained input ownership, changed-source refusal before
inference, and preservation of an existing output directory. Source closure,
phase11 source checks and staged diff checks pass. The actual source-control
ONNX inference and native pitch extraction are additional executed evidence,
not inferred from those unit tests.

## Next implementation decisions

1. Use the combined model as the stronger engineering comparison candidate; keep
   the pause-only result as a regression baseline rather than deleting it.
2. Continue the live vocoder epoch-2 run and compare its completed reconstruction
   before choosing another export. Partial checkpoints are recovery, not completion.
3. Investigate remaining errors on designated validation sources. Song 00423 is
   explicitly in `prepared-combined/dataset-config.json`'s `heldOutSongs`; repeated
   diagnosis here must not be presented as a fresh blind qualification trial.
   No optimizer or training label was changed based on this song in this work.
4. Preserve the unresolved voice naturalness, phonetic intelligibility, realistic
   corpus and product release requirements. Generated-teacher performance cannot
   establish an original human-quality singer.
