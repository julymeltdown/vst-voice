# Historical tool lifecycle and input contracts

Reviewed statically 2026-10-11 on `432d6bb6563fee53b298d52a86db9316d489a9b2`. No tool was imported to discover its interface, no checkpoint/corpus was substituted, and no packet or training output was generated. Source revision below means the last Git commit changing the tracked script; it is not proof that a historical packet captured those exact bytes.

| Tool | Lifecycle | Required inputs and output behavior | Supported alternative |
|---|---|---|---|
| `build_listener_packet_002.py` | Archived reproduction; **not import-safe** | Fixed repository root, `/tmp/seam_score/rerender`, `/Users/lhs/seam-corpus-large-2026-09-19/phrase-00022/baseline`, recipe `7688afab747661d59ec1423a9a101114d61956bd062c464c45a74f8fb26f893e`, and Q3 `/tmp/seam_voices2/out/original/master.wav`. Creates `seam-listening-*` via `mkdtemp` and renders/copies/writes at module top level. Missing historical data must not be replaced silently. | Packet 003 takes explicit inputs, but is partial and currently refuses Q1 on the dry procedural route. It does not recreate r2. |
| `listener_packet_003.py` | Current explicit-input diagnostic; **partial** | `build --request ABSOLUTE_REQUEST --output NEW_DIRECTORY`; verify requires recorded manifest SHA256. Captures supplied projects/recipe/renderer/source/tool identities, refuses overwrite and retains failed output. Current procedural Q1 markers lack the required lead properties, so integrity PASS does not produce a valid packet. | Reconcile the real production/sample timing adapter and objective Q1 evidence before obtaining manifest-bound human answers. Do not relax Q1 or reshape audio to pass. |
| `acoustic_encoder_denoiser_crossover.py` | Archived fixed-checkpoint experiment | Explicit configs/targets/roots/conditioning/trusted checkout/output and digests; internally fixed control/treatment e9 receipt pins below. Creates a new output directory before later input admission; refusals can leave partial diagnostics. Uses three fixed training phrases, seed 933 and first-step 2×2 encoder/denoiser interventions. `main` guard exists; main loads checkpoint bytes. | No portable replacement for this exact experiment. Use current explicit-input training/evaluation commands for new work; a deliberate experiment port requires a separate scope and tests. |
| `acoustic_sampler_trace.py` | Archived fixed-checkpoint experiment | Same explicit argument families and fixed e9 pair; three fixed training phrases, seed 933, ten-step trajectory and declared clamp diagnostic. New output required, but created before later admission; failures may leave partial output. `main` guard exists. | No exact portable replacement. Current frozen evaluation/campaign commands are the route for new evidence, not a substitute for this historical trace. |
| `acoustic_gradient_probe.py` | Archived fixed-checkpoint experiment | Same arguments plus fixed e8 parent/e9 pair; three fixed training phrases, seed 933 and timesteps 0/495/999. New directory created before later validation. Teacher-forced instantaneous gradients are not optimizer-update or free-running sampler proof. `main` guard exists. | No exact portable replacement. Any port must expose checkpoint/receipt choices and validate before output creation without weakening admission. |

## Fixed checkpoint and source bindings

All three acoustic diagnostics require the clean trusted DiffSinger checkout revision from `train.REVISION`: `336cf01b57f2ad44c6b37a79cf33993043291759`. They are tied to `/Users/lhs/seam-corpus-pauses-2026-09-19-r1/`:

| Selection | Fixed directory suffix | Expected receipt SHA256 |
|---|---|---|
| control, all three tools | `acoustic-flatness-control-e9-r1` | `fc73c90f6f451fb686b9d9a8163147d212abe8986430d59959541bb00812c5c4` |
| treatment, all three tools | `acoustic-flatness-unvoiced-e9-r1` | `a40f2383b68e1a2d62741f6170e4dccf360da72b07d2f7d8e95d0e633603dc76` |
| e8 parent, gradient tool only | `acoustic-breathiness-r1/epoch-000008` | `04f72263b700804cc4557879208def3ca4157d68955dea516b179140f755007e` |

The pinned checkpoint receipts, exact admitted dataset/config/target bytes, source rights/label anchors and dependencies are part of reproduction. A newer checkpoint or corpus is a new experiment. These tools do not update checkpoints or qualify a singer; inspect their declared read-only scope and before/after checks rather than inferring musical acceptance from output files.

## Tracked source inventory

These hashes bind the inspected tracked sources, not missing historical output. Archived packet audio/manifests/source snapshots remain immutable at their original identities. Do not rewrite a captured script to add this notice.

| Tracked script | Last source commit | SHA256 on review date |
|---|---|---|
| [build_listener_packet_002.py](../../tools/singing_quality/build_listener_packet_002.py) | `e0b16309146435384830586f6c94da18cfa765d5` | `ae943ee4849d24baf9a79fdc56dca744a2614efe9863156e5c41cdb71a61d1ff` |
| [listener_packet_003.py](../../tools/singing_quality/listener_packet_003.py) | `b961e7547259ebb6c0553def9381910a52e9f19a` | `30aef2357ffd1040c2a7c24d4b43da22c2a662a8a5e00e527d50d1afc4f9f4b7` |
| [acoustic_encoder_denoiser_crossover.py](../../tools/voice_model_training/acoustic_encoder_denoiser_crossover.py) | `f6bb2d9d0a76c0f2bbcaa2f5b1555848f0e9cca7` | `a53d077fe84649d0df0d03ef3dbbbe10f291d9c67bd2a9f2774dce5b4477d212` |
| [acoustic_sampler_trace.py](../../tools/voice_model_training/acoustic_sampler_trace.py) | `dd28b0a6edc4a0c6d3d8e2f7f70d5f91f552fab0` | `e80e8035d770edd57f8f0a541dfe6ba9a576bc65c8edbf89fc214e6bf535114b` |
| [acoustic_gradient_probe.py](../../tools/voice_model_training/acoustic_gradient_probe.py) | `6c1a4eb06e9ece0d015b74fbb590f180ad29e931` | `34e9dee8102d6632681e203cb50fcc39f0d3409f42e03483a0435caaf599c1ed` |

[Packet 002 historical guide](LISTENING_PACKET_002_GUIDE_2026-10-04.md) retains its original observations. [Singing-quality guide](../../tools/singing_quality/README.md) and [training guide](../../tools/voice_model_training/README.md) route current work. This inventory is static documentation, not execution evidence, approval of training sources, human listening or release admission.
