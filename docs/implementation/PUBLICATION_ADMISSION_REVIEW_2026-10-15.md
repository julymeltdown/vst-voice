# Independent publication-admission review

Date: 2026-10-15, Asia/Seoul. Integrating base: `e3ee6f00f1e784e21af524e4aa1e52808afce380`.
**APPROVE exact `c27d1d6f5b9a883b48ea8c11c170998b68ac7a04`, engineering scope only.**
No required changes; two P3 follow-ups. This records a completed review, not a new
runtime repair or approval of subsequent commits.

The peer **Review in English** independently verified that replacement, cancellation,
idle reset, owner invalidation and shutdown serialize with physical publication;
the final currency check executes under the same admission mutex as revocation.
Lock ordering has no observed reverse path, completion/progress callbacks execute
outside the admission lock, and the audio thread still takes no lock. The retained
source/build/run ordering is consistent; the reviewer found no analogous source-after-
build discrepancy in this unit. This does not establish authenticated build lineage.

The four late-publication controls fail against the baseline plus observation hook
and pass after the repair. The peer also audited frozen bytes, receipts, timestamps,
test counts and retained PCM/source identities independently. It ran no builds, tests,
native executables or captured runtime scripts. The exact full verdict is **17,040 bytes**,
SHA-256 `f193fde9e623f0b74543ab91e015a1b0811d22eb9ddcfc50a5023c5fa6e182f1`.

The two remaining P3 findings are concrete:

- The four late controls prove the final currency recheck, but would still pass if
  that recheck lacked its admission lock. Add a held `duringReset` control with its
  token still current, release the late worker and show it cannot publish while reset
  owns the mutex. Current exclusion is verified by source reading.
- Physical publication finishes under the lock, but completion/failure counters and
  Ready/failed/PublicationBusy reports currently run after release. A cancellation or
  replacement in that interval can report older publication fields. Playback refuses
  revoked identities correctly. Commit the outcome report with publication, keeping
  only notification outside; add an ordering regression before claiming this resolved.

Neither follow-up is silently marked fixed here. This metadata-only unit runs no
new native build or implementation test. A read-only verifier checks the exact verdict,
hash pins and immutable source bindings at the reviewed and integrating commits.
It preserves the 42-artifact original publication root and its execution records.

The [evidence index](PUBLICATION_ADMISSION_REVIEW_2026-10-15.evidence.json) binds
**three artifacts and eight preserved inputs** under
`out/evidence/publication-admission-review-2026-10-15`. The 2,149-byte manifest has
SHA-256 `ee399163751ea4f4fdedbc87dd9781e309ef7686ffb33ee4b3f06718e38b835b`.
Its source audit uses immutable Git commits rather than requiring current code to
remain identical forever. It executes no retained code.

Exact `c111cbac` pitch-scorer review has been dispatched after this review became
terminal. The bank diagnostic, admission, provenance supplement and fixture-ownership
units still need their own reviews. Physical/human/listening/DAW/Windows/release,
original singer/classical/neural/R9 resources, source permissions/actors/coverage/QC,
and canonical acoustic criteria/tolerance ownership remain. Alpha stays **0/20 recorded
passes**, U45 held, Beta **BLOCKED**, full goal ACTIVE. `.github`, README Windows TODO,
canonical gates, source recordings, historical evidence and the stash are preserved.

## Later local implementation of the two P3s

The separate [outcome-admission unit](PUBLICATION_OUTCOME_ADMISSION_2026-10-15.md)
addresses both findings locally: publication and its terminal counter/report share
admission, and a new held-reset control fails when only that lock is removed while
the four older late controls still pass. Four outcome-ordering controls fail on the
observed parent and pass after repair; reentrant notification also passes. Full
Release build, eight focused entries (253 native cases/11 Python methods), nine
additional runtime cases and Debug TSan (48 coordinator cases) pass. This follow-up
has its own 69-artifact/nine-input root. Independent review APPROVES exact `06803bb5` for engineering scope and closes both P3s. Its one documentation finding is disclosed in the outcome dossier. It does not
retroactively extend the exact c27 approval or change this frozen review root.
