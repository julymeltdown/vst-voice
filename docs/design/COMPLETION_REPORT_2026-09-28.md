# Native editor redesign completion report — 2026-09-28

Source baseline: master `1e424fd14855f3536d1dfbc368c4526fc09d92ca`. This is a
source-and-existing-evidence audit of [the redesign plan](SEAM_UI_REDESIGN_CODE_PLAN_2026-09-25.md)
§16, not a new build, run or visual acceptance. The test names below identify runnable contracts;
this report does not claim that they were rerun at this commit. The ignored
`build/evidence/ui-fidelity/r6-full/` packet was inspected in the original checkout. It is not
tracked in this worktree, and its `acceptance.md` identifies source `74ba8a6c`, not this baseline.

## §16 definition of done

| Requirement | Evidence at this baseline | Status |
|---|---|---|
| Both looks pass the owner and independent reviewer rubric | `libs/seam-native-ui/src/design/design_tokens.cpp`; the r6 packet checks EMO/SCENE geometry parity for 18 paired views, but its `acceptance.md` says reviewer PENDING, owner NOT_RUN | **Open:** rubric scores and concept-to-native judgement |
| SING, VOICE, TUNE, MIX and EXPORT complete with parity | `sing_shell.cpp`, `voice_workspace.cpp`, `tune_workspace.cpp`, `mix_workspace.cpp`, `shell_overlays.cpp` under `libs/seam-native-ui/src/design/`; relevant contract tests below. r6 contains SING, VOICE, TUNE and MIX captures, no EXPORT capture | **Implemented with automated coverage; full parity acceptance open** |
| Protagonist in ring, Stage, avatar, poses and splash with real state | `assets/character-01/manifest.json` schema 4; `libs/seam-native-ui/src/design/character_surface.cpp`; `tests/test_design_character_surface.cpp`, `tests/test_character_state_art.cpp` | **Source and tests present**; final art review and commercial clearance open |
| All tests and §10 budgets pass | `benchmarks/phase5_benchmark.cpp` gates shell `prepareFrame+paint` at p95 14/8/3/8 ms and 80 MiB. Reported p95: cold 9.5 EMO / 11.2 SCENE, scroll/zoom ~6, playback ~1.8, dense 10k notes ~3.2; cache ~65 MiB. The raw benchmark output and a full ctest log for `1e424fd1` are not tracked here | **Reported budgets met; rerun and archive a baseline-specific result before closure** |
| FL Studio shows upright, readable, themed editor | `docs/design/SEAM_UI_FIDELITY_REVIEW_2026-09-25.md` §11 calls for F02–F05; r6 `acceptance.md` has no FL Studio captures | **Open:** F02–F05 in both looks in the actual host |
| Legacy painter removed | `libs/seam-native-ui/src/editor_scene.cpp` is the unavailable-platform presenter; the standalone and CLAP use `SingShell`. `docs/design/SEAM_UI_REDESIGN_CODE_PLAN_2026-09-25.md` records step 21 retirement | **Source complete** |
| Design system and character bible match shipped source | `docs/design/NATIVE_EDITOR_DESIGN_SYSTEM.md`, `docs/brand/CHARACTER_BIBLE_DRAFT.md` updated in this documentation branch | **Documented for `1e424fd1`** |

The r6 packet has 36 software captures; each reports geometry, semantics and image checks PASS.
Its paint p50 range is 0.7–3.5 ms. The packet did not capture AppKit windows, and software-paint
timing is not a host presentation measurement. It does not replace the FL Studio or visual rubric
rows. The capture script is `scripts/capture_sing_fidelity_packet.py`; the packet checker is
`scripts/verify_ui_fidelity_contract.py`, and timing analysis is
`scripts/analyze_sing_ui_performance.py`.

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

1. Run and archive the release `seam_phase5_benchmark` design-shell JSON and relevant ctest
   results for the exact final source commit; distinguish its shell timing from host state,
   presentation, and process RSS.
2. Run FL Studio F02–F05 in both modes. Capture the embedded editor and verify upright text,
   pointer mapping, resizing, keyboard focus and the requested edit/export paths.
3. Walk the accessibility tree and keyboard flows with VoiceOver and Accessibility Inspector.
4. Have the owner and independent reviewer score the plan §14 rubric. The existing packet leaves
   reviewer PENDING and owner NOT_RUN.
5. Implement and verify a Windows editor; the current non-Apple vector backend is unavailable
   (`libs/seam-native-ui/src/paint/canvas2d_unavailable.cpp`, README Windows TODO item 5).

At `1e424fd1`, the separate Settings sheet and plan §9 motion are **in progress** on another
branch. The existing audio settings sheet is already present; this distinction is about the new
appearance Settings work. Neither in-progress feature is counted as shipped or accepted here.
