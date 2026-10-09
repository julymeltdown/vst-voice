"""Synthetic component fixtures for the archived U45 typed checks.

All 83 case rows have internally consistent declarations. The retained legacy
soak blobs deliberately fail current full-report admission; these fixtures are
never release or human evidence. The canonical contract is never modified.
"""
from __future__ import annotations

import copy
import hashlib
import io
import itertools
import json
import math
from pathlib import Path
import struct
import wave
import zlib

from tools.external_beta.full_product_contract import SYNTHETIC_CONTRACT_ID
from tools.external_beta.full_product_contract_profile import FIXED_CRITERIA
from tools.external_beta.full_product_contract_protocols import CHECKS, required_check_ids
from tools.external_beta.full_product_contract_registry import ARTIFACT_KINDS, LANGUAGES, PLATFORMS, REQUIREMENTS
from tools.external_beta.full_product_contract_protocols import CONTINUITY_CASES
from tools.external_beta.full_product_gate import (
    CREATOR_RECORD,
    ENGINEERING_FIXTURE,
    INSTALLED_RESOURCE_RECORD,
    LANGUAGE_BEARING_RESULTS,
    MEASUREMENT_RECORD,
    PACKAGE_KINDS,
    PRIMARY_BINDINGS,
    PROTOCOL_UNIT,
    REGISTRY_RECORD,
    REVIEW_RECORD,
    SEMANTIC_VALIDATION,
    SESSION_LOG_RECORD,
    approval_scope,
)
from tests.external_beta.test_full_product_definition import resolved_shape

ROOT = Path(__file__).resolve().parents[2]
PRODUCER = "fixture-implementer-producer"
DEFAULT_INSTALLED = {
    "macos-arm64": ("3" * 64, "2" * 64),
    "windows-x86_64": ("6" * 64, "2" * 64),
}
# One typed released resource per contract resource kind; "native" means the
# resource is exercised through the product shell rather than a renderer.
RESOURCE_PLAN = (
    ("fixture.sample-real", "sample-real", list(LANGUAGES), ["classical"]),
    ("fixture.sample-procedural", "sample-procedural", list(LANGUAGES), ["classical", "procedural"]),
    ("fixture.recipe-original", "recipe-original", list(LANGUAGES), ["procedural"]),
    ("fixture.neural-original", "neural-original", list(LANGUAGES), ["neural"]),
    ("fixture.dictionary-original", "dictionary-original", list(LANGUAGES), ["native"]),
    ("fixture.character-original", "character-original", [], ["native"]),
)
CREATOR_REQUIREMENTS = ("R10", "R11", "R16")


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).hexdigest()


def text_digest(value: str) -> str:
    return hashlib.sha256(value.encode()).hexdigest()


def tone_wav(frequency: float = 440.0) -> bytes:
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as stream:
        stream.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
        stream.writeframes(b"".join(
            struct.pack("<h", round(1000 * math.sin(i * 2 * math.pi * frequency / 48000))) for i in range(4800)
        ))
    return buffer.getvalue()


def png(width: int = 2, height: int = 2) -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\x00" + b"\x80\x80\x80" * width for _ in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


def project_document(project_id: str, bpm: int) -> dict:
    return {"formatId": "com.project-seam.project", "schemaVersion": 9, "projectId": project_id,
        "name": "Synthetic fixture project, not product evidence", "ppq": 960,
        "tempoMap": [{"tick": 0, "bpm": bpm}], "meterMap": [{"tick": 0, "numerator": 4, "denominator": 4}]}


def released_resource(identifier: str, kind: str, languages: list[str], backends: list[str]) -> dict:
    bindings = [{"kind": PRIMARY_BINDINGS[kind], "id": identifier + ".content", "version": "1",
        "sha256": text_digest("content:" + identifier)}]
    if kind == "neural-original":
        bindings.append({"kind": "vocoder", "id": identifier + ".vocoder", "version": "1",
            "sha256": text_digest("vocoder:" + identifier)})
    return {"id": identifier, "resourceKind": kind, "packageKind": PACKAGE_KINDS[kind], "version": "1.0.0-fixture",
        "packageSha256": text_digest("package:" + identifier), "bindings": bindings, "dependencies": [],
        "languages": languages, "backends": backends, "platforms": list(PLATFORMS)}


