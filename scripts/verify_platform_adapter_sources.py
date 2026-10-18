#!/usr/bin/env python3
"""Static contract verification for the platform-gated adapter sources.

This does not substitute for building and running on Windows or macOS. It
prevents a build on one platform from silently dropping another platform's
window, text-input, accessibility, audio or helper-process adapter, or renaming
the native composition and audio integration points. Renamed on 2026-10-18 from
verify_phase8_platform_sources.py.
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
    "libs/seam-native-ui/src/accessibility_win32.cpp": (
        "#include <ole2.h>",
        "#include <UIAutomation.h>",
        "public IRawElementProviderSimple",
        "public IRawElementProviderFragment,",
        "IRawElementProviderFragmentRoot",
        "UiaReturnRawElementProvider",
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
    "libs/seam-platform/src/helper_process.cpp": (
        "CreateProcessW",
        "PROC_THREAD_ATTRIBUTE_HANDLE_LIST",
        "AssignProcessToJobObject",
        "JOB_OBJECT_LIMIT_PROCESS_MEMORY",
        "GetProcessMemoryInfo",
        "TerminateJobObject",
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
    "psapi",
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

    if failures:
        for failure in failures:
            print(f"[platform-adapter-sources] ERROR: {failure}", file=sys.stderr)
        return 1
    print("[platform-adapter-sources] Windows Win32/TSF/WASAPI/helper-process source contract=PASS")
    print("[platform-adapter-sources] macOS AppKit/NSTextInputClient/CoreAudio source contract=PASS")
    print("[platform-adapter-sources] githubActions=DEFERRED; runtime verification remains platform-specific")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
