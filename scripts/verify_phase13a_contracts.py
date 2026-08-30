#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.phase13a.documentation_contract import validate_documentation  # noqa: E402

EXPECTED_COMMITS = {
    "clap": "195b42a004144fab0b3cf95e9c067187d15365b7",
    "clap-wrapper": "35f524b771ec09f54c164720bb90f271273b37d3",
    "vst3sdk": "3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96",
    "AudioUnitSDK": "bd98b31feff57a15989fcfab4cd86dc63382b1ac",
    "openssl": "8cf17aaeb4599f8af87fefd810b5b5fee90fe69e",
}


def require_tokens(path: Path, tokens: tuple[str, ...], errors: list[str]) -> None:
    if not path.is_file():
        errors.append(f"missing {path}")
        return
    text = path.read_text(encoding="utf-8")
    for token in tokens:
        if token not in text:
            errors.append(f"{path}: missing {token!r}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Verify Phase 13A source and fail-closed release contracts")
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    errors: list[str] = []
    try:
        lock = json.loads((root / "phase13a" / "dependency-lock.json").read_text(encoding="utf-8"))
        commits = {item["name"]: item["commit"] for item in lock["dependencies"]}
        if commits != EXPECTED_COMMITS:
            errors.append("dependency lock does not match approved exact revisions")
        mirror = json.loads((root / "packaging" / "phase13a" / "dependencies.lock.json").read_text(encoding="utf-8"))
        if mirror != lock:
            errors.append("packaging dependency-lock mirror differs from canonical lock")
    except (OSError, KeyError, json.JSONDecodeError) as exc:
        errors.append(f"dependency lock is invalid: {exc}")

    require_tokens(
        root / ".github" / "workflows" / "phase13a-plugin-formats.yml",
        ("vst3-validator", "auval", EXPECTED_COMMITS["clap-wrapper"], EXPECTED_COMMITS["vst3sdk"], EXPECTED_COMMITS["AudioUnitSDK"], EXPECTED_COMMITS["openssl"]),
        errors,
    )
    require_tokens(
        root / ".github" / "workflows" / "phase13a-commercial-host-validation.yml",
        ("self-hosted", "host_certification.py", "result_record"),
        errors,
    )
    require_tokens(
        root / "packaging" / "windows" / "ProjectSEAM.nsi",
        ("ProjectSEAMEditor.clap", "ProjectSEAMEditor.vst3", "Documentation", "SetCompressor zlib"),
        errors,
    )
    require_tokens(
        root / "scripts" / "sign_macos_plugin_payload.sh",
        ("codesign", "--options runtime", "--timestamp"),
        errors,
    )
    require_tokens(
        root / "scripts" / "notarize_macos_installer.sh",
        ("notarytool", "stapler", "spctl"),
        errors,
    )
    require_tokens(
        root / "scripts" / "sign_windows_payload.ps1",
        ("sign_windows_file.ps1", "WINDOWS_SIGN_CERT_SHA1"),
        errors,
    )
    require_tokens(
        root / "scripts" / "sign_windows_file.ps1",
        ("signtool.exe", "WINDOWS_SIGN_CERT_SHA1", "timestamp.digicert.com"),
        errors,
    )
    require_tokens(
        root / "docs" / "phase13a" / "MANDATORY_VALIDATION_KO.md",
        ("필수", "실제 대상 운영체제", "실제 DAW", "NOT_RUN", "Beta", "Release Candidate", "General Availability"),
        errors,
    )
    matrix_path = root / "docs" / "phase13a" / "mandatory-validation-matrix.json"
    try:
        matrix = json.loads(matrix_path.read_text(encoding="utf-8"))
        required_ids = {
            "linux-vst3-validator", "windows-vst3-validator", "macos-vst3-validator", "macos-auval",
            "windows-authenticode", "macos-notarization", "windows-installer-clean-os", "macos-installer-clean-os",
            "reaper", "bitwig-studio", "cubase", "ableton-live", "studio-one", "fl-studio", "logic-pro", "garageband",
        }
        ids = {item.get("id") for item in matrix.get("targets", [])}
        if not required_ids.issubset(ids):
            errors.append(f"mandatory matrix is missing ids: {sorted(required_ids - ids)}")
        if any(item.get("runtimeResult") == "PASS" for item in matrix.get("targets", [])):
            errors.append("checked-in Phase 13A baseline must not contain fabricated PASS results")
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"mandatory validation matrix is invalid: {exc}")

    require_tokens(
        root / "docs" / "phase13a" / "MANDATORY_FUTURE_VALIDATION_KO.md",
        ("반드시", "실제 대상", "NOT_RUN", "7,200초", "Official Voicebank 01"),
        errors,
    )
    require_tokens(
        root / "scripts" / "build_windows_installer.ps1",
        ("NSIS 3.12", "makensis.exe", "ProjectSEAM.nsi"),
        errors,
    )
    require_tokens(
        root / "packaging" / "windows" / "ProjectSEAM.nsi",
        ("SetCompressor zlib", "ProjectSEAMEditor.resources", "WriteUninstaller"),
        errors,
    )
    require_tokens(
        root / "scripts" / "uninstall_macos_plugins.sh",
        ("ProjectSEAMEditor.clap", "ProjectSEAMEditor.vst3", "ProjectSEAMEditor.component"),
        errors,
    )
    require_tokens(
        root / "scripts" / "sign_windows_installer.ps1",
        ("signtool.exe", "WINDOWS_SIGN_CERT_SHA1", "timestamp.digicert.com"),
        errors,
    )
    require_tokens(
        root / "packaging" / "phase13a" / "wrapper-project" / "CMakeLists.txt",
        ("CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES OFF", "CLAP_WRAPPER_WINDOWS_SINGLE_FILE OFF", "WINDOWS_FOLDER_VST3 TRUE", "SEAM_WRAPPER_CXX_STANDARD 23", "CMAKE_OSX_DEPLOYMENT_TARGET \"12.0\""),
        errors,
    )
    require_tokens(
        root / "scripts" / "build_phase13a_formats.py",
        ("clap-wrapper-vst3-editor-compatibility.patch", "dependencyPatches"),
        errors,
    )
    require_tokens(
        root / "libs" / "seam-clap-editor" / "src" / "plugin_entry.cpp",
        ("static auto* mutex = new std::mutex", "static auto* path = new std::filesystem::path", "entryMutex()"),
        errors,
    )
    for relative, tokens in {
        "tools/phase13a/update_contract.py": ("verify_sealed_handoff", "verify_update_manifest", "ed25519_verify"),
        "tools/phase13a/support_bundle.py": ("ExportSafe", "write_export_bundle", "delete_owned_report"),
        "tools/phase13a/wrapper_preflight.py": ("validate_preflight", "networkDownloads", "windowsFolderVst3"),
        "tools/phase13a/wrapper_state.py": ("canonicalStateSha256", "validate_projected_state", "future or unsupported"),
        "scripts/run_vst3_test_host.py": ("repeatLifecycle", "installed", "test-host-nonzero"),
        "scripts/verify_update_manifest.py": ("sealed installer handoff", "trusted root"),
    }.items():
        require_tokens(root / relative, tokens, errors)
    if not (root / "docs/product/external-beta-sealed-handoff.schema.json").is_file():
        errors.append("missing sealed installer handoff schema")
    documentation_manifest_path = root / "docs/product/external-beta-documentation.json"
    try:
        documentation_manifest = json.loads(documentation_manifest_path.read_text(encoding="utf-8"))
        documentation_errors, _ = validate_documentation(root, documentation_manifest)
        errors.extend(documentation_errors)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        errors.append(f"offline documentation contract is invalid: {exc}")
    if errors:
        for error in errors:
            print("[phase13a-contract] ERROR", error, file=sys.stderr)
        return 1
    print("[phase13a-contract] dependencyPins=PASS")
    print("[phase13a-contract] mandatoryValidationDocs=PASS")
    print("[phase13a-contract] packagingPipelines=PASS")
    print("[phase13a-contract] externalRuntimeResults=NOT_RUN")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
