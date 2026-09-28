# Native editor redesign completion report — 2026-09-28

Source baseline for the source-and-existing-evidence audit: master `caabbaf5`. The reproducibility
result below was subsequently verified at clean master `2acd8ce4`. This audit of
[the redesign plan](SEAM_UI_REDESIGN_CODE_PLAN_2026-09-25.md) §16 does not establish full visual
acceptance. The test names below identify runnable contracts;
the earlier merged-tree run reported 206/206 ctest entries passing at `44bf8386`; a full ctest
result for `caabbaf5` is not tracked or verified here. The benchmark figures below are an
[operator-recorded snapshot](evidence/BENCHMARK_2026-09-28.md) on an M3 Max from a pre-merge
candidate, not a new run in this documentation worktree. The ignored
`build/evidence/ui-fidelity/r6-full/` packet was inspected in the original checkout. It is not
tracked in this worktree, and its `acceptance.md` identifies source `74ba8a6c`, not this baseline.

## §16 definition of done

| Requirement | Evidence at this baseline | Status |
|---|---|---|
| Both looks pass the owner and independent reviewer rubric | `libs/seam-native-ui/src/design/design_tokens.cpp`; the r6 packet checks EMO/SCENE geometry parity for 18 paired views, but its `acceptance.md` says reviewer PENDING, owner NOT_RUN | **Open:** rubric scores and concept-to-native judgement |
| SING, VOICE, TUNE, MIX and EXPORT complete with parity | `sing_shell.cpp`, `voice_workspace.cpp`, `tune_workspace.cpp`, `mix_workspace.cpp`, `shell_overlays.cpp` under `libs/seam-native-ui/src/design/`; relevant contract tests below. r6 contains SING, VOICE, TUNE and MIX captures, no EXPORT capture | **Implemented with automated coverage; full parity acceptance open** |
| Protagonist in ring, Stage, avatar, poses and splash with real state | `assets/character-01/manifest.json` schema 4; `libs/seam-native-ui/src/design/character_surface.cpp`; `tests/test_design_character_surface.cpp`, `tests/test_character_state_art.cpp` | **Source and tests present**; final art review and commercial clearance open |
| All tests and §10 budgets pass | The earlier merged-tree ctest run passed **206/206** at `44bf8386`; no tracked full ctest result establishes that count for `caabbaf5`. `benchmarks/phase5_benchmark.cpp` gates shell `prepareFrame+paint`. The recorded 40-sample M3 Max run has true `cold-full-frame` p50/p95 of 15.59/16.50 ms EMO and 14.65/15.26 ms SCENE against the 14 ms p95 budget: **MISS in both looks**. Retained-background p95 is 9.03 / 9.74 ms; scroll/zoom, playback and dense 10k notes all pass. **Update at `3fb39459`:** a local Release ctest passed 210/210. The 40-sample full run passed every case in both looks, with true-cold p50/p95 of 10.89/12.47 ms EMO and 10.99/12.55 ms SCENE, and the design-shell gate reported a pass. All ten alternating A/B look-runs at load average ~12 stayed under 14 ms. See the [snapshot](evidence/BENCHMARK_2026-09-28.md) for every case, the load and the one contention run. | **Met on this machine in the recorded runs**; p95 still responds to heavy unrelated load, and the final-commit JSON must still be archived |
| §14.4 visual reproducibility | Frozen-clock `rep1`/`rep2` packets at clean master `2acd8ce4` compare **36/36 identical** software frames by RGBA pixel hash with `--require-identical`; the tracked [`rep1` manifest](evidence/ui-fidelity-rep1-manifest.json) records `softwarePixelSha256` per frame | **Partial pass:** the plan's full contrast, scale and platform matrix remains open |
| FL Studio shows upright, readable, themed editor | `docs/design/SEAM_UI_FIDELITY_REVIEW_2026-09-25.md` §11 calls for F02–F05; r6 `acceptance.md` has no FL Studio captures | **Open:** F02–F05 in both looks in the actual host |
| Legacy painter removed | `libs/seam-native-ui/src/editor_scene.cpp` is the unavailable-platform presenter; the standalone and CLAP use `SingShell`. `docs/design/SEAM_UI_REDESIGN_CODE_PLAN_2026-09-25.md` records step 21 retirement | **Source complete** |
| Design system and character bible match shipped source | `docs/design/NATIVE_EDITOR_DESIGN_SYSTEM.md` refreshed for this baseline; `docs/brand/CHARACTER_BIBLE_DRAFT.md` remains the existing character reference | **Design system documented for `caabbaf5`; final art review open** |

