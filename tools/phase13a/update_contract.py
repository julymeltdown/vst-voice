#!/usr/bin/env python3
from __future__ import annotations

import base64
import binascii
import datetime as _datetime
import hashlib
import json
import os
import re
import secrets
import shutil
import stat
from pathlib import Path
from typing import Any, Mapping


SCHEMA_VERSION = 1
HANDOFF_SCHEMA_VERSION = 2
HEX64 = re.compile(r"^[0-9a-f]{64}$")
KEY_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$")
SAFE_FILENAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
ALGORITHMS = {"Ed25519"}
UPDATE_PURPOSE = "update"
RECOVERY_PURPOSE = "update-recovery"
CHANNEL = "external-beta"
PLATFORMS = {"macos-arm64", "windows-x64", "linux-x64"}
RANGE_FIELDS = {"project", "media", "bank", "settings", "autosave", "clap-state", "host-state"}


def canonical_json(value: Mapping[str, Any], *, include_signature: bool = False) -> bytes:
    payload = dict(value)
    if not include_signature:
        payload.pop("signature", None)
    return json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _decode_b64(value: Any, label: str, *, expected_size: int | None = None) -> tuple[bytes | None, str | None]:
    if not isinstance(value, str) or not value:
        return None, f"{label} must be a non-empty base64 string"
    try:
        decoded = base64.b64decode(value.encode("ascii"), validate=True)
    except (UnicodeEncodeError, binascii.Error):
        return None, f"{label} is not valid base64"
    if expected_size is not None and len(decoded) != expected_size:
        return None, f"{label} must decode to {expected_size} bytes"
    return decoded, None


def _parse_time(value: Any, label: str) -> tuple[_datetime.datetime | None, str | None]:
    if not isinstance(value, str) or not value:
        return None, f"{label} must be an ISO-8601 timestamp"
    try:
        parsed = _datetime.datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None, f"{label} must be an ISO-8601 timestamp"
    if parsed.tzinfo is None:
        return None, f"{label} must include a timezone"
    return parsed.astimezone(_datetime.timezone.utc), None


def _now() -> _datetime.datetime:
    return _datetime.datetime.now(_datetime.timezone.utc)


def _semver(value: Any) -> tuple[int, int, int, tuple[str, ...]] | None:
    if not isinstance(value, str):
        return None
    match = re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z.-]+))?", value)
    if not match:
        return None
    return int(match.group(1)), int(match.group(2)), int(match.group(3)), tuple(match.group(4).split(".")) if match.group(4) else ()


