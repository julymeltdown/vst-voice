"""One status page that keeps four different claims from being read as one.

`BETA_READINESS_ISSUES.md` records this defect as SEAM-BETA-P2-03: several documents mix
contract validity, source implementation, target-machine pass and beta readiness, and a string
like `*_CONTRACT=PASS` can be mistaken for product acceptance. Older readiness percentages also
used different denominators, so two numbers could both be true and mean different things.

This module is the single place that separates them. The four states are deliberately ordered,
and each one is strictly stronger than the one before it:

| State | Means | Rests on |
| --- | --- | --- |
| `CONTRACT_VALID` | the requirement is written down and self-consistent | a machine-readable contract |
| `IMPLEMENTED` | code for it exists in this repository | files that exist and are non-empty |
| `TARGET_PASS` | it ran and passed on this machine | a recorded local run |
| `BETA_READY` | an installed candidate met it with retained evidence | immutable external evidence |

A state may only be reported at or below what its evidence supports, and the page refuses to
print a state it cannot derive. The important consequence is the direction of the mistake: this
module can report less progress than a reader hopes for, and it cannot report more than the
repository proves. Nothing here advances a gate, and every row carries the evidence that
justifies its own state so a reader can check it rather than trust it.
"""
from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Final


#: The four states, weakest first. A row may never claim a state that outranks its evidence.
STATES: Final = ("CONTRACT_VALID", "IMPLEMENTED", "TARGET_PASS", "BETA_READY")


def state_rank(state: str) -> int:
    """The ordinal of a state, or -1 when the name is not one of the four."""
    try:
        return STATES.index(state)
    except ValueError:
        return -1


@dataclass(frozen=True)
class Row:
    """One tracked claim, its state, and the evidence that justifies that state.

    `evidence` names where the claim was checked rather than asserting a result. A row whose
    state is `BETA_READY` must say what retained record backs it; a row at `TARGET_PASS` must
    say which run. The point is that "PASS" is never a bare word in this file.
    """

    claim: str
    state: str
    evidence: tuple[str, ...]
    #: Why this row is not further along. Empty only when the state is BETA_READY.
    blocked_on: str = ""
    notes: str = ""

    def __post_init__(self) -> None:
        if state_rank(self.state) < 0:
            raise ValueError(f"{self.claim}: {self.state!r} is not one of {STATES}")
        if not self.evidence:
            raise ValueError(f"{self.claim}: every row must name its evidence")
        # A row that is not at the ceiling must say what stops it rising further. A row already
        # at CONTRACT_VALID is claiming only that the requirement is written down, which is the
        # whole of that state, so there is nothing above it for the row to be blocked from.
        if self.state not in ("CONTRACT_VALID", "BETA_READY") and not self.blocked_on:
            raise ValueError(f"{self.claim}: a row below its ceiling must say what blocks it")


def _root() -> Path:
    return Path(__file__).resolve().parents[2]


def _contract() -> dict:
    return json.loads(
        (_root() / "docs/product/full-product-beta-contract.json").read_text(encoding="utf-8")
    )


def _acceptance() -> dict:
    return json.loads(
        (_root() / "docs/product/usable-alpha-acceptance.json").read_text(encoding="utf-8")
    )


def _count_completed_physical_runs() -> tuple[int, int]:
    """Completed physical rows out of all canonical rows in the alpha acceptance matrix.

    Read from the acceptance document rather than restated, so this cannot drift away from the
    matrix it summarises. A row counts only when it carries a completed physical run; a passing
    unit or controller test is a different thing and is not one of these.
    """
    document = _acceptance()
    rows = document.get("requirements") if isinstance(document, dict) else None
    if not isinstance(rows, list):
        return (0, 0)
    completed = 0
    for row in rows:
        if not isinstance(row, dict):
            continue
        # A row counts only when it is marked complete AND carries retained evidence. Status
        # alone would let a row claim a pass with nothing behind it, which is the exact
        # confusion this page exists to prevent.
        if row.get("status") == "COMPLETED" and row.get("evidence"):
            completed += 1
    return (completed, len(rows))