The r6 packet has 36 software captures; each reports geometry, semantics and image checks PASS.
Its paint p50 range is 0.7–3.5 ms. The packet did not capture AppKit windows, and software-paint
timing is not a host presentation measurement. It does not replace the FL Studio or visual rubric
rows. The capture script is `scripts/capture_sing_fidelity_packet.py`; the packet checker is
`scripts/verify_ui_fidelity_contract.py`, and timing analysis is
`scripts/analyze_sing_ui_performance.py`.

At clean master `2acd8ce4`, the full software packet was captured twice on this machine:

```sh
python3 scripts/capture_sing_fidelity_packet.py --output build/evidence/ui-fidelity/rep1 --no-appkit
python3 scripts/capture_sing_fidelity_packet.py --output build/evidence/ui-fidelity/rep2 --no-appkit
python3 scripts/compare_fidelity_packets.py build/evidence/ui-fidelity/rep1 build/evidence/ui-fidelity/rep2 --require-identical
```

The comparison printed `frames compared: 36, identical: 36` and
`PASS every frame's pixels match`. The 36 captures cover EMO and SCENE; empty, ready, rendering,
failed, dense-overlap, inspector, VOICE, TUNE and MIX states; and 720×480, 860×640, 1100×720,
1280×800, 1440×900 and 1600×900 viewports. The script sets `SEAM_UI_FREEZE_CLOCK=1` so blink,
breathing, ring phase and tweens use a fixed animation time. Each capture records
`softwarePixelSha256` over decoded RGBA pixels, excluding PNG creation-time metadata from the
comparison. The [`rep1` manifest](evidence/ui-fidelity-rep1-manifest.json) is tracked; both full
packets are ignored build evidence. The earlier [`rq1` manifest](evidence/ui-fidelity-rq1-manifest.json)
documents an 18-frame ready/empty comparison against `rq2` at dirty `4e47209c`. The older
36-frame `det1` packet (`52fc8ff5`) predates clock freezing and has no pixel hashes.

This passing result covers one machine and the AppKit software raster backend, at Standard
contrast and 2× device scale only; the manifest records `deviceScale: 2`. Plan §14.4 asks for
{EMO, SCENE} × {Standard, High} × {Wide, Standard, Compact, Minimum} × {1×, 2×} ×
{empty, dense song, selection, rendering, error}. Its High Contrast and 1× captures, among other
matrix cells, remain open. No Win32/X11 packet or cross-platform tolerance diff was recorded.
AppKit window captures were skipped with `--no-appkit`; FL Studio, VoiceOver and the owner and
independent reviewer rubric remain NOT_RUN.

The shipped shell also has a consolidated Settings sheet with Audio, Appearance (EMO/SCENE,
High Contrast, Reduce Motion, and character Full/Minimal/Off), Language, and About sections
(`shell_overlays.cpp`, `design_preferences_appkit.mm`). Plan §9 motion now includes tab and mode
cross-fades, toast in/out, note-add feedback and a render sweep; Reduce Motion resolves tweens
immediately (`motion.hpp`, `sing_shell.cpp`). Round-6 fixes scope translations to each shell
instance, bound glow sprites safely, and record effective runtime font faces in capture packets.
These are shipped source behaviors, not substitutes for the open host and human review rows.

### §10 benchmark meaning and cold-frame work

The earlier 9.5 ms EMO / 11.2 ms SCENE figure called “cold” measured a retained L0 background
snapshot with upper layers recomposed. It was **not** a true first paint. The corrected benchmark
separates `cold-full-frame` (L0 painter and L1–L3 run) from
`retained-background-invalidation` (L0 snapshot reused, upper layers recomposed). The measured
p50/p95 values in the recorded representative run are 15.59/16.50 ms EMO and 14.65/15.26 ms
SCENE for true cold. Across the recent three-run set, cold p50 was about 15.2–15.6 ms EMO and
14.2–14.6 ms SCENE; p95 remained over budget in each run. Retained-background p95 in the
representative run is 9.03 ms EMO and 9.74 ms SCENE. The true cold 14 ms budget remains unmet
in both looks. The earlier 17.0/18.96 ms EMO and 17.3/30.43 ms SCENE measurements belong to
the historical `44bf8386` baseline.

