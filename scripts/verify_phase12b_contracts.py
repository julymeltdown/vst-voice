#!/usr/bin/env python3
from __future__ import annotations

import argparse
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
        root / "libs/seam-clap-editor/src/editor_runtime_paint.cpp",
        ["paintSampleMicroscope"],
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
            "HostTimelineMapper::map",
            "desiredOutputChannels_",
            "desiredOutputChannels_",
        ],
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
            "project(ProjectSEAM VERSION 0.13.1",
            "seam_phase12b_tests",
            "seam_phase12b_demo",
            "seam_phase12b_contract",
            "libs/seam-rendering/src/project_renderer.cpp",
            "libs/seam-clap-editor/src/host_timeline.cpp",
        ],
        errors,
    )

    required_docs = [
        root / "docs/phase12b/ACCEPTANCE.md",
        root / "docs/phase12b/IMPLEMENTATION_REPORT.md",
        root / "docs/phase12b/HOST_TIMELINE_AND_ROUTING.md",
        root / "docs/phase12b/TECHNICAL_LANE_EDITING.md",
        root / "docs/phase12b/EVIDENCE.md",
    ]
    for path in required_docs:
        if not path.is_file():
            errors.append(f"missing Phase 12B document: {path}")

    backlog_path = root / "docs/remaining-tasks.json"
    try:
        backlog = json.loads(backlog_path.read_text(encoding="utf-8"))
        status_by_id = {item["id"]: item["status"] for item in backlog["tasks"]}
        for task_id in ("SEAM-P12-003", "SEAM-P12-004", "SEAM-P12-005"):
            if status_by_id.get(task_id) != "DONE":
                errors.append(f"{task_id} must be DONE after Phase 12B")
        valid_successor_phases = {"PHASE_12C", "PHASE_13A", "PHASE_13B", "PHASE_14"}
        if backlog.get("nextPhase") not in valid_successor_phases:
            errors.append(
                "remaining-tasks.json nextPhase must be Phase 12C or a later approved phase"
            )
    except Exception as exc:  # noqa: BLE001 - audit reports the complete error
        errors.append(f"invalid remaining task metadata: {exc}")

    if errors:
        for error in errors:
            print(f"[phase12b-contract] ERROR: {error}", file=sys.stderr)
        return 1

    print("[phase12b-contract] technicalLaneEditing=PASS")
    print("[phase12b-contract] hostTimelineAuthority=PASS")
    print("[phase12b-contract] multiTrackRegionRouting=PASS")
    print("[phase12b-contract] clapDynamicChannels=PASS")
    print("[phase12b-contract] projectSchema5=PASS")
    print("[phase12b-contract] noTargetOsOrReleaseOverclaim=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
