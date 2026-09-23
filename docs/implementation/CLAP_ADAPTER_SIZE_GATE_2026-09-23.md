# A gate that enforced a size ceiling was never wired up, and had been failing

Date: 2026-09-23
Plan unit: cross-cutting maintenance. This repairs an existing gate and does not
add a roadmap unit.
Base commit: 2ae745fb
Status: **reproduced, fixed, verified.**

## The finding

`scripts/verify_clap_authoring_adapter.py` enforces two things about the CLAP
editor runtime: that `EditorRuntime` owns the shared `AuthoringRuntime` instead of
private business state, and that no `editor_runtime_*.cpp` file exceeds **600
lines**. It was failing:

```
editor_runtime_adapter.cpp exceeds 600 lines: 684
editor_runtime_project.cpp exceeds 600 lines: 621
```

This was found by running every `scripts/verify_*.py` gate by hand and sorting by
exit status, not by any signal the project produces. That is the actual defect:

**the gate was reachable only by hand.** It is not referenced in `CMakeLists.txt`,
in any workflow under `.github/workflows/`, or in any CTest test. Nothing ran it,
so nothing reported that it had stopped passing.

The ceiling itself was respected when the gate was written: at `6e3be9ec`
("refactor: CLAP editor to shared authoring adapter") the files were 395 and 193
lines. Both grew past 600 afterwards, and the gate went quiet rather than red.

## The fix

**1. Put the code back under the ceiling by splitting along a real seam.**
`editor_runtime_interchange.cpp` now holds the interchange surface:
`prepareInterchangeImport`, `acceptInterchangeImport` and `exportInterchange`, the
import/export/review handoff setters, and the `requestInterchangeImport` /
`requestInterchangeExport` command entrypoints. These already formed a unit — they
were the only consumers of the interchange includes in either file.

| file | before | after |
|---|---:|---:|
| `editor_runtime_adapter.cpp` | 683 | 592 |
| `editor_runtime_project.cpp` | 620 | 577 |
| `editor_runtime_interchange.cpp` | — | 150 |

**2. Wire the gate into CTest as `seam_clap_authoring_adapter_contract`.**
Repairing the two files without this would leave the same trap set for the next
growth spurt: the code would drift over 600 again and again nothing would say so.

## The contract is not weakened

`verify_phase12b_contracts.py` asserts the U32 requirement that the embedded editor
shares the standalone interchange boundary rather than growing a second conversion
path. It checked four tokens in `editor_runtime_project.cpp`:
`InterchangeService{}.importFile`, `InterchangeService{}.exportFile`,
`acceptInterchangeImport`, and `replaceProject(std::move(draft.project))`.

Those tokens moved with the code, so the gate's path was updated to
`editor_runtime_interchange.cpp`. **The required token set is byte-for-byte the
same four strings**; only where they are looked for changed. The constraint is
still enforced, not relaxed.

## Verified

| check | result |
|---|---|
| `verify_clap_authoring_adapter.py` | exit 0 (was exit 1) |
| `seam_clap_authoring_adapter_contract` (new CTest entry) | 1/1 passed |
| `verify_phase12b_contracts.py` | 6/6 PASS |
| `verify_phase11_contracts.py` | PASS |
| `verify_tracked_source_closure.py` | PASS |
| Release CTest | 184/184, up from 183 because the gate is now a test |
| `seam_tests` | 1042 passed, 0 failed |
| external-beta suite | 189/189 in 24.69 s |
| CI Debug `seam_phase11_tests` | 1/1 in 1.17 s |
| ThreadSanitizer, 12 runs | 12/12 clean, zero reports |

The ThreadSanitizer re-run matters here specifically: the preview-status race fix
from the same day touches `editor_runtime_preview.cpp`, and the split reorganised
translation units around it. 12/12 clean confirms the race repair survived the move.

## What this does not claim

Behaviour is unchanged. This is a translation-unit boundary change plus a build-
and-test wiring change; no logic, no rendered output, no renderer revision. It
**advances no roadmap unit and no Beta acceptance criterion.**

## The survey: was this the only rotted gate?

The same trap was checked everywhere else. Of 28 `scripts/verify_*.py`, nine are not
referenced by `CMakeLists.txt` or any workflow. Each was run by hand:

| gate | result |
|---|---|
| `verify_creator_scope_ratification` | exit 0, PASS |
| `verify_phase12c_canonical_contract` | exit 0 |
| `verify_phase12c_live_contracts` | exit 0, PASS |
| `verify_phase13b_contracts` | exit 0, PASS |
| `verify_public_windows_standalone_contract` | exit 0, PASS |
| `verify_usable_alpha_contract` | exit 0, PASS |
| `verify_full_product_report` | exit 2, requires `--report`/`--candidate` by design |
| `verify_neural_feasibility_inputs` | exit 2, requires a `record` argument by design |
| `verify_update_manifest` | exit 2, requires inputs by design |

So the CLAP adapter ceiling was the only un-wired gate that had actually stopped
passing. The other six pass; the last three are argument-driven instruments that
cannot be run without a subject and were not silently broken. Wiring the remaining
gates that take no arguments would be defensible hardening, but it is not a repair
of an active defect and is deliberately not bundled into this change.
