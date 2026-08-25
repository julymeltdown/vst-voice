#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
from typing import Any, Mapping


STATE_SCHEMA_VERSION = 1
STATE_FIELDS = {"parameters", "noteExpression", "audioBuses", "gui", "transport", "bankIdentity", "engine"}


def canonical_state_bytes(state: Mapping[str, Any]) -> bytes:
    return json.dumps(dict(state), ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def canonical_state_sha256(state: Mapping[str, Any]) -> str:
    return hashlib.sha256(canonical_state_bytes(state)).hexdigest()


def project_state(canonical_state: Mapping[str, Any], format_name: str) -> dict[str, Any]:
    normalized = "AUv2" if format_name.lower() in {"au", "auv2"} else "VST3"
    unknown = set(canonical_state) - STATE_FIELDS
    if unknown:
        raise ValueError("canonical state contains unknown fields: " + ", ".join(sorted(unknown)))
    return {"schemaVersion": STATE_SCHEMA_VERSION, "format": normalized, "canonicalStateSha256": canonical_state_sha256(canonical_state), "state": dict(canonical_state)}


def validate_projected_state(projected: Mapping[str, Any], canonical_state: Mapping[str, Any], format_name: str) -> list[str]:
    errors: list[str] = []
    normalized = "AUv2" if format_name.lower() in {"au", "auv2"} else "VST3"
    if not isinstance(projected, Mapping):
        return ["projected state must be an object"]
    if projected.get("schemaVersion") != STATE_SCHEMA_VERSION:
        errors.append("future or unsupported projected state schema")
    if projected.get("format") != normalized:
        errors.append("projected state format differs from requested wrapper")
    if projected.get("canonicalStateSha256") != canonical_state_sha256(canonical_state):
        errors.append("projected state is not bound to the canonical state")
    state = projected.get("state")
    if not isinstance(state, Mapping):
        errors.append("projected state payload must be an object")
        return errors
    unknown = set(state) - STATE_FIELDS
    if unknown:
        errors.append("projected state contains unknown fields: " + ", ".join(sorted(unknown)))
    for field, expected in canonical_state.items():
        if field not in state:
            errors.append(f"projected state is missing canonical field: {field}")
        elif state[field] != expected:
            errors.append(f"projected state differs for canonical field: {field}")
    return errors


__all__ = ["STATE_SCHEMA_VERSION", "canonical_state_bytes", "canonical_state_sha256", "project_state", "validate_projected_state"]
