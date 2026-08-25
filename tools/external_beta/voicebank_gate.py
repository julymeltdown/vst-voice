from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path, PurePosixPath
from typing import Any


HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")
SEMVER = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$")
ALLOWED_RESULTS = {"NOT_RUN", "BLOCKED", "FAIL", "PASS"}

REQUIRED_GATE_IDS = (
    "rights-approval",
    "recording-session-logs",
    "source-derived-hash-inventory",
    "inventory-coverage",
    "retake-closure",
    "marker-pitch-qa",
    "renderer-listening-qa",
    "hostile-package-validation",
    "signed-package",
    "clean-install",
    "reference-song",
)

REQUIRED_PERMISSIONS = (
    "recordingSourceUse",
    "transformation",
    "redistribution",
    "endUserRenderedAudio",
)

_FORBIDDEN_PUBLIC_KEYS = {
    "privateContract",
    "privateContractPath",
    "contractPath",
    "rawRecordingsPath",
    "rawArchivePath",
    "performerEmail",
    "performerPhone",
    "performerAddress",
    "governmentId",
    "personalData",
}
_FORBIDDEN_PATH_PARTS = {
    "private",
    "contract",
    "contracts",
    "raw-recordings",
    "raw_recordings",
}


@dataclass(frozen=True, slots=True)
class BetaVoicebankGateResult:
    passed: bool
    errors: tuple[str, ...] = ()
    blocked: tuple[str, ...] = ()
    release_status: str = "BLOCKED"

    @property
    def unresolved(self) -> tuple[str, ...]:
        return self.blocked

    @property
    def blocked_targets(self) -> tuple[str, ...]:
        return self.blocked

    def as_dict(self) -> dict[str, Any]:
        return {
            "passed": self.passed,
            "releaseStatus": self.release_status,
            "errors": list(self.errors),
            "blocked": list(self.blocked),
        }


def _is_hex(value: Any) -> bool:
    return isinstance(value, str) and HEX64.fullmatch(value) is not None


def _safe_relative(value: Any) -> bool:
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    path = PurePosixPath(value)
    return not path.is_absolute() and all(part not in {"", ".", ".."} for part in path.parts)


def _parse_time(value: Any) -> datetime | None:
    if not isinstance(value, str) or not value.strip():
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None


def _is_under(root: Path, relative: Any, label: str, errors: list[str], *, directory: bool = False) -> Path | None:
    if not _safe_relative(relative):
        errors.append(f"{label} must be a safe relative POSIX path")
        return None
    candidate = root / str(relative)
    try:
        root_resolved = root.resolve(strict=True)
        if candidate.is_symlink():
            errors.append(f"{label} must not be a symbolic link")
            return None
        resolved = candidate.resolve(strict=True)
        if resolved != root_resolved and root_resolved not in resolved.parents:
            errors.append(f"{label} escapes the approved evidence root")
            return None
        if directory and not resolved.is_dir():
            errors.append(f"{label} must reference a directory")
            return None
        if not directory and not resolved.is_file():
            errors.append(f"{label} must reference a regular file")
            return None
        return resolved
    except FileNotFoundError:
        errors.append(f"{label} does not exist: {relative}")
    except OSError as exc:
        errors.append(f"{label} cannot be inspected: {exc}")
    return None


def _check_file_hash(root: Path, item: Any, label: str, errors: list[str], *, forbid_private_path: bool = False) -> None:
    if not isinstance(item, dict):
        errors.append(f"{label} must be an object")
        return
    path_text = item.get("path")
    if forbid_private_path and isinstance(path_text, str):
        parts = {part.lower() for part in PurePosixPath(path_text).parts}
        if parts & _FORBIDDEN_PATH_PARTS:
            errors.append(f"{label}.path points at private or raw content")
    if not _is_hex(item.get("sha256")):
        errors.append(f"{label}.sha256 must be a 64-character hexadecimal digest")
    path = _is_under(root, path_text, f"{label}.path", errors)
    if path is None:
        return
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != str(item.get("sha256", "")).lower():
        errors.append(f"{label}.sha256 does not match file bytes")


