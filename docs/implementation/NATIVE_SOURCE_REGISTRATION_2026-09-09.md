# Native Studio source registration

## Delivered capability

Studio now connects the source-registration repository operation to real AppKit
and Win32 forms. A source-free, inventory-bound Draft can register a new source
without editing durable JSON or using the registration CLI. Full-Scope Beta GO
remains open; this increment does not accept an additional implementation unit.

In Review, **L Capture Source License** selects an authorization document and
reads/hashes it on the existing serialized worker. This is read-only. **S Register
New Source** displays the captured producer, project digest, evidence path and
evidence digest, then requires a unique source ID, source kind, declared rights
outcome and all four explicit Yes/No permissions. No permissive defaults exist.
Cancel is the modal default. Registration does not infer legal rights or musical
quality from the file or source kind.

The worker binds the whole captured context and evidence identity. Changing the
project, manifest, selection or license capture invalidates an old form. The
repository rereads evidence against the captured hash before committing. Capture
cancellation is discard-only; a real committed receipt survives late cancellation
and reports stale UI adoption explicitly. Existing source/take provenance and
unsaved manifest edits are preserved.

Source registration has separate capture/receipt state from source-quality
assessment and unit review. Eighteen controls remain non-overlapping at 720,
960 and 1440 pixels; the added source row uses the existing Review surface.
The waveform is scaled to its revised height rather than using old fixed offsets.
macOS uses the modal focus-restoration helper verified in the prior increment.

## Verification

- Full Release build passed, with existing non-fatal duplicate-library warnings.
- **7/7 focused CTest suites passed, 24.22 seconds**: U2, U3, core, Studio review,
  Studio draft/source, macOS source contract and Windows source contract.
- Core: **839 passed, 0 failed**. Studio draft/source: **34/34**. Studio review:
  **9/9**. `git diff --check` passed.
- Four new native-controller regressions cover source-free registration,
  cancellation, required permission choices, changed evidence, stale edited
  manifests, recapture during a nested modal and late cancellation after commit.
- Raw execution is retained in `evidence/native-source-registration-2026-09-09/`.
  This is not a new full release-matrix run or Windows runtime qualification.

## Live macOS evidence

The actual app was opened at 960 x 640 against isolated fixture
`/tmp/seam-native-source-setup.dPCAeG/producer`, initialized through the actual CLI
as a source-free Draft with one missing planned assignment. Synthetic input was
forced; no microphone or audio recording was used.

1. L opened the file chooser; capture exposed the expected evidence path/hash.
2. S opened a readable form with every choice initially unselected. The captured
   context was selectable/read-only, and no fields or buttons overlapped.
3. Escape followed by S reopened the form without a canvas click. Cancellation
   preserved project SHA-256
   `9aeba554808924d26a9e88e48a0cfd64955fe91092a248c9ffe0732e5d291f96`.
4. The only committed declaration was fixture source `qa-unassessed-source`,
   procedural kind, **Not assessed** rights and **No** for every permission.
   Generation advanced to 2; coverage/listening remained Not assessed, with
   zero takes, zero reviews and zero unit approvals.
5. Final project SHA-256:
   `cc5efa8ee04ea9c1cd5e50ed1d652d200f1918fe3406c7c39c0e1a0357fee247`.
   Evidence SHA-256:
   `b052af47e65c4dae984bd9d4c201a90a920bcf34827818634a9af7a4976529cd`.
6. The app closed normally with exit 0, `input_physical=false`, zero callbacks,
   zero recorded frames, one missing assignment and zero approved units.

Studio executable SHA-256:
`642dd9e2852be31fcc6a43f9ac58cd95ae8f55a9e4729dd7aa93a603fd24064c`.
Screenshots were visually inspected during the live run. This establishes the
focused macOS form flow, not broad accessibility, IME or musician acceptance.

## Preservation and remaining work

Comparison against the 1,933-file `session-preservation-SBxgp7` snapshot found
no missing captured files. The ten changed prior paths match this native UI
increment. Existing unrelated dirty work remains intact; no staging, commit or
push was performed, and no real source rights or singer qualification were asserted.

Native creation of an entirely unconfigured producer, operator management,
selection of an already registered source, legacy take attribution,
language/style-aware assignments and unit-kind QC remain open. The current native
open path still requires an inventory digest; this increment does not pretend an
inventory-free project is a configured recording plan. Voice construction,
classical/neural musical qualification, host and release requirements also remain.
