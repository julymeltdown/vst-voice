#!/usr/bin/env python3
"""Static contract verification for platform-gated Phase 8 source files.

This does not substitute for building and running on Windows/macOS. It prevents
Linux-only package verification from silently dropping the platform adapters or
renaming the native composition/audio integration points.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

REQUIRED: dict[str, tuple[str, ...]] = {
    "libs/seam-native-ui/src/native_window_win32.cpp": (
        "CreateWindowExW",
        "ITfThreadMgr",
        "CLSID_TF_ThreadMgr",
        'L"EDIT"',
        "SEAM_NATIVE_WIN32",
    ),
    "libs/seam-platform/src/wasapi_audio_device.cpp": (
        "IAudioClient",
        "IAudioRenderClient",
        "AUDCLNT_STREAMFLAGS_EVENTCALLBACK",
        "SEAM_AUDIO_WASAPI",
    ),
    "libs/seam-platform/src/wasapi_audio_input_device.cpp": (
        "IAudioCaptureClient",
        "GetDefaultAudioEndpoint(eCapture",
        "SEAM_AUDIO_WASAPI",
    ),
    "libs/seam-native-ui/src/native_window_appkit.mm": (
        "NSTextInputClient",
        "setMarkedText",
        "firstRectForCharacterRange",
        "SEAM_NATIVE_APPKIT",
    ),
    "libs/seam-platform/src/coreaudio_audio_device.mm": (
        "kAudioUnitSubType_DefaultOutput",
        "AudioOutputUnitStart",
        "SEAM_AUDIO_COREAUDIO",
    ),
    "libs/seam-platform/src/coreaudio_audio_input_device.mm": (
        "kAudioUnitSubType_HALOutput",
        "AudioUnitRender",
        "SEAM_AUDIO_COREAUDIO",
    ),
}

CMAKE_MARKERS = (
    "libs/seam-native-ui/src/native_window_win32.cpp",
    "libs/seam-native-ui/src/native_window_appkit.mm",
    "libs/seam-platform/src/wasapi_audio_device.cpp",
    "libs/seam-platform/src/wasapi_audio_input_device.cpp",
    "libs/seam-platform/src/coreaudio_audio_device.mm",
    "libs/seam-platform/src/coreaudio_audio_input_device.mm",
    "SEAM_NATIVE_WIN32=1",
    "SEAM_NATIVE_APPKIT=1",
    "SEAM_AUDIO_WASAPI=1",
    "SEAM_AUDIO_COREAUDIO=1",
    "SEAM_RUN_NATIVE_GUI_TESTS",
    "seam_native_editor_platform_smoke",
    "seam_voicebank_studio_platform_smoke",
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    root = args.root.resolve()
    failures: list[str] = []

    for relative, markers in REQUIRED.items():
        path = root / relative
        if not path.is_file():
            failures.append(f"missing platform source: {relative}")
            continue
        text = path.read_text(encoding="utf-8")
        for marker in markers:
            if marker not in text:
                failures.append(f"{relative}: missing contract marker {marker!r}")

    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    for marker in CMAKE_MARKERS:
        if marker not in cmake:
            failures.append(f"CMakeLists.txt: missing platform selection marker {marker!r}")

    workflow = root / ".github/workflows/ci.yml"
    if not workflow.is_file():
        failures.append("missing .github/workflows/ci.yml")
    else:
        workflow_text = workflow.read_text(encoding="utf-8")
        for marker in ("windows-latest", "macos-latest",
                       "SEAM_RUN_NATIVE_GUI_TESTS=ON"):
            if marker not in workflow_text:
                failures.append(f"ci.yml: missing target-host marker {marker!r}")

    if failures:
        for failure in failures:
            print(f"[phase8-platform-source] ERROR: {failure}", file=sys.stderr)
        return 1
    print("[phase8-platform-source] Windows Win32/TSF/WASAPI source contract=PASS")
    print("[phase8-platform-source] macOS AppKit/NSTextInputClient/CoreAudio source contract=PASS")
    print("[phase8-platform-source] runtime verification remains platform-specific")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
