# Prepared donor capture and native CLI import — 2026-10-15

The CLI now displays a selected donor's original license, attribution, source phones and reviewed declaration beside an explicit producer assignment, and retains that display as an unapproved request. A second command imports only against the unchanged request, current producer state and reverified donor bytes. This makes the existing held-byte importer usable from the command line. It does not admit any downloaded recording or establish a qualified bank.

## Commands and caller responsibilities

```text
seam_voicebank_cli prepare-donor-intake WORKSPACE DIRECTORY PACKET_SHA256 DONOR_ID SOURCE_ID COVERAGE MIDI STYLE NEW_REQUEST_JSON
seam_voicebank_cli import-prepared-donor WORKSPACE DIRECTORY REQUEST_JSON REQUEST_SHA256 PRODUCER UTC
```

`WORKSPACE` is an existing English style-owned producer workspace, not the parent bundle. `DIRECTORY` is a complete prepared donor directory. The caller supplies the retained lowercase packet digest, exact donor and selected source IDs, and an existing coverage/pitch/style row. Preparation does not choose a row, map a phone, register a source, measure pitch suitability or infer permission. The selected human-recording source must already have an executable producer declaration. Review its factual correctness separately.

The output file must be new, in an existing real directory outside both input directories. Preparation prints the entire request as JSON, its resolved output path and the SHA-256 of the saved file bytes. Preserve that hash independently. Review the displayed material and assignment before invoking import with the exact request path/hash, an already registered producer and an explicit UTC timestamp. A digest is an integrity/staleness check, not a signature, proof that a person read the display, or rights approval. This is CLI presentation; no Studio visual workflow or human usability acceptance is claimed.

Do not recompute a hash merely to bypass an unexpected change. A stale request must be prepared and reviewed again against the current producer state. After an uncertain committed result, inspect the workspace and returned generation/durability diagnostic before retrying. There is no automatic retry or approval path. Replaying a committed request refuses as stale.

## Code and data contract

`apps/seam-voicebank-cli/source_intake_commands.cpp` owns both commands. `describe()` reconstructs a typed `com.project-seam.prepared-donor-intake` schema-1 object from the recovered project, retained declaration and owned `PreparedPhoneDonorSnapshot`. It includes:

- Project ID/hash, source strategy, packet/donor/intake-plan identities and language.
- The complete unreviewed proposal and its exact source phones, without normalization or phonetic mapping.
- Coverage, integer MIDI pitch, style, prompt, new take ID and explicit superseded take ID.
- Full original license text and publisher metadata, full attribution, and the separate reviewed declaration with its declared permissions.
- Measured selected WAV/PCM digests, sample rate, frames and nonzero-PCM fact.
- Explicit `UNAPPROVED`, `NOT_QUALIFIED`, `sourceRightsApproved: false` and `releaseEligible: false` values.

Original license and reviewed declaration identities remain independent. No equality or inequality is required. The existing `nextProceduralTakeIdentity()` helper supplies only the established deterministic take/supersession naming; this path performs no synthesis. Retakes preserve earlier takes and bindings. Explicit assignment remains required for every capture.

The output is bounded to 64 MiB and must be UTF-8; the local validator rejects malformed, truncated, overlong, surrogate and out-of-range encodings rather than rewriting notices. The existing native JSON parser also enforces its input, node, depth, collection and string limits. Preparation uses new-file-only durable atomic writing, refusing occupied paths and input-directory output paths. The request carries notices; keep it under the same handling rules as the source material.

Import reads the request through the bounded held-file reader, verifies its retained SHA-256/UTF-8, and requires an int32 integer pitch. It recovers the workspace and refuses a stale project hash before loading donor files. It then loads the exact selected donor, reconstructs the complete typed request and requires equality, including all fields and wire types. Rehashed edits to notices, phones, proposal, take identity or approval flags cannot pass by preserving only selected header fields. A deliberately authored, fully consistent new request remains an explicit caller instruction, not a cryptographic authorization mechanism.

Only then does it call `ProductionProjectRepository::importPreparedDonor()`, supplying the held snapshot and independent captured identities. The repository enforces the registered producer, UTC, assignment and source rules, revalidates owned bytes, performs actual technical inspection and commits schema-5/v2 source binding and raw asset history. The CLI reports the actual committed generation, project/audio hashes, queue state and durability diagnostic. It grants no review, qualification or bank approval; failed technical QC remains review material.

The legacy import path still rereads the original reviewed-declaration locator before commitment. Removing or changing that locator before import refuses, even though preparation displayed retained declaration bytes. After successful commitment, recovery and validation use retained assets, packet files and declarations. External input deletion after import is covered; independence before import is not claimed.

