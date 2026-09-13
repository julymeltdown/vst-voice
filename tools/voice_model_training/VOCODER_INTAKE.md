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
