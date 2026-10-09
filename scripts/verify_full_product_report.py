#!/usr/bin/env python3
"""Run the typed EB-009 audit on one hash-bound full-product report.

Exit 0 only when the report authorizes release under the canonical contract.
A complete engineering fixture under the synthetic contract reports
SYNTHETIC_FIXTURE_PASS and exits 5; every other result is BLOCKED (exit 3).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.external_beta import release_gate  # noqa: E402
from tools.external_beta.full_product_gate import audit_full_product_reference  # noqa: E402
from tools.external_beta.full_product_report import FullProductReportError, MAXIMUM_REPORT_BYTES, _read_regular_file  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Fail-closed semantic validation of a Project SEAM full-product report"
    )
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument(
        "--acceptance-contract",
        type=Path,
        default=ROOT / "docs/product/external-beta-acceptance.json",
    )
    parser.add_argument(
        "--full-product-contract",
        type=Path,
        default=ROOT / "docs/product/full-product-beta-contract.json",
    )
    parser.add_argument("--evidence-root", type=Path, help="Restored archive root; defaults to the report directory")
    args = parser.parse_args(argv)
    result = None
    try:
        # Keep the lexical path so the report validator can reject a symlink;
        # resolving it here would silently turn the link into its target.
        report_path = Path(os.path.abspath(os.fspath(args.report)))
        report_bytes = _read_regular_file(report_path, label="fullProductReport", maximum_bytes=MAXIMUM_REPORT_BYTES)
        reference = {
            "locator": str(report_path),
            "sha256": hashlib.sha256(report_bytes).hexdigest(),
        }
        candidate = release_gate.load_candidate(args.candidate)
        acceptance = release_gate.load_candidate(args.acceptance_contract)
        full_contract = release_gate.load_candidate(args.full_product_contract)
        result = audit_full_product_reference(
            reference,
            candidate=candidate,
            acceptance_contract=acceptance,
            full_product_contract=full_contract,
            evidence_root=args.evidence_root if args.evidence_root is not None else report_path.parent,
        )
        errors = list(result.errors)
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError, FullProductReportError) as error:
        errors = [str(error)]
    payload = result.as_dict() if result is not None else {"passed": False, "authorizesRelease": False}
    if result is not None and result.authorizes_release:
        status, code = "PASS", 0
    elif result is not None and result.passed:
        status, code = "SYNTHETIC_FIXTURE_PASS", 5
    else:
        status, code = "BLOCKED", 3
    payload.update(status=status, errors=errors)
    print(json.dumps(payload, ensure_ascii=False, sort_keys=True))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
