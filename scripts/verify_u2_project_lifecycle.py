#!/usr/bin/env python3
"""Static source contract for the U2 native project lifecycle.

This is not a substitute for target-OS runtime tests. It prevents the Linux
build from silently dropping the AppKit/Win32 integrations or moving document
paths into canonical project state.
"""
from __future__ import annotations

import argparse
from pathlib import Path


def require(text: str, token: str, label: str, failures: list[str]) -> None:
    if token not in text:
        failures.append(f"missing {label}: {token}")


def forbid(text: str, token: str, label: str, failures: list[str]) -> None:
    if token in text:
        failures.append(f"forbidden {label}: {token}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    failures: list[str] = []

    lifecycle_hpp = (root / "libs/seam-authoring-runtime/include/seam/authoring/project_lifecycle.hpp").read_text()
    lifecycle_cpp = (root / "libs/seam-authoring-runtime/src/project_lifecycle.cpp").read_text()
    document_hpp = (root / "libs/seam-authoring-runtime/include/seam/authoring/project_document.hpp").read_text()
    app_controller = (root / "libs/seam-standalone/src/application_controller.cpp").read_text()
    native_app = (root / "libs/seam-standalone/src/native_editor_app.cpp").read_text()
    appkit_dialog = (root / "libs/seam-platform/src/file_dialog_appkit.mm").read_text()
    win_dialog = (root / "libs/seam-platform/src/file_dialog_win32.cpp").read_text()
    appkit_menu = (root / "libs/seam-platform/src/application_menu_appkit.mm").read_text()
    x11_window = (root / "libs/seam-native-ui/src/native_window_x11.cpp").read_text()
    win_window = (root / "libs/seam-native-ui/src/native_window_win32.cpp").read_text()
    mac_window = (root / "libs/seam-native-ui/src/native_window_appkit.mm").read_text()
    cmake = (root / "CMakeLists.txt").read_text()

    require(lifecycle_hpp, "class ProjectLifecycleService", "shared lifecycle service", failures)
    require(lifecycle_cpp, "codec_.decode", "canonical project codec decode", failures)
    require(lifecycle_cpp, "codec_.encode", "canonical project codec encode", failures)
    require(lifecycle_cpp, "durableAtomicWriteText", "durable project persistence", failures)
    require(document_hpp, "std::optional<std::filesystem::path> projectPath", "external document path", failures)
    require(document_hpp, "std::optional<std::filesystem::path> autosavePath", "external autosave path", failures)
    require(app_controller, "ApplicationCommand::NewProject", "New command", failures)
    require(app_controller, "ApplicationCommand::OpenProject", "Open command", failures)
    require(app_controller, "ApplicationCommand::SaveProject", "Save command", failures)
    require(app_controller, "ApplicationCommand::SaveProjectAs", "Save As command", failures)
    require(app_controller, "ApplicationCommand::RecoverLatestAutosave", "recovery command", failures)
    require(native_app, "StandaloneApplicationController", "standalone lifecycle adapter", failures)
    require(native_app, "requestClose", "unsaved-close interception", failures)

    require(appkit_dialog, "NSOpenPanel", "AppKit open panel", failures)
    require(appkit_dialog, "NSSavePanel", "AppKit save panel", failures)
    require(appkit_dialog, "allowedContentTypes", "AppKit type filtering", failures)
    forbid(appkit_dialog, "static_cast<NSOpenPanel*>", "invalid Objective-C pointer cast", failures)
    require(win_dialog, "CLSID_FileOpenDialog", "Win32 open dialog", failures)
    require(win_dialog, "CLSID_FileSaveDialog", "Win32 save dialog", failures)

    for token, label in (
        ("New Project", "AppKit New menu"),
        ("Open…", "AppKit Open menu"),
        ("Save As…", "AppKit Save As menu"),
        ("Recover Autosave", "AppKit recovery menu"),
        ("UnsavedDecision::Save", "AppKit Save decision"),
        ("UnsavedDecision::Discard", "AppKit Discard decision"),
        ("UnsavedDecision::Cancel", "AppKit Cancel decision"),
    ):
        require(appkit_menu, token, label, failures)

    for source, label in (
        (x11_window, "X11"),
        (win_window, "Win32"),
        (mac_window, "AppKit"),
    ):
        require(source, "requestClose", f"{label} close interception", failures)

    for token in (
        "libs/seam-platform/src/file_dialog_win32.cpp",
        "libs/seam-platform/src/file_dialog_appkit.mm",
        "libs/seam-platform/src/application_menu_appkit.mm",
        "libs/seam-platform/src/file_dialog_unavailable.cpp",
        "tests/test_project_lifecycle.cpp",
        "tests/test_autosave_service.cpp",
        "tests/test_recent_projects.cpp",
        "tests/test_standalone_project_lifecycle.cpp",
    ):
        require(cmake, token, "CMake U2 source/test registration", failures)

    if failures:
        print("U2_PROJECT_LIFECYCLE_CONTRACT=FAIL")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print("U2_PROJECT_LIFECYCLE_CONTRACT=PASS")
    print("TARGET_OS_RUNTIME_VALIDATION=REQUIRED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
