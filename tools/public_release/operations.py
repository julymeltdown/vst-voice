from __future__ import annotations

import copy
import argparse
import json
from datetime import datetime
from pathlib import Path
from typing import Final

from .approval_validation import approval_errors
from .contracts import (
    PUBLIC_STATES,
    JsonObject,
    ReleaseGateInputError,
    is_sha256,
    parse_time,
)
from .crypto_validation import operation_policy_errors, signed_record_errors
from .replay import read_reference, restored_directory
from .contracts import sha256_json


ADVANCE_ACTIONS: Final[dict[str, tuple[str, str]]] = {
    "AUTHORIZE_FREEZE": ("DRAFT", "AUTHORIZED_FROZEN"),
    "MARK_SIGNED": ("AUTHORIZED_FROZEN", "SIGNED"),
    "MARK_CLEAN_INSTALLED": ("SIGNED", "CLEAN_INSTALLED"),
    "MARK_BANK_READY": ("CLEAN_INSTALLED", "BANK_READY"),
    "MARK_EVIDENCE_PASSED": ("BANK_READY", "EVIDENCE_PASSED"),
    "MARK_EXTERNAL_BETA_READY": ("EVIDENCE_PASSED", "EXTERNAL_BETA_READY"),
    "MARK_EXTERNAL_BETA_CLOSED": ("EXTERNAL_BETA_READY", "EXTERNAL_BETA_CLOSED"),
    "ACTIVATE": ("EXTERNAL_BETA_CLOSED", "PUBLIC_ACTIVE"),
}


def _fail(message: str) -> ReleaseGateInputError:
    return ReleaseGateInputError(message)


def _validate_snapshot(snapshot: JsonObject) -> None:
    if snapshot.get("schemaVersion") != 1:
        raise _fail("operation snapshot schemaVersion must be 1")
    lineage = snapshot.get("candidateLineageId")
    if not isinstance(lineage, str) or not lineage:
        raise _fail("operation snapshot candidateLineageId is required")
    if not is_sha256(snapshot.get("evidenceRootSha256")):
        raise _fail("operation snapshot evidenceRootSha256 is invalid")
    if snapshot.get("state") not in PUBLIC_STATES:
        raise _fail("operation snapshot state is invalid")
    log = snapshot.get("decisionLog")
    if not isinstance(log, list):
        raise _fail("operation snapshot decisionLog must be an array")
    decision_ids: set[str] = set()
    for item in log:
        if not isinstance(item, dict):
            raise _fail("operation decision log item must be an object")
        decision_id = item.get("decisionId")
        if not isinstance(decision_id, str) or not decision_id:
            raise _fail("operation decisionId is required")
        if decision_id in decision_ids:
            raise _fail("operation decisionId must be unique")
        decision_ids.add(decision_id)


def _validate_decision(
    snapshot: JsonObject,
    decision: JsonObject,
    operation_policy: JsonObject,
) -> None:
    if decision.get("schemaVersion") != 1:
        raise _fail("operation decision schemaVersion must be 1")
    for key in ("decisionId", "action", "candidateLineageId", "actorId", "actorRole"):
        if not isinstance(decision.get(key), str) or not decision[key]:
            raise _fail(f"operation decision {key} is required")
    if decision.get("candidateLineageId") != snapshot.get("candidateLineageId"):
        raise _fail("operation decision candidate lineage differs")
    if decision.get("evidenceRootSha256") != snapshot.get("evidenceRootSha256"):
        raise _fail("operation decision evidence root differs")
    if decision.get("actorRole") != "release-manager":
        raise _fail("operation decision actor must be release-manager")
    if parse_time(decision.get("createdAt")) is None:
        raise _fail("operation decision createdAt is invalid")
    policy_errors = operation_policy_errors(operation_policy)
    signature_errors = signed_record_errors(
        decision,
        operation_policy,
        "release-manager",
        "decisionSha256",
        "actorId",
    )
    if policy_errors or signature_errors:
        raise _fail(
            "operation decision signature failed: "
            + "; ".join((*policy_errors, *signature_errors))
        )
    log = snapshot["decisionLog"]
    assert isinstance(log, list)
    if any(isinstance(item, dict) and item.get("decisionId") == decision.get("decisionId") for item in log):
        raise _fail("operation decisionId has already been recorded")


def _latest_pause_time(snapshot: JsonObject) -> datetime | None:
    log = snapshot["decisionLog"]
    assert isinstance(log, list)
    for item in reversed(log):
        if isinstance(item, dict) and item.get("action") == "PAUSE":
            return parse_time(item.get("createdAt"))
    return None


