#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "phase13b"))

from character_gate import evaluate_character_dossier  # noqa: E402
from voicebank_gate import evaluate_voicebank_dossier  # noqa: E402


REQUIRED = (
    "docs/phase13b/ACCEPTANCE.md",
    "docs/phase13b/MANDATORY_OFFICIAL_VOICEBANK_AND_CHARACTER_VALIDATION_KO.md",
    "docs/phase13b/MANDATORY_OFFICIAL_VOICEBANK_AND_CHARACTER_VALIDATION.md",
    "docs/phase13b/MANDATORY_FUTURE_VALIDATION_KO.md",
    "docs/phase13b/MANDATORY_FUTURE_VALIDATION.md",
    "docs/phase13b/EVIDENCE.md",
    "docs/phase13b/official-voicebank-01-dossier.json",
    "docs/phase13b/character-01-dossier.json",
    "docs/phase13b/mandatory-validation-matrix.json",
    "docs/phase13b/product-release-dossier.json",
    "docs/phase13b/IMPLEMENTATION_REPORT.md",
    "assets/character-01/production-development/asset-manifest.json",
    "phase13b/CMakeLists.txt",
)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    errors: list[str] = []
    for relative in REQUIRED:
        if not (root / relative).is_file():
            errors.append(f"required Phase 13B file is missing: {relative}")
    if errors:
        for error in errors:
            print(f"[phase13b] ERROR: {error}", file=sys.stderr)
        return 1

    voice_dossier = json.loads((root / "docs/phase13b/official-voicebank-01-dossier.json").read_text(encoding="utf-8"))
    char_dossier = json.loads((root / "docs/phase13b/character-01-dossier.json").read_text(encoding="utf-8"))
    voice = evaluate_voicebank_dossier(voice_dossier, root)
    character = evaluate_character_dossier(char_dossier, root)
    if voice.passed:
        errors.append("checked-in Official Voicebank 01 baseline must remain blocked until real external evidence exists")
    if character.passed:
        errors.append("checked-in Character 01 baseline must remain blocked until real external evidence exists")
    if voice_dossier.get("official") is not False or voice_dossier.get("contractedSinger") is not False:
        errors.append("checked-in voicebank dossier must retain official=false and contractedSinger=false")
    if char_dossier.get("commercialRelease") is not False or char_dossier.get("finalPublicName"):
        errors.append("checked-in character dossier must retain commercialRelease=false and an empty finalPublicName")

    asset_manifest = json.loads((root / "assets/character-01/production-development/asset-manifest.json").read_text(encoding="utf-8"))
    if asset_manifest.get("developmentOnly") is not True:
        errors.append("Character 01 derived assets must remain developmentOnly=true")
    if asset_manifest.get("productionStatus") != "NOT_A_PRODUCTION_TURNAROUND":
        errors.append("Character 01 derived assets must not claim production turnaround status")

    product_dossier = json.loads((root / "docs/phase13b/product-release-dossier.json").read_text(encoding="utf-8"))
    for requirement in ("final-eula", "voicebank-license"):
        item = product_dossier.get("requirements", {}).get(requirement, {})
        if item.get("result") == "PASS" and not item.get("evidence"):
            errors.append(f"product PASS requirement lacks evidence: {requirement}")
        if item.get("result") == "PASS":
            errors.append(f"checked-in product dossier must not pre-approve {requirement}")

    matrix = json.loads((root / "docs/phase13b/mandatory-validation-matrix.json").read_text(encoding="utf-8"))
    for target in matrix.get("targets", []):
        if target.get("runtimeResult") == "PASS" and not target.get("evidence"):
            errors.append(f"Phase 13B PASS target lacks evidence: {target.get('id')}")

    remaining = json.loads((root / "docs/remaining-tasks.json").read_text(encoding="utf-8"))
    tasks = {item.get("id"): item for item in remaining.get("tasks", [])}
    for task_id in ("SEAM-P13-005", "SEAM-P13-006"):
        if tasks.get(task_id, {}).get("status") != "EXTERNAL_GATE":
            errors.append(f"{task_id} must remain EXTERNAL_GATE")
    if tasks.get("SEAM-P13-007", {}).get("status") != "DONE":
        errors.append("SEAM-P13-007 engineering tooling must be DONE")

    if errors:
        for error in errors:
            print(f"[phase13b] ERROR: {error}", file=sys.stderr)
        print(f"PHASE13B_CONTRACT=FAIL errors={len(errors)}", file=sys.stderr)
        return 1
    print(f"PHASE13B_CONTRACT=PASS voicebankBlocked={not voice.passed} characterBlocked={not character.passed}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
