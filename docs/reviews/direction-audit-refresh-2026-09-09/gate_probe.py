"""Pure audit diagnostics. Does not load or write a release candidate."""
from pathlib import Path
from tools.external_beta.operations import transition, can_distribute
from tools.external_beta.full_product_report import _reference_errors

snapshot = {
    "schemaVersion": 1, "candidateRootId": "synthetic-audit-only",
    "state": "FROZEN", "decisionLog": [],
}
decision = {
    "schemaVersion": 1, "decisionId": "audit-probe", "action": "PROMOTE_READY",
    "candidateRootId": "synthetic-audit-only", "actorRole": "A3",
    "createdAt": "2026-09-09T00:00:00Z", "auditPassed": True,
    "approvals": ["A3", "A4"],
}
result = transition(snapshot, decision)
print("state=" + result["state"])
print("can_distribute=" + str(can_distribute(result["state"])))
print("artifact_count=0; external_writes=False")
print(_reference_errors(
    {"kind": "audio", "locator": "audio.wav", "sha256": "a" * 64},
    base=Path("."), label="valid-schema-artifact", verify=False,
))
