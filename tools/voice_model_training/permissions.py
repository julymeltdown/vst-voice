"""Capture training-specific permission assertions without issuing authority."""
import hashlib
from pathlib import Path
from .prepare import prepare_sources

from tools.external_beta._source_admission import PERMISSIONS

TRAINING_PERMISSIONS = (*PERMISSIONS, "modelTraining", "modelRedistribution", "commercialModels")


def inspect_permission_sources(manifest: dict, evidence: dict[str, bytes], *, root: Path,
                               sources: list[dict], sample_rate: int) -> dict:
    """Join assertions to freshly inspected audio, never caller-supplied reports."""
    permissions = permission_report(manifest, evidence)
    prepared = prepare_sources(root, sources, sample_rate=sample_rate)
    if prepared["rejectedCount"]:
        raise ValueError("Permission source inspection failed; run prepare for diagnostics")
    asserted = {r["sourceId"]: r for r in permissions["sources"]}
    inspected = {r["sourceId"]: r["inspection"] for r in prepared["sources"]}
    if set(asserted) != set(inspected):
        raise ValueError("Permission assertions must cover exactly the inspected sources")
    joined = []
    for source in sorted(sources, key=lambda r: r["sourceId"]):
        identity = source["sourceId"]
        assertion, audio = asserted[identity], inspected[identity]
        if assertion["sourceSha256"] != audio["sourceSha256"]:
            raise ValueError("Permission source digest differs from actual recording")
        joined.append(dict(assertion, audioSha256=audio["audioSha256"], sampleRate=audio["sampleRate"],
                           frameCount=audio["frameCount"], **{k: source[k] for k in ("songId", "sessionId", "lineageId")}))
    return dict(permissions, schemaVersion=2, sources=joined, sourceBytesVerified=True)


def permission_report(manifest: dict, evidence: dict[str, bytes]) -> dict:
    """Check explicit asserted scope and exact evidence, not legal interpretation.

    A true permission is a supplied assertion. It cannot authenticate the
    reviewer, establish a license's meaning or turn bank permission into model
    permission. Training admission remains false even with complete assertions.
    """
    if (not isinstance(manifest, dict) or set(manifest) != {"formatId", "schemaVersion", "sources"}
            or manifest["formatId"] != "com.project-seam.training-permission-manifest"
            or type(manifest["schemaVersion"]) is not int or manifest["schemaVersion"] != 1
            or not isinstance(manifest["sources"], list) or not 1 <= len(manifest["sources"]) <= 10000):
        raise ValueError("Invalid training permission manifest")
    if (not isinstance(evidence, dict) or len(evidence) > 10000
            or any(not isinstance(blob, bytes) or not 1 <= len(blob) <= 4 * 1024 * 1024 for blob in evidence.values())
            or sum(len(blob) for blob in evidence.values()) > 64 * 1024 * 1024):
        raise ValueError("Permission evidence exceeds capture budget")
    seen, results = set(), []
    for row in manifest["sources"]:
        fields = {"sourceId", "sourceSha256", "identityId", "kind", "evidenceId", "evidenceSha256", "permissions", "reviewRevision"}
        if not isinstance(row, dict) or set(row) != fields:
            raise ValueError("Invalid training permission source fields")
        for key in ("sourceId", "identityId", "evidenceId", "reviewRevision"):
            text = row[key]
            if (not isinstance(text, str) or not 1 <= len(text.encode()) <= 256
                    or any(ord(c) < 32 or ord(c) == 127 for c in text)):
                raise ValueError("Invalid permission identity or review reference")
        if row["sourceId"] in seen:
            raise ValueError("Duplicate permission source identity")
        seen.add(row["sourceId"])
        if row["kind"] not in ("HUMAN_RECORDING", "PROCEDURAL_SYNTHESIS", "TTS_DERIVED"):
            raise ValueError("Invalid permission source kind")
        for key in ("sourceSha256", "evidenceSha256"):
            digest = row[key]
            if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
                raise ValueError("Invalid captured permission digest")
        scopes = row["permissions"]
        if not isinstance(scopes, dict) or set(scopes) != set(TRAINING_PERMISSIONS) or any(type(v) is not bool for v in scopes.values()):
            raise ValueError("Explicit bank and model permission booleans are required")
        blob = evidence.get(row["evidenceId"])
        if blob is None or hashlib.sha256(blob).hexdigest() != row["evidenceSha256"]:
            raise ValueError("Permission evidence is missing or differs from captured digest")
        results.append(dict(sourceId=row["sourceId"], sourceSha256=row["sourceSha256"],
                            identityId=row["identityId"], evidenceSha256=row["evidenceSha256"],
                            missingScopes=[p for p in TRAINING_PERMISSIONS if not scopes[p]]))
    return dict(formatId="com.project-seam.training-permission-report", schemaVersion=1,
                sources=results, assertionsComplete=all(not r["missingScopes"] for r in results),
                sourceBytesVerified=False, reviewAuthenticated=False, trainingAdmitted=False, releaseEligible=False)
