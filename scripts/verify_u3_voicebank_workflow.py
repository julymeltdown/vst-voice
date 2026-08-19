#!/usr/bin/env python3
"""Fail-closed source contract for the U3 standalone voicebank workflow."""
from __future__ import annotations

import argparse
from pathlib import Path


def require(path: Path, needle: str) -> None:
    text = path.read_text(encoding="utf-8")
    if needle not in text:
        raise SystemExit(f"U3_CONTRACT=FAIL missing {needle!r} in {path}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()

    required = [
        "libs/seam-authoring-runtime/include/seam/authoring/voicebank_browser.hpp",
        "libs/seam-authoring-runtime/src/voicebank_browser.cpp",
        "libs/seam-authoring-runtime/include/seam/authoring/voicebank_installer_service.hpp",
        "libs/seam-authoring-runtime/src/voicebank_installer_service.cpp",
        "libs/seam-voicebank/include/seam/voicebank/coverage.hpp",
        "libs/seam-voicebank/src/coverage.cpp",
        "tests/test_voicebank_browser.cpp",
        "tests/test_voicebank_installer_service.cpp",
        "tests/test_voicebank_relink.cpp",
        "tests/test_voicebank_coverage.cpp",
        "tests/test_standalone_voicebank_workflow.cpp",
    ]
    missing = [item for item in required if not (root / item).is_file()]
    if missing:
        raise SystemExit("U3_CONTRACT=FAIL missing files: " + ", ".join(missing))

    require(root / required[0], "struct VoicebankCard final")
    require(root / required[1], "VoicebankTrust::TrustedInstalled")
    require(root / required[1], "allowDevelopmentFixtures_")
    require(root / required[2], "ExistingVoicebankDecision")
    require(root / required[3], "requireTrustedSigner = true")
    require(root / required[3], "explicit Replace is required")
    require(root / "libs/seam-authoring-runtime/src/voicebank_session.cpp", "selectTrackExact")
    require(root / "libs/seam-authoring-runtime/src/voicebank_session.cpp", "relinkTrack")
    require(root / "libs/seam-authoring-runtime/src/voicebank_session.cpp", "replaceTrackVoicebank")
    require(root / required[4], "CoverageIssueKind")
    require(root / required[5], "UnsupportedPitchRange")
    require(root / "libs/seam-rendering/src/project_renderer.cpp", "ProjectRenderDiagnostic")
    require(root / "libs/seam-standalone/src/application_controller.cpp", "selectedRegionCoverage")
    require(root / "libs/seam-platform/include/seam/platform/application_menu.hpp", "InstallVoicebank")
    require(root / "libs/seam-platform/src/application_menu_appkit.mm", "Install Voicebank")

    print("U3_CONTRACT=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
