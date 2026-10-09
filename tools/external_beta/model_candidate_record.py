"""Opaque model package verification and observed install refusal, engineering only."""
from __future__ import annotations

from .installed_candidate_record import HEX, _audit_record

RECORD_TYPE = "seam.u14.model-installation-refusal.v1"
FIXED = {
    "schemaVersion": 1, "recordType": RECORD_TYPE, "result": "ModelPackageVerifiedInstallRefused",
    "evidenceScope": "ENGINEERING_ONLY", "resourceKind": "neural-original", "payloadFamily": "model",
    "installationResult": "REFUSED", "refusalReason": "MODEL_INSTALL_UNSUPPORTED",
    "installDirectoryCreated": False, "dependencyEvidence": "SIGNED_DECLARATION",
    "runtimeAvailability": "NOT_CHECKED", "graphExecution": "NOT_RUN",
    "qualification": "NOT_QUALIFIED", "humanAcceptance": "NOT_RUN",
    "authorizesRelease": False, "releaseEligible": False,
}
DIGESTS = ("resourceCandidateSha256", "packageDigest", "candidateContentSha256", "signerKeyId")
FIELDS = frozenset(FIXED) | frozenset(DIGESTS) | {
    "resourceId", "resourceVersion", "packageEntries", "externalDependencies"}


def validate_record(record):
    if not isinstance(record, dict) or set(record) != FIELDS:
        raise ValueError("model refusal record fields differ from the closed engineering schema")
    for key, expected in FIXED.items():
        if type(record[key]) is not type(expected) or record[key] != expected:
            raise ValueError(f"model refusal record has an invalid {key}")
    for key in DIGESTS:
        if not isinstance(record[key], str) or HEX.fullmatch(record[key]) is None:
            raise ValueError(f"model refusal record has an invalid {key}")
    for key in ("resourceId", "resourceVersion"):
        if not isinstance(record[key], str) or not 1 <= len(record[key]) <= 256:
            raise ValueError(f"model refusal record has an invalid {key}")
    if type(record["packageEntries"]) is not int or not 3 <= record["packageEntries"] <= 100000:
        raise ValueError("model refusal record has an invalid packageEntries")
    dependencies = record["externalDependencies"]
    if not isinstance(dependencies, list) or len(dependencies) != 1:
        raise ValueError("model refusal requires one declared neural-runtime dependency")
    dependency = dependencies[0]
    if not isinstance(dependency, dict) or set(dependency) != {"kind", "id", "revision"}:
        raise ValueError("model dependency requires only declared kind/id/revision")
    if dependency["kind"] != "neural-runtime":
        raise ValueError("model dependency must declare neural-runtime")
    for key, limit in (("id", 128), ("revision", 64)):
        if not isinstance(dependency[key], str) or not 1 <= len(dependency[key]) <= limit:
            raise ValueError(f"model dependency has an invalid {key}")


def audit_model_candidate_record(*, record_path, record_sha256, package_path, public_key_path,
        public_key_sha256, cli_path, cli_sha256, timeout_seconds=60):
    return _audit_record(record_path=record_path, record_sha256=record_sha256, package_path=package_path,
        public_key_path=public_key_path, public_key_sha256=public_key_sha256, cli_path=cli_path, cli_sha256=cli_sha256,
        validator=validate_record, command="probe-model-candidate",
        identity_fields=("resourceCandidateSha256", "packageDigest", "refusalReason"), timeout_seconds=timeout_seconds)
