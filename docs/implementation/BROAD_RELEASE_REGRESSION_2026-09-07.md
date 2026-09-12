# Broad Release regression checkpoint

Scope: the current dirty worktree on `codex/production-readiness-completion`, based on `741ae2f244b9d3ff8eb6f31dc1f73cae54ea974d`. This is not release approval or completion of the Full-Scope Beta GO plan.

## 2026-09-08 expanded integration run

Rebuilt all default Release targets successfully (86 steps), then ran all 90 registered targets with `ctest --preset release --output-on-failure -j 2`. The initial run completed in 63.30 seconds: 88 targets passed, `seam_performance_snapshot_tests` failed five cases, and `seam_tracked_source_closure` reported 146 unindexed required inputs. Native GUI tests remain disabled. This is an integration/test result, not a completion percentage.

The snapshot failures shared a fixture-contract mismatch introduced when explicit pronunciation hints became effective: `PerformanceSnapshotFixture` deliberately retains hint `a`, but several old tests changed visible lyrics to `か` or `ああ` without clearing that hint. The renderer therefore correctly kept producing the explicitly requested vowel. Two procedural negative tests unexpectedly succeeded; the CV bank could not cover the hinted single vowel; and two multi-nucleus tests received one nucleus instead of two.

The repair is scoped to the intended test inputs, not renderer admission: retain the shared fixture hint, explicitly clear it only in CV/multi-nucleus scenarios, assert the generated CV/two-phone sequence, and add positive checks that an explicit vowel hint still overrides an unsupported visible CV lyric. Also repaired a previously passing continuation-context case that was masked by the same hint: it now clears the hint and proves isolated resolution produces `pau`, whereas full-region context produces the predecessor vowel.

The corrected 41-case snapshot suite passes Release (1.96 s) and Debug (13.80 s). No synthesis guard, assertion meaning, source gate or test exclusion was relaxed.

Final full rerun after the fixture repair: **89 of 90 targets passed in 54.98 seconds**. Only `seam_tracked_source_closure` fails, still reporting 146 unindexed required inputs. All functional targets pass in that same run, including the 41-case snapshot target (1.49 s), 595-case core target (12.61 s), singing-quality workflow (20.29 s), allocation/cancellation/soak checks, recovery/export, schema/migration and CLAP host smoke paths. No staging, commit, exclusions or gate changes were used. Source files remain local/uncommitted.

This supersedes the older 88-target checkpoint below. It does not prove live native GUI/IME, the complete host matrix, independent acoustic/language acceptance, a rights-cleared shipping singer, signed installation or the unfinished Full-Scope Beta units. Test-target pass rate is not project completion. Engineering can continue; release/source-publication gates remain closed.

## Current full rerun after fixes

Rebuilt all default Release targets again (85 build steps) after classical/Raw short-transition integration, then reran **all 88 registered CTest targets** with `ctest --preset release --output-on-failure -j 2`.

Result: **87 passed, 1 failed, 62.46 seconds**. The sole failure is `seam_tracked_source_closure`, reporting required new implementation/test files not indexed in Git. No functional test failure was reported in this run. No staging, commit, exclusions or gate changes were used to obtain this result.

The formerly failing Phase2 demo, Phase12B schema/migration test, macOS/Windows source contracts and unchanged bank/Raw singing-quality workflow all passed in this same run. Existing synthesis, source-map/timing, performance, migration, producer/export/recovery, allocation, smoke/soak and release-contract targets also passed. `seam_tests` itself completed in 11.41 seconds and the singing-quality workflow in 20.42 seconds.

This supersedes the earlier partial-rerun boundary below. It does not turn a synthetic corpus or contract test into acoustic acceptance, host/OS matrix coverage, rights evidence or the unimplemented Full-Scope Beta requirements. Native GUI tests remain disabled in this CTest inventory; prior manual AppKit checks remain separately scoped. The pass rate is a test-target result, **not project completion**. The full goal remains active.

## Broad run

`cmake --build --preset release -j 4` rebuilt all default targets successfully (168 build steps). `ctest --preset release --output-on-failure -j 2` then ran all 88 registered targets: **82 passed, 6 failed**, in 78.14 seconds. This target-level result must not be confused with the individual case count reported by `seam_tests`.

| Failed target | Finding | Follow-up |
| --- | --- | --- |
| `seam_phase2_demo_smoke` | The synthetic demo requested an explicit zero end offset on the first consonant, not just its advertised -70 ms onset. The current selector correctly rejects that unsupported internal timing boundary. | Removed the unintended end override; retained onset lock and production admission checks. Focused rerun passes. |
| `seam_phase12b_tests` | Test expected current writer schema 7, then fabricated a schema-4 document by relabelling the current payload. Current writer schema is 9. | Current-version assertion uses the codec constant; migration now loads the existing genuine schema-4 historical writer fixture. Focused rerun passes. |
| `seam_macos_source_contract` | A substring assertion required `ExportSet;`, although the save-dialog predicate now continues with additional purposes using `||`. | Preserve the ExportSet predicate assertion without depending on its final-expression punctuation. Focused rerun passes. |
| `seam_windows_source_contract` | Same punctuation-bound assertion as macOS. | Same assertion repair; focused rerun passes. This does not constitute Windows runtime qualification. |
| `seam_singing_quality_workflow` | `unequal-rests-bank-render` exits 4 because a selected CV transition cannot fit its short target phoneme span. | Reproduced with retained logs; unresolved synthesis/short-span behavior, not bypassed or silenced. |
| `seam_tracked_source_closure` | Required new source/test files are not indexed in Git. | Expected publication gap for the retained uncommitted worktree. No automatic staging/commit was used to mask it. |

## Retained singing-quality reproduction

The existing `run_corpus` runner was invoked with the current Release driver/analyzer/build evidence and the unchanged corpus contract. It reproduced the failure in:

`/tmp/seam-broad-quality.rP8rbv/u1-public-domain-diagnostic-k0sfd5q7`

The retained `commands/unequal-rests-bank-render.stderr` reports:

> Phoneme span is too short for the selected unit transition; lengthen the note or choose a shorter unit (demo.ja.g4.k-o.01 at 0000000000000100:0)

The rejecting condition is in `libs/seam-synthesis/src/timing_solver.cpp`: the target voiced span must exceed the selected unit's required post-vowel transition length. The next work must distinguish a genuinely infeasible unit from a missing supported time-warp/selection fallback, preserve intelligibility and source/target bounds, and add an actual short-note render regression. Do not simply lengthen the corpus notes or suppress this guard to make the workflow green.

## Evidence boundaries

Follow-up: the short-transition fix is now integrated for classical and Raw rendering, and the unchanged `seam_singing_quality_workflow` target passes. See `SHORT_UNIT_TRANSITION_MAPPING_2026-09-07.md` for exact output and pitch-loop evidence. This supersedes the earlier unresolved singing-workflow finding, but does not itself constitute another complete 88-target run. Git source closure remains a publication gap.

The four repaired failures were rerun as a focused set; the entire 88-target suite has not been rerun after those repairs. The other two failures remain open, so this checkpoint makes no all-green claim. Existing source changes, tests and documents remain local/uncommitted. Native GUI tests are not enabled in this CTest configuration; earlier live AppKit evidence is separate. Synthetic/development bank success is not a rights-cleared production singer or acoustic-quality acceptance.
