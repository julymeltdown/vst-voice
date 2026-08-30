from __future__ import annotations

import base64
import binascii
import re
from dataclasses import dataclass
from functools import lru_cache
from typing import Final

from tools.phase13a.update_contract import (
    canonical_json as signing_json,
    ed25519_verify,
)

from .contracts import JsonObject, JsonValue, sha256_json


KEY_ID: Final[re.Pattern[str]] = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$")
MAX_CACHED_PAYLOAD_BYTES: Final[int] = 64 * 1024


@dataclass(frozen=True, slots=True)
class TrustedKey:
    public_key: bytes
    signer_id: str


@lru_cache(maxsize=256)
def _verify_cached(signature: bytes, message: bytes, public_key: bytes) -> bool:
    return ed25519_verify(signature, message, public_key)


def _verify_signature(signature: bytes, message: bytes, public_key: bytes) -> bool:
    if len(message) > MAX_CACHED_PAYLOAD_BYTES:
        return ed25519_verify(signature, message, public_key)
    return _verify_cached(signature, message, public_key)


def _decode_base64(
    value: JsonValue,
    label: str,
    size: int,
) -> tuple[bytes | None, str | None]:
    if not isinstance(value, str) or not value:
        return None, f"{label} must be a non-empty base64 string"
    try:
        decoded = base64.b64decode(value.encode("ascii"), validate=True)
    except (UnicodeEncodeError, binascii.Error):
        return None, f"{label} is not valid base64"
    if len(decoded) != size:
        return None, f"{label} must decode to {size} bytes"
    return decoded, None


def canonical_signing_payload(value: JsonObject, digest_field: str) -> bytes:
    unsigned = {
        key: item
        for key, item in value.items()
        if key not in {"signature", digest_field}
    }
    return signing_json(unsigned)


def _trusted_keys(
    policy: JsonObject,
    allowed_roles: tuple[str, ...],
    label: str,
) -> tuple[dict[tuple[str, str], TrustedKey], list[str]]:
    errors: list[str] = []
    values = policy.get("trustedKeys")
    if not isinstance(values, list):
        return {}, [f"{label}.trustedKeys must be an array"]
    keys: dict[tuple[str, str], TrustedKey] = {}
    key_ids: set[str] = set()
    signer_ids: set[str] = set()
    for index, value in enumerate(values):
        prefix = f"{label}.trustedKeys[{index}]"
        if not isinstance(value, dict):
            errors.append(f"{prefix} must be an object")
            continue
        key_id = value.get("keyId")
        role = value.get("role")
        signer_id = value.get("signerId")
        if not isinstance(key_id, str) or KEY_ID.fullmatch(key_id) is None:
            errors.append(f"{prefix}.keyId is invalid")
            continue
        if key_id in key_ids:
            errors.append(f"{label} keyId must be unique: {key_id}")
        key_ids.add(key_id)
        if not isinstance(signer_id, str) or not signer_id:
            errors.append(f"{prefix}.signerId is required")
            continue
        if signer_id in signer_ids:
            errors.append(f"{label} signerId must be unique: {signer_id}")
        signer_ids.add(signer_id)
        if role not in allowed_roles:
            errors.append(f"{prefix}.role is invalid")
            continue
        assert isinstance(role, str)
        public_key, error = _decode_base64(
            value.get("publicKey"),
            f"{prefix}.publicKey",
            32,
        )
        if error is not None or public_key is None:
            errors.append(error or f"{prefix}.publicKey is invalid")
            continue
        keys[(key_id, role)] = TrustedKey(public_key, signer_id)
    return keys, errors


