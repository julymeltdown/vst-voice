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
The existing 512 MiB checkpoint ceiling remains; larger GAN configurations need
an explicit storage strategy rather than silently raising limits. Full upstream
GAN continuation and an admitted-data epoch service remain to be integrated.