def _scan_for_private_keys(value: Any, location: str = "dossier") -> list[str]:
    errors: list[str] = []
    if isinstance(value, dict):
        for key, child in value.items():
            if key in _FORBIDDEN_PUBLIC_KEYS:
                errors.append(f"{location}.{key} is private material and cannot be in the public dossier")
            errors.extend(_scan_for_private_keys(child, f"{location}.{key}"))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            errors.extend(_scan_for_private_keys(child, f"{location}[{index}]"))
    return errors


def _validate_evidence(item: Any, evidence_root: Path, label: str, errors: list[str]) -> None:
    if not isinstance(item, dict):
        errors.append(f"{label} must be an object")
        return
    for key in ("kind", "reviewer", "executedAt"):
        if not isinstance(item.get(key), str) or not item[key].strip():
            errors.append(f"{label}.{key} is required")
    if item.get("result", "PASS") != "PASS":
        errors.append(f"{label}.result must be PASS")
    if isinstance(item.get("path"), str):
        parts = {part.lower() for part in PurePosixPath(item["path"]).parts}
        if parts & _FORBIDDEN_PATH_PARTS:
            errors.append(f"{label}.path points at private or raw content")
    _check_file_hash(evidence_root, item, label, errors)


def _validate_requirements(dossier: dict[str, Any], evidence_root: Path, errors: list[str], blocked: list[str]) -> None:
    requirements = dossier.get("gates")
    if not isinstance(requirements, dict):
        errors.append("gates must be an object")
        blocked.extend(REQUIRED_GATE_IDS)
        return
    for gate_id in REQUIRED_GATE_IDS:
        item = requirements.get(gate_id)
        if not isinstance(item, dict):
            errors.append(f"missing Beta Voicebank gate: {gate_id}")
            blocked.append(gate_id)
            continue
        status = item.get("status", item.get("result"))
        if status not in ALLOWED_RESULTS:
            errors.append(f"gate {gate_id} has invalid status")
            continue
        if status in {"NOT_RUN", "BLOCKED"}:
            blocked.append(gate_id)
            if item.get("evidence"):
                errors.append(f"gate {gate_id} cannot attach evidence while {status}")
            continue
        if status == "FAIL":
            errors.append(f"gate {gate_id} failed")
            continue
        evidence = item.get("evidence")
        if not isinstance(evidence, list) or not evidence:
            errors.append(f"gate {gate_id} PASS requires evidence")
            continue
        for index, record in enumerate(evidence):
            _validate_evidence(record, evidence_root, f"gates.{gate_id}.evidence[{index}]", errors)


def _validate_package(dossier: dict[str, Any], root: Path, errors: list[str]) -> None:
    package = dossier.get("package")
    if not isinstance(package, dict):
        errors.append("package is required")
        return
    for key in ("id", "version", "entryManifestSha256"):
        if not isinstance(package.get(key), str) or not package[key].strip():
            errors.append(f"package.{key} is required")
    if package.get("id") != dossier.get("voicebankId"):
        errors.append("package.id must match voicebankId")
    if package.get("version") != dossier.get("version"):
        errors.append("package.version must match dossier version")
    if not _is_hex(package.get("entryManifestSha256")):
        errors.append("package.entryManifestSha256 must be a 64-character hexadecimal digest")
    _check_file_hash(root, package.get("file"), "package.file", errors)
    if isinstance(package.get("file"), dict) and package["file"].get("sha256") != package.get("contentSha256"):
        errors.append("package.file.sha256 must match package.contentSha256")
    if not _is_hex(package.get("contentSha256")):
        errors.append("package.contentSha256 must be a 64-character hexadecimal digest")
    signature = package.get("signature")
    if not isinstance(signature, dict):
        errors.append("package.signature is required")
        return
    required = ("purpose", "delegatedKeyId", "delegatedKeyFingerprint", "algorithm", "signedAt", "trustEpoch", "signatureSha256")
    for key in required:
        if key not in signature:
            errors.append(f"package.signature.{key} is required")
    if signature.get("purpose") != "BANK_PACKAGE":
        errors.append("package signature purpose must be BANK_PACKAGE")
    if not _is_hex(signature.get("delegatedKeyFingerprint")):
        errors.append("package.signature.delegatedKeyFingerprint is invalid")
    if not _is_hex(signature.get("signatureSha256")):
        errors.append("package.signature.signatureSha256 is invalid")
    if not isinstance(signature.get("trustEpoch"), int) or isinstance(signature.get("trustEpoch"), bool) or signature.get("trustEpoch", 0) < 1:
        errors.append("package.signature.trustEpoch must be a positive integer")
    signed_at = _parse_time(signature.get("signedAt"))
    if signed_at is None:
        errors.append("package.signature.signedAt must be an ISO-8601 timestamp")