def approval_policy_errors(
    policy_value: JsonValue,
    required_roles: tuple[str, ...],
) -> tuple[str, ...]:
    if not isinstance(policy_value, dict):
        return ("approval policy must be an object",)
    errors: list[str] = []
    if policy_value.get("policyVersion") != "public-release-approval-1.0":
        errors.append("approval policyVersion differs")
    if policy_value.get("requiredRoles") != list(required_roles):
        errors.append("approval policy roles differ")
    if policy_value.get("algorithm") != "Ed25519":
        errors.append("approval policy algorithm must be Ed25519")
    if policy_value.get("distinctSigners") is not True:
        errors.append("approval policy requires distinct signers")
    if policy_value.get("trustedKeyFields") != [
        "keyId",
        "role",
        "signerId",
        "publicKey",
    ]:
        errors.append("approval policy trustedKeyFields differ")
    keys, key_errors = _trusted_keys(policy_value, required_roles, "approvalPolicy")
    errors.extend(key_errors)
    key_roles = {role for _, role in keys}
    missing = sorted(set(required_roles) - key_roles)
    if missing:
        errors.append("approval policy lacks role-bound trusted keys: " + ", ".join(missing))
    return tuple(errors)


def operation_policy_errors(policy_value: JsonValue) -> tuple[str, ...]:
    if not isinstance(policy_value, dict):
        return ("operation policy must be an object",)
    errors: list[str] = []
    if policy_value.get("policyVersion") != "public-release-operation-1.0":
        errors.append("operation policyVersion differs")
    if policy_value.get("publisherRole") != "release-manager":
        errors.append("operation publisher role must be release-manager")
    if policy_value.get("algorithm") != "Ed25519":
        errors.append("operation policy algorithm must be Ed25519")
    if policy_value.get("trustedKeyFields") != [
        "keyId",
        "role",
        "signerId",
        "publicKey",
    ]:
        errors.append("operation policy trustedKeyFields differ")
    keys, key_errors = _trusted_keys(
        policy_value,
        ("release-manager",),
        "operationPolicy",
    )
    errors.extend(key_errors)
    if not keys:
        errors.append("operation policy lacks a release-manager trusted key")
    return tuple(errors)


def signed_record_errors(
    value: JsonObject,
    policy_value: JsonValue,
    role: str,
    digest_field: str,
    identity_field: str,
) -> tuple[str, ...]:
    if not isinstance(policy_value, dict):
        return ("signature policy must be an object",)
    errors: list[str] = []
    if "signatureVerified" in value:
        errors.append("signatureVerified is not an authority field")
    if value.get("algorithm") != "Ed25519":
        errors.append("signature algorithm must be Ed25519")
    if value.get("policyVersion") != policy_value.get("policyVersion"):
        errors.append("signature policyVersion differs")
    key_id = value.get("keyId")
    if not isinstance(key_id, str) or KEY_ID.fullmatch(key_id) is None:
        errors.append("signature keyId is invalid")
        return tuple(errors)
    role_values = policy_value.get("requiredRoles")
    policy_roles = (
        tuple(item for item in role_values if isinstance(item, str))
        if isinstance(role_values, list)
        else (str(policy_value.get("publisherRole")),)
    )
    keys, key_errors = _trusted_keys(policy_value, policy_roles, "signaturePolicy")
    errors.extend(key_errors)
    trusted_key = keys.get((key_id, role))
    if trusted_key is None:
        errors.append("signature key is not role-bound and trusted")
    elif value.get(identity_field) != trusted_key.signer_id:
        errors.append("signed record signer identity differs from trusted key")
    signature, signature_error = _decode_base64(value.get("signature"), "signature", 64)
    if signature_error is not None or signature is None:
        errors.append(signature_error or "signature is invalid")
    elif trusted_key is not None and not _verify_signature(
        signature,
        canonical_signing_payload(value, digest_field),
        trusted_key.public_key,
    ):
        errors.append("signature does not verify against the role-bound trusted key")
    if value.get(digest_field) != sha256_json(
        {key: item for key, item in value.items() if key != digest_field}
    ):
        errors.append(f"{digest_field} differs from the signed record")
    return tuple(errors)
