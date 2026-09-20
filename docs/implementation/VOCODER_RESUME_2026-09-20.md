# Vocoder epoch 2 continuation — September 20, 2026

Windows support remains TODO as recorded in the root README. Current development
and verification use macOS arm64. This entry records the resumed training work;
it does not establish singer qualification or Beta GO.

## Recovery evidence

- The previous execution handle `56187` no longer exists, and process inspection
  found no running vocoder trainer before starting this continuation.
- `vocoder-512-segments-r3/epoch-000001/checkpoint.json` matches the recorded
  SHA-256 `cec64e7c5adb3ec81cceaa7c81046ca8d62447e0126f49df2bba639fe536dd94`.
  Its model and optimizer files are present; the trainer's normal restoration
  validates them before entering epoch 2.
- `vocoder-512-segments-r4` contains only an empty reconstruction directory.
  It has no saved update to resume. That directory was preserved.
- Run r5 passed restoration and reported `startingCompletedEpochs: 1`, then
  `epoch-started` for epoch 2. This proves restoration, not epoch completion.
- The trainer then reported update 1 of 2,804 at 22.27 seconds, with generator
  loss 30.0891 and discriminator loss 0.7371. Optimization has resumed; these
  training losses do not establish held-out quality.

All corpus and run paths above are relative to
`/Users/lhs/seam-corpus-xl-2026-09-19/`.

## Active invocation

Execution session: `53194`; PID observed at startup: `49160`. These are historical
handles, not proof of future liveness. Recheck the handle and process before any
restart. Output: `vocoder-512-segments-r5`.

The invocation uses `build/neural-runtime/diffsinger-model-env/bin/python`,
`tools.voice_model_training.train_vocoder`, the existing
`prepared/vocoder-training-512-segments.json`, `prepared/dataset-config.json`,
`prepared/targets.json`, `prepared/conditioning`, and `prepared` as source root.
It retains the 512-channel profile, one CPU thread and captured environment.
The trusted checkout is `build/neural-runtime/singing-vocoders-source` and native
pitch extraction uses `build/release/seam_voicebank_cli`.

Run bounds: one additional epoch, 21,600 seconds, 1 GiB complete-checkpoint budget,
partial checkpoints every 50 updates, two retained partial checkpoints, and a
3 GiB recovery budget allowing the temporary third checkpoint during publication.
The external epoch-1 input is not a retention target.

Policy arguments use canonical JSON trust-anchor digests, which differ from the
file-byte hashes in the dataset configuration. The first launch supplied byte
hashes and was rejected before training. The corrected invocation uses the
existing review policy identities and passes admission:

- Rights: `ec1f39f07b9b881565956dbed4f4abd8a1375284b0813ba152132e035cae0185`.
- Labels: `e3f2fe1b4c270611cdd4cfc22a035bacdce78e4d6deb90c57997c3aae379e81b`.

## Next evidence required

1. First partial checkpoint published at update 50 after 251.68 seconds:
   `vocoder-512-segments-r5/recovery-000002/update-000050`, receipt SHA-256
   `26dfbaf43ec0be654b508dde96c80dadf2e6377ce29591985b1c2728178a4012`.
   Reported retained binary size: 720,450,469 bytes. This is partial progress,
   not completion of epoch 2.
2. Require the complete epoch receipt and held-out reconstruction results before
   calling epoch 2 finished. A live process or reconstruction directory is insufficient.
3. Compare epoch 2 against epoch 1 on the same held-out sources, then export and
   evaluate the selected checkpoint through the actual song-rendering path.
4. Keep the generated-teacher corpus limitation and strict pitch diagnostic visible.
   Training progress does not establish a qualified original singing voice.