def _is_relative_to(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def _path_has_symlink(path: Path, root: Path) -> bool:
    current = path
    while current != root and current != current.parent:
        if current.is_symlink():
            return True
        current = current.parent
    return current.is_symlink()


def _safe_private_root(path: Path) -> Path:
    resolved = Path(path).expanduser().resolve()
    if resolved == Path(resolved.anchor):
        raise ValueError("private staging root cannot be a filesystem root")
    if resolved.exists() and resolved.is_symlink():
        raise ValueError("private staging root cannot be a symbolic link")
    resolved.mkdir(parents=True, exist_ok=True)
    if resolved.is_symlink():
        raise ValueError("private staging root became a symbolic link")
    try:
        resolved.chmod(stat.S_IRWXU)
    except OSError:
        pass
    return resolved


def _required_string(value: Mapping[str, Any], key: str, errors: list[str], prefix: str = "") -> str | None:
    candidate = value.get(key)
    if not isinstance(candidate, str) or not candidate.strip():
        errors.append(f"{prefix}{key} must be a non-empty string")
        return None
    return candidate


def _validate_signature_shape(value: Any, errors: list[str], prefix: str = "signature") -> None:
    if not isinstance(value, dict):
        errors.append(f"{prefix} must be an object")
        return
    if value.get("algorithm") not in ALGORITHMS:
        errors.append(f"{prefix}.algorithm must be Ed25519")
    key_id = value.get("keyId")
    if not isinstance(key_id, str) or not KEY_ID.fullmatch(key_id):
        errors.append(f"{prefix}.keyId is invalid")
    declared_hash = value.get("payloadSha256")
    if not isinstance(declared_hash, str) or not HEX64.fullmatch(declared_hash):
        errors.append(f"{prefix}.payloadSha256 must be a lowercase SHA-256")
    decoded, error = _decode_b64(value.get("value"), f"{prefix}.value", expected_size=64)
    if error or decoded is None:
        errors.append(error or f"{prefix}.value is invalid")


def _signature_errors(document: Mapping[str, Any], signature: Mapping[str, Any], public_key: bytes) -> list[str]:
    errors: list[str] = []
    payload = canonical_json(document)
    if signature.get("payloadSha256") != sha256_bytes(payload):
        errors.append("signature.payloadSha256 does not match canonical payload")
    decoded, error = _decode_b64(signature.get("value"), "signature.value", expected_size=64)
    if error:
        errors.append(error)
    elif decoded is not None and not ed25519_verify(decoded, payload, public_key):
        errors.append("signature does not verify against the trusted public key")
    return errors


def validate_trust_policy(policy: Mapping[str, Any], *, now: _datetime.datetime | None = None) -> list[str]:
    errors: list[str] = []
    if not isinstance(policy, Mapping):
        return ["trust policy must be an object"]
    if policy.get("schemaVersion") != SCHEMA_VERSION:
        errors.append(f"schemaVersion must equal {SCHEMA_VERSION}")
    if policy.get("purpose") != "update-trust-policy":
        errors.append("purpose must be update-trust-policy")
    if policy.get("channel") != CHANNEL:
        errors.append(f"channel must equal {CHANNEL}")
    epoch = policy.get("policyEpoch")
    if not isinstance(epoch, int) or isinstance(epoch, bool) or epoch < 0:
        errors.append("policyEpoch must be a non-negative integer")
    root_key_id = policy.get("rootKeyId")
    if not isinstance(root_key_id, str) or not KEY_ID.fullmatch(root_key_id):
        errors.append("rootKeyId is invalid")
    root_key, key_error = _decode_b64(policy.get("rootPublicKey"), "rootPublicKey", expected_size=32)
    if key_error:
        errors.append(key_error)
    allowed = policy.get("allowedPlatforms")
    if allowed != sorted(allowed or []):
        errors.append("allowedPlatforms must be sorted for canonical policy identity")
    if not isinstance(allowed, list) or not allowed or any(platform not in PLATFORMS for platform in allowed):
        errors.append("allowedPlatforms must contain only supported platform identifiers")
    issued, issued_error = _parse_time(policy.get("issuedAt"), "issuedAt")
    not_before, before_error = _parse_time(policy.get("notBefore"), "notBefore")
    expires, expires_error = _parse_time(policy.get("expiresAt"), "expiresAt")
    cutoff, cutoff_error = _parse_time(policy.get("compromiseCutoff"), "compromiseCutoff")
    for error in (issued_error, before_error, expires_error, cutoff_error):
        if error:
            errors.append(error)
    if issued and not_before and issued > not_before:
        errors.append("issuedAt cannot be later than notBefore")
    if not_before and expires and not_before >= expires:
        errors.append("notBefore must be earlier than expiresAt")
    if expires and cutoff and cutoff > expires:
        errors.append("compromiseCutoff cannot be later than expiresAt")
    delegated = policy.get("delegatedKeys")
    if not isinstance(delegated, list) or not delegated:
        errors.append("delegatedKeys must be a non-empty array")
        delegated = []
    seen: set[str] = set()
    for index, item in enumerate(delegated):
        prefix = f"delegatedKeys[{index}]"
        if not isinstance(item, Mapping):
            errors.append(f"{prefix} must be an object")
            continue
        key_id = item.get("keyId")
        if not isinstance(key_id, str) or not KEY_ID.fullmatch(key_id):
            errors.append(f"{prefix}.keyId is invalid")
        elif key_id in seen or key_id == root_key_id:
            errors.append(f"{prefix}.keyId is duplicated or collides with rootKeyId")
        else:
            seen.add(key_id)
        if item.get("purpose") not in {UPDATE_PURPOSE, RECOVERY_PURPOSE}:
            errors.append(f"{prefix}.purpose is not an allowed update purpose")
        if item.get("algorithm") not in ALGORITHMS:
            errors.append(f"{prefix}.algorithm must be Ed25519")
        _, key_error = _decode_b64(item.get("publicKey"), f"{prefix}.publicKey", expected_size=32)
        if key_error:
            errors.append(key_error)
        start, start_error = _parse_time(item.get("notBefore"), f"{prefix}.notBefore")
        end, end_error = _parse_time(item.get("expiresAt"), f"{prefix}.expiresAt")
        for error in (start_error, end_error):
            if error:
                errors.append(error)
        if start and end and start >= end:
            errors.append(f"{prefix}.notBefore must be earlier than expiresAt")
        if item.get("revokedAt") is not None:
            revoked, revoked_error = _parse_time(item.get("revokedAt"), f"{prefix}.revokedAt")
            if revoked_error:
                errors.append(revoked_error)
            elif start and revoked and revoked < start:
                errors.append(f"{prefix}.revokedAt cannot precede notBefore")
    _validate_signature_shape(policy.get("signature"), errors)
    if root_key is None:
        errors.append("rootPublicKey is unavailable for policy verification")
    current = now or _now()
    if not_before and current < not_before:
        errors.append("trust policy is not yet valid")
    if expires and current >= expires:
        errors.append("trust policy is expired")
    return errors


def verify_trust_policy(policy: Mapping[str, Any], trusted_roots: Mapping[str, bytes | str], *, now: _datetime.datetime | None = None) -> list[str]:
    errors = validate_trust_policy(policy, now=now)
    key_id = policy.get("rootKeyId")
    trusted = trusted_roots.get(key_id) if isinstance(key_id, str) else None
    if isinstance(trusted, str):
        trusted, decode_error = _decode_b64(trusted, "trusted root key", expected_size=32)
        if decode_error:
            errors.append(decode_error)
    if not isinstance(trusted, bytes) or len(trusted) != 32:
        errors.append("rootKeyId is not present in the offline trusted-root store")
    else:
        declared, _ = _decode_b64(policy.get("rootPublicKey"), "rootPublicKey", expected_size=32)
        if declared != trusted:
            errors.append("policy root key differs from the offline trusted-root store")
        if policy.get("signature", {}).get("keyId") != key_id:
            errors.append("trust policy must be signed by its declared root key")
        errors.extend(_signature_errors(policy, policy.get("signature", {}), trusted))
    return errors


def _delegated_key(policy: Mapping[str, Any], key_id: str, purpose: str, now: _datetime.datetime) -> bytes | None:
    for item in policy.get("delegatedKeys", []):
        if not isinstance(item, Mapping) or item.get("keyId") != key_id or item.get("purpose") != purpose:
            continue
        start, _ = _parse_time(item.get("notBefore"), "delegated.notBefore")
        end, _ = _parse_time(item.get("expiresAt"), "delegated.expiresAt")
        revoked, _ = _parse_time(item.get("revokedAt"), "delegated.revokedAt") if item.get("revokedAt") else (None, None)
        if (start and now < start) or (end and now >= end) or (revoked and now >= revoked):
            return None
        key, _ = _decode_b64(item.get("publicKey"), "delegated.publicKey", expected_size=32)
        return key
    return None


def _validate_ranges(value: Any, label: str, errors: list[str]) -> None:
    if not isinstance(value, Mapping):
        errors.append(f"{label} must be an object")
        return
    unknown = set(value) - RANGE_FIELDS
    if unknown:
        errors.append(f"{label} contains unknown fields: {', '.join(sorted(unknown))}")
    for name, range_value in value.items():
        if not isinstance(name, str) or not isinstance(range_value, Mapping):
            errors.append(f"{label}.{name} must be an object")
            continue
        for field in ("min", "max"):
            number = range_value.get(field)
            if not isinstance(number, int) or isinstance(number, bool) or number < 0:
                errors.append(f"{label}.{name}.{field} must be a non-negative integer")
        minimum = range_value.get("min")
        maximum = range_value.get("max")
        if (
            isinstance(minimum, int)
            and not isinstance(minimum, bool)
            and isinstance(maximum, int)
            and not isinstance(maximum, bool)
            and minimum > maximum
        ):
            errors.append(f"{label}.{name}.min cannot exceed max")


def validate_update_manifest(manifest: Mapping[str, Any], policy: Mapping[str, Any], *, installed_version: str | None = None, now: _datetime.datetime | None = None) -> list[str]:
    errors: list[str] = []
    if not isinstance(manifest, Mapping):
        return ["update manifest must be an object"]
    if manifest.get("schemaVersion") != SCHEMA_VERSION:
        errors.append(f"schemaVersion must equal {SCHEMA_VERSION}")
    if manifest.get("purpose") != "update-manifest":
        errors.append("purpose must be update-manifest")
    if manifest.get("channel") != CHANNEL:
        errors.append(f"channel must equal {CHANNEL}")
    if manifest.get("platform") not in PLATFORMS:
        errors.append("platform is not supported")
    for field in ("manifestId", "targetBuild", "targetVersion", "minimumVersion"):
        _required_string(manifest, field, errors)
    for field in ("releaseNotesSha256",):
        _required_string(manifest, field, errors)
        if isinstance(manifest.get(field), str) and not HEX64.fullmatch(manifest[field]):
            errors.append(f"{field} must be a lowercase SHA-256")
    manifest_epoch = manifest.get("manifestEpoch")
    policy_epoch = policy.get("policyEpoch") if isinstance(policy, Mapping) else None
    if not isinstance(manifest_epoch, int) or isinstance(manifest_epoch, bool) or manifest_epoch < 0:
        errors.append("manifestEpoch must be a non-negative integer")
    elif isinstance(policy_epoch, int) and manifest_epoch < policy_epoch:
        errors.append("manifestEpoch cannot precede policyEpoch")
    package = manifest.get("package")
    if not isinstance(package, Mapping):
        errors.append("package must be an object")
        package = {}
    filename = package.get("fileName")
    if not isinstance(filename, str) or not SAFE_FILENAME.fullmatch(filename) or filename in {".", ".."}:
        errors.append("package.fileName must be a portable basename")
    url = package.get("url")
    if not isinstance(url, str) or not re.fullmatch(r"https://[^\s]+", url):
        errors.append("package.url must be an HTTPS URL")
    size = package.get("size")
    if not isinstance(size, int) or isinstance(size, bool) or size <= 0 or size > 8 * 1024 * 1024 * 1024:
        errors.append("package.size must be between 1 byte and 8 GiB")
    digest = package.get("sha256")
    if not isinstance(digest, str) or not HEX64.fullmatch(digest):
        errors.append("package.sha256 must be a lowercase SHA-256")
    _validate_ranges(manifest.get("readRanges"), "readRanges", errors)
    _validate_ranges(manifest.get("writeRanges"), "writeRanges", errors)
    if manifest.get("downgradePolicy") != "REJECT":
        errors.append("downgradePolicy must be REJECT")
    current_version = installed_version or manifest.get("minimumVersion")
    target_version = _semver(manifest.get("targetVersion"))
    if target_version is None:
        errors.append("targetVersion must use strict semantic versioning")
    if current_version is not None and target_version is not None:
        current_parsed = _semver(current_version)
        if current_parsed is None:
            errors.append("installed/minimum version must use strict semantic versioning")
        elif target_version <= current_parsed:
            errors.append("normal downgrade or same-version update is rejected")
    signature = manifest.get("signature")
    _validate_signature_shape(signature, errors)
    key_id = signature.get("keyId") if isinstance(signature, Mapping) else None
    current = now or _now()
    if isinstance(key_id, str) and _delegated_key(policy, key_id, UPDATE_PURPOSE, current) is None:
        errors.append("manifest signer is not an active delegated update key")
    recovery = manifest.get("recoveryAuthorization")
    if recovery is not None:
        if not isinstance(recovery, Mapping):
            errors.append("recoveryAuthorization must be an object")
        else:
            if recovery.get("purpose") != RECOVERY_PURPOSE:
                errors.append("recoveryAuthorization.purpose must be update-recovery")
            _validate_signature_shape(recovery.get("signature"), errors, "recoveryAuthorization.signature")
            recovery_signature = recovery.get("signature")
            recovery_key_id = recovery_signature.get("keyId") if isinstance(recovery_signature, Mapping) else None
            if isinstance(recovery_key_id, str) and _delegated_key(policy, recovery_key_id, RECOVERY_PURPOSE, current) is None:
                errors.append("recovery authorization signer is not an active delegated recovery key")
    issued, issued_error = _parse_time(manifest.get("issuedAt"), "issuedAt")
    expires, expires_error = _parse_time(manifest.get("expiresAt"), "expiresAt")
    for error in (issued_error, expires_error):
        if error:
            errors.append(error)
    if issued and expires and issued >= expires:
        errors.append("manifest issuedAt must precede expiresAt")
    if expires and current >= expires:
        errors.append("update manifest is expired")
    return errors


def verify_update_manifest(manifest: Mapping[str, Any], policy: Mapping[str, Any], *, installed_version: str | None = None, now: _datetime.datetime | None = None, trusted_policy_roots: Mapping[str, bytes | str] | None = None) -> list[str]:
    errors = validate_update_manifest(manifest, policy, installed_version=installed_version, now=now)
    if trusted_policy_roots is not None:
        errors.extend(verify_trust_policy(policy, trusted_policy_roots, now=now))
    current = now or _now()
    signature = manifest.get("signature", {})
    key_id = signature.get("keyId") if isinstance(signature, Mapping) else None
    public_key = _delegated_key(policy, key_id, UPDATE_PURPOSE, current) if isinstance(key_id, str) else None
    if public_key is None:
        errors.append("manifest signature key is not available")
    else:
        errors.extend(_signature_errors(manifest, signature, public_key))
    return errors


def manifest_identity(manifest: Mapping[str, Any]) -> str:
    return sha256_bytes(canonical_json(manifest, include_signature=True))


def accept_manifest(manifest: Mapping[str, Any], state: Mapping[str, Any] | None) -> tuple[dict[str, Any] | None, list[str]]:
    previous = dict(state or {})
    epoch = manifest.get("manifestEpoch")
    if not isinstance(epoch, int):
        return None, ["manifestEpoch must be an integer"]
    highest = previous.get("highestManifestEpoch", -1)
    if not isinstance(highest, int):
        return None, ["stored highestManifestEpoch is invalid"]
    if epoch <= highest:
        return None, ["manifest epoch is stale or replayed"]
    return {
        "schemaVersion": SCHEMA_VERSION,
        "highestManifestEpoch": epoch,
        "acceptedManifestId": str(manifest.get("manifestId", "")),
        "acceptedTargetBuild": str(manifest.get("targetBuild", "")),
        "acceptedManifestSha256": manifest_identity(manifest),
    }, []


def _copy_nofollow(source: Path, destination: Path) -> None:
    if source.is_symlink() or destination.exists() or destination.is_symlink():
        raise ValueError("package source/destination cannot be a link or pre-existing path")
    nofollow = getattr(os, "O_NOFOLLOW", 0)
    source_fd = os.open(source, os.O_RDONLY | nofollow)
    destination_fd: int | None = None
    try:
        source_stat = os.fstat(source_fd)
        if not stat.S_ISREG(source_stat.st_mode):
            raise ValueError("package source is not a regular file")
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination_fd = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL | nofollow, stat.S_IRUSR | stat.S_IWUSR)
        with os.fdopen(source_fd, "rb", closefd=False) as input_stream, os.fdopen(destination_fd, "wb", closefd=False) as output_stream:
            shutil.copyfileobj(input_stream, output_stream, length=1024 * 1024)
            output_stream.flush()
            os.fsync(destination_fd)
    finally:
        if destination_fd is not None:
            os.close(destination_fd)
        os.close(source_fd)