def _validate_trust(dossier: dict[str, Any], errors: list[str]) -> None:
    trust = dossier.get("trust")
    package = dossier.get("package")
    if not isinstance(trust, dict):
        errors.append("trust policy binding is required")
        return
    required = ("rootPolicyVersion", "rootKeyId", "rootKeyFingerprint", "epoch", "validFrom", "validUntil", "compromiseCutoff", "delegatedKeyId", "delegatedKeyFingerprint", "delegatedKeyPurpose", "rootSignedDelegation", "rootSignedDelegationSha256", "delegatedKeyRevoked", "revalidation")
    for key in required:
        if key not in trust:
            errors.append(f"trust.{key} is required")
    if not isinstance(trust.get("rootPolicyVersion"), str) or not trust.get("rootPolicyVersion"):
        errors.append("trust.rootPolicyVersion is required")
    for key in ("rootKeyFingerprint", "delegatedKeyFingerprint", "rootSignedDelegationSha256"):
        if not _is_hex(trust.get(key)):
            errors.append(f"trust.{key} must be a 64-character hexadecimal digest")
    epoch = trust.get("epoch")
    if not isinstance(epoch, int) or isinstance(epoch, bool) or epoch < 1:
        errors.append("trust.epoch must be a positive integer")
    if trust.get("delegatedKeyPurpose") != "BANK_PACKAGE":
        errors.append("trust.delegatedKeyPurpose must be BANK_PACKAGE")
    if trust.get("rootSignedDelegation") is not True:
        errors.append("delegated key rotation must be authorized by the offline root")
    if trust.get("delegatedKeyRevoked") is not False:
        errors.append("delegated bank key is revoked")
    valid_from = _parse_time(trust.get("validFrom"))
    valid_until = _parse_time(trust.get("validUntil"))
    cutoff = _parse_time(trust.get("compromiseCutoff"))
    if None in (valid_from, valid_until, cutoff):
        errors.append("trust validity and compromiseCutoff must be ISO-8601 timestamps")
    elif valid_from >= valid_until:
        errors.append("trust.validFrom must precede trust.validUntil")
    signature = package.get("signature") if isinstance(package, dict) else None
    if isinstance(signature, dict):
        if signature.get("delegatedKeyId") != trust.get("delegatedKeyId"):
            errors.append("package signature delegatedKeyId does not match trust policy")
        if signature.get("delegatedKeyFingerprint") != trust.get("delegatedKeyFingerprint"):
            errors.append("package signature fingerprint does not match trust policy")
        if signature.get("trustEpoch") != epoch:
            errors.append("package signature trust epoch is stale or replayed")
        signed_at = _parse_time(signature.get("signedAt"))
        if signed_at is not None and valid_from is not None and valid_until is not None and not (valid_from <= signed_at <= valid_until):
            errors.append("package signature is outside the delegated-key validity window")
        if signed_at is not None and cutoff is not None and signed_at <= cutoff:
            errors.append("package signature predates the delegated-key compromise cutoff")
    revalidation = trust.get("revalidation")
    if not isinstance(revalidation, dict):
        errors.append("trust.revalidation is required")
        return
    if revalidation.get("result") != "PASS":
        errors.append("installed provenance revalidation must be PASS")
    if revalidation.get("trustEpoch") != epoch:
        errors.append("revalidation trust epoch is stale or replayed")
    if revalidation.get("installedTreeLinkCount") != 0:
        errors.append("installed provenance tree must contain zero links")
    if not _is_hex(revalidation.get("signedEntryManifestSha256")):
        errors.append("revalidation signedEntryManifestSha256 is required")
    if not _is_hex(revalidation.get("installedProvenanceTreeSha256")):
        errors.append("revalidation installedProvenanceTreeSha256 is required")
    package_manifest = package.get("entryManifestSha256") if isinstance(package, dict) else None
    if revalidation.get("signedEntryManifestSha256") != package_manifest:
        errors.append("revalidation manifest digest does not match package manifest")
    if revalidation.get("installedProvenanceTreeSha256") != dossier.get("installedProvenanceTreeSha256"):
        errors.append("installed provenance digest is not bound to the dossier")


