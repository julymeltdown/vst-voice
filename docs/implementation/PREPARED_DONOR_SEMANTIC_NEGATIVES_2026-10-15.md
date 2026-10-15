# Prepared donor semantic refusal coverage — 2026-10-15

The candidate lifecycle test now reaches the shared source verifier's deeper provenance rules instead of relying only on damaged-file/listing refusals. Eight negative forms and one positive control exercise the exact checks identified by the independent `01e78a0c` review. Runtime implementation, schemas, QC thresholds and approval policy are unchanged. This is a locally verified repair of that native coverage finding; exact independent closure is pending.

## Why the earlier tests were insufficient

The existing twelve re-signed omission/substitution forms remain useful end-to-end package tests. They alter a required file or its descriptor entry while the producer snapshot still fixes the original digest. `verifyResourceCandidateSourceEvidence()` therefore refuses inside `readListed()` before parsing packet facts or comparing later captured bindings. Those cases could remain green if a deeper comparison were accidentally removed.

The new cases extend `tests/test_english_candidate_lifecycle.cpp`, inside the existing prepared-donor lifecycle case. The fixture already uses four actual native imports over two distinct generated tones, two pitches, two styles and two independent synthetic declarations. No downloaded voice, real source permission or listening decision is used.

## Fixture invariants and tested branches

`rebind()` recomputes each changed `TakeSourceBinding` ID with `takeSourceBindingId()` and updates the owning take's `sourceBindingId`. `exerciseSource()` encodes the modified project, requires successful native decoding, supplies those owned snapshot bytes and updates the descriptor's project digest plus production-snapshot listing. It sorts and validates the descriptor before calling the shared source-evidence API.

Every reader call must return a listed file with the exact listed size/hash and satisfy the requested cap. The test rejects duplicate reads. It requires the first read to be `provenance/production.json` and counts packet-file reads. Refusals must match their exact intended diagnostic; an unrelated generic failure cannot satisfy a case. `[SOURCE-SEMANTICS]` lines retain the observed outcome and read count in CTest output.

| Form | Internally maintained identities | Required outcome |
|---|---|---|
| Unchanged positive control | Same native project, descriptor and evidence | PASS; four packet files read |
| Existing donor, different audio | Last binding names the other present tone's donor; binding/take IDs and snapshot listing rehashed | `Candidate prepared donor differs from its retained packet`; four files read |
| Absent donor | Safe but absent donor ID, valid rehashed binding/snapshot | Same precise per-binding refusal; four files read |
| Different intake plan | Only a later shared member's plan digest differs; first member remains valid | Same per-binding refusal after packet facts; four files read |
| Different original license | Only a later member's original-license digest differs; reviewed declaration unchanged | Same per-binding refusal; four files read |
| Different attribution | Only a later member's attribution digest differs | Same per-binding refusal; four files read |
| Rehashed malformed packet facts | Packet status changed to `APPROVED`; new packet digest, all binding IDs, directory-prefix listing and held file identities recomputed consistently | `Expected unapproved prepared donor field: status`; four files read |
| Unreferenced closure entry | Extra structurally valid listed declaration with valid path, digest and byte size | `Candidate source evidence union contains an unreferenced entry`; four files read |
| Schema 4 without a prepared binding | All optional prepared bindings removed; source binding IDs, take references and snapshot listing recomputed | Exact schema-4/prepared-binding diagnostic; zero packet files read |

The later-member cases preserve the first shared member so the verifier can read all four files under its correct digests and parse all-row facts before comparing the modified member. This isolates the cross-member checks from the earlier listing checks. Original-license and reviewed-declaration identities remain independent; no equality policy or global donor uniqueness is introduced.

These new forms call the owned-reader source-check API directly. They do not rewrite a certified producer history or claim independently valid signed packages for the semantic mutations. The original twelve re-signed and twelve installed-damage forms still execute in the same lifecycle case, as do its authentic positive installation and the separate English rendering lifecycle. The separate Python history-isolation finding is not addressed by these C++ API fixtures.

