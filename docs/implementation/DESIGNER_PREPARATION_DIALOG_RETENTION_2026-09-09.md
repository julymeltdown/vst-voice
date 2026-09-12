# Preserve frozen Designer selection across preparation dialogs

The region/destination dialog path previously called the consuming
`takeGenerationScoreSelection` before either dialog was confirmed. Cancelling
lost the inspected score and its frozen Designer recipe; retry could fall back
to ordinary score inspection without that recipe override.

The native path now copies the non-consuming `generationScoreSelection` view.
Cancellation or dialog errors preserve the controller-owned score hash, region
list and selected recipe. Producer context is still checked before and after
dialogs. An already stale assignment clears the obsolete selection and reports
the conflict so a subsequent attempt can inspect a fresh score.

Successful preparation-worker dispatch clears the retained selection. Failure to
start retains it. The pre-inspection producer status is preserved rather than
restoring a consumed snapshot-ready prompt on worker failure. This change does
not promise retention after a worker has started; late publication, cancellation
and job identity continue to use the existing generation transaction.

The export integration test now reads the snapshot repeatedly without consuming
it, checks retained hash/status, starts preparation, verifies selection is then
cleared and loads the published job to verify the frozen recipe hash. Existing
changed-score and immutable producer-state checks remain in place.

Native modal cancellation itself remains live-unverified in this increment.
Final Release rebuild and Designer/export/core suites passed 3/3 in 36.31 seconds.
All 2,059 paths from the preceding recovery checkpoint remained present.
This is a U21/U22 handoff repair, not full lifecycle or Beta GO acceptance. No
saved bank, approval, Git index or remote state was changed.

## Assignment-switch follow-up

Retaining a snapshot across dialogs must not retain it across a different
producer assignment. Successful selection of a different row now clears the
snapshot, including when an asynchronously loaded editable row is adopted.
Rejected selection leaves it intact; reselecting the same inventory row preserves
both snapshot and ready prompt. This closes the A -> B -> A case that a comparison
against only the current row index could otherwise miss.

The integration regression creates a two-assignment workspace, inspects a frozen
Designer score on A, switches to B and back to A, and checks that neither switch
revives the snapshot. Separate assertions preserve it on invalid/same-row
selection. Native row-switch interaction itself remains live-unverified.

Follow-up Release build and export/core/sample-review suites passed 3/3 in
33.25 seconds. No release acceptance or whole-unit completion is inferred.

## Late cancellation at result adoption

Score inspection previously checked its stop token in the worker but not when
the completed result was adopted. A stop request between those points could
still expose a ready snapshot. The controller now rejects a cancelled inspection
snapshot at adoption and restores the prior status.

The guard is limited to read-only score-selection results. Job preparation may
have already durably published a job before cancellation; that successful result
must not be discarded or described as rolled back. Regression cases observe
completed, unconsumed futures through a non-consuming readiness query, then
cancel and verify these distinct outcomes. This avoids relying on an arbitrary
sleep to guess whether the worker has completed.
Final Release rebuild and export/core suites passed 2/2 in 26.11 seconds, including
both completed-future cancellation cases. Live worker-cancel UI remains outside
this regression result; no full Beta GO qualification is implied.
