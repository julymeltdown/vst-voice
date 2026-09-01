from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path
from typing import Final


CONTRACT_DIRECTORY: Final = Path("docs/product/creator-beta")
CANONICAL_DOCUMENT: Final = CONTRACT_DIRECTORY / "CREATOR_SCOPE_RATIFICATION.md"
SCHEMA_PATH: Final = CONTRACT_DIRECTORY / "creator-scope-ratification.schema.json"
RECORD_PATH: Final = CONTRACT_DIRECTORY / "creator-scope-ratification.json"
SESSION_IDS: Final = tuple(f"CSR-{index:03d}" for index in range(1, 6))
PARTICIPANT_IDS: Final = tuple(f"creator-{index:02d}" for index in range(1, 6))
HYPOTHESIS_IDS: Final = tuple(f"CSR-H{index:02d}" for index in range(1, 7))
APPROVAL_ROLES: Final = ("PRODUCT_RESEARCH", "QA", "ENGINEERING")
REVIEWER_PREFIXES: Final = {
    "PRODUCT_RESEARCH": "research-reviewer-",
    "QA": "qa-reviewer-",
    "ENGINEERING": "engineering-reviewer-",
}
PREREQUISITE_IDS: Final = (
    "consentProtocol",
    "seamCandidate",
    "openUtauReference",
    "voicebank",
    "ustxFixture",
    "lowFidelityPrototype",
)
OPENUTAU_COMMIT: Final = "8c0dc4007e6e8c8181f3a12c10205671800eeb8b"


class RecordStatus(StrEnum):
    NOT_RUN = "NOT_RUN"
    BLOCKED = "BLOCKED"
    PASS = "PASS"


@dataclass(frozen=True, slots=True)
class SessionFacts:
    recruited: int
    completed: int
    continuing: int
    unresolved_p0: int
    unresolved_p1: int
    unresolved_p2: int
    completed_ids: frozenset[str]
    errors: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class ScopeVerification:
    state: str
    schema8_authorized: bool
    errors: tuple[str, ...]
