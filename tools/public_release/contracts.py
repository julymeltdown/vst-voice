from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from datetime import datetime
from typing import Final


JsonValue = (
    str | int | float | bool | None | list["JsonValue"] | dict[str, "JsonValue"]
)
JsonObject = dict[str, JsonValue]

PUBLIC_STATES: Final[tuple[str, ...]] = (
    "DRAFT",
    "AUTHORIZED_FROZEN",
    "SIGNED",
    "CLEAN_INSTALLED",
    "BANK_READY",
    "EVIDENCE_PASSED",
    "EXTERNAL_BETA_READY",
    "EXTERNAL_BETA_CLOSED",
    "PUBLIC_ACTIVE",
    "DISTRIBUTION_PAUSED",
    "SUPERSEDED",
    "REVOKED",
)

PUBLIC_REQUIREMENT_IDS: Final[tuple[str, ...]] = (
    "PR-001-contract",
    "PR-002-root-chain",
    "PR-003-external-beta-closed",
    "PR-004-public-documents",
    "PR-005-signed-artifacts",
    "PR-006-clean-installed",
    "PR-007-bank-ready",
    "PR-008-target-matrices",
    "PR-009-update-channel",
    "PR-010-support-intake",
    "PR-011-incident-drill",
    "PR-012-archive-restore",
    "PR-013-approvals",
    "PR-014-rollback-revoke",
)

REQUIRED_APPROVAL_ROLES: Final[tuple[str, ...]] = (
    "independent-release-verifier",
    "content-rights",
    "security-privacy",
    "macos-reviewer",
    "windows-reviewer",
    "musician-reviewer",
    "accessibility-reviewer",
    "archive-reviewer",
)

SUPPORT_LIFECYCLE_STAGES: Final[tuple[str, ...]] = (
    "INTAKE",
    "ACKNOWLEDGED",
    "TRIAGED",
    "REPRODUCED",
    "RESOLVED_OR_ESCALATED",
    "USER_COMMUNICATED",
    "RETAINED_OR_DELETED",
)

INCIDENT_ACTIONS: Final[tuple[str, ...]] = (
    "PAUSE",
    "SUPPORT_INTAKE",
    "ACKNOWLEDGE",
    "TRIAGE",
    "REPAIR",
    "ROLLBACK",
    "REVOKE_REHEARSAL",
    "USER_COMMUNICATION",
)

HEX40: Final[re.Pattern[str]] = re.compile(r"^[0-9a-f]{40}$")
HEX64: Final[re.Pattern[str]] = re.compile(r"^[0-9a-f]{64}$")


@dataclass(frozen=True, slots=True)
class ValidationFinding:
    requirement_id: str
    message: str


@dataclass(frozen=True, slots=True)
class GateResult:
    state: str
    passed: bool
    errors: tuple[str, ...] = ()
    blocked_ids: tuple[str, ...] = ()

    def as_dict(self) -> JsonObject:
        return {
            "state": self.state,
            "passed": self.passed,
            "errors": list(self.errors),
            "blockedIds": list(self.blocked_ids),
        }


@dataclass(frozen=True, slots=True)
class ReleaseGateInputError(ValueError):
    message: str

    def __str__(self) -> str:
        return self.message


def canonical_json(value: JsonValue) -> str:
    return json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    )


def sha256_json(value: JsonValue) -> str:
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def root_sha256(value: JsonObject) -> str:
    return sha256_json({key: item for key, item in value.items() if key != "sha256"})


def approval_envelope_sha256(value: JsonObject) -> str:
    return sha256_json(
        {key: item for key, item in value.items() if key != "envelopeSha256"}
    )


def operation_envelope_sha256(value: JsonObject) -> str:
    return sha256_json(
        {key: item for key, item in value.items() if key != "envelopeSha256"}
    )


def operation_decision_sha256(value: JsonObject) -> str:
    return sha256_json(
        {key: item for key, item in value.items() if key != "decisionSha256"}
    )


def is_sha256(value: JsonValue) -> bool:
    return isinstance(value, str) and HEX64.fullmatch(value) is not None


def parse_time(value: JsonValue) -> datetime | None:
    if not isinstance(value, str) or not value:
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None