def stage_verified_package(package: Path, manifest: Mapping[str, Any], staging_root: Path) -> dict[str, Any]:
    package = Path(package)
    package_meta = manifest.get("package", {})
    if package.is_symlink() or not package.is_file():
        raise ValueError("package must be a regular file and cannot be a symbolic link")
    if package.stat().st_size != package_meta.get("size"):
        raise ValueError("package size does not match signed manifest")
    actual_hash = sha256_file(package)
    if actual_hash != package_meta.get("sha256"):
        raise ValueError("package hash does not match signed manifest")
    filename = package_meta.get("fileName")
    if not isinstance(filename, str) or not SAFE_FILENAME.fullmatch(filename):
        raise ValueError("signed package filename is not portable")
    root = _safe_private_root(staging_root)
    candidate_root = root / ("candidate-" + secrets.token_hex(16))
    candidate_root.mkdir(mode=stat.S_IRWXU)
    destination = candidate_root / filename
    _copy_nofollow(package, destination)
    copied_stat = destination.stat()
    handoff = {
        "schemaVersion": HANDOFF_SCHEMA_VERSION,
        "purpose": "sealed-installer-handoff",
        "candidateId": candidate_root.name,
        "platform": manifest.get("platform"),
        "publisherKeyId": manifest.get("signature", {}).get("keyId"),
        "manifestSha256": manifest_identity(manifest),
        "package": {
            "fileName": filename,
            "relativePath": destination.relative_to(root).as_posix(),
            "size": copied_stat.st_size,
            "sha256": sha256_file(destination),
            "device": getattr(copied_stat, "st_dev", 0),
            "inode": getattr(copied_stat, "st_ino", 0),
        },
        "requiresExplicitUserAction": True,
        "requiresInstallerRevalidation": True,
        "createdAt": manifest.get("issuedAt"),
        "expiresAt": manifest.get("expiresAt"),
    }
    handoff_path = candidate_root / "handoff.json"
    handoff_path.write_bytes(canonical_json(handoff, include_signature=True) + b"\n")
    handoff_path.chmod(stat.S_IRUSR | stat.S_IWUSR)
    return handoff