## Exact verification and limits

Base commit is `166833ef330cdea484ed475305c1a03d6f7b72a8`. The only implementation/test diff is the 109-line addition to the lifecycle test, SHA-256 `4d6dbfc828537869847cd8cc0fb064ece5f108957b837802a8cbf2f54653513f`. The adjacent index binds frozen evidence under `out/evidence/prepared-donor-semantic-negatives-2026-10-15`: **38 artifacts, 10 pins**, manifest **8,795 bytes**, SHA-256 `70ef5ea6f66f38531394bf22a98c83578528efc4dd13c31a9670543920cb2323`.

| Source-bound run | Result | Recorded wall time |
|---|---|---:|
| Release lifecycle target and dependency check | PASS; graph up to date, no objects compiled in this frozen stage | 0.070 s |
| Release CTest lifecycle lane | 1/1 lane, both existing native cases PASS; new control + all eight exact refusal traces present | 2.702 s |
| ASan/UBSan lifecycle target and dependency rebuild | PASS, including dependent actual CLI | 12.596 s |
| Sanitized CTest lifecycle lane | 1/1 lane, both existing native cases PASS; same nine traces | 33.753 s |

The two native cases were extended, not added: this is eight new negative forms per run, not eight new CTest cases. Both tested executables, `seam_english_candidate_lifecycle_tests` and its actual `seam_voicebank_cli`, are copied and hashed before/after each run. HEAD and the exact source diff remain stable. Both build versions embed the base commit. Runtime source/header pins match the base Git blobs. The read-only verifier checks all retained artifacts/pins, exact traces/diagnostics/counts, source snapshots/diff, native identities and result summaries without running a retained native binary or test.

A preliminary focused Release build/test passed before the final source/binary-inventoried runs; its summary logs are retained but not used as equivalent identity-bound evidence. The final Release CTest overlapped the separate sanitizer build, so timings are observations rather than a benchmark. These were focused target rebuilds and one CTest lane per configuration, not full builds or the full suite. ASan used `detect_leaks=0:abort_on_error=1:strict_string_checks=1`; UBSan used `print_stacktrace=1:halt_on_error=1`. No leak, TSan, OS-error/power-loss, device, DAW, Windows, acoustic or human acceptance is claimed. No Python tests were rerun because this unit changes only the C++ lifecycle test.

## Review and Beta boundaries

The originating `01e78a0c` exact review remains APPROVE for engineering; it closed schema-4 declaration completeness and raised this coverage P3. The test repair requires its own exact post-commit review. The independent review of CLI commit `166833ef` was separate and in progress at this evidence freeze. It has since APPROVED that exact commit with zero findings; the adjacent index binds the retained final answer after the manifest freeze. Exact `874c5fa2` test-repair review is now in progress. Existing Python present-donor/history-isolation gaps, native PCM range-clause isolation, wrapper cancellation/commit-fault coverage and the lower-impact style-migration follow-up remain open.

No original or prepared real recording was modified, registered, imported, played or approved. Actual owner source/performer disposition, producer and distinct reviewer, coverage/pitch/style choices, failed-QC policy and listening remain required. Canonical Alpha stays **0/20**, U45 **HELD**, Beta **BLOCKED**, full goal **ACTIVE**. `.github` and README are untouched; Windows stays TODO.

Exact `874c5fa2` has now independently received APPROVE with zero findings, closing the originating native candidate negative-coverage P3. The exact 10,708-byte final answer (`46310bb97a6871ab9d22e2b37f8090df32abc7bb0f2fea585718d39c73dddbf8`) and metadata are retained under `out/evidence/prepared-donor-semantic-negatives-independent-2026-10-15` and bound by this tracked index after the original manifest freeze. No frozen manifest or original execution claim changes. The separate [Python test-gap repairs](PREPARED_DONOR_PYTHON_NEGATIVES_2026-10-15.md) are locally verified and await their own exact review. Native PCM-range and genuine admission/Beta gates remain open.