def _validate_identity_and_inventory(dossier: dict[str, Any], errors: list[str]) -> None:
    if dossier.get("schemaVersion") != 1:
        errors.append("Beta Voicebank dossier schemaVersion must be 1")
    if dossier.get("component") != "beta-voicebank":
        errors.append("component must be beta-voicebank")
    voicebank_id = dossier.get("voicebankId")
    if not isinstance(voicebank_id, str) or not voicebank_id or voicebank_id == "official.voice.01" or not voicebank_id.startswith("beta."):
        errors.append("voicebankId must identify a non-official beta bank")
    if not isinstance(dossier.get("version"), str) or SEMVER.fullmatch(dossier["version"]) is None:
        errors.append("version must be a semantic version")
    if dossier.get("official") is not False:
        errors.append("official must be false for the External Beta bank")
    if dossier.get("characterAssociated") is not False or dossier.get("characterId") not in (None, ""):
        errors.append("character-associated content is not accepted by the Beta bank gate")
    inventory = dossier.get("inventory")
    if not isinstance(inventory, dict):
        errors.append("inventory is required")
        return
    for key in ("language", "profileId", "supportedStyles", "requiredUnits", "pitchLayers", "supportedRange", "coverageResult"):
        if key not in inventory:
            errors.append(f"inventory.{key} is required")
    if inventory.get("language") != "ja":
        errors.append("Beta Voicebank inventory language must be ja")
    if not isinstance(inventory.get("supportedStyles"), list) or not inventory.get("supportedStyles"):
        errors.append("inventory.supportedStyles must be non-empty")
    if not isinstance(inventory.get("requiredUnits"), list) or not inventory.get("requiredUnits"):
        errors.append("inventory.requiredUnits must be non-empty")
    layers = inventory.get("pitchLayers")
    if not isinstance(layers, list) or len(layers) not in {2, 3} or any(not isinstance(item, int) for item in layers):
        errors.append("inventory.pitchLayers must contain two or three integer layers")
    pitch_range = inventory.get("supportedRange")
    if not isinstance(pitch_range, dict) or not isinstance(pitch_range.get("minMidi"), int) or not isinstance(pitch_range.get("maxMidi"), int) or pitch_range["minMidi"] >= pitch_range["maxMidi"]:
        errors.append("inventory.supportedRange must define an increasing MIDI range")
    if inventory.get("coverageResult") != "PASS":
        errors.append("inventory coverage must be PASS")


def _validate_rights(dossier: dict[str, Any], errors: list[str]) -> None:
    rights = dossier.get("rights")
    if not isinstance(rights, dict):
        errors.append("rights approval is required")
        return
    approval = rights.get("approval")
    if not isinstance(approval, dict):
        errors.append("rights.approval is required")
    else:
        for key in ("reviewer", "reviewedAt", "scope", "territory", "status", "redactedApprovalSha256"):
            if key not in approval:
                errors.append(f"rights.approval.{key} is required")
        if approval.get("status") != "APPROVED":
            errors.append("rights approval must be APPROVED")
        if not isinstance(approval.get("scope"), list) or not approval.get("scope"):
            errors.append("rights approval scope must be non-empty")
        if not isinstance(approval.get("territory"), list) or not approval.get("territory"):
            errors.append("rights approval territory must be non-empty")
        if not _is_hex(approval.get("redactedApprovalSha256")):
            errors.append("rights approval must reference a redacted approval hash")
    permissions = rights.get("permissions")
    if not isinstance(permissions, dict):
        errors.append("rights.permissions is required")
    else:
        for key in REQUIRED_PERMISSIONS:
            if permissions.get(key) is not True:
                errors.append(f"rights permission {key} must be explicitly true")
    if rights.get("providerIdentityDisclosure") not in {"DISCLOSED", "WITHHELD"}:
        errors.append("providerIdentityDisclosure must distinguish identity disclosure from rights validity")
    license_summary = rights.get("publicLicenseSummary")
    if not isinstance(license_summary, dict) or license_summary.get("status") != "APPROVED":
        errors.append("rights.publicLicenseSummary must be APPROVED")


