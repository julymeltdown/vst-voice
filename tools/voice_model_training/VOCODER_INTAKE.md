# Vocoder integration intake — September 13, 2026

Status: source inspected; no pretrained vocoder admitted, downloaded or qualified.
This records the next acoustic-to-audio implementation work, not a replacement
for M2.P2/P3 or the full Beta GO plan.

## Inspected sources

- [OpenVPI vocoder releases](https://github.com/openvpi/vocoders/releases), queried
  through the GitHub API. The 2025.02 PC-NSF-HiFiGAN and 2024.02 NSF-HiFiGAN
  releases label their pretrained models CC BY-NC-SA 4.0. Do not infer commercial
  model redistribution permission from the training repository's code license.
- [SingingVocoders](https://github.com/openvpi/SingingVocoders), local inspection
  checkout `build/neural-runtime/singing-vocoders-source`, revision
  `4d0889c4c180c75ad3000cc565864656344f8190`. Its root LICENSE contains the MIT
  software license. The README separately discusses restrictions on pretrained
  vocoders. Source-code intake and weight/source-material admission are separate.
- Pinned DiffSinger revision `336cf01b57f2ad44c6b37a79cf33993043291759`,
  `deployment/modules/nsf_hifigan.py` and `deployment/exporters/nsf_hifigan_exporter.py`.

## Why the published weights are not a drop-in completion

| Feature | Current SEAM acoustic diagnostic | OpenVPI 2025.02 release |
|---|---:|---:|
| Sample rate | 48,000 Hz | 44,100 Hz |
| Mel bins | 80 | 128 |
| Hop | 256 samples | 512 samples |
| Window / FFT | 1,024 | 2,048 |
| Mel frequency bounds | 20–24,000 Hz | 40–16,000 Hz |

Changing an output WAV's sample rate does not repair mismatched mel features.
Either train a compatible vocoder or deliberately revise and retrain the acoustic
profile as a bound pipeline. Do not silently reshape bins, change hop metadata,
or substitute a public model for a commercially admitted original singer.

## Concrete implementation route to investigate next

1. **Model-only bridge first.** Instantiate the inspected NSF-HiFiGAN generator
   with 48 kHz, 80 bins and an upsample product of 256. A candidate schedule is
   `[8, 8, 2, 2]` with matched convolution kernels; test exact dynamic output
   lengths and architecture assumptions before adopting it. Seed and retain
   configuration. Random initialization is a geometry diagnostic, not a singer.
2. **Use SEAM-owned admitted data.** Feed captured PCM, reviewed F0 and the same
   log-mel profile used by the acoustic model. Preserve the existing source-group
   splits, lineage, review expiry and exact hashes. Do not call the upstream
   preprocessor's independent random validation selection on admitted material.
3. **Implement an actual training step.** The upstream task uses generator,
   multi-period and multi-scale discriminators plus `HiFiloss`. Inspect those
   implementations before adapting them. Verify generator/discriminator gradient
   isolation, finite loss/parameters and correct crop alignment. Keep validation
   material untouched and apply augmentation only after split assignment.
4. **Checkpoint all training state.** Persist generator, both discriminators,
   optimizer/scheduler states and RNG with dataset/profile/configuration identity.
   The current acoustic single-model/single-optimizer checkpoint service is not
   automatically a complete GAN checkpoint. Require resume-versus-continuous
   equality on a deterministic fixture before long-running training.
5. **Export owned checkpoints explicitly.** Upstream `export_ckpt.py` uses a
   broad `generator.` substring replacement and ordinary checkpoint loading. A
   SEAM adapter must instead verify its captured local checkpoint, use an exact
   prefix and strict state loading, and bind the generated config and graph hashes.
   Target DiffSinger's BTF mel + BF F0 → BS mono waveform interface. Inspect the
   exported graph and verify actual sample count, finite output and PyTorch/ORT
   parity under explicitly controlled stochastic input where applicable.
6. **Join the real worker path.** Validate the acoustic/vocoder profile pair,
   reconstruct PCM, trim declared padding and apply request dynamics once. Then
   exercise ordinary song rendering; a standalone export probe cannot close M2.

## Observed upstream hazards to avoid importing unchanged

`training/nsf_HiFigan_task.py` recursively chooses another random item when F0
exceeds its bound. That can hide coverage failures or recurse indefinitely if
all entries are invalid. SEAM must return a per-source diagnostic and correct
the data, not silently substitute a different sample.

The inspected default `configs/base_hifi.yaml` has a 512-sample upsample product,
44.1 kHz/128-bin features, and a from-scratch setting. Its defaults are not the
SEAM profile. No training has been launched from that configuration.

From-scratch training remains contingent on admitted source material and measured
quality. Synthetic fixtures may validate optimization and export mechanics, but
cannot establish intelligibility, female-voice identity or commercial source rights.

## Implemented architecture and export check

```sh
build/neural-runtime/diffsinger-telemetry-free-env/bin/python -m pip install \
  -r tools/voice_model_training/requirements-vocoder-model-check.txt
build/neural-runtime/diffsinger-telemetry-free-env/bin/python \
  -m tools.voice_model_training.check_vocoder_model \
  build/neural-runtime/singing-vocoders-source \
  build/neural-runtime/diffsinger-source --check-onnx
```

The optional ONNX check additionally requires the existing ONNX diagnostic
dependencies and the separately built telemetry-free runtime. This command does
not install or select an ONNX Runtime itself. The plotting dependency was added
after the ten-run runtime comparison; that environment comparison is historical,
not a claim that the extended vocoder environment still has an identical package set.

The command verifies both clean pinned checkouts before importing source and
downloads no weights. Standard NSF and MiniNSF both passed exact training-to-
deployment state loading and output equality on 1, 3, 16 and 23 mel frames,
producing 256, 768, 4096 and 5888 PCM samples respectively. A single fixture L1
optimization step changed 100/92 parameter tensors respectively with finite loss
and parameters. That is gradient connectivity, not a completed GAN trainer.

The deterministic MiniNSF graph exported after the update passed ONNX inspection
and all four PyTorch-versus-ORT comparisons at the declared 1e-5 tolerances;
maximum absolute error was `2.8001522878184915e-8`. Graph size: 168,336 bytes;
SHA-256: `8a19add81b6e11df1a235f33ec0b171fa1f254b71d8a51a4cdffc44b772082ee`.
The graph itself is not retained or admitted as a model bundle. Standard NSF's
stochastic ONNX parity is not claimed. Retained local process reports are under
`build/neural-runtime/vocoder-architecture-first` and
`build/neural-runtime/vocoder-onnx-first`, both with exit 0.

Next: implement actual admitted PCM/mel/F0 batches and the GAN training/checkpoint
state, then join the exported acoustic/vocoder graphs in the native worker path.

### Alternating GAN step

`vocoder_optimization.vocoder_gan_step` now performs a least-squares discriminator
update followed by generator adversarial, feature-matching and supplied
reconstruction losses. It rejects held-out partitions, invalid PCM geometry,
nonfinite tensors, shared parameters and incorrect optimizer ownership before
training. Discriminator inputs detach generated audio; the generator phase freezes
discriminator parameters and switches them to evaluation mode to prevent spectral
normalization buffer updates. Prior mixed module modes are restored afterward.
Both updates clip gradient norm to 1 and reject nonfinite parameters.

Failures after the discriminator update require discarding the in-memory attempt;
this operation is not transactional and cannot replace a full GAN checkpoint.

`check_vocoder_model --check-gan --check-onnx` executed the real upstream multi-scale
discriminator and a two-period `[3, 5]` multi-period discriminator on original
oscillator PCM with 48 kHz/80-bin/256-hop log-mel reconstruction. The process exited
0 in 7.492 seconds. Gradient ownership checks passed, and the post-GAN exported
MiniNSF matched PyTorch in all four dynamic cases (maximum error
`1.871376298367977e-8`). Local evidence is retained under
`build/neural-runtime/vocoder-gan-first`. This verifies one actual GAN mechanics
step, not an admitted dataset, a long training run, all discriminator configurations,
checkpoint continuation or musical quality. The broader training service still
needs those integrations.

### Complete local GAN checkpoint transport

`vocoder_checkpoint` now reuses the bounded checkpoint/receipt transport with a
joint generator/discriminator module owner and explicit dual-optimizer state.
It captures optional named schedulers, Torch CPU RNG, Python's global RNG and
NumPy's legacy global RNG. Independently created RNG instances are not captured;
a future data service must either use these owned streams or explicitly extend
its checkpoint contract. Optimizer/scheduler class identities, discriminator
count and NumPy version are bound alongside the caller's run metadata.

A deterministic alternating-GAN regression with AdamW and two learning-rate
schedulers verified exact next-step losses, generator/discriminator state,
scheduler state and RNG draws after restoring. Changed run metadata, missing
schedulers and a different optimizer type are rejected. This is a trusted local
checkpoint API, not a hostile archive importer or renewed source permission.

### Upstream continuation and two-file storage

The actual upstream GAN continuation check exposed a 553,464,348-byte checkpoint,
which exceeds the acoustic transport's 512 MiB single-file ceiling. The first
attempt failed honestly; its report is retained at
`build/neural-runtime/vocoder-gan-resume-first`.

GAN publication now uses a distinct `com.project-seam.gan-checkpoint` receipt with
`models.pt` and `training.pt`. Each file retains a maximum of 512 MiB, and the
two-file aggregate cannot exceed 1 GiB. The `maximum_bytes` option on the GAN API
is a per-file bound. The acoustic checkpoint format and ceiling are unchanged.
The GAN loader retains support for the earlier local single-file format.

Both files are captured and hash-verified before either Torch deserializer runs.
The final receipt binds individual file sizes/digests, aggregate identity, run
metadata and epoch completion. Publication failure leaves no completion receipt;
tests cover a damaged training-state file and the pre-publication authority callback.

`check_vocoder_model --check-gan --check-resume --check-onnx` then passed with the
actual upstream models: resumed next-step losses/gradient norms and every model
weight/buffer matched the uninterrupted result exactly. Post-resume ONNX export
passed all four cases (maximum error `2.2351741790771484e-8`). This run exited 0
in 7.897 seconds; local report directory:
`build/neural-runtime/vocoder-gan-resume-split`. Checkpoint aggregate identity:
`08c342b39026712cdb89d603d9b6355429af66f4a2406986fa256bf107354b9e`.
Diagnostic checkpoints are temporary; no trained singer weights are published.

An admitted-data epoch service, real corpus training, production worker integration
and quality qualification remain unfinished.

### PCM/mel/F0 batch join

`vocoder_batches.iter_vocoder_batches` now consumes the existing source-bound
supervised-batch iterator plus an explicit source-ID-to-WAV-path map. It verifies
captured WAV and PCM identities and sample clocks, uses a shared inspected
16/24/32-bit PCM decoder, and returns owned CPU float32 BFT mel, BF F0 and B1S PCM.
It reads only the selected partition's material and does not randomly substitute
sources. Batch PCM is limited to 1,048,576 samples; source WAV intake remains
bounded to 64 MiB. Later file failures invalidate the epoch, not just that item.

Offsets are in whole feature hops. A partial final hop is zero-padded according
to the acoustic profile and returns explicit `validSamples` and `paddedSamples`.
The padding participates in the current GAN's full-window losses; it is a declared
boundary convention, not evidence of additional recorded silence or extra valid
coverage. The epoch service must count only valid source samples toward completion.

The real acoustic/conditioning integration test verifies partition isolation,
exact reconstructed PCM across consecutive batches and changed-source rejection.
A separate partial-hop test verifies 500 real samples plus 12 zero-padding samples,
and that emitted tensors do not alias the caller's mel array. The shared decoder
retains the existing cross-width/sign-extension tests and acoustic feature hashes.
The reader does not grant source rights or make a saved snapshot current; fresh
review/source admission still belongs to the epoch service being integrated next.

### Reviewed vocoder epoch service

`vocoder_training_run.train_reviewed_vocoder_epoch` now connects fresh
`assemble_dataset` admission, PCM/mel/F0 batches, alternating GAN updates and
two-file checkpoint publication. It consumes all 13 captured rights/label/source
inputs rather than accepting a saved snapshot as permission. Admission is repeated
after training and immediately before the completion receipt is published.

The first service uses complete admitted phrases, at most 4096 feature frames and
1,048,576 PCM samples per phrase. Longer material must follow the existing reviewed
segmentation workflow. The update budget must cover every selected training phrase;
missing, repeated, offset or differently bound batches prevent publication. Only
valid source samples count toward epoch coverage. Deadlines, review expiry and
cancellation are checked between updates and publication phases. Optional named
schedulers step once per complete epoch and are stored in the GAN checkpoint.

Orchestration regressions cover exact source coverage, partition/offset failures,
cancellation, resumed dataset mismatch and changed identity at either final
revalidation point. These use mocked admission and optimization; they do not prove
the integrated signed-fixture execution. The next verification is the actual
fixture-policy → byte admission → GAN epoch → complete checkpoint route, followed
by real admitted material and the ordinary native singer/render workflow.

Verification note: the 54-test training-tool run had one error in the existing
`test_exited_leader_does_not_skip_descendant_group_cleanup`: after its three-second
deadline, the fixture PID file did not exist. One isolated diagnostic rerun passed
in 3.004 seconds; the epoch test also passed in isolation. This does not convert
the failed full run into a passing suite or establish the timing failure's cause.
Source-closure verification passed. No process-supervision behavior was weakened.

### Signed-fixture integrated vocoder execution

The optional `check_diffsinger_model --vocoder-checkout` path now reuses the
actual synthetic fixture's signed rights/label policies, captured source bytes,
conditioning and targets. It executes first and continuous second GAN epochs,
restores the first complete checkpoint, then executes the resumed second epoch.
Epoch results and every generator/discriminator state tensor must match exactly.
The temporary continuous-reference checkpoint is released before publishing the
resumed checkpoint; all three publication paths still execute.

The retained `build/neural-runtime/vocoder-reviewed-epoch-write-errors/report.json`
records one complete exit-zero run in 37.789 seconds. Its `001.stdout` SHA-256 is
`dc7ef4b3765325e537ed4101e5b572ecf61fd835ac5ba6bd4aa3216598bcb97c`.
The report confirms exact continuation, a 553,464,348-byte GAN checkpoint,
complete 4,096-valid-sample fixture coverage, and the existing three-case native
acoustic export smoke test. This is synthetic engineering evidence, not a trained
singer, retained production checkpoint, listening qualification or Beta GO.

Two preceding integrated attempts failed with ENOSPC and remain retained under
`vocoder-reviewed-epoch-first` and `vocoder-reviewed-epoch-bounded-storage`.
Available storage changed between attempts; the successful run does not establish
that machine capacity is now adequate for sustained training. No build artifacts
were removed. GAN storage now preserves the original writer failure when Torch's
ZIP finalizer masks it with an `unexpected pos` error. Regressions verify ENOSPC
identity, the real serializer's file-size bound, and absent completion receipts
after failure. Eight focused vocoder tests and source-closure verification passed;
this does not supersede the earlier failed full-suite result.

### Persistent checkpoint-to-vocoder export

`python -m tools.voice_model_training.export_vocoder` accepts `--checkpoint`,
`--receipt-sha256`, `--profile`, `--profile-sha256`, `--trusted-checkout` (the pinned
DiffSinger deployment checkout), and `--output` (new directory). It captures and
validates both local GAN files through the existing transport, verifies the
reviewed-epoch architecture/profile/dataset/objective bindings, strictly loads
the generator, exports and compares dynamic-length ONNX, then writes
`vocoder.onnx` and publishes `export.json` last. No optimizer state is stripped
from the input checkpoint or substituted with freshly initialized state.

The current export family is the explicitly tested deterministic 48 kHz/80-bin
MiniNSF configuration. Other configurations fail explicitly; this is not a claim
that its small diagnostic architecture is the final singer model. The command
executes trusted upstream Python and accepts trusted local Torch checkpoints,
not arbitrary third-party archives. Source rights are not revalidated by export;
the receipt does not grant model-bundle admission or singer/release qualification.

The integrated diagnostic now launches this command on the resumed reviewed
fixture checkpoint and compares the printed receipt, published receipt and graph
bytes. `build/neural-runtime/vocoder-checkpoint-export-first/report.json` records
exit zero in 43.506 seconds; stdout SHA-256 is
`c0619d5150ab0e63a0705fd54d97078bbfdc40b16f59f5d285b3ecedd65d66b3`.
The graph was 168,336 bytes with SHA-256
`696c835272c44afde36355758c146d7f09fd2beec18dd663a168f01466ddc8a1`.
Torch/ORT comparisons covered 1/3/16/23 frames, including unvoiced conditioning;
maximum absolute error was 2.981e-8. The command produces persistent artifacts,
but this diagnostic deliberately invokes it inside a temporary fixture: only
the diagnostic logs survive. No production singer checkpoint was created.

Verification: the full training-tool discovery run passed 57 tests in 17.397
seconds in the telemetry-free Torch environment; source closure and diff checks
also passed. This is a new passing run, not removal of the earlier timing-failure
record or proof that its intermittent cause has been repaired.
