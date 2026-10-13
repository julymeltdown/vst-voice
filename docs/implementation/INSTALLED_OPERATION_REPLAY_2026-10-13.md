# Installed sample and recipe operation replay

2026-10-13. Engineering unit; technical review **APPROVE** for `168467a4`. The [review closeout](INSTALLED_OPERATION_REPLAY_CLOSEOUT_2026-10-13.md) retains the exact verdict and supplemental evidence corrections. Alpha remains **0/20** and Beta remains **BLOCKED**.

## Problem and resulting behavior

Native installed sample and recipe exports already emitted observations, but no closed Python reader could compare a retained observation with a new native render. Hash consistency alone could accept an audio file, receipt and observation changed together. `scripts/verify_installed_export_record.py` now requires caller-selected input paths and externally pinned observation, public key and producer executable. It renders again into a private temporary directory and compares the native result with the retained export. The retained export is never recovered or rewritten by this reader.

This is a local reproducibility check. It reports `ENGINEERING_PASS`, `FRESH_NATIVE_RENDER_MATCH`, `historicalExecution: NOT_AUTHENTICATED` and `authorizesRelease: false`. It does not attest that a historical process ran, authenticate the operator, approve a bank or satisfy the canonical U45 semantic validator. In particular, an externally supplied executable pin identifies bytes; it does not establish that those bytes implement trusted code.

## Code and data boundary

`tools/external_beta/installed_export_record.py` closes the sample/recipe record shapes and delegates common installed/project linkage to `validate_installed_binding` in the existing composite reader. Sample id/version/content and selected project pins must agree. Recipe identity remains derived by the native verifier from the captured signed payload; Python does not substitute package aliases for recipe identity. The existing composite record shape and resource-language validation remain unchanged.

The observation supplies data only. The caller supplies project, package, installation, retained export, trusted public key and executable paths. The command is selected exclusively from `export-installed-sample-project` and `export-installed-recipe-project`. The native producer remains responsible for project/package/installation verification, signed asset capture, rendering and export commitment. No new C++ product API is introduced.

Replay compares every observation field except `exportReceiptSha256`. That digest necessarily changes because each native receipt has a new `transactionId` and `executionDateUnixMs`. Both receipt digests are individually checked; the entire parsed receipt is compared except those two native execution fields. Their shapes are checked, but their historical truth is not authenticated. Artifact hashes, writer measurements, project languages, source/build identity and receipt render semantics must match the fresh render. Comparing the writer's peak does not claim to independently measure the encoded WAV peak.

The retained and fresh export roots use held POSIX directory descriptors. Owned files require canonical relative paths, no final-component or traversed-child symlinks, stable file observations and streamed SHA-256 checks. Receipt capture is bounded at 16 MiB. Artifact lists have a structural upper bound of 1,024 entries, but the 64 KiB record/native-stdout limit reduces the usable count according to serialized sizes; 1,024 pretty-printed native entries cannot fit. Individual files are bounded at 2 GiB, and each list traversal at 2 GiB aggregate; repeated traversals are intentional, so 2 GiB is not the total I/O budget for the whole call. Timeout is finite and within `(0, 300]` seconds. The existing process helper bounds output and handles timed-out process groups. A sibling interrupted-export journal refuses replay without recovery. After native execution, retained owned files and the externally pinned executable/key are checked again. These controls detect ordinary substitutions; they are not a sandbox against a hostile executable, privileged concurrent actor, substituted parent path or dynamic loader. Unowned stable export entries are outside the file comparison.

## Execution evidence

The frozen root is `out/evidence/installed-operation-replay-2026-10-13`; its manifest binds **46 artifacts and 10 preserved inputs**. Baseline source is `a50dade048b6371f5ba993df0633d861993fcb18`. The retained original baseline descriptor covers three files; a separate addendum captures the export-service test from that immutable Git commit after execution. No failing old-reader test is claimed because the reader did not exist. The tested current native executable reports source base `2dc07921c71c2e9bfac7ca1e52d17cae8677e84e`; exact working source captures bind the subsequent uncommitted test/reader edits. This is not a clean committed-candidate build claim.

