from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
from typing import assert_never


class PayloadPlatform(StrEnum):
    MACOS_ARM64 = "macos-arm64"
    WINDOWS_X64 = "windows-x64"


@dataclass(frozen=True, slots=True)
class Surface:
    identifier: str
    relative_path: str
    binary_relative_path: str


def surface_matrix(platform: PayloadPlatform) -> tuple[Surface, ...]:
    match platform:
        case PayloadPlatform.MACOS_ARM64:
            return (
                Surface(
                    "standalone",
                    "Standalone/Project SEAM.app",
                    "Standalone/Project SEAM.app/Contents/MacOS/Project SEAM",
                ),
                Surface(
                    "clap",
                    "CLAP/ProjectSEAMEditor.clap",
                    "CLAP/ProjectSEAMEditor.clap/Contents/MacOS/ProjectSEAMEditor",
                ),
                Surface(
                    "vst3",
                    "VST3/ProjectSEAMEditor.vst3",
                    "VST3/ProjectSEAMEditor.vst3/Contents/MacOS/ProjectSEAMEditor",
                ),
                Surface(
                    "auv2",
                    "AU/ProjectSEAMEditor.component",
                    "AU/ProjectSEAMEditor.component/Contents/MacOS/ProjectSEAMEditor",
                ),
                Surface(
                    "installer-verifier",
                    "Tools/seam_installer_verifier",
                    "Tools/seam_installer_verifier",
                ),
            )
        case PayloadPlatform.WINDOWS_X64:
            return (
                Surface(
                    "standalone",
                    "Standalone/seam_editor_native.exe",
                    "Standalone/seam_editor_native.exe",
                ),
                Surface(
                    "clap",
                    "CLAP/ProjectSEAMEditor.clap",
                    "CLAP/ProjectSEAMEditor.clap",
                ),
                Surface(
                    "vst3",
                    "VST3/ProjectSEAMEditor.vst3",
                    "VST3/ProjectSEAMEditor.vst3/Contents/x86_64-win/ProjectSEAMEditor.vst3",
                ),
                Surface(
                    "installer-verifier",
                    "Tools/seam_installer_verifier.exe",
                    "Tools/seam_installer_verifier.exe",
                ),
            )
        case unreachable:
            assert_never(unreachable)
