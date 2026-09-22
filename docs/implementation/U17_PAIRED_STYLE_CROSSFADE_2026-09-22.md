# U17 ordered sample-style PCM crossfade

Date: 2026-09-22. Baseline: `b89ae04c`. This is a connected implementation
increment in U17/U6, not acceptance of all U17 or full-product Beta GO.

## User-visible capability

The native Style / structural coverage sheet now offers **Configure PCM style
crossfade** after the declared styles. Choose an explicit primary and secondary
style, adjust the secondary amount in five-percentage-point steps, and Apply.
The draft does not alter the project until Apply. Cancel, Refresh, source
freshness, undo and redo retain the existing editor transaction semantics.
The panel shows secondary structural coverage separately from audio approval.

An accepted regional StyleBlend performance lane can animate this ordered pair
through the existing proposal/acceptance commands. This increment does not add
a generator that produces style lanes or a freehand blend-curve editor.

## Product and signal contract

- This is **linear PCM crossfade**, `y = (1-b)A + bB`, not arbitrary-bank
  cancellation-free timbre morphing. It neither moves pitch intentionally nor
  silently aligns phase, flips polarity, normalizes level, or substitutes gain
  for a missing second source.
- Both styles belong to the same frozen sample manifest and exact selected
  bank identity. The normal application still owns trusted bank resolution.
  Each selected audio file and alignment sidecar is separately byte-bound.
  Changing the selected bank version/hash requires clearing the pair first;
  both styles must then be selected and checked against the replacement.
- The pair must have matching token partitions and destination timing,
  including note timing, nucleus/end/start anchors, voicing, explicit edits,
  and short-transition decisions. Different source landmarks are allowed, but
  multi-nucleus sources require valid audio-bound landmarks on **both** sides.
  Forced unit choices are not dropped to make a secondary plan succeed.
- Both arms use the same compiled performance. Matching Classic PSOLA or
  Spectral Classic renderers are supported; mixed renderers, Raw, Stretch and
  fallback output are refused. Procedural/neural pairs remain unsupported.
- The pair composer checks the complete rendered absolute timeline before any
  owned-output crop. Energy-active 50 ms windows on an absolute half-window
  grid reject correlation below -0.5. Windows where either arm's mean square
  is at most 1e-12 are not treated as phase observations. Opposite polarity,
  destructive delay and tested drifting-phase pairs fail rather than yielding
  an apparently successful silent blend.
- This window rule is a provisional engineering defect guard, **not** a
  perceptual compatibility certificate, proof against every possible local
  cancellation, or a frozen Beta-quality threshold. Listening qualification
  and resource-specific blend/range claims remain separate.
- Zero and one preserve the selected standalone arm's exact samples and
  extent. Both resources and compatibility checks still apply at endpoints.
  Identical arms are not boosted. Intermediate output is aligned by absolute
  frames; legitimate absent support contributes zero, never a truncated or
  stretched substitute for a missing source.

## Implementation

- `VoiceStyleSelection` carries optional `VoiceStyleBlend {targetStyleId,
  amount}`. Validation requires distinct explicit styles and a finite amount
  in [0,1]. Project schema **19** stores the ordered pair; old projects retain
  their single style. Older schema tags cannot smuggle in pair fields.
- Performance compiler **19** reads the track default before any active-note
  early return, so preutterance/gaps/tails retain that value. Accepted lanes
  use existing absolute half-open scopes, interpolation and source offsets.
  Manual Replace restores the default, including zero; proposals alone are
  inert. No per-sample song array is introduced.
- `SampleSingerResource::blendStyle` owns the secondary plan, selected hashes
  and decoded sources. The snapshot factory freezes both arms through the
  same source/alignment loader and budgets. Ordered identities, project intent,
  source metadata, both sets of hashes and pair algorithm revision **1** enter
  content identity. `splitOwnedOutput` retains and hashes both arms.
- Pair timing is checked before DSP. Aggregate requested unit work is capped
  at 32 Mi frames. Completed arm buffers together, and their union timeline,
  must fit 32 Mi frames before mixing. Existing per-renderer limits still
  apply. Frozen encoded/decoded budgets (256/512 MiB) and alignment budget
  (4 MiB) are shared, not independently granted to each style. Selected plans
  together are capped at 8192 entries; chunk metadata counts both arms.