class _Writer:
    def __init__(self, root: Path):
        self.root = root
        self.cache: dict[str, dict] = {}

    def raw(self, name: str, value) -> dict:
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        data = value if isinstance(value, bytes) else json.dumps(value, sort_keys=True).encode()
        path.write_bytes(data)
        return {"locator": name, "sha256": hashlib.sha256(data).hexdigest()}

    def shared(self, name: str, value) -> dict:
        if name not in self.cache:
            self.cache[name] = self.raw(name, value)
        return copy.deepcopy(self.cache[name])


def synthetic_contract(root: Path) -> dict:
    """Private synthetic contract copy with frozen fixture qualification values."""

    writer = _Writer(root)
    generic = writer.shared("synthetic-evidence.json", {"evidenceScope": "synthetic-test-only", "releaseEligible": False})
    contract = json.loads((ROOT / "docs/product/full-product-beta-contract.json").read_text())
    contract["contractId"] = SYNTHETIC_CONTRACT_ID
    contract["semanticValidation"] = copy.deepcopy(SEMANTIC_VALIDATION)
    contract["scope"]["matrixStatus"] = "FROZEN"
    contract["scope"]["releasedResources"] = [released_resource(*row) for row in RESOURCE_PLAN]
    contract["evaluationProfile"]["status"] = "FROZEN"
    for row in contract["evaluationProfile"]["criteria"]:
        if row["kind"] != "empirical":
            continue
        resolved_shape(row)
        for cell in row["value"]["cells"]:
            for key in ("machineProfile", "workload", "resourceMatrix"):
                cell["bindings"][key] = copy.deepcopy(generic)
            if cell["valueType"] == "machine-profile":
                cell["value"]["profile"] = copy.deepcopy(generic)
        row["measurement"] = writer.raw("qualification-" + row["id"] + ".json", row["value"])
        row["independentReview"] = copy.deepcopy(generic)
    return contract


def _case_plan(case: dict, resources: list[dict]) -> list[dict]:
    requirement = REQUIREMENTS[case["requirementId"]]
    dimensions = case["dimensions"]
    languages = dimensions["language"]
    if languages == ["each-declared-language"]:
        languages = list(LANGUAGES) if requirement.result_type in LANGUAGE_BEARING_RESULTS else ["language-independent"]
    kinds = [kind for kind in dimensions["resource"] if kind != "all-released"]
    pool = kinds + ([entry["resourceKind"] for entry in resources] if "all-released" in dimensions["resource"] else [])
    by_kind = {entry["resourceKind"]: entry for entry in resources}

    def compatible(kind: str, backend: str) -> bool:
        return backend == "native" or backend in by_kind[kind]["backends"]

    pairs, uncovered_kinds, uncovered_backends = [], set(kinds), set(dimensions["backend"])
    while uncovered_kinds or uncovered_backends or not pairs:
        options = [(int(kind in uncovered_kinds) + int(backend in uncovered_backends), kind, backend)
            for kind in dict.fromkeys(pool) for backend in dimensions["backend"] if compatible(kind, backend)]
        score, kind, backend = max(options, key=lambda item: item[0])
        if score == 0 and pairs:
            raise AssertionError(f"fixture cannot cover {case['id']}")
        pairs.append((kind, backend))
        uncovered_kinds.discard(kind)
        uncovered_backends.discard(backend)
    hosts, platforms = dimensions["host"], dimensions["platform"]
    count = max(len(pairs), len(languages), len(hosts), len(platforms))
    plan = []
    for index in range(count):
        kind, backend = pairs[index % len(pairs)]
        host = hosts[index % len(hosts)]
        platform = host.split("/")[0] if host != "standalone" else platforms[index % len(platforms)]
        plan.append({"language": languages[index % len(languages)], "backend": backend, "platform": platform,
            "host": host, "resourceIds": [by_kind[kind]["id"]]})
    return plan