def verify_sealed_handoff(handoff: Mapping[str, Any], manifest: Mapping[str, Any], staging_root: Path) -> list[str]:
    errors: list[str] = []
    if handoff.get("schemaVersion") != HANDOFF_SCHEMA_VERSION or handoff.get("purpose") != "sealed-installer-handoff":
        errors.append("handoff schema or purpose is invalid")
    if not handoff.get("requiresExplicitUserAction") or not handoff.get("requiresInstallerRevalidation"):
        errors.append("handoff must require explicit user action and installer revalidation")
    if handoff.get("manifestSha256") != manifest_identity(manifest):
        errors.append("handoff is bound to a different manifest")
    if handoff.get("platform") != manifest.get("platform"):
        errors.append("handoff platform differs from signed manifest")
    if handoff.get("publisherKeyId") != manifest.get("signature", {}).get("keyId"):
        errors.append("handoff publisher differs from signed manifest")
    if handoff.get("createdAt") != manifest.get("issuedAt") or handoff.get("expiresAt") != manifest.get("expiresAt"):
        errors.append("handoff freshness window differs from signed manifest")
    package = handoff.get("package")
    if not isinstance(package, Mapping):
        return errors + ["handoff.package must be an object"]
    relative = package.get("relativePath")
    if not isinstance(relative, str) or Path(relative).is_absolute() or ".." in Path(relative).parts:
        errors.append("handoff package path must remain relative to staging root")
        return errors
    root = Path(staging_root).expanduser().resolve()
    candidate = (root / relative).resolve()
    if not _is_relative_to(candidate, root) or _path_has_symlink(candidate, root) or candidate.is_symlink():
        errors.append("handoff package path escapes private staging root or contains a link")
        return errors
    if not candidate.is_file():
        errors.append("handoff package no longer exists")
        return errors
    expected = manifest.get("package", {})
    if candidate.name != expected.get("fileName") or package.get("fileName") != expected.get("fileName"):
        errors.append("handoff package filename differs from signed manifest")
    if candidate.stat().st_size != expected.get("size") or package.get("size") != expected.get("size"):
        errors.append("handoff package size differs from signed manifest")
    actual_hash = sha256_file(candidate)
    if actual_hash != expected.get("sha256") or package.get("sha256") != actual_hash:
        errors.append("handoff package hash differs from signed manifest")
    stat_result = candidate.stat()
    if package.get("device") not in {None, 0, getattr(stat_result, "st_dev", 0)}:
        errors.append("handoff package device identity changed")
    if package.get("inode") not in {None, 0, getattr(stat_result, "st_ino", 0)}:
        errors.append("handoff package inode identity changed")
    return errors


