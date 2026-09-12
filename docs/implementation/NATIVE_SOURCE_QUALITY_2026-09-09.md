# Native source-quality assessment and preservation checkpoint

## Result

Studio now exposes the existing source-quality workflow through native evidence
selection and an explicit reviewer-decision form. This increment does not grant
rights, approve units, qualify a real singer, or accept another Full-Scope unit.
The full Beta GO objective remains open. Changes are local and uncommitted.

## Implementation

- `voicebank_studio_source_quality.cpp` captures producer/selection context,
  policy/material identities and evidence bytes on the existing serialized
  review worker. A draft manifest is not required for source assessment.
- The decision binds the entire captured inspection, including evidence identity.
  Replacing a capture in a nested modal cannot silently approve different data.
  Backend validation rechecks evidence, independence and current project state.
- Durable commit receipts survive late cancellation or stale UI context. Unsaved
  manifest edits are preserved. Source permissions and unit approvals remain
  separate from coverage/listening decisions.
- AppKit and Win32 forms require explicit reviewer choice and assessment ID;
  both outcomes default to Not assessed. Cancel performs no write.
- Review adds Source Evidence and Assess Source controls and I/D shortcuts.
  Text sizes were increased; prose now wraps at spaces while retaining all
  original bytes, with hard wrapping for long identifiers and UTF-8 boundaries.
- Native QA exposed lost keyboard focus after modal cancellation. The generic
  AppKit file chooser and source-decision form now restore the prior visible
  owner and responder after the modal returns, without reopening a closed owner.

## Verification

Full Release build passed after the final focus/wrapping changes. Existing
duplicate-library linker warnings remain non-fatal; compiler policy was unchanged.

Final focused CTest: **7/7 PASS, 23.84 seconds**, covering U2, U3, core, Studio
review, Studio draft/source workflow, macOS source contract and Windows source
contract. Core: **832 passed, 0 failed**; Studio draft/source: **30/30**;
Studio review: **9/9**. `git diff --check` passed.

Six new source workflow cases cover explicit commit without a manifest,
ineligible reviewer/changed evidence, stale modals, cancellation/control bounds,
same-project recapture and late cancellation after a durable commit. Existing
minimum-width wrapping assertions now verify prose reconstruction and word breaks.
All 16 controls have non-overlap checks at 720, 960 and 1440 pixels.

Raw final results: `evidence/native-source-quality-2026-09-09/`.
This was a focused regression, not a new full 120-entry run. The prior full
source-closure failure remains unresolved; no staging was used to hide it.

## Live macOS evidence

An isolated synthetic producer copy was exercised at 960 x 640. No physical
microphone, recording, actual reviewer identity or actual singer approval was used.

1. Evidence chooser and captured source/hash context worked in the real app.
2. Cancelling the decision left the project bytes unchanged.
3. Explicitly recording fixture ID `qa-not-assessed`, fixture reviewer `reviewer`
   and two Not assessed outcomes committed generation 3. Approved unit count
   remained zero, with one MarkerReview unit. No source permissions were granted.
4. After the final rebuild, I -> Escape -> I reopened the evidence chooser;
   D -> Escape -> D reopened the assessment form without a canvas click.
5. The final capture/cancel-only check retained project SHA-256
   `deecb70128b2f31909fde45143b80f5961bad4eee8e85e6e299e75316d20b29e`.
   The final Review screenshot showed word-wrapped text and separated controls.

Final Studio executable SHA-256:
`ccea8df54d08d2abd2e371e2bbc91888e2b16f359de546ade449dbd7aabdf0b6`.
The earlier native Not assessed commit used the pre-typography binary
`4077192e3bd6f340f9e072f6911d1321f1dc4847ab2a54292656bde5e74cc598`.
These are engineering observations, not broad UX/IME/platform qualification.
Win32 is implemented and source-checked, but has not been runtime-tested here.

## Session-loss precautions

Compared all file entries in recovery snapshot `session-preservation-o0Etlj`
(1,927 captured paths) against disk: **no captured file is missing**. The ten
changed pre-existing files matched this native source-review increment; the new
controller source and this report are additional files. Existing dirty changes
were preserved. This comparison cannot establish whether changes lost before
the first available snapshot ever reached disk.

The local preservation script creates a new immutable-by-convention directory,
source archive, per-file hashes and working/index patches, then verifies source
stability and archive readability. It does not stage, commit, upload or include
ignored build outputs. Continue comparing actual files on resume; do not treat
chat claims as proof of persisted implementation.

## Next development boundaries

Initial source/rights configuration, legacy take attribution, language/style
migration, unit-kind QC, large-bank responsiveness, evidence disclosure policy,
complete singer construction and independent musical qualification remain open.
The next capability should improve the real producer-to-editable-bank journey;
passing this workflow's engineering tests must not be relabeled Beta GO.
