#!/usr/bin/env python3
"""Verify the CLAP editor's platform sources and packaging entry points.

A source check only. It proves that the embedded editor views, the plug-in
entry, the first-party test host and the packaging scripts still carry their
contract markers, so a build on one platform cannot silently drop another
platform's adapter. It does not build, sign, package or run anything.

Merged on 2026-10-18 from verify_phase11_contracts.py and the unused
verify_phase11_sources.py. The public-domain demo bank provenance checks that
both carried now live in verify_production_pipeline_contract.py.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Repository-relative files this check requires. tests/test_verifier_required_paths.py
# imports this tuple, so a refactor that splits or deletes one of them fails there
# with the exact path rather than here with a generic error.
REQUIRED = (
    "libs/seam-clap-editor/src/plugin_entry.cpp",
    "libs/seam-clap-editor/src/editor_runtime_adapter.cpp",
    "libs/seam-clap-editor/src/editor_runtime_input.cpp",
    "libs/seam-clap-editor/src/editor_runtime_paint.cpp",
    "libs/seam-clap-editor/src/editor_runtime_preview.cpp",
    "libs/seam-clap-editor/src/editor_runtime_project.cpp",
    "libs/seam-clap-editor/src/editor_runtime_state.cpp",
    "libs/seam-clap-editor/src/editor_runtime_internal.hpp",
    "libs/seam-clap-editor/src/embedded_view_x11.cpp",
    "libs/seam-clap-editor/src/embedded_view_win32.cpp",
    "libs/seam-clap-editor/src/embedded_view_appkit.mm",
    "apps/seam-clap-editor-host/main.cpp",
    "apps/seam-clap-editor-host/platform_host_appkit.mm",
    "apps/seam-clap-editor-host/platform_host_win32.cpp",
    "apps/seam-clap-editor-host/platform_host_x11.cpp",
    "packaging/macos/ProjectSEAMEditor-Info.plist",
    "scripts/package_macos_clap.sh",
    "scripts/sign_notarize_macos.sh",
    "scripts/package_macos_installer.sh",
    "scripts/package_windows_plugin.ps1",
    "scripts/sign_windows_plugin.ps1",
    "scripts/run_clap_validator.sh",
    "scripts/build_clap_wrappers.sh",
)

# Markers each file must keep: an API the adapter must call or a contract symbol.
MARKERS: dict[str, tuple[str, ...]] = {
    "libs/seam-clap-editor/src/plugin_entry.cpp": (
        "CLAP_EXT_GUI", "CLAP_EXT_NOTE_PORTS", "CLAP_EVENT_NOTE_ON", "acquireRenderedPreview"),
    "libs/seam-clap-editor/src/embedded_view_win32.cpp": (
        "CreateWindowExW", "WS_CHILD", "EDIT", "SetWindowTextW", "GetWindowTextW"),
    "libs/seam-clap-editor/src/embedded_view_appkit.mm": (
        "NSView", "addSubview", "NSTextField", "controlTextDidChange"),
    "libs/seam-clap-editor/src/embedded_view_x11.cpp": (
        "XCreateSimpleWindow", "XMapWindow", "XNextEvent"),
    "apps/seam-clap-editor-host/main.cpp": (
        "platform_host.hpp", "HostWindow", "LoadLibraryW", "dlopen"),
    "apps/seam-clap-editor-host/platform_host_appkit.mm": (
        "NSApplication", "CLAP_WINDOW_API_COCOA", "makeKeyAndOrderFront"),
    "apps/seam-clap-editor-host/platform_host_win32.cpp": (
        "CreateWindowExW", "CLAP_WINDOW_API_WIN32", "GetDIBits"),
    "apps/seam-clap-editor-host/platform_host_x11.cpp": (
        "XOpenDisplay", "CLAP_WINDOW_API_X11", "XGetImage"),
    "scripts/sign_notarize_macos.sh": ("codesign", "notarytool", "stapler"),
    "scripts/build_clap_wrappers.sh": (
        "CLAP_WRAPPER_ROOT", "VST3_SDK_ROOT", "CLAP_WRAPPER_BUILD_AUV2"),
}


def verify(root: Path) -> list[str]:
    errors: list[str] = []
    for relative in REQUIRED:
        if not (root / relative).is_file():
            errors.append(f"missing: {relative}")
    for relative, markers in MARKERS.items():
        path = root / relative
        if not path.is_file():
            continue  # already reported through REQUIRED
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            errors.append(f"unreadable: {relative}: {error}")
            continue
        for marker in markers:
            if marker not in text:
                errors.append(f"{relative}: missing contract marker {marker!r}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify the CLAP editor platform sources")
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    errors = verify(args.root.resolve())
    if errors:
        for error in errors:
            print(f"[clap-editor-sources] ERROR: {error}", file=sys.stderr)
        return 1
    print("[clap-editor-sources] embeddedViews=PASS")
    print("[clap-editor-sources] pluginEntry=PASS")
    print("[clap-editor-sources] testHost=PASS")
    print("[clap-editor-sources] packagingScripts=present")
    print("[clap-editor-sources] packagingExecution=NOT_CHECKED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
