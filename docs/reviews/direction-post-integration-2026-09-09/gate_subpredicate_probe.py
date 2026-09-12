"""Read-only diagnostic: a narrow predicate, NOT a full-report/GO bypass."""
import hashlib
import json
from pathlib import Path

from tools.external_beta.full_product_report import _observation_errors, _case_dimensions, _sha256_json
from tools.external_beta.full_product_contract_protocols import CHECKS

root = Path.cwd()
case = "R13.macos-arm64-reaper-clap"
check_id = "fp.check." + case + ".v1"
reference = {"locator": "README.md", "sha256": hashlib.sha256((root / "README.md").read_bytes()).hexdigest()}
observation = {key: values[0] for key, values in _case_dimensions(case).items() if key != "resource"}
observation.update({
    "sourceCommit": "a" * 40,
    "buildId": "read-only-audit",
    "resourceIds": ["audit-resource"],
    "checkResults": [{
        "id": check_id,
        "protocolSha256": _sha256_json(CHECKS[check_id]),
        "rawEvidence": [reference],
        "operationObservations": [
            {"operationId": operation, "inputs": [reference], "outputs": [reference]}
            for operation in CHECKS[check_id]["requiredOperations"]
        ],
    }],
})
errors = _observation_errors(observation, case_id=case, source_commit="a" * 40,
    build_id="read-only-audit", resource_ids={"audit-resource"}, report_base=root,
    verify_references=True, criterion_definitions={})
print(json.dumps({"probe": "RAW_OPERATION_SUBPREDICATE_ONLY", "case": case,
    "rawIsREADME": True, "rawSha256": reference["sha256"], "errors": errors,
    "notFullReportOrGO": True}, sort_keys=True))
print(json.dumps({"probe": "TEMPO_FORMULA_ONLY", "trueSeconds": 4 * 60 / 120 + 4 * 60 / 60,
    "mappedSeconds": 8 * 60 / 60, "errorAt48kFrames": 96000,
    "notNativeHostExecution": True}, sort_keys=True))
