#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


def verify(root: Path) -> list[str]:
    errors: list[str] = []
    header = (root / "libs/seam-clap-editor/include/seam/clap_editor/editor_runtime.hpp").read_text()
    preview = (root / "libs/seam-clap-editor/src/editor_runtime_preview.cpp").read_text()
    plugin = (root / "libs/seam-clap-editor/src/plugin_entry.cpp").read_text()
    phase_engine = (root / "phase12c/src/live_voice.cpp").read_text()
    cmake = (root / "CMakeLists.txt").read_text()
    if "LiveSampleInstrument" in header or "LiveSampleInstrument" in preview:
        errors.append("legacy LiveSampleInstrument remains in the canonical editor surface")
    if "human_vowel_data.hpp" in header or "human_vowel_data.hpp" in preview or "human_vowel_data.hpp" in plugin:
        errors.append("generated human vowel fixture is referenced by the canonical CLAP path")
    if (root / "libs/seam-clap-editor/generated/human_vowel_data.hpp").exists():
        errors.append("generated human vowel fixture header remains in the release tree")
    if "renderLiveSample()" in plugin:
        errors.append("canonical CLAP process still renders live audio one sample at a time")
    if "liveScratch_" not in plugin:
        errors.append("canonical CLAP process has no activation-time live scratch buffer")
    if "CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI" not in plugin:
        errors.append("canonical CLAP input does not advertise exactly CLAP and MIDI dialects")
    if "libs/seam-live-voice/src/voice_engine.cpp" not in cmake:
        errors.append("root CMake does not own the production live engine source")
    phase_header = (root / "phase12c/include/seam/phase12c/live_voice.hpp").read_text()
    if "kMaxVoices = 32" not in phase_header:
        errors.append("live voice capacity is not fixed at 32")
    if "kMaxEventsPerBlock = 1024" not in phase_header:
        errors.append("live event capacity is not fixed at 1024")
    if "kMaxResourceBytes = 256u * 1024u * 1024u" not in phase_header:
        errors.append("live resource bound is not fixed at 256 MiB")
    if "human_fixture.hpp" in phase_engine:
        errors.append("embedded human fixture is compiled into the production live engine")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    errors = verify(args.root.resolve())
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print("PHASE12C_LIVE_CONTRACT=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