The merged path composes fully damaged cold frames in place, avoiding redundant full-window
snapshot copies. A software rasterizer writes the procedural wash (EMO ink strands, SCENE sparkles
and radial gradients) directly into parallel band surfaces; immutable seeded strand geometry is
built once and shared across the bands (`sing_shell.cpp`, `layer_cache.cpp`). Subsequent cached
CoreGraphics colors, gradients and paths, cached CoreText lines, and indexed layer items reduced
the observed EMO cold p95 from an earlier ~36.0 ms to ~16.5 ms by the `52fc8ff5` candidate.
The later keyboard-fill and grid-stroke batching is present in `caabbaf5`, but this snapshot does
not quantify its effect. Against the previous
CoreGraphics wash, maximum channel difference is 14 across 9.23% of EMO pixels and 8 across 6.03%
of SCENE pixels. This is a recorded reference comparison, not plan §14 visual acceptance.

At `87d8fe02` the glass-panel fills are also painted in software, in the same band pass as the
wash. CoreGraphics dithers long gradients with a fixed 64-column threshold pattern, so the
software fill adds an 8×8 ordered dither to keep the dark panel ramps free of banding. At
`3fb39459` the canvas draws two-stop gradient fills of rounded rectangles and circles, such as the
note capsules and knob bodies, with the same rasterizer. It does so only under plain state:
rectangular clip, normal blend and no glow. Coverage is exact on vertical edges and uses the
half-plane share along the boundary's tangent elsewhere. The dither follows the logical origin, so
drawing whole, clipped or banded gives the same pixels. Because the vector reference now draws its
panels with that fill too, the comparison isolates the wash: 9.88% and 7.13% of pixels differ,
with a maximum of 14 and 8. The focused tests measure the fills against CoreGraphics itself
(`ScopedBackendGradients`): at most 2 levels for the panels, and at most 5 levels with a mean
of 0.21 for capsules and knobs. On a steep capsule ramp, the software rows are within 0.27 levels
of the exact gradient. CoreGraphics' rows there follow a coarse, undithered table and stray by up
to 2.91. The remaining main-thread costs are the CoreGraphics strokes of capsules and knobs, the
grid replay and the character art.

Historical diagnostic stage timing was approximately 7.2–7.8 ms for EMO L0 and 5.6–6.9 ms for SCENE L0,
7.1–8.1 ms for grid plus content replay, and 0.24–0.36 ms for snapshot copying. Frame preparation
and dynamic replay account for the rest. These are approximate stage timings, not a sum of p95s.
The next measured lever is repeated CoreGraphics chrome replay within L0 bands. A single
full-surface replay measured 45–50 ms and was rejected.

Earlier alternatives rejected during this work:

| Branch / commits | Recorded result | Reason |
|---|---|---|
| `codex/background-paint` (`6e28c022`) | Opaque 3-bytes-per-pixel underlay cache | No first-paint improvement |
| `codex/background-half-res` (`53a18095`, `51095f78`) | Half-resolution wash ~21 ms; 16% of pixels differ, maximum channel delta 24 | Still over 14 ms and beyond the plan's visual-packet tolerance (max delta 2, ≤0.1% pixels) |

The software-wash branch was subsequently improved and merged into this baseline; its earlier
prototype timing is superseded by the cold-frame figures above. Further L0 chrome replay work or an
explicit budget decision is needed to close the true cold miss.

## Parity checklist from plan §13

These rows name the most direct source and automated contract. A passing source contract proves
the behavior that its test asserts; it does not prove a complete human editing journey.

