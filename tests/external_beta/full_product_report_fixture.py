"""Complete synthetic report for the semantic reader, never production GO.

Only a temporary copy of the contract receives synthetic qualification values.
The canonical contract and its unresolved owners are never changed; this
fixture is not passed through a promotion or authenticated release operation.
"""
from __future__ import annotations

import copy
import hashlib
import io
import json
import math
from pathlib import Path
import struct
import wave

from tools.external_beta.full_product_contract_profile import FIXED_CRITERIA
from tools.external_beta.full_product_contract_protocols import CHECKS, required_check_ids
from tools.external_beta.full_product_contract_registry import ARTIFACT_KINDS, REQUIREMENTS, CRITERIA_BY_REQUIREMENT
from tests.external_beta.test_full_product_definition import resolved_shape

ROOT = Path(__file__).resolve().parents[2]


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).hexdigest()


def complete_report(root: Path):
    def raw(name, value):
        path = root / name
        data = value if isinstance(value, bytes) else json.dumps(value, sort_keys=True).encode()
        path.write_bytes(data)
        return {"locator": name, "sha256": hashlib.sha256(data).hexdigest()}

    generic = raw("synthetic-evidence.json", {"evidenceScope": "synthetic-test-only", "releaseEligible": False})
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as stream:
        stream.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
        stream.writeframes(b"".join(struct.pack("<h", round(1000 * math.sin(i * 2 * math.pi * 440 / 48000))) for i in range(4800)))
    audio = raw("synthetic-tone.wav", buffer.getvalue())
    contract = json.loads((ROOT / "docs/product/full-product-beta-contract.json").read_text())
    contract["contractId"] = "synthetic-test-only.full-product-reader"
    contract["scope"]["matrixStatus"] = "FROZEN"
    contract["scope"]["releasedResources"] = [{"id": "synthetic-test-only-resource"}]
    contract["evaluationProfile"]["status"] = "FROZEN"
    empirical = {}
    for row in contract["evaluationProfile"]["criteria"]:
        if row["kind"] != "empirical":
            continue
        resolved_shape(row)
        for cell in row["value"]["cells"]:
            for key in ("machineProfile", "workload", "resourceMatrix"):
                cell["bindings"][key] = copy.deepcopy(generic)
            if cell["valueType"] == "machine-profile":
                cell["value"]["profile"] = copy.deepcopy(generic)
        row["measurement"] = raw("qualification-" + row["id"] + ".json", row["value"])
        row["independentReview"] = copy.deepcopy(generic)
        empirical[row["id"]] = copy.deepcopy(row["value"])

    all_kinds = set(kind for kinds in ARTIFACT_KINDS.values() for kind in kinds)
    all_kinds.update(("partition-manifest", "phoneme-alignment", "modulation-phase", "dependency-invalidation"))
    artifacts = [{"kind": kind, **(audio if kind == "audio" else generic)} for kind in sorted(all_kinds)]
    cases = []
    for case in contract["cases"]:
        identifier = case["id"]
        requirement = REQUIREMENTS[case["requirementId"]]
        dimensions = case["dimensions"]
        language = dimensions["language"][0]
        if language == "each-declared-language":
            language = "ja"
        observation = {
            "language": language, "resourceIds": ["synthetic-test-only-resource"],
            "backend": dimensions["backend"][0], "platform": dimensions["platform"][0],
            "host": dimensions["host"][0], "workloadId": case["workload"]["id"],
            "workloadSha256": digest(case["workload"]),
            "machineProfileId": "synthetic-test-only-machine", "machineProfileSha256": generic["sha256"],
            "sourceCommit": "1" * 40, "buildId": "synthetic-test-only-build",
            "signedDeliverableSha256": "2" * 64, "installedTreeSha256": "3" * 64,
            "bindings": [{"kind": "bank", "id": "synthetic-test-only-resource", "version": "1", "sha256": audio["sha256"]}],
            "artifacts": copy.deepcopy(artifacts), "reviews": [], "measurements": [], "checkResults": [],
        }
        for role in requirement.review_roles:
            review = {"reviewerId": "synthetic-reviewer-" + role, "producerId": "synthetic-producer",
                "role": role, "language": language, "status": "ACCEPTED", "rubricSha256": generic["sha256"]}
            review["rawEvidence"] = raw(f"review-{identifier}-{role}.json", review)
            observation["reviews"].append(review)
        for criterion in CRITERIA_BY_REQUIREMENT[case["requirementId"]]:
            definition = next(row for row in contract["evaluationProfile"]["criteria"] if row["id"] == criterion)
            fixed = FIXED_CRITERIA.get(criterion)
            value = fixed[1] if fixed else 1
            unit = fixed[2] if fixed else "synthetic-protocol-result"
            measurement = {"criterionId": criterion, "value": value, "unit": unit, "methodSha256": digest(definition)}
            measurement["rawEvidence"] = raw(f"measurement-{identifier}-{criterion}.json", measurement)
            observation["measurements"].append(measurement)
        study = {"assignmentSha256": generic["sha256"], "participantId": "synthetic-creator",
            "nativeLanguage": language if language != "language-independent" else "ja",
            "taskOrder": "MANUAL_THEN_ASSISTED", "materialSha256": generic["sha256"],
            "priorExposure": "synthetic test only", "manualCorrectionSeconds": 2,
            "assistedCorrectionSeconds": 1, "remainingPronunciationDefects": 0, "remainingTimingDefects": 0}
        study["rawEvidence"] = raw(f"creator-{identifier}.json", study)
        observation["creatorStudy"] = study
        for check_id in required_check_ids(identifier):
            check = {"id": check_id, "protocolSha256": digest(CHECKS[check_id]), "rawEvidence": [copy.deepcopy(generic)]}
            operations = CHECKS[check_id].get("requiredOperations", [])
            if operations:
                check["operationObservations"] = [{"operationId": op, "inputs": [copy.deepcopy(generic)], "outputs": [copy.deepcopy(generic)]} for op in operations]
            if check_id == "fp.check.chunk-continuity.v1":
                check["continuity"] = {key: copy.deepcopy(audio if key in ("wholeRender", "chunkedRender") else generic)
                    for key in ("wholeRender", "chunkedRender", "partitionManifest", "phonemeAlignment", "modulationPhase", "neighborContextChange", "dependencyInvalidation")}
            observation["checkResults"].append(check)
        cases.append({"id": identifier, "requirementId": case["requirementId"], "status": "PASS",
            "resultType": requirement.result_type, "observations": [observation]})
    report = {"schemaVersion": 1, "recordType": "full-product-beta-report", "status": "PASS",
        "candidateRootId": "synthetic-test-only-not-a-release", "candidateRootSha256": "4" * 64,
        "acceptanceContractSha256": "5" * 64, "fullProductContractSha256": digest(contract),
        "evaluationProfileSha256": digest(contract["evaluationProfile"]),
        "resourceMatrixSha256": digest(contract["scope"]["releasedResources"]),
        "requirements": [{"id": key, "status": "PASS", "caseIds": [key + "." + slug for slug in spec.case_slugs]} for key, spec in REQUIREMENTS.items()],
        "cases": cases, "rawArchive": generic, "empiricalResults": empirical}
    return report, contract
