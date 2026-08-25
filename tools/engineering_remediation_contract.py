from __future__ import annotations

import re
from typing import TypeAlias


JsonValue: TypeAlias = (
    str
    | int
    | float
    | bool
    | None
    | list["JsonValue"]
    | dict[str, "JsonValue"]
)
JsonObject: TypeAlias = dict[str, JsonValue]

REQUIRED_GATES = (
    "tracked-source-closure",
    "debug-suite",
    "release-suite",
    "external-beta-contracts",
    "phase13a-contracts",
    "asan-ubsan",
    "thread-sanitizer",
    "realtime-callback",
    "source-contracts",
    "built-identity",
    "installed-byte-certification",
    "export-crash-recovery",
    "whitespace-python-lint",
    "fresh-review",
)
REQUIRED_MANUAL_QA = (
    "backing-only-preview",
    "audio-unavailable",
    "recovery-save-as",
    "export-process-restart",
    "clap-runtime-identity",
    "installed-byte-mutation",
    "voicebank-path-safety",
)
REQUIRED_REVIEW_LANES = (
    "correctness",
    "testing",
    "maintainability",
    "security",
    "performance",
    "api-contract",
    "reliability",
)
SHA40 = re.compile(r"[0-9a-f]{40}")
SHA256 = re.compile(r"[0-9a-f]{64}")
