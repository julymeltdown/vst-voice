from __future__ import annotations

import math

try:
    from .full_product_contract_empirical_catalog import EMPIRICAL_SPECS
    from .full_product_contract_protocols import canonical_definition_errors
    from .release_gate_validation import HEX64, JsonObject, JsonValue
except ImportError:
    from full_product_contract_empirical_catalog import EMPIRICAL_SPECS
    from full_product_contract_protocols import canonical_definition_errors
    from release_gate_validation import HEX64, JsonObject, JsonValue


def _text(value: JsonValue) -> bool:
    return isinstance(value, str) and bool(value.strip())


def _reference(value: JsonValue) -> bool:
    if not isinstance(value, dict) or set(value) != {"locator", "sha256"}:
        return False
    digest = value.get("sha256")
    return _text(value.get("locator")) and isinstance(digest, str) and HEX64.fullmatch(digest) is not None


def _bindings_errors(value: JsonValue, label: str) -> list[str]:
    if not isinstance(value, dict) or set(value) != {"machineProfile", "workload", "resourceMatrix", "backend", "provider", "precision"}:
        return [f"{label}: exact machine/workload/resource/backend/provider/precision bindings required"]
    errors: list[str] = []
    for key in ("machineProfile", "workload", "resourceMatrix"):
        if not _reference(value[key]):
            errors.append(f"{label}.{key}: content-bound reference required")
    for key in ("backend", "provider", "precision"):
        identity = value[key]
        if not isinstance(identity, dict) or set(identity) != {"id", "version", "sha256"}:
            errors.append(f"{label}.{key}: versioned identity required")
            continue
        digest = identity.get("sha256")
        if not _text(identity.get("id")) or not _text(identity.get("version")) or not isinstance(digest, str) or HEX64.fullmatch(digest) is None:
            errors.append(f"{label}.{key}: versioned content digest required")
    return errors


def _machine_errors(value: JsonValue, label: str) -> list[str]:
    fields = {"cpuModel", "logicalCpuCount", "ramBytes", "osId", "osVersion", "toolchainId", "profile"}
    if not isinstance(value, dict) or set(value) != fields:
        return [f"{label}: typed CPU/RAM/OS/toolchain machine profile required"]
    errors: list[str] = []
    for key in ("cpuModel", "osId", "osVersion", "toolchainId"):
        if not _text(value[key]):
            errors.append(f"{label}.{key}: non-empty identity required")
    for key in ("logicalCpuCount", "ramBytes"):
        number = value[key]
        if not isinstance(number, int) or isinstance(number, bool) or number <= 0:
            errors.append(f"{label}.{key}: positive integer required")
    if not _reference(value["profile"]):
        errors.append(f"{label}.profile: content-bound reference required")
    return errors


def _cell_errors(actual: JsonObject, expected: JsonObject, label: str) -> list[str]:
    descriptor = {key: value for key, value in actual.items() if key not in {"value", "bindings"}}
    errors = canonical_definition_errors(descriptor, expected, f"{label} dimension/unit/comparator")
    errors.extend(_bindings_errors(actual.get("bindings"), label))
    value = actual.get("value")
    if expected.get("valueType") == "machine-profile":
        return errors + _machine_errors(value, label)
    if not isinstance(value, (int, float)) or isinstance(value, bool) or (isinstance(value, float) and not math.isfinite(value)):
        return errors + [f"{label}: finite typed measurement required"]
    if expected.get("valueType") == "integer" and not isinstance(value, int):
        errors.append(f"{label}: integer measurement required")
    minimum = expected.get("minimumValue")
    maximum = expected.get("maximumValue")
    if isinstance(minimum, (int, float)) and value < minimum:
        errors.append(f"{label}: measurement below allowed domain")
    if isinstance(maximum, (int, float)) and value > maximum:
        errors.append(f"{label}: measurement above allowed domain")
    return errors


def empirical_result_errors(value: JsonValue, spec: JsonObject, label: str) -> list[str]:
    if not isinstance(value, dict) or set(value) != {"schemaVersion", "resultType", "cells"}:
        return [f"{label}: typed result grid required"]
    if type(value.get("schemaVersion")) is not int or value.get("schemaVersion") != 1 or value.get("resultType") != spec.get("resultType"):
        return [f"{label}: typed result version or kind differs"]
    cells = value.get("cells")
    expected_rows = spec.get("cells")
    if not isinstance(cells, list) or not isinstance(expected_rows, list):
        return [f"{label}: typed result cells required"]
    expected = {row["id"]: row for row in expected_rows if isinstance(row, dict) and isinstance(row.get("id"), str)}
    errors: list[str] = []
    seen: set[str] = set()
    for row in cells:
        if not isinstance(row, dict) or not isinstance(row.get("id"), str):
            errors.append(f"{label}: typed result cell identity required")
            continue
        identifier = row["id"]
        if identifier in seen or identifier not in expected:
            errors.append(f"{label}: duplicate or unknown typed result cell {identifier}")
            continue
        seen.add(identifier)
        errors.extend(_cell_errors(row, expected[identifier], f"{label}/{identifier}"))
    if seen != set(expected):
        errors.append(f"{label}: missing typed result dimension coverage")
    return errors


def empirical_criterion_errors(row: JsonObject) -> list[str]:
    identifier = row.get("id")
    if not isinstance(identifier, str) or identifier not in EMPIRICAL_SPECS:
        return ["full-product empirical criterion identity is unknown"]
    spec = EMPIRICAL_SPECS[identifier]
    if not isinstance(spec, dict):
        return ["full-product empirical catalog has invalid specification"]
    label = f"full-product final criteria {identifier}"
    errors = canonical_definition_errors(row.get("resultSpec"), spec, f"{label} typed result specification")
    fields = {"id", "status", "ownerUnits", "kind", "value", "resultSpec"}
    if row.get("status") == "RESOLVED":
        fields |= {"measurement", "independentReview"}
        errors.extend(empirical_result_errors(row.get("value"), spec, label))
        for key in ("measurement", "independentReview"):
            if not _reference(row.get(key)):
                errors.append(f"{label}: measured and independent-review references required")
    elif row.get("status") != "UNRESOLVED" or row.get("value") is not None:
        errors.append(f"{label}: unresolved qualification must retain null value")
    if row.get("kind") != "empirical" or set(row) != fields:
        errors.append(f"{label}: unknown or missing typed result fields")
    return errors
