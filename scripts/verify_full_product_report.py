#!/usr/bin/env python3
"""Validate one hash-bound full-product Beta report outside the release gate."""

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
from tools.external_beta.full_product_report import (  # noqa: E402
    FullProductReportError,
    validate_full_product_report_reference,
)


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
    args = parser.parse_args(argv)
    try:
        # Keep the lexical path so the report validator can reject a symlink;
        # resolving it here would silently turn the link into its target.
        report_path = Path(os.path.abspath(os.fspath(args.report)))
        report_bytes = report_path.read_bytes()
        reference = {
            "locator": str(report_path),
            "sha256": hashlib.sha256(report_bytes).hexdigest(),
        }
        candidate = release_gate.load_candidate(args.candidate)
        acceptance = release_gate.load_candidate(args.acceptance_contract)
        full_contract = release_gate.load_candidate(args.full_product_contract)
        errors = list(
            validate_full_product_report_reference(
                reference,
                candidate=candidate,
                acceptance_contract=acceptance,
                full_product_contract=full_contract,
            )
        )
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError, FullProductReportError) as error:
        errors = [str(error)]
    result = {"status": "PASS" if not errors else "BLOCKED", "errors": errors}
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0 if not errors else 3


if __name__ == "__main__":
    raise SystemExit(main())
