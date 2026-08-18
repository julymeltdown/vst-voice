#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


def require_text(path: Path, needles: list[str], errors: list[str]) -> None:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        errors.append(f"missing source: {path}: {exc}")
        return
    for needle in needles:
        if needle not in text:
            errors.append(f"{path}: missing contract token {needle!r}")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    errors: list[str] = []

    require_text(
        root / "libs/seam-clap-editor/src/editor_runtime.cpp",
        [
            "ProductionProjectRenderer",
            "voicebankResolution_",
            "VoicebankContentHashMissing",
            "VoicebankContentMismatch",
            "bindVoicebankLocked",
            "selectVoicebank",
            "previewCacheRoot",
        ],
        errors,
    )
    require_text(
        root / "libs/seam-rendering/src/region_renderer.cpp",
        [
            "PhraseSegmenter",
            "RenderSnapshotFactory",
            "PhraseRenderPipeline",
            "cache->load",
            "cache->store",
        ],
        errors,
    )
    require_text(
        root / "libs/seam-voicebank/src/catalog.cpp",
        [
            "computeVoicebankContentHash",
            "TrustedInstalled",
            "ContentHashMissing",
            "ContentMismatch",
            "SEAM_VOICEBANK_PATH",
        ],
        errors,
    )
    require_text(
        root / "libs/seam-distribution/src/installer.cpp",
        [
            'receipt.emplace("schemaVersion", static_cast<std::int64_t>(2))',
            'receipt.emplace("contentHash"',
            "computeVoicebankContentHash",
        ],
        errors,
    )

    fixture = root / "assets/demo-human-voicebank-public-domain"
    production = fixture / "production-bank"
    try:
        fixture_manifest = json.loads((fixture / "manifest.json").read_text(encoding="utf-8"))
        provenance = json.loads((fixture / "provenance.json").read_text(encoding="utf-8"))
        production_manifest = json.loads(
            (production / "manifest.json").read_text(encoding="utf-8")
        )
    except Exception as exc:  # noqa: BLE001 - contract audit reports all metadata errors
        errors.append(f"invalid Phase 12A fixture metadata: {exc}")
        fixture_manifest = {}
        provenance = {}
        production_manifest = {}

    if fixture_manifest.get("official") is not False:
        errors.append("public-domain fixture must retain official=false")
    if fixture_manifest.get("contractedSinger") is not False:
        errors.append("public-domain fixture must retain contractedSinger=false")
    if provenance.get("officialVoicebank") is not False:
        errors.append("provenance must retain officialVoicebank=false")
    if production_manifest.get("id") != "demo.public-domain.human.production":
        errors.append("production fixture ID is not the pinned Phase 12A ID")
    if production_manifest.get("version") != "0.12.0":
        errors.append("production fixture version is not 0.12.0")
    renderer_names = {unit.get("renderer") for unit in production_manifest.get("units", [])}
    required_renderers = {"raw", "classic-psola", "spectral-classic", "stretch"}
    if not required_renderers.issubset(renderer_names):
        errors.append("production fixture does not exercise all four renderers")

    source = fixture / "source" / "talking.wav"
    derived = fixture / "audio" / "human-vowel-demo.wav"
    production_audio = production / "audio" / "human-vowel-demo.wav"
    if not source.is_file() or digest(source) != provenance.get("sourceSha256"):
        errors.append("public-domain source WAV is absent or hash-mismatched")
    if not derived.is_file() or digest(derived) != provenance.get("derivedSha256"):
        errors.append("derived human WAV is absent or hash-mismatched")
    if not production_audio.is_file() or not derived.is_file() or digest(production_audio) != digest(derived):
        errors.append("production fixture WAV does not match the documented derived WAV")

    if errors:
        for error in errors:
            print(f"[phase12a-contract] ERROR: {error}", file=sys.stderr)
        return 1
    print("[phase12a-contract] productionPipeline=PASS")
    print("[phase12a-contract] exactVoicebankIdentity=PASS")
    print("[phase12a-contract] noOfficialVoicebankClaim=PASS")
    print("[phase12a-contract] allFourRenderers=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
