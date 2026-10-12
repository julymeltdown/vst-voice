# Take inspector 3: current evidence and readable history

Date: 2026-10-13, Asia/Seoul. Base: `5906c9b8fd484e9ab2bdd46e8bef5c4f025f81b0`. Fetch and fast-forward pull completed before this unit; prior edits and the existing stash were preserved. This is an identity and compatibility repair, not production-bank or Beta acceptance. [Evidence index](TAKE_INSPECTOR_VERSION_HISTORY_2026-10-13.evidence.json).

## Problem and resulting behavior

The boundary pitch repair changed 20 of 54 retained inspector policy cases while both binaries reported inspector 2. It affected format-valid 48 kHz PCM24 input, so an equivalence exception for 48 kHz would be false. The independent reviewer approved the parent numerical scope and requested this coupled repair. The exact parent verdict is retained in the unit manifest; that approval does not cover this implementation.

New raw imports, processed outputs and explicit reinspections now report `seam.take-inspector` **3**. The receipt schema remains 2 and QC threshold policy remains 1: neither thresholds nor failed-QC approval disposition changed. Historical inspector-2 receipts remain structurally readable, canonical and byte-exact. Only inspector 3 counts as current automated evidence for a new approval or candidate publication. Import with an expected inspector-2 receipt returns an explicit older-inspector conflict before mutation.

Existing approvals are retained as history on loading. Publication refuses an approved take lacking a current inspection of its reviewed audio. Explicit reinspection appends a version-3 receipt, retains prior receipt/review records and generation bytes, and lowers the previous approval to marker review because its material basis changed. A new independent decision is still required. This deliberately avoids bulk migration or silently assigning new measurements to old approvals.

## Code contract

- [Inspector identity](../../libs/seam-voicebank/include/seam/voicebank/take_inspection.hpp): compiled identity 3. [Decoded evidence](../../libs/seam-voicebank-production/include/seam/voicebank_production/take_inspection_receipt.hpp) carries the recorded version and exposes an explicit current-algorithm predicate.
- [Receipt codec and lookup](../../libs/seam-voicebank-production/src/take_inspection_receipt.cpp): closed supported set `{2,3}`; canonical re-encoding preserves the decoded version. Existing exact binding, digest, derived lineage, asset size, applicability and measurement/outcome checks remain. Raw and active-derived currency checks skip version 2. Unsupported versions 1/4, numeric/boolean/null identities remain refused.
- [Import](../../libs/seam-voicebank-production/src/repository_import.cpp): an older-inspector expectation cannot admit a new take. [Candidate binding](../../libs/seam-voicebank-production/src/repository_candidate.cpp): current active inspection must describe the exact reviewed audio SHA before a candidate binding passes. This guards publication from approvals that have not been reinspected.
- [Journal contract](../../libs/seam-voicebank-production/src/repository_history_internal.hpp): new writes declare review inspection policy 3. [Native verification](../../libs/seam-voicebank-production/src/repository_verify.cpp) accepts only absent, 2 or 3 markers for supported history. It validates structure/ancestry and does not replay historical decisions; current native writes still apply the current review transition.
- [Python transition mirror](../../tools/external_beta/_production_review_transition.py): policy 2 replays approvals with inspector 2; policy 3 requires inspector 3. The active policy is monotonic, so later marker 2 or an unmarked save cannot downgrade 3. Full [draft validation](../../tools/external_beta/_production_draft_validation.py) validates complete receipts before transition replay. The presence helper alone is not a receipt-content validator. The broader [workspace validator](../../tools/external_beta/_production_workspace.py) shares supported marker recognition; it does not gain full decision replay from this change.
- [Studio](../../libs/seam-native-ui/src/voicebank_studio_sample_review.cpp): current checks display the receipt's recorded version. Matching historical checks display inspector 2 and an explicit reinspection requirement, without appearing as current signal-check acceptance.

## Verification and its limits

The new version-2 currency regression failed against the baseline: six existing take-inspection cases passed, the new case failed. An earlier harness build typo is retained separately and is not a product failure. After implementation and Studio changes, full dependent Release rebuilds passed before CTest. The final run passed **7/7 entries, 140 cases, 16.65 seconds**: Studio 14, recording-import parity 1, sample-review CLI 26, pitch 9, take inspection 7, production review 24 and production 59. Intermediate passing runs overlap and are not added to this total.

Three Python modules passed **43 tests** (receipt, review-transition and production-draft parity), including actual native CLI calls; the draft-workspace module passed **4** more. No skips occurred in these runs. Native regressions cover old raw/derived decoding, exact re-encoding, current lookup, new-approval refusal, import refusal, candidate-binding refusal and historical Studio presentation. Python controls cover the closed identity set and per-generation policy progression.

The retained baseline CLI SHA is `e351b579d5c7a4559a214a32196aa2f853621a3f83c4b7e08811dc02e1fb582e`. It actually created a synthetic workspace and approved one take under inspector/policy 2. The current CLI, SHA `1ffca5ca58435f1d27ee1a879fd81953456edfc4396d6fec56097b0ca89d361d`, then:

1. Loaded the old workspace; full Python history validation passed.
2. Refused a new approval using the old receipt, with every workspace file unchanged.
3. Reinspected the take, recorded inspector 3 and lowered `APPROVED` to `MARKER_REVIEW`.
4. Preserved all prior metadata/review objects, original generation/journal bytes and the untouched baseline fixture.
5. Accepted a subsequent explicitly issued fixture reviewer decision with current evidence; native loading and Python full-history validation passed.

Additional forged negative controls append an approval for a take still carrying only inspector 2 after policy 3, using either marker 2 or no marker. Python rejects both. They are explicitly forged controls, not generations written by the native CLI. Fixture operator IDs, permissions and decisions are synthetic test data; none identifies a real producer, rights holder or human listener. No installed U14/U45 journey, general pitch-accuracy claim or release acceptance is established.

The evidence verifier checks retained source snapshots, binaries, run logs, old fixture bindings and canonical status. The predecessor's separate verifier also passed: 142 parent artifacts, predecessor 90+2 bindings and source supplement 124+8 bindings. Original media, the null worksheet, cuts and frozen owner kit remain untouched. The historical parent manifest's open-identity wording is preserved as a snapshot; this new unit supersedes that implementation status after landing.

## Remaining product work

Independent technical review of this unit is pending. The parent's optional numerical/report follow-ups remain explicit: the 1,088 changed frames include 976 previously voiced in-range frames becoming unvoiced; general aliasing, boundary rounding and true voicing accuracy are not proved by an interval check. This unit identifies changed measurements; it does not qualify them musically.

The internet route has 25 clean female VocalSet originals and 272 female Karoryfer growl originals. Production source/performer rights disposition, actual producer/distinct reviewer, phonetic mapping, useful pitch coverage, human listening and failed-QC policy remain unresolved. Full original product, platform, host, recovery and release exits remain required. Canonical Usable Alpha is **0/20 recorded passes** and External Beta is **BLOCKED**. Overall implementation percentage is **not established**; the full goal stays active. `.github` is untouched and README Windows support remains TODO.
