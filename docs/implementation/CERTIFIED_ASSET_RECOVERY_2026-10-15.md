# Certified asset-loss recovery — 2026-10-15

Recovery now refuses to return older work when a structurally certified generation has a missing, altered or linked immutable raw/derived asset. This implements the pre-existing P2 follow-up in the independent review of `7aae8bdf`. It is locally verified; independent review of this asset repair remains pending.

## Implementation

`verifyGeneration` uses a private `RetainedContentFailure` enum to distinguish no retained-content failure, source evidence and immutable assets. It sets the asset case only after the existing generation, journal, inspection-policy and ancestry certification checks. `recover()` then returns the original error code/context with the affected generation and `Retained immutable asset` diagnostic instead of searching older generations. The source-evidence diagnostic remains unchanged.

Incomplete, malformed or incorrectly bound supported generation/journal writes retain their fallback behavior. Unsupported schemas/inspection policies still refuse. Schema-1 legacy live-license behavior is unchanged. No journal/project format, write protocol, approval rule or destructive repair is introduced. The library never deletes committed assets; their loss after certification must be visible to the caller.

## Executed evidence

Source base: `0ec9c143f435461a6091d19dd121120b41f9ed3d`; exact four-file final implementation/test patch SHA-256: `5842ae4767188caac074479b330e55195aa0c6ee061ef349da95d3ea4ae69a42`. The adjacent JSON index binds `out/evidence/certified-asset-recovery-2026-10-15/`.

| Execution | Result |
| --- | --- |
| Test-only baseline build | PASS, 8.221 seconds |
| Baseline native raw and derived loss cases | 0 PASS / 2 FAIL at `CHECK(!recovered)`, 0.319 seconds: older work was returned |
| Full dependent Release rebuild after library edits | PASS, 77.793 seconds |
| Snapshot, draft, import-outcome, review and general production CTest lanes | 5/5 PASS, 8.642 seconds; 8 + 28 + 8 + 24 + 64 = 132 native case executions |

Two new unique cases each exercise deletion, changed bytes and a symlink to the same bytes. The derived case first commits a native gain operation and confirms its output differs from raw input. Both current and pre-import stale writers must receive the asset-loss diagnostic when saving. Recovery and failed saves must leave the current/stale caller projects and every workspace byte/link unchanged. The two cases run in both draft and general production lanes. The existing incomplete/misbound-journal guard, retained declaration/quality failures and schema-5 packet-damage tests pass in this run as well.

The frozen evidence contains 35 artifacts and ten preserved bindings: exact baseline/final patches/source blobs, complete build/test logs, copied executables, generated version headers, explicit baseline and final before/after executable digests, the prior schema/recovery manifests, both exact independent verdicts and unchanged canonical gate/dossier files. The read-only verifier passes without running any retained native tool. Binaries embed the source base; the patch identifies the implementation tested. No failed build or setup attempt occurred in this unit.

```sh
python3 -B out/evidence/certified-asset-recovery-2026-10-15/verify.py
# Use --commit COMMIT after integration or later source changes.
```

No sanitizer, full-suite, real-source import, listening, AppKit, device, screen-reader, DAW, Windows, signing or human acceptance is claimed. Recorded Alpha remains 0/20, U45 HELD, Beta BLOCKED. `.github` is untouched; Windows remains TODO.

## Remaining development

Independent review of the schema-5 prerequisite is in progress; this asset repair still needs its separate review. The parser wire-type repair `bd35ddac` is independently approved with no findings and its P3 is closed. The exact verdict is retained and bound separately, without rewriting the original API/execution manifests.

Next implement the inspected prepared-donor importer using held validated bytes, explicit project/packet/donor/assignment expectations and one durable provenance commit. Then enforce candidate-4 complete per-take declaration and four-file notice closure through directory/package/install checks, preserving existing schema-3 dependency semantics. Before using the evidence-only reader as a candidate authority, check declared frame/rate/interval/duration consistency without claiming audio measurement. Packet-bound publication continues to refuse until that path exists. Actual admission still requires the pending owner rights/EULA/performer decision, real producer/distinct reviewer, phonetic/pitch/listening inputs and failed-QC policy; internet samples alone satisfy none of those acceptance records.


## Independent disposition

The exact combined read-only review of `7e141c0f` and `24cf12d5` is retained under `out/evidence/asset-and-geometry-independent-2026-10-15/` (verdict 13,074 bytes, SHA-256 `3d85c506c3f83af9a4a1d881a667dbff73bf3d95529c89ea6bb618b83c5da92f`) and bound by the adjacent index. The asset repair received APPROVE with no findings and its P2 is independently closed. Declared geometry received APPROVE and its concern is closed, with one open P3: add internally consistent out-of-range test inputs so each range clause is independently protected. The reviewer inspected exact committed source and retained evidence without builds, native execution or running retained verifiers. No later importer/cache, source, human or Beta acceptance is implied. Original manifests remain immutable.
