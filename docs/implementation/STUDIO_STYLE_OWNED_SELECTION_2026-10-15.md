# Studio style-owned selection and durable marker targeting

Date: 2026-10-15, Asia/Seoul. Engineering result: **independently APPROVED; one P3 follow-up independently resolved**. This unit fixes a reproduced producer-data targeting defect. It does not approve an internet source or production voicebank.

## Defect and implementation

`libs/seam-native-ui/src/voicebank_studio_production_project.cpp:1170`, `VoicebankStudioController::selectedProductionAssignment()`, matched a loaded bank unit to a producer assignment using coverage key and pitch only. Style-owned projects can legitimately contain both `warm` and `soft` assignments for the same `sustain:a`/MIDI 69. With warm first in storage, selecting the soft bank unit and importing a retake superseded the warm take. The source's raw hash does not disambiguate the assignment: the regression deliberately uses the same synthetic WAV for both styles and distinct take IDs.

The lookup now additionally requires `assignment.style == unit.style` when the producer schema is at least `kProductionStyleSchemaVersion`. Legacy project matching and the producer-row selection path used without manifest units retain their existing behavior. A style-owned bank unit with an unowned style returns no assignment rather than borrowing the first coverage/pitch match. Existing inspection, retake and edit callers share this corrected lookup.

`libs/seam-native-ui/src/voicebank_studio_production_edits.cpp:100`, `persistProductionMetadata()`, independently duplicated the incomplete lookup. It now uses `selectedProductionAssignment()` and preserves its existing missing-assignment/take errors. Saving soft markers consequently records a durable metadata revision against `soft-a`, rather than `take-a`. There is no new source schema, rights declaration, approval transition or import override.

## Failure-first verification

The unchanged production implementation at base `c47c30b51c20064055bdb00d07751aaf9621b577`, plus the new tests, was fully rebuilt before execution. All three new native cases failed in 0.532 seconds:

| Regression | Observed baseline failure | Final durable assertion |
| --- | --- | --- |
| Selected soft retake | Stored in style `warm`, superseding `take-a` | New take is soft, supersedes `soft-a`; warm assignment remains `take-a`; soft assignment owns the new take |
| Selected soft marker save | Revision stored on `take-a` | One `MARKERS_AND_PITCH` revision targets `soft-a`; take IDs/styles/raw hashes, source bindings and assignments are preserved |
| Unowned selected style | Inspection unexpectedly succeeded | Inspection and import refuse; recovered and in-memory producer documents remain byte-equivalent to the original |

The new tests are in `tests/test_studio_manifest_draft.cpp:194`, `:213` and `:238`. The retake remains MarkerReview or Rejected according to actual QC, and no test creates reviewer approval. The unowned-style case loads a valid ordinary manifest outside a generated draft descriptor, ensuring refusal comes from assignment ownership.

After the two production fixes, `cmake --build build/release -j6` completed successfully in 11.572 seconds. This rebuilt dependent targets before CTest. The focused CTest selection passed **5/5 lanes in 25.558 seconds**, including all three regressions and the pre-existing legacy paths:

| Native lane | Passing cases |
| --- | ---: |
| `seam_style_owned_candidate_import_tests` | 3 |
| `seam_studio_sample_review_tests` | 14 |
| `seam_studio_manifest_draft_tests` | 41 |
| `seam_studio_generation_bank_tests` | 2 |
| `seam_studio_recording_import_parity_tests` | 1 |
| **Total** | **61** |

## Source and execution binding

The [evidence index](STUDIO_STYLE_OWNED_SELECTION_2026-10-15.evidence.json) binds the frozen root `out/evidence/studio-style-owned-selection-2026-10-15`: **41 artifacts/four unchanged gate pins**. Manifest SHA-256 is `f058b72ebd58f951ef19e7dd29339a64ac743813a54c09bb4cafb54be08fca57` (7,997 bytes). It retains baseline/final patches, exact source snapshots, execution receipts and raw output, generated version/CTest files, baseline test executable, six final executables and a read-only hash/source/log verifier.

The baseline patch hash is `51e989bff17ec4d4015811fad38980d272acf6acedddc9e3a57c809a41470cfe`; the final unit patch hash is `d8b7cd8aa4e58d4b14b5416276cbf38bd37295ac02a2bf99e8a264cce9658a43`. Receipts confirm unchanged HEAD and source diff during each command. Final executable hashes were checked before and after CTest. Generated version metadata embeds the base commit: the tested implementation is **base plus retained unit patch**, not the base tree alone or a later commit embedded in the binaries. `verify.py --commit <landed-commit>` can additionally compare the three changed source blobs with the landed commit without executing native tools.

## Acceptance boundary and next work

These are synthetic native-controller/repository cases. The recording-parity lane does not establish a physical microphone session. No full suite, sanitizer run for this unit, AppKit interaction, human listening, real-source registration/import, external human review, platform/DAW/signing or release acceptance is claimed. Dependency closure and a clean-from-scratch build are not claimed.

The four pinned Alpha/Beta/contract/bank-dossier documents are unchanged. **Alpha 0/20; U45 HELD; Beta BLOCKED; full goal ACTIVE.** Genuine producer and distinct reviewer identities, source-specific rights/performer/EULA disposition, failed-QC policy, pitch/phonetic coverage and listening remain required. The separate internet f1 source-extension acquisition has engineering approval on its exact commit; neither review substitutes for source admission. Next: continue the remaining source-to-bank and installed product acceptance work. `.github`, README's Windows TODO, stashes and prior evidence are preserved.

## Independent review and follow-up

**Review in English** independently approved exact `47d66514349258d617853391f74da4aa14c6f8f5` with **one P3/no required change to the landed fix**. The exact final answer is retained at `out/evidence/studio-style-owned-selection-independent-2026-10-15/review-47d66514.md`: 9,733 bytes, SHA-256 `d7c5096ed86e0d73fbeb8a77da5fefdf26fb0e389e006c4debff6e0004d7e6be`. Its own read-only Git/source/hash/log analysis confirmed the patches, dependent relinks, three exact baseline failures and 61 passing cases; it executed no build, test, native tool or retained script. This is engineering approval only.

The P3 is a separate queue-state lookup that ignored style. It is implemented by `271a03b9e616d7643a6321c75a768f79c4adb786`, with a shared helper and three additional regressions; see the [queue dossier](STUDIO_STYLE_OWNED_QUEUE_2026-10-15.md). Its exact implementation is independently APPROVED with zero findings, resolving the P3; see the queue dossier for the retained verdict. A style-owned project with a manifest carrying another style, including an older default `original` label, intentionally refuses when that style is not owned; there is no automatic remapping. The existing real-manifest unowned-style regression exercises refusal, but an explicit old-label migration test was not added.

Optional review coverage was expanded in the queue follow-up to the core, export and original-singer workflow lanes. The earlier five-lane executable recheck is an executed runner assertion without a separate retained after-record; do not treat later evidence as a retroactive paired record. The extra three lanes now retain explicit before/after identities. The pre-existing manifest-before-producer save ordering and the procedural CLI's first coverage/pitch style lookup remain separate open engineering observations. None is silently counted as source or Beta acceptance.
