#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Protocol, TypedDict

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools" / "phase13b"))

from candidate_builder import build_candidate  # noqa: E402
from character_assets import generate_character_assets  # noqa: E402
from character_gate import evaluate_character_dossier  # noqa: E402
from content_bundle import create_development_bundle  # noqa: E402
from release_gate import evaluate_g5  # noqa: E402
from voicebank_gate import evaluate_voicebank_dossier  # noqa: E402
from tools.phase13a.release_identity import read_project_version  # noqa: E402


class GateEvaluation(Protocol):
    passed: bool
    errors: list[str]
    blocked_targets: list[str]


class GatePayload(TypedDict):
    passed: bool
    errors: list[str]
    blockedTargets: list[str]


def gate_payload(result: GateEvaluation) -> GatePayload:
    return GatePayload(
        passed=result.passed,
        errors=result.errors,
        blockedTargets=result.blocked_targets,
    )


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    output = (args.output or (root / "out/phase13b")).resolve()
    output.mkdir(parents=True, exist_ok=True)
    version = read_project_version(root)

    character_assets = generate_character_assets(
        root / "assets/character-01/source/canonical-lowpoly.jpeg",
        root / "assets/character-01/production-development",
    )
    voice_dossier = json.loads((root / "docs/phase13b/official-voicebank-01-dossier.json").read_text(encoding="utf-8"))
    character_dossier = json.loads((root / "docs/phase13b/character-01-dossier.json").read_text(encoding="utf-8"))
    voice = evaluate_voicebank_dossier(voice_dossier, root)
    character = evaluate_character_dossier(character_dossier, root)
    matrices = []
    for relative in (
        "docs/phase12c/mandatory-validation-matrix.json",
        "docs/phase13a/mandatory-validation-matrix.json",
        "docs/phase13b/mandatory-validation-matrix.json",
    ):
        matrices.append(json.loads((root / relative).read_text(encoding="utf-8")))
    product_dossier = json.loads((root / "docs/phase13b/product-release-dossier.json").read_text(encoding="utf-8"))
    release = evaluate_g5(voice, character, matrices, product_dossier, root)

    voice_path = output / "official-voicebank-01-result.json"
    character_path = output / "character-01-result.json"
    release_path = output / "g5-release-gate.json"
    write_json(voice_path, gate_payload(voice))
    write_json(character_path, gate_payload(character))
    write_json(release_path, release)
    write_json(output / "character-development-assets.json", character_assets)

    bundle_path = output / f"ProjectSEAM-{version}-content-development.zip"
    bundle = create_development_bundle(
        bundle_path,
        {
            "demo-human-voicebank-public-domain": root / "assets/demo-human-voicebank-public-domain",
            "character-01-development": root / "assets/character-01",
        },
        version,
    )
    bundle = {**bundle, "path": bundle_path.name}
    write_json(output / "development-content-bundle.json", bundle)
    candidate = build_candidate(
        output=output / f"ProjectSEAM-{version}-release-candidate-BLOCKED.zip",
        component_files={
            "voicebank-dossier.json": root / "docs/phase13b/official-voicebank-01-dossier.json",
            "character-dossier.json": root / "docs/phase13b/character-01-dossier.json",
            "product-release-dossier.json": root / "docs/phase13b/product-release-dossier.json",
            "release-report.json": release_path,
        },
        release_result=release,
        product_version=version,
    )
    candidate = {**candidate, "path": Path(candidate["path"]).name}
    write_json(output / "blocked-release-candidate.json", candidate)
    summary = {
        "phase": "13B",
        "engineeringStatus": "PASS",
        "productAcceptance": "BLOCKED" if not release["passed"] else "ACCEPTED",
        "officialVoicebankAccepted": voice.passed,
        "characterAccepted": character.passed,
        "unresolvedMandatoryCount": release["unresolvedMandatoryCount"],
        "developmentBundle": bundle,
    }
    write_json(output / "phase13b-summary.json", summary)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
