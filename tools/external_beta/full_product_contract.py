from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import stat
from typing import Final

try:
    from .full_product_contract_profile import validate_profile
    from .full_product_contract_validation import validate_registry
    from .release_gate_validation import HEX64, JsonObject, JsonValue
except ImportError:
    from full_product_contract_profile import validate_profile
    from full_product_contract_validation import validate_registry
    from release_gate_validation import HEX64, JsonObject, JsonValue


ROOT: Final = Path(__file__).resolve().parents[2]
ORIGIN_SHA256: Final = "635606cfd10be803612dfcb47cf84651796a06860ff34dc8343705eac20c9c01"
MAXIMUM_CONTRACT_BYTES: Final = 1024 * 1024
MAXIMUM_JSON_DEPTH: Final = 64


def _read_contract(path: Path) -> bytes:
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode):
        raise ValueError("contract must be a regular file")
    if before.st_size > MAXIMUM_CONTRACT_BYTES:
        raise ValueError("contract exceeds the 1 MiB size limit")
    flags = os.O_RDONLY | getattr(os, "O_NONBLOCK", 0) | getattr(os, "O_NOFOLLOW", 0)
    with os.fdopen(os.open(path, flags), "rb") as stream:
        opened = os.fstat(stream.fileno())
        if not stat.S_ISREG(opened.st_mode):
            raise ValueError("contract must be a regular file")
        if (before.st_dev, before.st_ino) != (opened.st_dev, opened.st_ino):
            raise ValueError("contract file changed while opening")
        if opened.st_size > MAXIMUM_CONTRACT_BYTES:
            raise ValueError("contract exceeds the 1 MiB size limit")
        contents = stream.read(MAXIMUM_CONTRACT_BYTES + 1)
    if len(contents) > MAXIMUM_CONTRACT_BYTES:
        raise ValueError("contract exceeds the 1 MiB size limit")
    return contents


def _unique_object(pairs: list[tuple[str, JsonValue]]) -> JsonObject:
    result: JsonObject = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate object key")
        result[key] = value
    return result


def _reject_constant(value: str) -> JsonValue:
    raise ValueError(f"nonfinite numeric constant: {value}")


def _finite_float(value: str) -> float:
    result = float(value)
    if not math.isfinite(result):
        raise ValueError("nonfinite numeric value")
    return result


def _parse_contract(contents: bytes) -> JsonValue:
    contract: JsonValue = json.loads(contents, object_pairs_hook=_unique_object,
                                    parse_constant=_reject_constant, parse_float=_finite_float)
    pending: list[tuple[JsonValue, int]] = [(contract, 0)]
    while pending:
        value, depth = pending.pop()
        if depth > MAXIMUM_JSON_DEPTH:
            raise ValueError("contract exceeds the JSON depth limit")
        if isinstance(value, dict):
            pending.extend((child, depth + 1) for child in value.values())
        elif isinstance(value, list):
            pending.extend((child, depth + 1) for child in value)
    return contract


def full_product_contract_errors(acceptance: JsonObject) -> list[str]:
    reference = acceptance.get("fullProductContract")
    if not isinstance(reference, dict) or set(reference) != {"locator", "sha256"}:
        return ["full-product contract reference requires a locator and content digest"]
    locator, digest = reference.get("locator"), reference.get("sha256")
    if not isinstance(locator, str) or not locator or not isinstance(digest, str) or HEX64.fullmatch(digest) is None:
        return ["full-product contract reference requires a locator and SHA-256 content digest"]
    try:
        contents = _read_contract(ROOT / locator)
    except (OSError, ValueError) as error:
        return [f"full-product contract reference cannot be read: {error}"]
    if hashlib.sha256(contents).hexdigest() != digest:
        return ["full-product contract content digest does not match referenced bytes"]
    try:
        contract = _parse_contract(contents)
    except (UnicodeError, ValueError, RecursionError) as error:
        return [f"full-product contract content is invalid JSON: {error}"]
    registry = validate_registry(contract)
    errors = list(registry.errors)
    if not isinstance(contract, dict):
        return errors
    if contract.get("schemaVersion") != 1 or isinstance(contract.get("schemaVersion"), bool) or contract.get("contractId") != "project-seam.full-product-beta" or contract.get("contractVersion") != "1.0.0" or contract.get("beforeBetaGO") is not True:
        errors.append("full-product contract version or mandatory scope differs")
    authority = contract.get("authority")
    if not isinstance(authority, dict) or authority.get("sha256") != ORIGIN_SHA256 or authority.get("decision") != "USER_SETTLED_FULL_SCOPE":
        errors.append("full-product contract origin authority differs")
    profile = validate_profile(contract)
    errors.extend(profile.errors)
    if profile.pending:
        errors.append("full-product unresolved final criteria: " + ", ".join(profile.pending))
    return errors


def full_product_report_reference_errors(candidate: JsonObject) -> list[str]:
    records = candidate.get("evidence")
    if not isinstance(records, list):
        return []
    errors: list[str] = []
    for record in records:
        if not isinstance(record, dict) or record.get("requirementId") != "EB-009-full-product":
            continue
        reference = record.get("fullProductReport")
        if not isinstance(reference, dict) or set(reference) != {"locator", "sha256"}:
            errors.append("EB-009-full-product: fullProductReport reference is required")
            continue
        locator, digest = reference.get("locator"), reference.get("sha256")
        if not isinstance(locator, str) or not locator or not isinstance(digest, str) or HEX64.fullmatch(digest) is None:
            errors.append("EB-009-full-product: fullProductReport requires locator and content digest")
    return errors
