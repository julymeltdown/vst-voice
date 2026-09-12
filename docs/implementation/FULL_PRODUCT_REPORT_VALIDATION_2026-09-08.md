# U45 full-product report validation foundation

The External Beta gate now treats the `fullProductReport` reference as an
actual input instead of an opaque PASS-shaped record. The reader performs a
bounded no-follow read, rejects duplicate JSON keys, non-finite numbers,
oversized/deep input and non-regular files, then validates the checked-in
Draft 2020-12 evidence schema.

After shape validation, the semantic layer binds the report to the candidate
root and acceptance/full-product contract digests. It checks the exact R1–R20
requirement set, all 83 case IDs, case-to-requirement/result-type bindings,
declared dimensions, platform/host coherence, source/build continuity, raw
artifact kinds, accepted independent-review roles, operation/check coverage,
continuity checks, and empirical result-cell identities. Relative raw-evidence
locators are resolved from the report directory and every referenced file is
rehash-checked when the report is evaluated.

This is an admission boundary, not a result generator. The canonical contract
still declares its resource matrix and eleven empirical criteria unresolved;
those values remain blocking. No acoustic score, creator approval, trained
neural resource, installed host result, or release PASS is inferred from a
schema-valid report. The old “semantic validator unavailable” message is kept
only for candidates that omit the mandatory report reference entirely.

The implementation is in `tools/external_beta/full_product_report.py`, with a
standalone diagnostic command at `scripts/verify_full_product_report.py` and
reader/gate regression coverage in
`tests/external_beta/test_full_product_report.py` and
`tests/external_beta/test_full_product_gate.py`.
