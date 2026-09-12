# Integration and Studio close repair

This implements findings from the checkpoint direction review; the earlier
review remains historical evidence for its own source snapshot. The Full-Scope
R1–R20 goal is unchanged. No new implementation unit or Beta GO is accepted.

## Repairs

- Studio draft tests derive copied-audio paths through public manifest/unit
  accessors instead of calling the private `selectedAudioPath()` method.
- Phase12B preserves the unsupported interior-edit negative case and exact undo
  check. It now waits for a current selectable plan before preparing the positive
  edit sequence. Timeout no longer returns a possibly stale preview as success.
- That newly reachable test exposed two further issues. Auxiliary mono/multichannel
  stereo conversion used a mutable copy-on-write read and detached Final PCM;
  it now reads through a const view. A pending debounced edit also left the old
  Final publication current; the shared authoring runtime now invalidates its
  current request token immediately, while retaining reader-owned audio and
  diagnostic publications. Repreparation is still required.
- Phase12B verifies immediate invalidation and exact retained PCM storage through
  1-, 2-, 8-, and 4-channel output changes, in addition to existing rate, quality,
  missing/partial Final, and Follow Host rejection checks.
- Studio has a sample Save/Discard/Cancel close decision. Dirty manifest/producer
  context is captured and checked after the modal. Cancellation and explicit
  discard preserve the model and saved bytes; saving uses the existing save
  transaction; save failure and in-flight work reject closing.
- Native close handling also retains Designer confirmation, checks its epoch/
  revision after sample confirmation, rejects reentrant close requests, and
  catches failures at the native noexcept boundary. AppKit defaults to Cancel;
  Win32 uses Yes/No/Cancel with Cancel as default. Four new controller cases
  cover cancellation/discard, actual save/reopen, modal-time edits, and failed
  saving/background work.

## Current verification

- Full Release build: `cmake --build build/release -j 4` — PASS.
- Full serial CTest: **119/120 PASS**, 255.74 seconds.
- Only failure: `seam_tracked_source_closure`, with **347 unindexed required
  inputs at that run**. This includes policy-covered documents. Nothing was
  staged to hide the failure; later evidence documents can change this count.
- Current core: **818 passed, 0 failed**.
- Current Studio draft/close target: **20 passed, 0 failed**.
- Phase12B: PASS, 2 tracks / 3 regions / 4 channels / 148,988 frames.
- Actual loaded-plugin cold Final bounce, canonical plugin matrix/admission,
  producer, CLI installation/export, allocation, and release regressions pass
  within that same full run. This does not qualify real singers or installed DAWs.
- `git diff --check`: PASS.

Command: `ctest --test-dir build/release --output-on-failure -j 1 --output-log
build/release/close-and-final-repair-full.log`.

CLAP binary SHA-256:
`ebf5640b8760f25fde3cf1850d081c5087d0a37983d15efb1a73494dd34cd8b3`.
Tracked working diff before this evidence/ledger update:
`eae4ec99276f866e9875513ed81e3ffc51a67a99b1db88e7b0607db6067e22d5`.
Base HEAD remains `741ae2f244b9d3ff8eb6f31dc1f73cae54ea974d`.

## Preservation and boundaries

The owner's local recovery baseline is
`build/recovery-checkpoints/session-preservation-rsWotr`. Comparing captured
source/test inputs found **no missing files and exactly 12 intentionally changed
files** in this batch. This does not prove or disprove older session-related
loss. Raw suite output and changed-file hashes are retained under
`evidence/close-final-2026-09-09/`.

No staging, commit, push, source qualification, review approval, or release
promotion was performed. Native modal presentation/actual quit interaction was
not newly exercised by computer use; Windows implementation is source-only on
this machine. The existing producer save/uncertain-durability semantics were not
redesigned. Full native shutdown qualification, ordinary Editor async selection,
source assessment/reassessment workflow, real singer articulation/quality,
neural deployment, Follow Host and full installed acceptance remain open.
