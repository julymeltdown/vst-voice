# Prepared-donor candidate provenance closure — 2026-10-15

Prepared-donor material can now be published as an engineering sample candidate with schema 4. Publication retains every captured take's reviewed declaration and each shared packet's original license, attribution, intake plan and donor metadata. Directory and signed-package verification use the same provenance check; installation and installed auditing preserve those exact signed bytes. This removes the temporary unconditional publication refusal for packet-bound takes. It does not approve any real source or qualify a voicebank.

## Version and ownership contract

Schemas 1/2 retain their historical read-only semantics and cannot be re-encoded. Schema 3 remains the default for ordinary sample, recipe and model candidates, with its complete dependency set intact. Schema 4 is restricted to sample candidates whose captured producer snapshot has at least one prepared binding. A packet-bound snapshot inside schema 3 is refused, including an otherwise authentic, re-signed package. Schema 4 without a prepared binding is also refused. CLI publication receipts report the schema actually written.

The reviewed source declaration and the original corpus license have independent identities. No equality or inequality between them is required. The synthetic positive fixture intentionally gives them different bytes to detect accidental conflation. Source strategy, importer, assignment, inspection, producer generation and review rules continue to apply before publication; this change supplies no actor, factual permission, phone assignment, quality override or review decision.

## Retained files and verification

`publishReviewedCandidate` deduplicates prepared bindings by packet SHA-256 and copies four retained canonical files into `provenance/source-evidence/packets/<packet-sha256>/`: `donors.json`, `intake-plan.json`, `license-source.txt` and `attribution.txt`. Their roles are respectively `source-donor-packet`, `source-intake-plan`, `source-original-license` and `source-attribution`. Original relative notice references remain valid within that directory. New directories use the durable parent/directory creation helper. The staged candidate must pass its complete directory verifier before atomic publication.

The shared `verifyResourceCandidateSourceEvidence` callback accepts bounded owned bytes from either the directory reader or the signed-container reader. It binds the decoded producer snapshot's exact hash, project ID, generation and inventory, checks the published units' captured source kinds, and binds the primary declaration to the first unit. For schema 4, every snapshot source binding, including retained retakes, requires its declaration under `provenance/<licenseSnapshotPath>` with role `source-license-snapshot`, exact SHA-256 and a 4 MiB cap. Reused declaration paths must agree on identity.

Each distinct packet is read once per verification call. The new owned `validatePreparedPhoneDonorPacketFacts` API verifies the canonical four-file hash chain and all rows' typed declared facts without opening a WAV or filesystem path. Every captured prepared binding must name an existing donor whose derived audio hash equals its captured raw asset, with the exact intake-plan, original-license and attribution digests. Packet/donor reuse across styles or retakes remains permitted. These are declared-metadata and captured-identity checks, not new acoustic measurements or phonetic validation.

The required declaration/packet file union is exact: missing, substituted or unreferenced closure entries refuse. Per-packet limits remain 16/1/1/2 MiB. Unique declarations, shared packet files and listed source-quality evidence share a 64 MiB source-evidence budget; exceeding it refuses explicitly rather than dropping notices. The general typed candidate verifier still checks every listed size/hash, rejects extra files and links, and validates manifest/content/dependencies. Schema 3 retains its historical declaration-list rules; it is not silently promoted to schema-4 completeness.

Signed candidate verification reads the retained provenance directly from authenticated entries. A valid signature alone is insufficient. Candidate installation first verifies that typed package; installed auditing re-verifies it and compares all installed signed entries. No installer bypass or new release-eligibility path is added.

## Focused execution and its limits

Exact execution identities, timings, stage patches, source snapshots, native copies and build versions are recorded in the adjacent `.evidence.json` index. The evidence root is `out/evidence/prepared-donor-candidate-four-2026-10-15`. The initial five-lane Release pass used the earlier one-declaration fixture. The first final-source pass did not retain the dependent CLI identity; a final repeated run additionally hashes and copies that CLI, with all six native identities matched before/after. That repeated Release test overlaps part of the separate sanitizer build, so its wall time is not a performance comparison. Subsequent full Release builds retain the two-declaration fixture, API checks and corrected CLI receipt; no tests are claimed for intermediate builds 2/3. Final Release and ASan/UBSan results are recorded separately. These are focused lanes, not the entire repository suite.

One new lifecycle case drives the actual native held-byte importer for two distinct generated tones, two pitches, two styles and four captured bindings, then uses the real CLI draft/review/publication path with explicitly synthetic actors, rights and quality decisions. It removes external preparation and declaration files before publication. The resulting schema-4 candidate has two distinct reviewed declarations, one shared four-file packet and two raw assets. It signs, verifies, installs and audits the candidate.

For each of the six required files, installed removal and substitution refuse, then restoring the exact bytes restores successful auditing: 12 negative forms. Candidate removal also removes its descriptor entry; substitution updates that entry's size/hash. All 12 altered directories are refused by directory verification and typed packaging. Each is nevertheless packaged by the low-level signer, and raw signature verification passes; typed verification and installation still refuse, leaving no install directory. This proves provenance closure against authentically re-signed omissions/substitutions rather than only ordinary hash damage. A schema-3 downgrade with all packet files/listings removed is likewise authentically signed and refused. A separate oversized source-quality listing reaches the explicit 64 MiB budget refusal.