_Q = 2**255 - 19
_L = 2**252 + 27742317777372353535851937790883648493
_I = pow(2, (_Q - 1) // 4, _Q)
_D = (-121665 * pow(121666, _Q - 2, _Q)) % _Q
_B = (15112221349535400772501151409588531511454012693041857206046113283949847762202, 46316835694926478169428394003475163141307993866256225615783033603165251855960)


def _xrecover(y: int) -> int:
    xx = (y * y - 1) * pow(_D * y * y + 1, _Q - 2, _Q)
    x = pow(xx, (_Q + 3) // 8, _Q)
    if (x * x - xx) % _Q:
        x = (x * _I) % _Q
    if x & 1:
        x = _Q - x
    return x


def _edwards_add(point_a: tuple[int, int], point_b: tuple[int, int]) -> tuple[int, int]:
    x1, y1 = point_a
    x2, y2 = point_b
    denominator_x = pow(1 + _D * x1 * x2 * y1 * y2, _Q - 2, _Q)
    denominator_y = pow(1 - _D * x1 * x2 * y1 * y2, _Q - 2, _Q)
    return (((x1 * y2 + x2 * y1) * denominator_x) % _Q, ((y1 * y2 + x1 * x2) * denominator_y) % _Q)


def _scalar_mult(point: tuple[int, int], scalar: int) -> tuple[int, int]:
    result = (0, 1)
    addend = point
    while scalar:
        if scalar & 1:
            result = _edwards_add(result, addend)
        addend = _edwards_add(addend, addend)
        scalar >>= 1
    return result


def _encode_point(point: tuple[int, int]) -> bytes:
    x, y = point
    encoded = bytearray(y.to_bytes(32, "little"))
    encoded[31] |= (x & 1) << 7
    return bytes(encoded)


def _decode_point(encoded: bytes) -> tuple[int, int] | None:
    if len(encoded) != 32:
        return None
    value = int.from_bytes(encoded, "little")
    sign = value >> 255
    y = value & ((1 << 255) - 1)
    if y >= _Q:
        return None
    x = _xrecover(y)
    if (x & 1) != sign:
        x = _Q - x
    return x, y


def ed25519_public_key(seed: bytes) -> bytes:
    if len(seed) != 32:
        raise ValueError("Ed25519 seed must be 32 bytes")
    digest = bytearray(hashlib.sha512(seed).digest()[:32])
    digest[0] &= 248
    digest[31] &= 63
    digest[31] |= 64
    return _encode_point(_scalar_mult(_B, int.from_bytes(digest, "little")))


def ed25519_sign(message: bytes, seed: bytes) -> bytes:
    if len(seed) != 32:
        raise ValueError("Ed25519 seed must be 32 bytes")
    digest = hashlib.sha512(seed).digest()
    scalar_bytes = bytearray(digest[:32])
    scalar_bytes[0] &= 248
    scalar_bytes[31] &= 63
    scalar_bytes[31] |= 64
    scalar = int.from_bytes(scalar_bytes, "little")
    nonce = int.from_bytes(hashlib.sha512(digest[32:] + message).digest(), "little") % _L
    encoded_r = _encode_point(_scalar_mult(_B, nonce))
    challenge = int.from_bytes(hashlib.sha512(encoded_r + ed25519_public_key(seed) + message).digest(), "little") % _L
    return encoded_r + ((nonce + challenge * scalar) % _L).to_bytes(32, "little")


def ed25519_verify(signature: bytes, message: bytes, public_key: bytes) -> bool:
    if len(signature) != 64 or len(public_key) != 32:
        return False
    encoded_r, encoded_s = signature[:32], signature[32:]
    scalar_s = int.from_bytes(encoded_s, "little")
    if scalar_s >= _L:
        return False
    point_a = _decode_point(public_key)
    point_r = _decode_point(encoded_r)
    if point_a is None or point_r is None:
        return False
    challenge = int.from_bytes(hashlib.sha512(encoded_r + public_key + message).digest(), "little") % _L
    return _encode_point(_scalar_mult(_B, scalar_s)) == _encode_point(_edwards_add(point_r, _scalar_mult(point_a, challenge)))


__all__ = ["CHANNEL", "RECOVERY_PURPOSE", "SCHEMA_VERSION", "UPDATE_PURPOSE", "accept_manifest", "canonical_json", "ed25519_public_key", "ed25519_sign", "ed25519_verify", "manifest_identity", "sha256_file", "stage_verified_package", "validate_trust_policy", "validate_update_manifest", "verify_sealed_handoff", "verify_trust_policy", "verify_update_manifest"]