def _cover_resources(plans: dict[str, list[dict]], contract: dict) -> None:
    """Add supporting resources so every resource is exercised where it claims support."""

    resources = contract["scope"]["releasedResources"]
    cases = {case["id"]: case for case in contract["cases"]}
    use = {entry["id"]: set() for entry in resources}
    for plan in plans.values():
        for item in plan:
            for identifier in item["resourceIds"]:
                use[identifier].add((item["platform"], item["language"]))
    for entry in resources:
        singer = entry["resourceKind"] in {"sample-real", "sample-procedural", "recipe-original", "neural-original"}
        for platform in PLATFORMS:
            for language in entry["languages"] if singer else [None]:
                if any(used == platform and (language is None or sung == language) for used, sung in use[entry["id"]]):
                    continue
                target = next(item for case_id, plan in plans.items()
                    if "all-released" in cases[case_id]["dimensions"]["resource"] for item in plan
                    if item["platform"] == platform and (language is None or item["language"] == language))
                target["resourceIds"].append(entry["id"])
                use[entry["id"]].add((platform, target["language"]))


def complete_report(
    root: Path,
    *,
    contract: dict | None = None,
    candidate_root_id: str = "synthetic-test-only-not-a-release",
    candidate_root_sha256: str = "4" * 64,
    acceptance_contract_sha256: str = "5" * 64,
    full_product_contract_sha256: str | None = None,
    source_commit: str = "1" * 40,
    build_id: str = "synthetic-test-only-build",
    installed: dict | None = None,
):
    if contract is None:
        contract = synthetic_contract(root)
    installed = installed or DEFAULT_INSTALLED
    writer = _Writer(root)
    envelope = {"evidenceClass": ENGINEERING_FIXTURE, "candidateRootId": candidate_root_id}
    generic = writer.shared("synthetic-evidence.json", {"evidenceScope": "synthetic-test-only", "releaseEligible": False})
    audio = writer.shared("synthetic-tone.wav", tone_wav())
    capture = writer.shared("synthetic-capture.png", png())
    before = writer.shared("project-before.seam", json.dumps(project_document("fixture-before", 120)).encode())
    after = writer.shared("project-after.seam", json.dumps(project_document("fixture-after", 96)).encode())
    operation_input = writer.shared("operation-input.json", {"evidenceScope": "synthetic-test-only", "role": "input"})
    operation_output = writer.shared("operation-output.json", {"evidenceScope": "synthetic-test-only", "role": "output"})
    resources = contract["scope"]["releasedResources"]
    by_id = {entry["id"]: entry for entry in resources}
    criteria = {row["id"]: row for row in contract["evaluationProfile"]["criteria"]}
    review_rubric = digest(contract["protocolCatalog"]["independent-review"])
    roles = sorted({role for spec in REQUIREMENTS.values() for role in spec.review_roles})
    plans = {case["id"]: _case_plan(case, resources) for case in contract["cases"]}
    for requirement in CREATOR_REQUIREMENTS:
        case_ids = [case_id for case_id in plans if case_id.startswith(requirement + ".")]
        while sum(len(plans[case_id]) for case_id in case_ids) < 5:
            plans[case_ids[0]].append(copy.deepcopy(plans[case_ids[0]][-1]))
    _cover_resources(plans, contract)
    creators = itertools.count(1)
    participants: list[str] = []
    cases = []
    for case in contract["cases"]:
        identifier = case["id"]
        requirement = REQUIREMENTS[case["requirementId"]]
        kinds = list(ARTIFACT_KINDS[requirement.result_type])
        if identifier in CONTINUITY_CASES:
            kinds += ["partition-manifest", "phoneme-alignment", "modulation-phase", "dependency-invalidation"]
        observations = []
        for index, item in enumerate(plans[identifier]):
            platform = item["platform"]
            observation = {
                "language": item["language"], "resourceIds": item["resourceIds"], "backend": item["backend"],
                "platform": platform, "host": item["host"], "workloadId": case["workload"]["id"],
                "workloadSha256": digest(case["workload"]), "machineProfileId": "synthetic-test-only-machine",
                "machineProfileSha256": generic["sha256"], "sourceCommit": source_commit, "buildId": build_id,
                "signedDeliverableSha256": installed[platform][1], "installedTreeSha256": installed[platform][0],
                "bindings": [copy.deepcopy(binding) for resource_id in item["resourceIds"] for binding in by_id[resource_id]["bindings"]],
                "artifacts": [], "reviews": [], "checkResults": [],
            }
            prefix = f"cases/{identifier}/{index}"
            artifacts = observation["artifacts"]
            for kind in kinds:
                if kind == "audio":
                    artifacts.append({"kind": kind, **audio})
                elif kind == "project":
                    artifacts.extend([{"kind": kind, **before}, {"kind": kind, **after}])
                elif kind == "ui-capture":
                    artifacts.append({"kind": kind, **capture})
                elif kind == "session-log":
                    stateful = "project" in kinds
                    log = {"recordType": SESSION_LOG_RECORD, **envelope, "caseId": identifier, "operations": [{
                        "operationId": "EDIT_AND_CAPTURE" if stateful else "RUN_SESSION",
                        "beforeProjectSha256": before["sha256"] if stateful else None,
                        "afterProjectSha256": after["sha256"] if stateful else None,
                        "captureSha256s": [capture["sha256"]] if "ui-capture" in kinds else []}]}
                    artifacts.append({"kind": kind, **writer.shared(f"cases/{identifier}/session-log.json", log)})
                elif kind == "installed-resource":
                    for resource_id in item["resourceIds"]:
                        receipt = {"recordType": INSTALLED_RESOURCE_RECORD, **envelope, "caseId": identifier,
                            "resourceId": resource_id, "packageSha256": by_id[resource_id]["packageSha256"],
                            "installReceiptSha256": text_digest("receipt:" + resource_id + platform),
                            "platform": platform, "installedTreeSha256": installed[platform][0]}
                        artifacts.append({"kind": kind, **writer.shared(f"cases/{identifier}/install-{resource_id}-{platform}.json", receipt)})
                elif kind not in ("independent-review", "measurement"):
                    artifacts.append({"kind": kind, **writer.shared(f"artifact-{kind}.json",
                        {"artifactKind": kind, "evidenceScope": "synthetic-test-only"})})
            material = sorted({artifact["sha256"] for artifact in artifacts})
            measured_inputs = sorted({audio["sha256"], after["sha256"]} & set(material)) or material[:1]
            measurements = []
            for criterion in case["criteriaIds"]:
                definition = criteria[criterion]
                measurement = {"criterionId": criterion, "methodSha256": digest(definition)}
                extra = {}
                if criterion in FIXED_CRITERIA:
                    measurement.update(value=FIXED_CRITERIA[criterion][1], unit=FIXED_CRITERIA[criterion][2])
                elif definition["kind"] == "protocol":
                    protocol = definition["protocol"]
                    measurement.update(value=1, unit=PROTOCOL_UNIT)
                    extra = {"protocol": {"id": protocol["id"], "sha256": digest(protocol)},
                        "constraintResults": {key: {"status": "MET", "evidence": material[:1]} for key in protocol["constraints"]}}
                else:
                    cell = next((cell for cell in definition["value"]["cells"]
                        if all(cell["dimensions"].get(key, observation[key]) == observation[key] for key in ("platform", "backend", "language"))), None)
                    if cell is None:
                        continue
                    measurement.update(value=1 if cell["valueType"] != "machine-profile" else 0,
                        unit=cell.get("unit", "machine-profile"))
                    extra = {"cellId": cell["id"]}
                    if cell["valueType"] == "machine-profile":
                        extra["machineProfileSha256"] = observation["machineProfileSha256"]
                record = {"recordType": MEASUREMENT_RECORD, **envelope, "caseId": identifier, **measurement,
                    "inputs": measured_inputs, **extra}
                measurement["rawEvidence"] = writer.raw(f"{prefix}/measurement-{criterion}.json", record)
                artifacts.append({"kind": "measurement", **measurement["rawEvidence"]})
                measurements.append(measurement)
            if measurements:
                observation["measurements"] = measurements
            material = sorted({artifact["sha256"] for artifact in artifacts})
            for role in requirement.review_roles:
                review = {"reviewerId": "fixture-reviewer-" + role, "producerId": PRODUCER, "role": role,
                    "language": item["language"], "status": "ACCEPTED", "rubricSha256": review_rubric}
                record = {"recordType": REVIEW_RECORD, **envelope, "caseId": identifier, **review,
                    "approvalScope": approval_scope(observation, identifier), "reviewedArtifacts": material}
                review["rawEvidence"] = writer.raw(f"{prefix}/review-{role}.json", record)
                observation["reviews"].append(review)
                artifacts.append({"kind": "independent-review", **review["rawEvidence"]})
            if case["requirementId"] in CREATOR_REQUIREMENTS or requirement.result_type == "creator-session":
                participant = f"fixture-creator-{next(creators):03d}"
                participants.append(participant)
                study = {"assignmentSha256": generic["sha256"], "participantId": participant,
                    "nativeLanguage": item["language"] if item["language"] in LANGUAGES else "ja",
                    "taskOrder": "MANUAL_THEN_ASSISTED" if len(participants) % 2 else "ASSISTED_THEN_MANUAL",
                    "materialSha256": generic["sha256"], "priorExposure": "synthetic test only",
                    "manualCorrectionSeconds": 120, "assistedCorrectionSeconds": 60,
                    "remainingPronunciationDefects": 0, "remainingTimingDefects": 0}
                study["rawEvidence"] = writer.raw(f"{prefix}/creator.json", {"recordType": CREATOR_RECORD, **envelope, "caseId": identifier, **study})
                observation["creatorStudy"] = study
            for check_id in required_check_ids(identifier):
                check = {"id": check_id, "protocolSha256": digest(CHECKS[check_id]), "rawEvidence": [copy.deepcopy(generic)]}
                operations = CHECKS[check_id].get("requiredOperations", [])
                if operations:
                    check["operationObservations"] = [{"operationId": op, "inputs": [copy.deepcopy(operation_input)],
                        "outputs": [copy.deepcopy(operation_output)]} for op in operations]
                if check_id == "fp.check.chunk-continuity.v1":
                    check["continuity"] = {key: copy.deepcopy(audio if key in ("wholeRender", "chunkedRender") else generic)
                        for key in ("wholeRender", "chunkedRender", "partitionManifest", "phonemeAlignment", "modulationPhase", "neighborContextChange", "dependencyInvalidation")}
                observation["checkResults"].append(check)
            observations.append(observation)
        cases.append({"id": identifier, "requirementId": case["requirementId"], "status": "PASS",
            "resultType": requirement.result_type, "observations": observations})
    registry = {"recordType": REGISTRY_RECORD, **envelope,
        "reviewers": [{"reviewerId": "fixture-reviewer-" + role, "kind": "HUMAN", "roles": [role], "nativeLanguages": list(LANGUAGES)} for role in roles],
        "producers": [{"producerId": PRODUCER, "kind": "HUMAN", "affiliation": "IMPLEMENTER"}]
            + [{"producerId": participant, "kind": "HUMAN", "affiliation": "INDEPENDENT"} for participant in participants]}
    empirical = {row["id"]: copy.deepcopy(row["value"]) for row in contract["evaluationProfile"]["criteria"] if row["kind"] == "empirical"}
    report = {"schemaVersion": 1, "recordType": "full-product-beta-report", "status": "PASS",
        "evidenceClass": ENGINEERING_FIXTURE, "candidateRootId": candidate_root_id,
        "candidateRootSha256": candidate_root_sha256, "acceptanceContractSha256": acceptance_contract_sha256,
        "fullProductContractSha256": full_product_contract_sha256 or digest(contract),
        "evaluationProfileSha256": digest(contract["evaluationProfile"]),
        "resourceMatrixSha256": digest(contract["scope"]["releasedResources"]),
        "requirements": [{"id": key, "status": "PASS", "caseIds": [key + "." + slug for slug in spec.case_slugs]} for key, spec in REQUIREMENTS.items()],
        "cases": cases, "rawArchive": generic, "reviewerRegistry": writer.raw("reviewer-registry.json", registry),
        "empiricalResults": empirical}
    return report, contract



