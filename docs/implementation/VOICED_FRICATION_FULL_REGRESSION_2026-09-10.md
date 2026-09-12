# Full local regression after voiced-frication integration

Result: 119/120 registered Release CTest entries passed in 133.37 seconds using
`ctest --test-dir build/release --output-on-failure -j 2`. Exit code was 8.

The only failing entry was `seam_tracked_source_closure`: 523 required inputs
existed locally but were not in the Git index; no missing-input records were
reported. This is an unresolved source reproducibility/publication gate. No
staging, commit, push or gate-policy relaxation was performed to mask it.
The count describes the tested tree before adding this report and its copied log.

The run covers the accumulated recipe-v5 codec, mixed rendering, typed marker
transport, candidate-v5 loading/writing, onset/coda producer round trips and
Designer voicing controls through the registered local test suite. All entries
other than source closure passed. It does not prove Windows execution, complete
installed-host coverage, a qualified neural singer, listening acceptance, source
rights for release or full Beta GO.

During the run, all 2,075 files in recovery checkpoint
`session-preservation-tEvIRZ` remained present and all regular-file hashes matched.
This verifies continuity since that checkpoint, not absence of any historical
session loss. Existing dirty work was preserved.

Retained test output: `evidence/voiced-frication-full-2026-09-10/ctest.log`.
This supersedes older full-suite snapshots for the current code; focused tests
and live checks retain their narrower stated qualification boundaries.
