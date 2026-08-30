# Project SEAM External Beta runbook

This runbook describes the evidence order for a real candidate. A checked-in
template or engineering PASS never substitutes for target-machine evidence.

1. Freeze one authorized unsigned candidate and record its source/build identity.
2. Produce signed/notarized macOS and Windows descendants without rebuilding.
3. Run [run_external_beta_install_evidence.py](../../scripts/run_external_beta_install_evidence.py) from fresh verifier snapshots for every lifecycle row.
4. Run [run_external_beta_host_evidence.py](../../scripts/run_external_beta_host_evidence.py) for all nine host tuples using exact installed bytes.
5. Restore the governed raw archive and run [run_external_beta_evidence_audit.py](../../scripts/run_external_beta_evidence_audit.py).
6. Validate cohort consent, pseudonymous assignments, target-platform coverage, host coverage, check-ins, incident disposition, and approvals with [run_external_beta_cohort_gate.py](../../scripts/run_external_beta_cohort_gate.py).
7. Run [run_external_beta_release_audit.py](../../scripts/run_external_beta_release_audit.py) against the restored candidate, archive, and state before issuing a release decision.
8. Apply append-only operation decisions with [run_external_beta_operation.py](../../scripts/run_external_beta_operation.py). Pause blocks distribution, resume requires fresh multi-role GO, revoke is irreversible, and close requires a passing cohort audit.
9. Only after archive audit, final standalone/soak records, defects, and role approvals pass may the release gate be evaluated for `EXTERNAL_BETA_READY`.

The archive audit requires every raw locator to resolve inside the restored
archive, every byte hash to match, independent review roles, causal trusted
times, sealed candidate lineage, and an external immutable anchor. Missing
machines, DAW licenses, signing credentials, rights approvals, or raw bytes
remain `NOT_RUN` or `BLOCKED`.
