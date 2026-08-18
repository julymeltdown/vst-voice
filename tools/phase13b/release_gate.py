from __future__ import annotations

import argparse
import json
from pathlib import Path

from common import GateResult, validate_requirement_map
from product_gate import evaluate_product

PRODUCT_REQUIREMENTS = ("final-eula", "voicebank-license")


def _gate_result(value) -> GateResult:
    if isinstance(value, GateResult):
        return value
    if isinstance(value, dict):
        accepted = value.get("releaseStatus") == "ACCEPTED" or value.get("passed") is True
        blocked = list(value.get("unresolved", value.get("blockedTargets", [])))
        return GateResult(accepted, list(value.get("errors", [])), blocked)
    return GateResult(False, ["component result is invalid"], [])


def evaluate_g5(voicebank, character, matrices: list[dict], product_dossier: dict | None = None, root: Path | None = None) -> dict:
    voice = _gate_result(voicebank)
    char = _gate_result(character)
    merged = {"targets": []}
    for matrix in matrices:
        targets = matrix.get("targets", []) if isinstance(matrix, dict) else []
        if not isinstance(targets, list):
            continue
        for target in targets:
            if not isinstance(target, dict):
                continue
            mandatory = target.get("mandatory") is True or "G5" in target.get("mandatoryFor", [])
            copied = dict(target)
            copied["mandatory"] = mandatory
            merged["targets"].append(copied)
    result = evaluate_product(voice, char, merged, Path(root or "."))
    if product_dossier is not None:
        errors, blocked = validate_requirement_map(
            product_dossier.get("requirements"), PRODUCT_REQUIREMENTS, Path(root or ".")
        )
        result["errors"].extend(errors)
        result["blockedTargets"] = sorted(set(result["blockedTargets"] + blocked))
        result["passed"] = result["passed"] and not errors and not blocked
        result["unresolvedMandatoryCount"] = len(result["blockedTargets"]) + len(result["errors"])
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--voicebank-result", type=Path, required=True)
    parser.add_argument("--character-result", type=Path, required=True)
    parser.add_argument("--matrix", type=Path, action="append", default=[])
    parser.add_argument("--product-dossier", type=Path)
    parser.add_argument("--evidence-root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect-blocked", action="store_true")
    args = parser.parse_args(argv)
    voice_payload = json.loads(args.voicebank_result.read_text(encoding="utf-8"))
    char_payload = json.loads(args.character_result.read_text(encoding="utf-8"))
    matrices = [json.loads(path.read_text(encoding="utf-8")) for path in args.matrix]
    product = json.loads(args.product_dossier.read_text(encoding="utf-8")) if args.product_dossier else None
    result = evaluate_g5(voice_payload, char_payload, matrices, product, args.evidence_root)
    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    if args.expect_blocked:
        return 0 if not result["passed"] else 4
    return 0 if result["passed"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