def write_reference(root: Path, relative: str, value) -> dict:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    data = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()
    path.write_bytes(data)
    return {"locator": relative, "sha256": hashlib.sha256(data).hexdigest()}


def synthetic_beta_archive(root: Path, *, closed: bool = True):
    """Complete synthetic External Beta candidate archive around the fixture report.

    EB-001 to EB-008 come from the legacy candidate fixture and EB-009 binds
    the complete ENGINEERING_FIXTURE report under the synthetic contract.  The
    real release gate audits every byte of it and still refuses READY/CLOSED
    because synthetic evidence never carries release authority.
    """

    from tests.external_beta.release_gate_fixtures import candidate as beta_candidate
    from tests.external_beta.test_cohort_gate import _cohort
    from tools.external_beta.evidence_archive import create_archive_manifest
    from tools.external_beta.release_gate import candidate_root_sha256, sha256_json

    root.mkdir(parents=True, exist_ok=True)
    contract = synthetic_contract(root)
    full_ref = write_reference(root, "full-contract.json", contract)
    acceptance = json.loads((ROOT / "docs/product/external-beta-acceptance.json").read_text())
    acceptance["fullProductContract"] = full_ref
    policy_ref = write_reference(root, "acceptance.json", acceptance)
    beta = beta_candidate()
    beta["gate"] = "EXTERNAL_BETA_CLOSED" if closed else "EXTERNAL_BETA_READY"
    beta["acceptanceContractSha256"] = sha256_json(acceptance)
    beta["candidateRoot"]["acceptanceContractSha256"] = beta["acceptanceContractSha256"]
    beta["candidateRoot"]["sha256"] = candidate_root_sha256(beta["candidateRoot"])
    installed = {}
    for record in beta["evidence"]:
        if record["stageNodeId"].startswith("installed"):
            installed[record["platform"] + "-" + record["architecture"]] = (record["installedTreeSha256"], record["finalDeliverableSha256"])
    report, _ = complete_report(
        root, contract=contract, candidate_root_id=beta["candidateRoot"]["id"],
        candidate_root_sha256=beta["candidateRoot"]["sha256"], acceptance_contract_sha256=beta["acceptanceContractSha256"],
        full_product_contract_sha256=full_ref["sha256"], source_commit=beta["releaseIdentity"]["sourceCommit"],
        build_id=beta["releaseIdentity"]["buildId"], installed=installed)
    report_ref = write_reference(root, "report.json", report)
    references = []
    for platform in ("macos", "windows"):
        value = copy.deepcopy(next(record for record in beta["evidence"] if record["platform"] == platform and record["stageNodeId"].startswith("installed")))
        value.update(recordId="synthetic-full-product-" + platform, requirementId="EB-009-full-product",
            surface="standalone", host=None, fullProductReport=report_ref)
        references.append(value["recordId"])
        beta["evidence"].append(value)
    beta["requirements"]["EB-009-full-product"] = {"status": "PASS", "evidenceRecordIds": references}
    if closed:
        beta["cohort"] = _cohort()
    for value in beta["evidence"]:
        payload = {key: child for key, child in value.items() if key != "rawArchive"}
        value["rawArchive"] = write_reference(root, "archive/" + value["recordId"] + ".json", payload)
    paths = [path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()]
    manifest = create_archive_manifest(beta["candidateRoot"]["id"], root, paths,
        "synthetic-test-only-beta-archive", anchor_locator="https://8.8.8.8/synthetic-test-only")
    beta["archive"] = {"locator": "synthetic-test-only-beta-archive", "sha256": manifest["manifestSha256"], "anchored": True, "immutable": True}
    beta_ref = write_reference(root, "candidate.json", beta)
    manifest_ref = write_reference(root, "archive-manifest.json", manifest)
    return {"candidate": beta, "candidateReference": beta_ref, "manifest": manifest, "manifestReference": manifest_ref,
        "acceptance": acceptance, "policyReference": policy_ref, "trustedAnchor": manifest["anchor"]["sha256"]}