def _validate_assets(dossier: dict[str, Any], root: Path, errors: list[str]) -> None:
    for key in ("sourceAssets", "derivedAssets"):
        assets = dossier.get(key)
        if not isinstance(assets, list) or not assets:
            errors.append(f"{key} must contain at least one hashed asset")
            continue
        for index, asset in enumerate(assets):
            _check_file_hash(root, asset, f"{key}[{index}]", errors, forbid_private_path=False)


def _validate_reference_song(dossier: dict[str, Any], evidence_root: Path, errors: list[str]) -> None:
    song = dossier.get("referenceSong")
    if not isinstance(song, dict):
        errors.append("referenceSong receipt is required")
        return
    for key in ("projectSha256", "mediaSha256"):
        if not _is_hex(song.get(key)):
            errors.append(f"referenceSong.{key} must be a 64-character hexadecimal digest")
    receipt = song.get("renderReceipt")
    _validate_evidence(receipt, evidence_root, "referenceSong.renderReceipt", errors)


def evaluate_beta_voicebank_dossier(dossier: dict[str, Any], root: Path) -> BetaVoicebankGateResult:
    root = Path(root)
    errors: list[str] = []
    blocked: list[str] = []
    if not isinstance(dossier, dict):
        return BetaVoicebankGateResult(False, ("dossier must be an object",), (), "FAIL")
    if dossier.get("status") not in {"BLOCKED", "FAIL", "ACCEPTED"}:
        errors.append("dossier.status must be BLOCKED, FAIL, or ACCEPTED")
    errors.extend(_scan_for_private_keys(dossier))
    _validate_identity_and_inventory(dossier, errors)
    evidence_root_value = dossier.get("evidenceRoot")
    evidence_root = _is_under(root, evidence_root_value, "evidenceRoot", errors, directory=True)
    if evidence_root is None:
        blocked.append("evidence-root")
        evidence_root = root
    _validate_package(dossier, root, errors)
    _validate_trust(dossier, errors)
    _validate_rights(dossier, errors)
    _validate_assets(dossier, root, errors)
    if evidence_root_value is not None:
        _validate_reference_song(dossier, evidence_root, errors)
        _validate_requirements(dossier, evidence_root, errors, blocked)
    else:
        errors.append("evidenceRoot is required")
        blocked.append("evidence-root")
    passed = not errors and not blocked
    if passed:
        status = "ACCEPTED"
        if dossier.get("status") != "ACCEPTED":
            errors.append("dossier.status must be ACCEPTED when every Beta gate passes")
            passed = False
            status = "FAIL"
    elif blocked or any("NOT_RUN" in error or "BLOCKED" in error for error in errors):
        status = "BLOCKED"
    else:
        status = "FAIL"
    return BetaVoicebankGateResult(passed, tuple(errors), tuple(sorted(set(blocked))), status)


def evaluate_voicebank_dossier(dossier: dict[str, Any], root: Path) -> BetaVoicebankGateResult:
    return evaluate_beta_voicebank_dossier(dossier, root)


def main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(description="Fail-closed Project SEAM External Beta Voicebank gate")
    parser.add_argument("--dossier", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-blocked", action="store_true")
    args = parser.parse_args(argv)
    try:
        dossier = json.loads(args.dossier.read_text(encoding="utf-8"))
        result = evaluate_beta_voicebank_dossier(dossier, args.root)
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as exc:
        payload = {"passed": False, "releaseStatus": "FAIL", "errors": [str(exc)], "blocked": []}
        print(json.dumps(payload, sort_keys=True))
        return 0 if args.expect_blocked else 2
    payload = result.as_dict()
    text = json.dumps(payload, ensure_ascii=False, sort_keys=True) + "\n"
    print(text, end="")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    if args.expect_blocked:
        return 0 if not result.passed else 4
    return 0 if result.passed else 3


if __name__ == "__main__":
    raise SystemExit(main())
