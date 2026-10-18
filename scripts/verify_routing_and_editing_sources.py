#!/usr/bin/env python3
"""Verify the routing, host-timeline and technical-lane editing source contract.

Source markers prove that the domain routing model, the project codec, the
routed production renderer, the CLAP host-timeline mapping, the embedded
editor's editing and interchange commands, the shared SING shell routing and
the CLAP dynamic-channel and render extensions are still wired. Renamed on
2026-10-18 from verify_phase12b_contracts.py, dropping the checks that only
locked Phase 12B documents, the legacy remaining-tasks backlog, the project
version number and phase-named target names.
"""
from __future__ import annotations

import argparse
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    errors: list[str] = []

    require_text(
        root / "libs/seam-domain/include/seam/domain/project.hpp",
        ["hostStartOffsetTick", "ProjectRouting"],
        errors,
    )
    require_text(
        root / "libs/seam-domain/include/seam/domain/routing.hpp",
        ["struct AudioBus", "struct ProjectRouting", "RoutingMatrix"],
        errors,
    )
    require_text(
        root / "libs/seam-formats/src/project_json.cpp",
        ["ProjectJsonCodec::kSchemaVersion", "hostStartOffsetTick", "encodeRouting"],
        errors,
    )
    require_text(
        root / "libs/seam-rendering/src/project_renderer.cpp",
        ["ProductionProjectRenderer::render", "RoutedPlaybackTimeline", "voicebanks"],
        errors,
    )
    require_text(
        root / "libs/seam-rendering/src/render_snapshot.cpp",
        ["ProductionProjectRenderer", "RoutingMatrix::monoToStereo"],
        errors,
    )
    require_text(
        root / "libs/seam-clap-editor/include/seam/clap_editor/host_timeline.hpp",
        ["HostTimelineMapper", "HostTimelineState", "projectOffsetSeconds"],
        errors,
    )
    require_text(
        root / "libs/seam-clap-editor/include/seam/clap_editor/editor_runtime.hpp",
        [
            "movePhonemeBoundary",
            "selectUnitVariant",
            "cycleUnitVariant",
            "cycleUnitRenderer",
            "upsertPitchPoint",
            "movePitchPoint",
            "removePitchPoint",
            "cyclePitchInterpolation",
            "openSampleMicroscope",
            "configureOutputChannels",
            "setHostStartOffset",
            "selectTrack",
            "selectRegion",
        ],
        errors,
    )
    require_text(
        root / "libs/seam-clap-editor/src/editor_runtime_project.cpp",
        [
            "ConfigureProjectOutputCommand",
            "SetHostStartOffsetCommand",
            "setRenderQuality",
        ],
        errors,
    )
    require_text(
        root / "libs/seam-clap-editor/src/editor_runtime_interchange.cpp",
        [
            # U32 requires the embedded editor to share the standalone interchange boundary, not a
            # second conversion path: a draft import that the host reviews and explicitly accepts,
            # and a create-new export that cannot mutate the live song. These live in their own
            # translation unit so no single adapter file has to exceed the 600-line limit the
            # CLAP authoring-adapter gate enforces; the required token set is unchanged.
            "InterchangeService{}.importFile",
            "InterchangeService{}.exportFile",
            "acceptInterchangeImport",
            "replaceProject(std::move(draft.project))",
        ],
        errors,
    )
    require_text(
        root / "libs/seam-clap-editor/src/editor_runtime_paint.cpp",
        [
            # The microscope view is built by the shared controller, so the CLAP
            # surface paints that shared state through the SING shell (the one
            # editor surface), which presents the microscope as its own sheet.
            "controller_->sceneState()",
            "shell_.paint(canvas, *controller_, state, controller_->playheadTick())",
            "native_ui::paintEditorUnavailable(canvas)",
        ],
        errors,
    )
    require_text(
        root / "libs/seam-clap-editor/src/editor_runtime_input.cpp",
        [
            # Input reaches the shared controller through the same SING shell the
            # standalone app uses, so hit-testing and the details keyboard
            # controls cannot drift between the standalone and embedded surfaces.
            "controller_->sampleMicroscopeOpen()",
            "routeShellPointerLocked(ShellPointerPhase::Down, event)",
            "routeShellPointerLocked(ShellPointerPhase::Move, event)",
            "routeShellPointerLocked(ShellPointerPhase::Up, event)",
        ],
        errors,
    )
    # The shared controller owns the microscope layout and hit-testing that both
    # surfaces depend on.
    require_text(
        root / "libs/seam-native-ui/src/editor_controller.cpp",
        [
            "layout_.microscopeWaveformBounds(logicalWidth_, logicalHeight_)",
            "layout_.microscopeSpectrogramBounds(logicalWidth_, logicalHeight_)",
            "EditorSceneState::SampleMicroscopeView",
        ],
        errors,
    )
    require_text(
        root / "libs/seam-authoring-runtime/src/render_coordinator.cpp",
        [
            "ProductionProjectRenderer",
            "RealtimeProjectAudioPublication",
            "RenderQuality",
        ],
        errors,
    )
    require_text(
        root / "libs/seam-clap-editor/src/plugin_entry.cpp",
        [
            "CLAP_EXT_AUDIO_PORTS_CONFIG",
            "CLAP_EXT_AUDIO_PORTS_CONFIG_INFO",
            "CLAP_EXT_RENDER",
            "desiredOutputChannels_",
            "desiredOutputChannels_",
        ],
        errors,
    )
    require_text(
        root / "libs/seam-clap-editor/src/editor_runtime_paint.cpp",
        ["HostTimelineMapper::map"],
        errors,
    )
    require_text(
        root / "apps/seam-clap-editor-host/main.cpp",
        [
            "CLAP_EXT_AUDIO_PORTS_CONFIG",
            "CLAP_EXT_AUDIO_PORTS_CONFIG_INFO",
            "CLAP_EXT_RENDER",
            "outputChannels",
            "audioPortRescans",
            "offlineRenderAccepted",
        ],
        errors,
    )
    require_text(
        root / "libs/seam-application/include/seam/application/render_commands.hpp",
        [
            "SetVocalTrackMixCommand",
            "SetProjectRoutingCommand",
            "SetHostStartOffsetCommand",
            "ConfigureProjectOutputCommand",
        ],
        errors,
    )
    require_text(
        root / "CMakeLists.txt",
        [
            "libs/seam-rendering/src/project_renderer.cpp",
            "libs/seam-clap-editor/src/host_timeline.cpp",
        ],
        errors,
    )

    if errors:
        for error in errors:
            print(f"[routing-and-editing-sources] ERROR: {error}", file=sys.stderr)
        return 1

    print("[routing-and-editing-sources] technicalLaneEditing=PASS")
    print("[routing-and-editing-sources] hostTimelineAuthority=PASS")
    print("[routing-and-editing-sources] multiTrackRegionRouting=PASS")
    print("[routing-and-editing-sources] clapDynamicChannels=PASS")
    print("[routing-and-editing-sources] projectCodecRouting=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
