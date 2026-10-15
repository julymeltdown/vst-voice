# Prepared donor schema and retained evidence — 2026-10-15

The native project model can now persist and verify a take's prepared-donor provenance under schema 5. This is a prerequisite, not a completed importer: the new persistence tests deliberately construct synthetic models and retain their files before saving. No downloaded recording was registered, imported or approved by this unit. Packet-bound candidate publication explicitly refuses until its notice-closure format is implemented.

## Code contract

`TakeSourceBinding::preparedDonor` is an optional typed record with exactly five fields: `packetSha256`, `donorId`, `intakePlanSha256`, `originalLicenseSha256`, `attributionSha256`. Its presence requires schema 5, English ownership and a human-recording source strategy, matching the current prepared packet contract. Digests must be lowercase SHA-256; donor IDs are bounded safe identifiers. Missing, null, extra-field, malformed and legacy-schema forms are refused.

`takeSourceBindingId` preserves the existing compact-JSON v1 identity for ordinary imports. The v2 identity additionally commits all five prepared-donor fields; validation requires that identity to match. Existing immutable-binding equality/preservation now includes the typed optional record. Different takes can reuse one packet/donor and raw asset with distinct take identities. There is no new global uniqueness policy forbidding legitimate independent style ownership or retakes.

Schema 1–4 remain supported; ordinary projects are not automatically upgraded. Schema 5 retains schema-4 language/style and declared-layer semantics. The preceding reader rejects version 5 as unsupported before decoding records (source inspection, not an old-reader runtime test in this unit). The current decoder explicitly rejects unsupported version 6. Requiring a new schema prevents old readers from silently discarding required provenance as an unknown field.

`source_internal::verifySnapshots` checks the existing reviewed declaration and then reads the four retained packet files from `source-evidence/packets/<packetSha256>/`: `donors.json`, `intake-plan.json`, `license-source.txt`, `attribution.txt`. It uses the existing bounded evidence reader, checks the path chain for links, pins the packet/selection and compares plan/original-license/attribution identities and the selected row's declared derived-audio digest with the take's captured raw digest. It caches only four digest strings per distinct packet/selection for reused donors, not whole packet copies.

The reviewed source-specific declaration remains under `source-evidence/<strategy-license-sha>.txt`; its digest is independent of the original corpus license. Whether a real declaration authorizes the corpus is a real rights-review question, not inferred from two hashes. Evidence-only parsing does not decode or measure absent raw audio; repository asset verification separately checks the stored asset. The existing per-file caps are retained: 16 MiB packet, 1 MiB plan, 1 MiB original license, 2 MiB attribution, and a separate 4 MiB reviewed declaration.

`publishReviewedCandidate` refuses any project carrying packet-bound takes with `Unsupported` and a notice-closure diagnostic, before staging output. This temporary refusal covers both current and exact-generation publication entry points. No candidate descriptor, signing, package or installation format is changed here. Candidate-4 notice closure must replace this refusal as a later verified unit.

## Executed verification

Source base: `bd35ddac9851940c9b62a8c3c4546d10632b6989`; exact eleven-file implementation/test patch SHA-256: `af3df32449cd2a0100d78afbf9ca71117b6a35176c81d43079d16811a843fe91`. Evidence under `out/evidence/prepared-donor-schema-five-2026-10-15/` is bound by the adjacent JSON index.

| Execution | Result |
| --- | --- |
| First full dependent Release build | Compile refusal for a shadowed local variable, 10.101 seconds; corrected before tests |
| Corrected full dependent Release build | PASS, 80.181 seconds |
| Eight selected production CTest lanes | 8/8 PASS, 14.707 seconds |
| Native case executions | snapshot 8, style-owned import 3, draft 26, style migration 4, import outcome 8, English lifecycle 1, review 24, general production 62: total 136 |

Three new unique cases cover exact schema-5 round trip; every missing/invalid typed field; v2 identity change; null/unknown fields and version/source/language refusal; retained persistence after deleting all five preparation inputs; independent donor reuse with one audio blob; refused binding rewrite; publication refusal without output; and deletion/change/symlink damage to each of the four notices. Damage cases assert recovery/save refusal with unchanged workspace and caller bytes. These are synthetic records and PCM, with explicit unapproved state and no real operator, rights, source-QC or musical acceptance claim. The persistence fixture bypasses the future importer and does not prove its inspection/atomicity requirements.

All execution was terminal before freezing 50 artifacts and ten preserved bindings. Retained evidence includes the failed and corrected source snapshots/patches, build logs, final CTest logs, eight copied executables and explicit before/after identity records, version headers, parent evidence, the license-identity correction and the exact recovery-review verdict. The read-only verifier passes without executing retained native tools. No old-code failing baseline, sanitizer, full-suite, Python schema-5 producer parity, real-donor import, AppKit, device, DAW, Windows, signing or human acceptance is claimed. Binaries embed the source base; the patch identifies the implementation tested.

```sh
python3 -B out/evidence/prepared-donor-schema-five-2026-10-15/verify.py
# Use --commit COMMIT after integration or later source changes.
```

## Ordered remaining work

1. The independent review's pre-existing raw/derived asset-loss P2 now has a locally verified [repair](CERTIFIED_ASSET_RECOVERY_2026-10-15.md), with both reproduced failures, removal/change/link tests, current/stale writer refusals and the journal fallback control. Independent review of the asset repair is pending. Declaration/quality recovery P2 and the parser wire-type P3 are independently closed.
2. Implement inspected prepared-donor import from a validated owned snapshot: exact project/packet/donor expectations, genuine source strategy association, explicit assignment, private temporary inspection bytes, cancellation, atomic binding/snapshot commit and no reopening of preparation paths. Preserve ordinary import and retake behavior. Test genuine declaration/original-license differences with synthetic inputs.
3. Implement candidate-4 complete notice closure in directory and package verification, retaining schema-3 dependency semantics. Require every take's declaration plus the exact shared four-file packet union; test omitted/swapped/re-signed notices, aggregate evidence bounds and installed-file tampering. Remove the publication refusal only with the complete path verified.
4. Finish real source admission with the pending owner rights/EULA/performer disposition, actual producer/distinct reviewer, assignment/phonetic decisions, failed-QC policy and listening evidence, then execute the applicable acceptance gates and remaining full-product requirements.

The new schema unit awaits independent implementation review. The wire-type repair review is separate. Canonical gate/dossier bytes remain unchanged: Alpha recorded 0/20, U45 HELD, Beta BLOCKED. Internet speech/singing/growls remain distinct unadmitted source categories. `.github` is untouched; Windows remains TODO.