| Checklist item | Source and contract evidence | Limit |
|---|---|---|
| Note create/move/resize/delete; box selection; overlap badge/detail | `libs/seam-native-ui/src/design/sing_shell.cpp`, `tests/test_design_shell_input.cpp` (forwarded gestures, overlap popover, note semantics); existing controller tests under `tests/` | Full note-edit journey and FL host still need manual run |
| Lyric editing and batch lyrics; vibrato handles; phoneme boundary editing | `sing_shell.cpp`, `shell_overlays.cpp`; `tests/test_design_shell_input.cpp` (text anchors, batch input, vibrato focus, phoneme review) | End-to-end production project review open |
| Unit variant and renderer selection; seam edit and alternate preview | `shell_overlays.cpp`, `sing_shell.cpp`; `tests/test_design_shell_input.cpp` covers re-homed controls and refusal. Plan §13 states alternate B preview is standalone-only; CLAP refuses it | Host-specific behavior needs FL verification |
| Pitch points; expression lanes with refusal; technical lane expand/collapse | `tune_workspace.cpp`, `sing_shell.cpp`; `tests/test_design_tune_workspace.cpp` tests point add/move/remove and refusal, `tests/test_design_shell_input.cpp` tests lane input | Real-song visual review open |
| Time map; tempo and meter edit; loop; bounce timing choice | `shell_overlays.cpp`, `sing_shell.cpp`; `tests/test_design_shell_input.cpp` tests time-map overlay and EXPORT host capability path | Host timing and bounce journey open |
| Sample microscope; phoneme review; replacement review | `shell_overlays.cpp`; `tests/test_design_shell_input.cpp` tests microscope and review surfaces | Human usability review open |
| Voicebank browser, relink and replace; audio settings; recovery/support; diagnostics with actions | `shell_overlays.cpp`; `tests/test_design_shell_input.cpp` tests re-homed sheets, support, diagnostics and overlay routing | External resource and installed-singer journey not established by a UI packet |
| Export with receipt | `sing_shell.cpp`, `character_surface.cpp`; `tests/test_design_shell_input.cpp` tests EXPORT command and refusal; `tests/test_design_character_surface.cpp` tests the committed completion pose | r6 has no EXPORT capture; actual file/receipt journey remains to verify |
| Character Full/Minimal/Off | `libs/seam-domain/include/seam/domain/project.hpp`, `character_surface.cpp`; `tests/test_design_character_surface.cpp` tests every surface | Manual mode comparison still open |
| Accessibility tree and keyboard paths | `sing_shell.cpp`, `shell_overlays.cpp`; `tests/test_design_shell_input.cpp`, `tests/test_design_layout_properties.cpp`, and r6 `semantic-bounds.json` | VoiceOver/Accessibility Inspector walk-through NOT_RUN |

Related gates: `seam_design_frame_pipeline_tests` checks cached/partial composition and memory;
`seam_design_character_surface_tests` checks state and v4 artwork; `seam_design_layout_property_tests_*`
checks responsive controls, strings and Korean; `seam_brand_terms_source_contract` runs
`scripts/check_brand_terms.py` via `tests/design/test_check_brand_terms.py`; and
`seam_bundled_fonts_contract` checks `assets/fonts/manifest.json`. Source tests and a 36-frame
packet are narrower than the plan's proposed repeated, cross-platform, all-state visual matrix.

## Remaining acceptance work

1. The true cold §10 budget is met in the recorded runs at `3fb39459`. Run and archive the
   release `seam_phase5_benchmark` design-shell JSON and the relevant ctest results for the exact
   final source commit. Keep shell timing distinct from host state, presentation and process RSS.
2. Run FL Studio F02–F05 in both modes. Capture the embedded editor and verify upright text,
   pointer mapping, resizing, keyboard focus and the requested edit/export paths.
3. Walk the accessibility tree and keyboard flows with VoiceOver and Accessibility Inspector.
4. Have the owner and independent reviewer score the plan §14 rubric. The existing packet leaves
   reviewer PENDING and owner NOT_RUN.
5. Implement and verify a Windows editor; the current non-Apple vector backend is unavailable
   (`libs/seam-native-ui/src/paint/canvas2d_unavailable.cpp`, README Windows TODO item 5).

Settings, plan §9 motion and the software wash are present at `caabbaf5`. FL Studio F02–F05,
VoiceOver/Accessibility Inspector, owner and independent reviewer scoring, and a Windows editor
remain open; this repository cannot supply the external host, assistive-technology, human-judgement
or platform evidence by itself.