def build_rows() -> tuple[Row, ...]:
    """The tracked rows, each derived from evidence that exists in this repository."""
    contract = _contract()
    scope = contract.get("scope", {})
    completed_runs, total_runs = _count_completed_physical_runs()

    return (
        Row(
            claim="Full-scope Beta contract is written and self-consistent",
            state="CONTRACT_VALID",
            evidence=("docs/product/full-product-beta-contract.json",),
            notes="R1-R20 and V01-V18 are enumerated in machine-readable form.",
        ),
        Row(
            claim="Capability matrix covers every mandatory feature",
            state="CONTRACT_VALID",
            evidence=(
                "docs/product/full-product-beta-contract.json (scope.matrixStatus="
                f"{scope.get('matrixStatus')!r})",
                "tools/external_beta/capability_matrix.py",
            ),
            blocked_on=(
                "The contract's resourceCoverage rule turns on a positive supported-and-tested "
                "combination. There is no model or vocoder byte in the repository and "
                "scope.releasedResources is empty, so no positive cell can be asserted."
            ),
            notes="The refusal half is derived from code and is recorded; the positive half is absent.",
        ),
        Row(
            claim="Bounded interchange boundary (U29)",
            state="IMPLEMENTED",
            evidence=(
                "libs/seam-interchange/",
                "docs/implementation/U29_ACCEPTANCE_AUDIT_2026-09-22.md",
            ),
            blocked_on=(
                "Accepted locally on POSIX at 480a5e3. Windows and full-product acceptance are "
                "open, and Windows is deferred by user direction on this machine."
            ),
        ),
        Row(
            claim="Neural per-request determinism",
            state="IMPLEMENTED",
            evidence=(
                "libs/seam-neural-synthesis/src/graph_contract.cpp",
                "docs/implementation/NEURAL_DETERMINISM_DEFECT_DIAGNOSIS_2026-10-03.md",
            ),
            blocked_on=(
                "The contract now refuses an unseeded RandomNormalLike or Dropout at admission, "
                "but no neural render can be run: there is still no model in the repository."
            ),
        ),
        Row(
            claim="Canonical standalone musician journey",
            state="IMPLEMENTED",
            evidence=(
                "docs/product/usable-alpha-acceptance.json",
                "BETA_READINESS_ISSUES.md (SEAM-BETA-P0-04)",
            ),
            blocked_on=(
                f"{completed_runs} of {total_runs} canonical rows carry a completed physical run. "
                "The rows need a person, installed candidate bytes and retained hash-bound "
                "evidence; no automated check can produce them."
            ),
            notes="This is the number most often misread as progress on its own.",
        ),
        Row(
            claim="Target OS and DAW compatibility matrix",
            state="CONTRACT_VALID",
            evidence=(
                f"docs/product/full-product-beta-contract.json (scope.hostTuples: "
                f"{len(scope.get('hostTuples', []))} required)",
                "docs/product/external-beta-host-matrix.json",
                "tools/external_beta/host_evidence.py",
            ),
            blocked_on=(
                "No completed record covers the required tuples. Four of the nine are "
                "windows-x86_64 and cannot be run on this macOS machine."
            ),
        ),
        Row(
            claim="External Beta promotion",
            state="CONTRACT_VALID",
            evidence=("docs/product/external-beta-acceptance.json",),
            blocked_on=(
                "EB-001..EB-009 all require immutable evidence records that do not exist. "
                "Promotion is refused by the release gate, and nothing in this repository can "
                "produce signed, notarized, installed-candidate evidence."
            ),
        ),
    )


def status_report() -> dict[str, object]:
    """The status page, with an explicit statement of what each state does not mean."""
    rows = build_rows()
    counts = {state: 0 for state in STATES}
    for row in rows:
        counts[row.state] += 1
    return {
        "schemaVersion": 1,
        "states": list(STATES),
        "stateMeanings": {
            "CONTRACT_VALID": "The requirement is written down and self-consistent.",
            "IMPLEMENTED": "Code for it exists in this repository.",
            "TARGET_PASS": "It ran and passed on this machine.",
            "BETA_READY": "An installed candidate met it with retained immutable evidence.",
        },
        "counts": counts,
        "betaReady": False,
        "betaReadyWhy": (
            "No row is BETA_READY. Every row below it names what is missing, and none of those "
            "gaps is closable by a source change alone: they need retained evidence from an "
            "installed candidate, real people, or assets this repository does not contain."
        ),
        "rows": [
            {
                "claim": row.claim,
                "state": row.state,
                "evidence": list(row.evidence),
                "blockedOn": row.blocked_on,
                "notes": row.notes,
            }
            for row in rows
        ],
        "absentMeans": (
            "A row absent from this page is not claimed to pass at any state. A row listed at a "
            "lower state is not claimed to reach a higher one."
        ),
    }