| Check | Result and scope |
| --- | --- |
| Full dependent Release build | Passed, 49.359 s; all targets rebuilt, not a full CTest run |
| `seam_export_tests` | 38/38, 6.54 s |
| `seam_external_beta_python_contract_tests` | 434/434 Python methods, 103.71 s |
| `seam_sample_review_cli_tests` | 27/27 native cases, 14.44 s |
| Focused CTest total | Three entries, 499 cases/methods, 124.70 s wall time |
| Standalone policy probe | 9/9, overlapping the 434 methods; seven new mocked-process methods plus two existing composite methods |
| Retained original-producer replay | Real historical sample fixture, exit 0, 0.543 s; retained set unchanged |
| Read-only evidence verifier | PASS; hashes, source captures, receipts, counts, coordinated-forgery inputs and unchanged acceptance gates |

The new Python methods test policy, held-file refusal, limits, external pins, substitutions and private-output cleanup with a **mocked native process**. Real native sample and recipe replay runs inside two existing CLI cases. Each family has one valid replay, one altered writer-measurement refusal and one coordinated forgery refusal: **six real reader/producer calls**, embedded in the 27-case entry, not six additional C++ cases. The coordinated forgery changes the WAV, receipt file digest, observation master/artifact digests and external observation pin consistently; only fresh rendering reveals the mismatch. Retained test observations and forged file sets are bound in the new root. Their verdicts rest on the CTest assertions; individual helper stdout is not separately retained.

The independent historical sample replay uses the original producer bytes copied from the earlier sample fixture, SHA-256 `8668a0064d4992594436fd57cbe939fef03d4e76a2da561a26982d22ba060921`, compiled at `5fb5da63`. It matches master SHA-256 `7f19320ce555e2015b1da68a1dd6096ae6bea8350ed98af5af87ae0ae33e4c54`. This 0.543 s probe predates the final binding-module edit and is preserved as historical evidence, rather than proof of the committed reader. The [supplement](INSTALLED_OPERATION_REPLAY_CLOSEOUT_2026-10-13.md) reruns the real probe against hash-bound committed `168467a4` reader sources: exit 0 in 0.1315 s with unchanged retained export. Both additional real replays are separate from the six native calls above. They prove compatibility with that externally pinned producer; they do not approve arbitrary producer versions.

Two review controls are also closed. A direct `exportSetWithSources` test checks that a wrong expected WAV digest refuses before destination/journal commitment and leaves source bytes unchanged; the correct digest then commits. Its pins are caller-provided synthetic values with `UntrustedInstalled` trust, not proof of a real signed bank installation. The reverse recipe-as-sample command refuses with exit 2 and creates no output; the previous sample-as-recipe control remains.

The earlier sample manifest omitted `native-fixture/installed/manifest.json` from its artifact list. The new root binds those exact 7,214 bytes, SHA-256 `593038ff11dff97af906673fdf6e525587fa6551b91c1331bd2649375ad4aba4`, and verifies the candidate's matching `manifestSha256`. The old frozen root remains unchanged. New root enumeration excludes only its exact root manifest and verification log, so nested manifests are retained. The earlier sample/Studio/pointer approval verdict is also bound; it is not approval of this new reader.

## Remaining work and acceptance

This closes the local installed sample/recipe observation replay prerequisite. It does not reconcile the canonical U45 hold or implement its remaining continuity/measurement/authority semantics, learned-model/vocoder qualification, installed-candidate musician journey, Windows/DAW matrix, signing or release evidence. The previous complete 1,524-case core result belongs to the separate Studio repair; no new aggregate-core or entire-suite PASS is claimed here.

Internet female originals, unassigned cuts, null listening worksheets and the frozen observation kit are preserved and are not admitted as a production bank. Real producer and distinct reviewer, source/performer and public-licence disposition, failed-QC policy and human phonetic/pitch/listening judgments remain pending. Automated synthetic fixtures cannot supply those decisions. The full goal stays **ACTIVE**, Alpha stays **0/20**, Beta stays **BLOCKED**, `.github` is untouched, README Windows TODO remains and the existing stash is preserved.
