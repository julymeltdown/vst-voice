# Retained source-evidence recovery — 2026-10-15

Recovery now refuses to hide a committed generation whose captured source declaration or reviewer-quality evidence is missing, changed or replaced by a link. Previously, those failures caused recovery to return older work. Independent engineering review of exact `7aae8bdf1f17243da2461261eade2f18ef37c88e` is APPROVE and closes this P2 for declaration/quality evidence. The exact supplemental verdict and metadata are bound by the adjacent index; the original execution manifest remains immutable.

## Behavior and implementation

`ProductionProjectRepository::verifyGeneration` first checks the stored generation, journal identity, inspection-policy marker and ancestry. Only a generation with that structural binding can classify a retained source-snapshot failure as terminal. `recover()` propagates that failure with the generation number and original error context instead of selecting an older project.

An interrupted or incorrectly bound journal continues to permit recovery of the preceding valid generation. Asset verification remains after retained source-evidence verification, so a simultaneous asset error cannot conceal the evidence failure. Schema-1 legacy live-license behavior is unchanged. There is no project-schema, journal-format, source-policy or approval change. The implementation is in `repository.hpp`, `repository.cpp` and `repository_verify.cpp`; three regression cases are in `tests/test_production_draft.cpp`.

## Executed evidence

Source base: `3e27aa27fad4f4f87176c47aaaf6f316fcb27542`. Exact four-file final patch SHA-256: `61ccc1b995515fd9b993b4ba67245033088d399f592c85bdc92e12b9f782599a`. The adjacent JSON index binds the local evidence under `out/evidence/retained-source-recovery-2026-10-15/`.

| Execution | Result |
| --- | --- |
| Baseline: new tests against unchanged production code | Focused test build PASS, 4.723 seconds; native run 1 PASS / 2 FAIL, 0.410 seconds |
| Full dependent Release rebuild after library edits | PASS, 72.867 seconds |
| Release CTest: draft, import outcome, review, general production | 4/4 PASS, 7.827 seconds |
| Native case executions in those lanes | 26 + 8 + 24 + 62 = 120 PASS; three new unique cases are compiled into two lanes |

Both baseline failures occur at `CHECK(!recovered)`: the old implementation returned a project after retained declaration or quality evidence was damaged. The journal guard passed on the baseline. Final tests exercise removal, content change and symlink substitution for both evidence kinds, then assert recovery/save refusal and unchanged caller project/workspace bytes. The third case checks fallback for an incomplete journal and a complete but incorrectly bound journal.

All executions were terminal before freezing the 34-artifact manifest and nine preserved bindings. Retained records include both source snapshots/patches, build and test logs, copied native executables, generated version headers and explicit final executable hashes before/after CTest. The baseline has an after-execution executable digest; it does not have a separate before/after identity pair. Binaries embed the source base, while retained patches identify the changed implementation. The verifier checks exact source blobs, patches, receipts, negative baseline and final native counts without executing retained tools.

```sh
python3 -B out/evidence/retained-source-recovery-2026-10-15/verify.py
# Use --commit COMMIT after integration or later source changes.
```

No sanitizer, full-suite, real recording import, listening, AppKit, physical-device, screen-reader, DAW, Windows, signing or human acceptance is claimed for this unit. The four canonical gate/dossier files remain byte-identical: recorded Alpha 0/20, U45 HELD, Beta BLOCKED. `.github` is untouched; Windows remains TODO.

## Next development units

1. The review's separate pre-existing raw/derived asset-loss P2 now has a locally verified [asset recovery repair](CERTIFIED_ASSET_RECOVERY_2026-10-15.md), including removal/change/link tests, current/stale writers and the journal fallback control. Independent asset-repair review is pending; declaration/quality closure alone did not close that finding.
2. Add schema-5 durable prepared-donor provenance using the existing owned-byte validator. Retain packet, intake plan, original corpus license and attribution once per packet; capture the selected donor and all five provenance identities with the take in one project commit. Keep the reviewed rights declaration distinct from the corpus license. Use synthetic fixtures while real source admission is held.
3. Enforce candidate notice closure in directory and package verification: every take's reviewed declaration plus the exact packet/plan/original-license/attribution union. Preserve dependency-set semantics for existing candidate schema 3 while introducing schema 4 for the new required notices. Test a legitimately re-signed package with omitted notices, not only a broken manifest hash.
4. Verify publication, package and installation using distinct declaration/license fixtures, then complete real admission only with the missing owner rights/EULA/performer disposition, genuine producer and distinct reviewer, assignment/phonetic decisions, failed-QC policy and listening evidence. Existing independent findings about notice closure remain open until implementation is verified.

These are prerequisites on the internet-recording path. They do not replace AE1's original no-supplied-recordings requirement or the remaining language, resource, platform, host, DAW and release requirements. Downloaded female speech, singing and growls remain separate source categories; no sample has been promoted to an approved voicebank by this fix.