def transition(
    snapshot: JsonObject,
    decision: JsonObject,
    contract: JsonObject,
    *, base: Path | None = None,
) -> JsonObject:
    _validate_snapshot(snapshot)
    operation_policy = contract.get("operationPolicy")
    if not isinstance(operation_policy, dict):
        raise _fail("operation policy must be explicit")
    _validate_decision(snapshot, decision, operation_policy)
    current = snapshot["state"]
    action = decision["action"]
    assert isinstance(current, str)
    assert isinstance(action, str)
    if current == "REVOKED":
        raise _fail("REVOKED candidate cannot transition")
    advance = ADVANCE_ACTIONS.get(action)
    if advance is not None:
        source, target = advance
        if current != source:
            raise _fail(f"{action} requires {source} and a passing gate")
        if action == "ACTIVATE":
            errors = approval_errors(
                decision.get("approvals"),
                snapshot.get("evidenceRootSha256"),
                contract.get("approvalPolicy"),
            )
            if errors:
                raise _fail("ACTIVATE approval quorum failed: " + "; ".join(errors))
        next_state = target
    elif action == "PAUSE":
        if current != "PUBLIC_ACTIVE" or not decision.get("reason"):
            raise _fail("PAUSE requires PUBLIC_ACTIVE and a reason")
        next_state = "DISTRIBUTION_PAUSED"
    elif action == "RESUME":
        pause_time = _latest_pause_time(snapshot)
        if current != "DISTRIBUTION_PAUSED" or pause_time is None:
            raise _fail("RESUME requires a paused passing candidate")
        errors = approval_errors(
            decision.get("approvals"),
            snapshot.get("evidenceRootSha256"),
            contract.get("approvalPolicy"),
            fresh_after=pause_time,
        )
        if errors:
            raise _fail("RESUME requires a fresh approval quorum: " + "; ".join(errors))
        next_state = "PUBLIC_ACTIVE"
    elif action == "SUPERSEDE":
        if current not in {"PUBLIC_ACTIVE", "DISTRIBUTION_PAUSED"} or not decision.get("reason"):
            raise _fail("SUPERSEDE requires an active or paused candidate and a reason")
        next_state = "SUPERSEDED"
    elif action == "REVOKE":
        if not decision.get("reason"):
            raise _fail("REVOKE requires a reason")
        next_state = "REVOKED"
    else:
        raise _fail(f"unsupported operation action: {action}")
    receipt = None
    if advance is not None or action == "RESUME":
        receipt = _reproduce_public_audit(snapshot, decision, contract, base or Path.cwd(), next_state)
    updated = copy.deepcopy(snapshot)
    updated["state"] = next_state
    log = updated["decisionLog"]
    assert isinstance(log, list)
    log.append(copy.deepcopy(decision))
    if receipt is not None:
        updated["lastReproducedAudit"] = receipt
    return updated


def _reproduce_public_audit(snapshot: JsonObject, decision: JsonObject, contract: JsonObject,
                          base: Path, target: str) -> JsonObject:
    from .release_audit import audit_release
    inputs = decision.get("releaseAudit")
    if not isinstance(inputs, dict) or set(inputs) != {"candidate", "archiveManifest", "archiveRoot"}:
        raise _fail("public promotion requires hash-bound restored releaseAudit inputs, not gatePassed")
    if decision.get("acceptanceContractSha256") != sha256_json(contract):
        raise _fail("public operation must bind the exact trusted acceptance contract")
    try:
        root = restored_directory(inputs["archiveRoot"], base, "public archiveRoot")
        candidate = read_reference(inputs["candidate"], base, "public audit candidate")
        manifest = read_reference(inputs["archiveManifest"], base, "public archive manifest")
        chain = candidate.get("rootChain")
        evidence = chain.get("evidenceRoot") if isinstance(chain, dict) else None
        if candidate.get("candidateLineageId") != snapshot["candidateLineageId"] or not isinstance(evidence, dict) or evidence.get("sha256") != snapshot["evidenceRootSha256"]:
            raise _fail("public audit candidate differs from the operation's lineage or evidence root")
        result = audit_release(candidate, manifest, root, target, acceptance_contract=contract)
        if not result.passed:
            raise _fail("reproduced public release audit failed: " + "; ".join(result.errors[:8]))
        return {"state": target, "candidateSha256": inputs["candidate"]["sha256"],
            "archiveManifestSha256": inputs["archiveManifest"]["sha256"],
            "acceptanceContractSha256": sha256_json(contract), "evidenceRootSha256": evidence["sha256"]}
    except (OSError, ValueError, TypeError) as error:
        raise _fail(str(error)) from error


def main(argv: list[str] | None = None) -> int:
    from tools.external_beta.operations import load_json
    parser = argparse.ArgumentParser(description="Replay audits before applying a signed public release decision")
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--decision", type=Path, required=True)
    parser.add_argument("--acceptance-contract", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-blocked", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = transition(load_json(args.snapshot), load_json(args.decision), load_json(args.acceptance_contract), base=args.decision.absolute().parent)
        payload = {"passed": True, "state": result["state"], "snapshot": result}
        code = 4 if args.expect_blocked else 0
    except (OSError, ValueError, TypeError) as error:
        payload = {"passed": False, "blocked": ["operation"], "errors": [str(error)]}
        code = 0 if args.expect_blocked else 2
    text = json.dumps(payload, sort_keys=True) + "\n"
    print(text, end="")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
