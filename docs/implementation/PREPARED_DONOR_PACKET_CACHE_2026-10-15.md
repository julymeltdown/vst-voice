# Shared prepared-packet verification — 2026-10-15

Repository source verification now reads and parses a shared prepared-donor packet once per `verifySnapshots` call, then looks up each take's donor in a checked digest map. This addresses the scaling P3 from the independent schema-5 review of `0ec9c143`. The exact independent review now APPROVES `a42e8086` with no findings and closes that P3. This is engineering approval only; real source admission and Beta acceptance remain blocked.

## Code and complexity

`loadPreparedPhoneDonorPacketFacts` uses the existing bounded packet/plan/license/attribution reader and shared provenance validation. It returns four metadata digests plus a `donorId → derivedAudioSha256` map. It does not load donor WAVs, expose measured PCM facts or retain metadata bytes in its result. Every proposal passes the existing whole-packet shape, source, span, identity, digest and unapproved-state checks. It additionally checks every row's declared PCM wire types and geometry before returning the map, using the same extracted `declaredPcmFacts` helper as the selected-evidence path.

The selected-evidence/full/owned APIs retain their current selected-row PCM consistency contract. Whole-packet facts are deliberately stricter for nonselected rows because any map entry may be used by a later binding. A malformed declared row therefore refuses the shared map even if no current take selects it. The positive fixture contains two different donor IDs and different WAV digests. None of these metadata checks claims measured audio, annotation truth, rights or quality.

`source_internal::verifySnapshots` groups binding references by packet SHA-256. For each group it resolves the full retained path chain, loads that packet’s facts once, checks all group members and then releases the facts before checking the next packet. Only one bounded packet digest map is retained at a time; grouping memory grows with the project bindings, rather than all proposals of all packets. Every binding must still find its donor ID and match its own raw-audio digest, intake-plan digest, original-license digest and attribution digest. Its separate reviewed-declaration snapshot is verified as before. Ordinary source bindings, typed v2 identity, schema versions, retained notice paths and all existing source/asset recovery rules remain unchanged.

For one packet with P proposals and D bindings, packet parsing and proposal validation happen once rather than D times. Each binding performs a bounded map lookup. Separate save/recovery verification passes still check independently; this is not a persistent cache across operations, a change to declaration checking, or a measured end-to-end speed claim. The checked facts cannot hide a later notice change because they are released after each group and are never retained between verification calls. Packet-size/proposal/source caps are unchanged.

## Focused verification

Three new unique synthetic cases cover a two-donor digest map after deleting both WAVs, exact four metadata identities, wrong expected packet, pre-cancellation and re-reading changed attribution; second-row floating-point wire type/invalid rate rejection while the first selected-only read remains valid; and two actual inspected imports from the shared packet into independent styles. The latter verifies distinct raw assets and bindings, refuses an absent donor, a valid wrong donor's audio and each of the three wrong per-binding notice digests without writes, and detects a license changed after successful verification in both verify and recovery. Existing held-byte, import refusal, schema, damaged notice and asset-loss cases remain in the selected lanes.

Source base: `dd0b515dff22564e46357c896e78870f01f0657d`; exact four-file implementation/test patch SHA-256: `4873188f4b0f6f3bb8d1e29d6b721bedd7ee09269ca2b562251fd88b270341d6`. The adjacent index binds final execution details and retained source/log/binary identities.

| Final execution | Result |
| --- | --- |
| Full dependent Release build | PASS, 10.607 seconds |
| Three selected Release CTest lanes | 3/3 PASS, 6.527 seconds; 17/28/64 = 109 native case executions |
| Full dependent ASan/UBSan build | PASS, 34.279 seconds |
| Two selected ASan/UBSan CTest lanes | 2/2 PASS, 42.757 seconds; 17/28 = 45 native case executions |
| Read-only retained evidence verifier | PASS; 96 artifacts and ten preserved bindings |

The first design also passed full Release/ASan/UBSan builds and the same lanes (80.053/103.293-second builds; 7.740/38.922-second test runs). Its source, patch, receipts, logs and native copies remain retained. Final review then changed retention from all packet maps to grouped references plus one packet map, and both dependent builds and focused runs were repeated. Final case counts above exclude the earlier repeated executions. No old-code failing baseline exists for this new API. Every CTest run has explicit matching before/after binary identities and retained native copies. Source snapshots/patches and generated version headers bind the builds; binaries embed the source base. ASan uses abort/strict string checks and UBSan uses halt/stack traces, with leak detection disabled.

```sh
python3 -B out/evidence/prepared-donor-packet-cache-2026-10-15/verify.py
# Use --commit COMMIT after integration or later source changes.
``` No reader load-count hook or whole-bank benchmark was added: the one-load-per-packet property is directly visible in the grouped packet loop, with two-donor behavior tested. No new real-source run, production registration, listening, failed-QC approval, full suite, TSan, leak check, UI/CLI, Python schema-5 gate parity, candidate-4 publication, device/DAW/Windows/signing or human acceptance is claimed.

## Remaining boundaries

The exact independent review APPROVES `dd0b515d` with one new P3 for directory durability, and APPROVES `a42e8086` with no findings, closing the schema-review scaling P3. The importer creates notice directories without syncing each new entry into its parent; the older asset-bucket path has the same gap. The review corrects its earlier assumption that asset sync ordering already covered new buckets. This durability repair remains open. Candidate-4 directory/package/install notice closure and per-take declaration completeness, Python schema-5 gate parity, notice display and controlled real admission remain separate work. The exact schema review and prior importer evidence remain immutable and are bound by the adjacent evidence index.

The separate independent review approved the asset-loss repair `7e141c0f` with no findings and closed that P2; it also approved `24cf12d5` and closed the declared-geometry concern, with a new P3 range-bound test gap. That P3 remains open: internally consistent out-of-range metadata should isolate the range clauses from equality checks. The exact combined verdict and metadata are retained under `out/evidence/asset-and-geometry-independent-2026-10-15/` and bound by the adjacent index. That review did not review the importer or this packet-cache repair.

The pending owner rights/EULA/performer disposition, actual producer/distinct reviewer, explicit assignment/phonetic/pitch decisions, failed-QC policy and listening evidence remain required. Alpha recorded 0/20; U45 HELD; Beta BLOCKED. Canonical gate bytes, `.github` and README Windows TODO remain unchanged.

The exact combined verdict is retained at `out/evidence/prepared-donor-import-cache-independent-2026-10-15/review-dd0b515d-a42e8086.md` (14,576 bytes, SHA-256 `e5779512ede831f87b628e8510e820fa8f49496a77847463a1a114ff5d57c8dc`). The adjacent index binds it and its metadata. Review was read-only and ran no native binary, retained script, build, test or playback. [Python schema-5 validation](PREPARED_DONOR_PYTHON_PARITY_2026-10-15.md) is now locally verified in a separate follow-up; this does not change the exact historical test scope above.