The existing schema-5 snapshot test no longer expects the old unconditional Unsupported guard. Its deliberately incomplete publication request still fails and creates no candidate. Existing ordinary schema-3 sample and declared recipe/model packaging paths remain in the selected lanes. Full dependent builds precede CTest. No old-code failing baseline is claimed for this new format/API.

## Independent review and remaining work

The earlier Python validator commit `c9ef0d21` is independently APPROVED for engineering scope, with two P3 test gaps: an existing donor with different audio, and bypass fixtures whose history isolates the intended rule. Its exact final answer and metadata are retained in `out/evidence/prepared-donor-python-independent-2026-10-15` and bound by this unit's evidence. The native range-bound test P3 remains open. The separate exact review APPROVES directory durability commit `78a1b817`, closing the originating donor/asset durability P3. It identifies one pre-existing P3 at the quality-evidence and ordinary declaration writers; that remains open. The exact 15,546-byte answer and metadata were retained after this unit’s manifest was frozen and are bound by the adjacent tracked index, with the frozen manifest unchanged. Candidate schema 4 and the per-take declaration repair require their own exact post-commit independent review before independent closure is claimed.

Before genuine admission, Studio/CLI must present original license, attribution and donor phones beside the intended assignment. Owner disposition of source/EULA/performer rights, genuine producer and distinct reviewer, phone coverage (`jh`, missing `axr`), pitch/style choices including MIDI 66, failed-QC policy and listening remain required. Existing importer-wrapper cancellation/commit fault and other review follow-ups retain their prior scope; this lifecycle is not their acceptance evidence.

No downloaded recording was registered, imported, played or approved in this unit. The 1,469 originals and 312 prepared speech donors remain unadmitted; singing, growl and speech identities stay separate. Sample corpus availability does not replace the original full-product or recording-free voice scope. Alpha **0/20**, U45 **HELD**, Beta **BLOCKED**, goal **ACTIVE**. Full suite, TSan, leak detection, real power-loss/fault injection, DAW/device/Windows and human acceptance are not established. `.github`, README's Windows TODO, existing stash and previous immutable evidence remain preserved.

```sh
python3 -B out/evidence/prepared-donor-candidate-four-2026-10-15/verify.py
# Use --commit COMMIT after integration or subsequent source edits.
```

## Final recorded results

Source base: `78a1b817e237fe31837b171fd97c72f0c698d3f0`; exact 11-file source/test patch SHA-256: `0b6985aadc5e0469c40e6b42db607cb5ced62ba53d216501f4c3bbc08f772db5`.

| Execution | Result |
| --- | --- |
| Final full dependent Release build | PASS, 55.370 seconds |
| Final five Release lanes, including retained CLI identity | PASS, 52.334 seconds; 144 native cases |
| Full dependent ASan/UBSan build | PASS, 260.456 seconds |
| Five ASan/UBSan lanes | PASS, 287.562 seconds; the same 144 native cases |
| Read-only frozen verifier | PASS; 175 artifacts, 12 preserved bindings |

Both final test runs match all six native identities before/after and retain their copies. ASan uses `detect_leaks=0:abort_on_error=1:strict_string_checks=1`; UBSan uses `print_stacktrace=1:halt_on_error=1`. No leak-test claim is made. The manifest is 47,566 bytes, SHA-256 `3d1fe3112b93f506f61d7f3a2e9bf10a17fd9eacfecf544c259705bb92bd4142`. Initial and intermediate executions remain retained with their exact stage patches; their source states and identity limits are described above.

The sibling retained-evidence writer P3 now has a [separate local repair](RETAINED_SOURCE_EVIDENCE_DURABILITY_2026-10-15.md) for assessment, ordinary snapshot and source registration. Its exact independent closure is pending; this candidate unit’s original builds and sanitizer runs remain scoped to its own commit.

## Exact post-commit review and CLI follow-up

The exact `01e78a0c` review now APPROVES this unit for engineering and independently closes the schema-4 per-take declaration P3. Its final answer is 16,861 bytes, SHA-256 `b1d84e4aac372e766e095aaf56c366e544691f54fbcfa62e1eb0e047c1a73086`, retained under `out/evidence/prepared-donor-candidate-four-independent-2026-10-15` and bound by the adjacent index; the original frozen evidence remains unchanged. One new P3 remains open: negative tests stop at listing checks before the deeper packet-fact, per-binding, unreferenced-entry and no-prepared-binding rules. This verdict does not close the earlier Python/native range test gaps. The separate `bce11b9a` exact review APPROVES the sibling-writer repair with zero findings.

The [CLI capture/import unit](PREPARED_DONOR_INTAKE_CLI_2026-10-15.md) adds the before-use terminal presentation and retained request, with actual native import/Python-reader tests. Its own exact review is pending. Real notice review, rights, assignments and bank qualification remain unestablished.