- Neither source can publish alone after secondary failure or cancellation.
  Underlying renderers do not advertise StyleBlend; the pair composer consumes
  the requirement once. Normal unsupported snapshots reject explicit intent
  even when neutral or manually overridden.
- Both placement sets carry their style, and pipeline results retain both
  plans/timings and measured window compatibility. Region source counts include
  both arms; cold/cache renderer provenance is `seam.pcm-style-crossfade.v1`.
  Published performance identity retains the pair/default alongside its primary
  style. Saved/exported project data uses the same schema codec.
- The captured procedural-teacher importer now accepts schema 19 single-recipe
  projects as well as 17/18, but refuses a sample-style pair on that route.

## Verification

The dedicated suite covers real PSOLA and spectral endpoint PCM/pitch; identical,
opposite, delayed, drifting, unequal-level and silent arms; nonfinite/cancelled
input; command/schema/undo semantics; ordered identity and both-source freezing;
missing styles/explicit-style bypass; incompatible timing/renderers; generated
ramps with a source offset and narrow manual island; source tails; owned chunks;
save/reopen; stale jobs; both multi-nucleus sidecars; cache provenance; and
aggregate frame/work refusal. The native controller regression follows pair
selection, amount adjustment, Apply, disable, undo and redo.

Initial connected testing exposed a malformed PSOLA fixture without required
pitch marks. The fixture was repaired with period-derived marks; production
validation was not relaxed.

Final strict Release and Debug selected-target builds pass. Release CTest passes
9/9 selected targets (977 case executions, 22.85 s); Debug passes 6/6 focused
targets (123 case executions, 30.97 s). These totals are suite executions, not
deduplicated coverage or acoustic qualification.

| Suite | Release | Debug |
|---|---:|---:|
| style blending | 10/10 | 10/10 |
| native style coverage/pair workflow | 10/10 | 10/10 |
| performance compiler | 22/22 | 22/22 |
| performance snapshots | 48/48 | 48/48 |
| neural worker protocol | 26/26 | 26/26 |
| neural render admission | 7/7 | 7/7 |
| monolithic regression | 831/831 | not rerun |
| authoring render coordinator | 19/19 | not rerun |
| installed original-singer song journey | 4/4 | not rerun |

Logs: `build-u4-macos/u17-style-final-build.log`,
`build-u4-macos/u17-style-final-ctest.log`, and the same filenames under
`build/debug/`. CTest's `Testing/Temporary/LastTest.log` holds case-level output
at this checkpoint. `python3 -m unittest
tools.voice_model_training.test_prepare_captured_teacher` passes 12/12.
`verify_tracked_source_closure.py` and `git diff --check` pass.

Developer 2 APPROVED this bounded increment at
`bdb62019bcc2dca1306709de2076c6917ff32cb9` (baseline `b89ae04c`), with no
reproducible blocking findings. The reviewer independently reran 142 Release
cases (the six focused suites plus authoring coordinator), 123 Debug cases,
and 12 captured-teacher Python tests without rebuilding. Source inspection
covered the normal project-renderer/export route; no fresh live AppKit or
export-UI journey, installed-host/Windows check, or monolithic rerun is claimed.
The earlier empty review completion was only a role acknowledgement, not
approval; this is the first completed implementation verdict.

Approval covers documented PCM composition and its provisional defect guard,
not cancellation-free morphing, singer/listening qualification, full U17, or
Beta GO. The reviewer recommends a further multi-placement paired fixture with
different source attacks/overlaps, checking exact endpoints, seam/transient
landmarks, and whole-versus-owned output through the project/export path.

The 480x320 native scene was rendered and inspected at
`build-u4-macos/style-blend-pair.png`: pair labels, amount actions and Apply/Cancel
fit without visible overflow. This is a scene/controller check, not a fresh live
AppKit interaction or independent accessibility sign-off.

## Remaining obligations

U17 still requires contextual acoustic-join selection, selection rationale,
listener/resource qualification, and any broader pairing behavior promised by
the final resource matrix. The new pair guard does not qualify a voicebank,
neural model, pronunciation, signing, installed hosts, or Windows. Native
freehand blend editing and a style-lane generator are not claimed. Full Beta GO
and the mandatory qualified neural singer remain open; no new complete roadmap
unit is counted by this increment.
