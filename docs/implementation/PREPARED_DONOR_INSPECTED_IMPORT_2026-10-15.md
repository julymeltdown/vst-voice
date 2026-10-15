# Inspected prepared-donor import — 2026-10-15

The native repository now has an inspected import API for a selected prepared donor. It consumes an owned, revalidated snapshot and commits one unapproved take, its current native inspection receipt, the typed schema-5 source binding and references to its retained notices in one durable project generation. This unit uses synthetic fixtures only. No downloaded recording was registered, imported, listened to or approved.

## Caller and source contract

`ProductionProjectRepository::importPreparedDonor` requires an independently retained `PreparedDonorImportExpectation`: the exact encoded project SHA-256, packet SHA-256, donor ID and selected source strategy ID. It verifies the actual durable generation and current pointer before staging. The project must be English and style-owned, and the selected strategy must be `HumanRecording` with the existing execution authority and an unchanged reviewed declaration. The event must identify a registered `PRODUCER`, a valid UTC instant, this exact take and the appropriate import/retake action. Initial state is limited to `MarkerReview` or `Rejected`.

The caller explicitly supplies the coverage key, pitch layer, style, prompt and superseded take. All must identify the current assignment. There is no automatic phone/stress alias, pitch, style or source-type mapping. Execution permission admits material for review; it does not establish bank redistribution, source quality, annotation truth or listening acceptance. Real source admission remains dependent on actual owner decisions and actors.

The snapshot is passed by value to `validatePreparedPhoneDonorSnapshot`, which rehashes all five held files, pins the expected packet and donor, and rederives the selected proposal and measured WAV facts. Cached proposal text and cached measurements are not authoritative. Import does not reopen any preparation path.

## Inspection, retention and commit

On the current macOS/POSIX path, a mode-0700 directory is created exclusively inside the repository with `mkdtemp`. The selected held WAV is written there without replacing an existing file. The existing `importRawBound` path performs the actual native inspection, checks the digest and byte count of the content-addressed raw copy, snapshots the selected reviewed declaration, creates the inspection receipt and applies the existing assignment/retake rules to a private project draft. The receipt is for the stored bytes and their actual assignment. Failed technical QC remains failed; the material is unapproved.

The new binding includes the exact packet, donor, intake-plan, original-license and attribution identities, and its v2 binding ID is recomputed. The draft becomes schema 5. Ordinary raw/procedural import behavior and v1 binding identities are unchanged.

The four canonical held metadata files are retained once under `source-evidence/packets/<packetSha256>/`. A retained file can be reused only when its bytes match exactly. Conflicting files and linked/non-directory notice parents refuse; existing files are never overwritten. The reviewed source-specific declaration remains a separate `source-evidence/<strategy-declaration-sha>.txt` snapshot. Its digest is independent of the original corpus license, without any claim that a digest proves the legal relationship.

Only after notice retention does the existing save/commit helper publish the project generation and update caller state. A precommit refusal leaves the caller and committed generation unchanged. Late failure or cancellation can leave unreferenced content-addressed assets or an incomplete, unreferenced notice directory; this API does not promise a filesystem-wide rollback. The existing exact recovered-generation logic distinguishes a recoverable commit with uncertain pointer durability from an unconfirmed operation. That helper's fault behavior is covered by the selected existing import-outcome lane, rather than new injected failures specific to this wrapper.

The private capture destructor removes only its known WAV and empty directory after checking the directory device/inode identity. It never recursively deletes a directory. This is normal-path/bounded failure cleanup, not a crash-proof cleanup guarantee or a new proof against arbitrary concurrent parent replacement. Windows returns `Unsupported` for private capture and still requires implementation and verification; README Windows support stays TODO.

## Verification

Source base: `24cf12d5638e7fcebc4800de5f00608d6c72c013`. Exact three-file patch SHA-256: `cb3fb0caf615c2826dcd7f0fa984c65bc9d4bc8c089b6cb7de3980095a93c4b4`. The adjacent JSON index binds the execution evidence.

Five new unique synthetic cases establish:

- A genuine test source registration followed by inspected import after all five preparation files disappear, despite poisoned cached facts. The stored audio and all four notices match held bytes. The test declaration differs from the original license. Recovery still succeeds after deleting the external reviewed declaration because its retained snapshot remains. The deliberately unsuitable 8 kHz/80-frame fixture has a current failed inspection and no approval or source-quality review.
- Thirteen rejected request forms: wrong project/packet/donor/source, prompt/style mismatch, reviewer as importer, approved initial state, changed held audio/attribution, wrong event subject, invalid UTC and pre-cancellation. Each preserves caller, workspace bytes and recovered project.
- Reuse of one packet/donor across two styles and an explicit retake, with one raw asset, four shared notice files and three distinct binding identities. Existing neutral ownership is superseded only by the matching retake.
- Refusal for a linked notice parent or an existing different attribution, with no partial project commit, unchanged conflicting notice/unrelated files and private-capture cleanup.
- Refusal of a stale durable writer before any staging.

| Execution | Result |
| --- | --- |
| Full dependent Release build | PASS, 76.653 seconds |
| Seven selected Release CTest lanes | 7/7 PASS, 14.605 seconds; 142 native case executions |
| Full dependent ASan/UBSan build | PASS, 104.910 seconds |
| Two selected ASan/UBSan CTest lanes | 2/2 PASS, 40.088 seconds; 42 native case executions |
| Read-only retained evidence verifier | PASS; 49 artifacts and nine preserved bindings |

The Release lanes are snapshot, style-owned import, draft, import outcome, English lifecycle, review and general production (14/3/28/8/1/24/64 case executions). Sanitizers cover snapshot and draft (14/28). Leak detection is disabled; ASan abort/strict string checks and UBSan halt/stack traces are enabled. The native binaries were explicitly hashed before and after each CTest run, matched, and copied into the frozen evidence root. All source patches/snapshots, command receipts, logs and generated version headers are retained. Binaries embed the source base; the frozen patch identifies the code tested. There is no old-code failure baseline for this new API.

```sh
python3 -B out/evidence/prepared-donor-inspected-import-2026-10-15/verify.py
# Use --commit COMMIT after integration or later source changes.
``` Full suite, TSan, leak detection, real-donor import, UI/CLI exposure, Python schema-5 gate parity, candidate-4 notice closure, installed-package verification, DAW/device/Windows/signing and human acceptance are outside these runs.

## Independent review and remaining work

The exact schema-5 prerequisite review approved `0ec9c143` for engineering scope with one P3: current verification reloads a whole packet for each distinct donor. This is an acknowledged scaling issue for a production bank and remains open. Its proposed packet-level cache/map should preserve all per-binding digest checks and add a two-donor fixture. That review did not review this new importer. The exact verdict is retained under `out/evidence/prepared-donor-schema-five-independent-2026-10-15/`.

Next ordered engineering work is packet-level verification caching, Python schema-5 gate parity, and candidate-4 complete notice closure through directory/package/installed verification. The latter must retain schema-3 dependency semantics and require every take's reviewed declaration plus the exact shared four-file packet union, including legitimately re-signed omission tests. Packet-bound candidate publication remains explicitly refused until that complete path exists. Studio/CLI integration must display original license and attribution before real use.

Actual owner rights/EULA/performer disposition, registered real producer/distinct reviewer, phone/pitch/style assignments, failed-QC policy and listening evidence are still required before real bank admission. The original full-product requirements are unchanged. Canonical gate/dossier bytes remain Alpha recorded 0/20, U45 HELD and Beta BLOCKED. Internet singing, growls and speech remain distinct unadmitted source pools. `.github` is untouched.