## Execution evidence

The adjacent evidence index binds the frozen root `out/evidence/prepared-donor-intake-cli-2026-10-15`, exact four-file implementation/test diff and retained native copies. Every final run records unchanged HEAD/source diff; every test run records matching binary hashes before/after. Build version headers embed base `bce11b9a`, as expected before commit.

| Final execution | Result | Recorded wall time |
|---|---|---:|
| Full Release dependency check | PASS; graph up to date, no objects compiled in this frozen stage | 1.491 s |
| Release Python/native integration | 40/40 methods, zero skips: 6 new CLI, 8 persistence parity, 26 preparation/verification | 8.516 s |
| Focused Release CTest | 3/3 lanes; 20 snapshot + 30 sample-review CLI + 2 candidate lifecycle = 52 existing native cases | 22.390 s |
| ASan/UBSan CLI target/dependency rebuild | PASS; `seam_voicebank_cli` target only | 8.746 s |
| New CLI integration against that sanitized CLI | 6/6 methods, zero skips | 8.916 s |

The new tests use two distinct synthetic PCM/WAV donors and actual native workspace creation, source registration, capture, import and retake. They never manufacture saved generations or inspection receipts. The Python production validator and native quality inspector read the actual resulting workspace, including after all external preparation/declaration files are deleted. They check notice/assignment presentation, both independent license identities, three bindings, two assets, one shared four-file packet, preserved retake history, automatic inspection and absent review/quality approval.

Negative forms cover altered/rehashed requests and typed fields, invalid identities/actor/UTC, duplicate-key/UTF-16/BOM/invalid-UTF-8 requests, linked requests, stale capture/replay, changed/missing/linked donor inputs, incomplete packets, declaration changes, implicit assignments, output overwrite/input paths and malformed declaration UTF-8. Refusals compare workspace directory/file/link inventories and bytes. Existing preparation/snapshot/publication tests remain separate. This adds no isolated candidate-verifier or PCM-range negative coverage and closes none of those open P3 test gaps.

Preliminary logs are retained and do not replace final evidence. The first build was not identity-inventoried. The first six-method run had one fixture error because it tried to re-register an immutable source ID; the correction uses distinct synthetic source IDs for malformed declarations. A subsequent preliminary six-method run passed. During manifest creation, only the final summary-print helper raised a relative/absolute-path error after the complete manifest had been written; the independent read-only verifier then passed against that manifest. Final build/test receipts above are the source-bound runs. Release Python overlapped the focused sanitizer build; these times are observations, not performance comparisons. Leak detection was disabled, TSan/full-suite/platform/device/power-loss tests were not run, and no OS fault or mid-run cancellation was injected.

## Independent review and admission state

Exact candidate commit `01e78a0c` is now independently APPROVED for engineering. Its retained final answer closes the schema-4 per-take declaration P3, with one new P3 remaining open: existing negative cases stop at listing checks before the deeper packet-fact, per-binding, unreferenced-entry and no-prepared-binding checks. The exact answer/metadata are pinned here. The sibling-writer repair at `bce11b9a` is independently APPROVED with zero findings, closing that P3. Its exact 12,571-byte final answer and metadata arrived after this CLI manifest froze and are bound by the tracked index, leaving the manifest unchanged. Exact CLI commit `166833ef` has now received its own independent APPROVE with zero findings. The exact final answer and metadata are retained under `out/evidence/prepared-donor-intake-cli-independent-2026-10-15` and separately bound by the tracked index after the manifest freeze; the original frozen evidence remains unchanged.

The source pool remains 1,469 distinct downloaded originals, separated as 65 same-female f1 singing, 272 growls and 1,132 female SLT speech recordings. The 312 prepared speech donors remain unreviewed and unassigned. This unit registers/imports/plays/approves no real source. Genuine owner source/EULA/performer decisions, producer and distinct reviewer, phone mapping/missing coverage, pitch/style choices, failed-QC policy and listening remain outstanding. No terminal-output test proves any human reviewed the notices. Canonical Alpha remains 0/20, U45 HELD, Beta BLOCKED; the full goal remains active. Windows remains TODO and `.github` is untouched.

The reviewer confirms the frozen Release stage is an up-to-date graph check. Actual compilation appears only in the preliminary build log, without the final source/binary inventory; the historical commit wording “dependency rebuild” must not be read as a newly inventoried compile. The focused sanitized CLI stage did compile. Optional follow-ups cover readable display of C1/bidirectional controls and superseded-take queue state, additional linked-output/source-selection/cancellation cases, and retained-declaration fallback. None is claimed implemented here.
