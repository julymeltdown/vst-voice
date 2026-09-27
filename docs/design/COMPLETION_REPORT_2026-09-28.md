# Native editor redesign completion report — 2026-09-28

Source baseline: master `85b0a1dd30086dc9b5aa2eac23dac96fa7fda24a`. This is a
source-and-existing-evidence audit of [the redesign plan](SEAM_UI_REDESIGN_CODE_PLAN_2026-09-25.md)
§16, not a new build, run or visual acceptance. The test names below identify runnable contracts;
this report does not claim that they were rerun at this commit. The benchmark figures below are
recorded measurements on an M3 Max, not a new run in this documentation worktree. The ignored
`build/evidence/ui-fidelity/r6-full/` packet was inspected in the original checkout. It is not
tracked in this worktree, and its `acceptance.md` identifies source `74ba8a6c`, not this baseline.

## §16 definition of done

| Requirement | Evidence at this baseline | Status |
|---|---|---|
| Both looks pass the owner and independent reviewer rubric | `libs/seam-native-ui/src/design/design_tokens.cpp`; the r6 packet checks EMO/SCENE geometry parity for 18 paired views, but its `acceptance.md` says reviewer PENDING, owner NOT_RUN | **Open:** rubric scores and concept-to-native judgement |
| SING, VOICE, TUNE, MIX and EXPORT complete with parity | `sing_shell.cpp`, `voice_workspace.cpp`, `tune_workspace.cpp`, `mix_workspace.cpp`, `shell_overlays.cpp` under `libs/seam-native-ui/src/design/`; relevant contract tests below. r6 contains SING, VOICE, TUNE and MIX captures, no EXPORT capture | **Implemented with automated coverage; full parity acceptance open** |
| Protagonist in ring, Stage, avatar, poses and splash with real state | `assets/character-01/manifest.json` schema 4; `libs/seam-native-ui/src/design/character_surface.cpp`; `tests/test_design_character_surface.cpp`, `tests/test_character_state_art.cpp` | **Source and tests present**; final art review and commercial clearance open |
| All tests and §10 budgets pass | `benchmarks/phase5_benchmark.cpp` gates shell `prepareFrame+paint`. At 1440×900 logical ×2, 40 samples on an M3 Max (load average ~11), true `cold-full-frame` p95 is 36.0 ms EMO / 24.5 ms SCENE against 14 ms: **MISS in both looks**. `retained-background-invalidation` p95 is 10.5 / 10.0 ms against 14 ms: PASS. Scroll/zoom p95 ~7.1 ms against 8 ms: PASS; playback p95 ~2.9 ms EMO / ~2.1 ms SCENE against 3 ms: PASS, EMO with ~0.1 ms margin; dense 10k notes p95 ~3.7 ms against 8 ms: PASS; layer cache stays within 80 MiB: PASS. The benchmark exits 1 while any case misses. Raw output and full ctest log for this baseline are not tracked here | **Open: true cold §10 budget misses; no all-tests claim for this commit** |
| FL Studio shows upright, readable, themed editor | `docs/design/SEAM_UI_FIDELITY_REVIEW_2026-09-25.md` §11 calls for F02–F05; r6 `acceptance.md` has no FL Studio captures | **Open:** F02–F05 in both looks in the actual host |
| Legacy painter removed | `libs/seam-native-ui/src/editor_scene.cpp` is the unavailable-platform presenter; the standalone and CLAP use `SingShell`. `docs/design/SEAM_UI_REDESIGN_CODE_PLAN_2026-09-25.md` records step 21 retirement | **Source complete** |
| Design system and character bible match shipped source | `docs/design/NATIVE_EDITOR_DESIGN_SYSTEM.md` refreshed for this baseline; `docs/brand/CHARACTER_BIBLE_DRAFT.md` remains the existing character reference | **Design system documented for `85b0a1dd`; final art review open** |

The r6 packet has 36 software captures; each reports geometry, semantics and image checks PASS.
Its paint p50 range is 0.7–3.5 ms. The packet did not capture AppKit windows, and software-paint
timing is not a host presentation measurement. It does not replace the FL Studio or visual rubric
rows. The capture script is `scripts/capture_sing_fidelity_packet.py`; the packet checker is
`scripts/verify_ui_fidelity_contract.py`, and timing analysis is
`scripts/analyze_sing_ui_performance.py`.

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
p50/p95 values are 26.6/36.0 ms EMO and 19.1/24.5 ms SCENE for true cold, versus 9.9/10.5 ms
EMO and 9.5/10.0 ms SCENE for retained background. The true cold 14 ms budget remains unmet.

Attribution is from timing the stages of that first-paint path, not a subtraction of the table's
p95 values: recording the L0 painter calls takes ~0.4 ms; banded parallel rasterization of the
recording takes ~8 ms; and roughly ~10 ms remains in L1 grid and L2 content replay plus full-window
snapshot copies. EMO ink strands and SCENE sparkles dominate background rasterization; disabling
them moved EMO true-cold p50 from ~24 ms to ~16.8 ms in the recorded diagnostic. These stage
figures are approximate and do not form a new budget pass.

Three unmerged branches tested alternatives and were rejected:

| Branch / commits | Recorded result | Reason |
|---|---|---|
| `codex/background-paint` (`6e28c022`) | Opaque underlay cache | No first-paint improvement |
| `codex/background-half-res` (`53a18095`, `51095f78`) | Half-resolution wash ~21 ms; 16% of pixels differ, maximum channel delta 24 | Still over 14 ms and beyond the plan's visual-packet tolerance (max delta 2, ≤0.1% pixels) |
| `codex/background-software-wash` (`de4a3f7c`, `87d84f69`) | Direct software base wash ~21.8 ms EMO / ~20.3 ms SCENE; maximum deltas 14/8, with 9.2%/6.0% of pixels differing | Still over 14 ms and beyond visual tolerance |

Closing true cold needs a genuinely faster wash rasterizer that writes directly into parallel band
surfaces, or a plan-level decision to relax the 14 ms budget or accept measured visual tolerance
for a half-resolution wash. None of those decisions or implementations is in this baseline.

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

1. Close or explicitly rebaseline the true cold §10 miss, then run and archive the release
   `seam_phase5_benchmark` design-shell JSON and relevant ctest results for the exact final source
   commit; distinguish shell timing from host state, presentation, and process RSS.
2. Run FL Studio F02–F05 in both modes. Capture the embedded editor and verify upright text,
   pointer mapping, resizing, keyboard focus and the requested edit/export paths.
3. Walk the accessibility tree and keyboard flows with VoiceOver and Accessibility Inspector.
4. Have the owner and independent reviewer score the plan §14 rubric. The existing packet leaves
   reviewer PENDING and owner NOT_RUN.
5. Implement and verify a Windows editor; the current non-Apple vector backend is unavailable
   (`libs/seam-native-ui/src/paint/canvas2d_unavailable.cpp`, README Windows TODO item 5).

Settings and plan §9 motion are shipped at `85b0a1dd`. FL Studio F02–F05,
VoiceOver/Accessibility Inspector, owner and independent reviewer scoring, and a Windows editor
remain open; this repository cannot supply the external host, assistive-technology, human-judgement
or platform evidence by itself.
