"""Generate moduleinfo.json for VST3 folder packages with the pinned SDK tool.

The VST3 specification places a folder plug-in's metadata at
MyPlugin.vst3/Contents/Resources/moduleinfo.json on every desktop platform.
The SDK's own CMake helper smtg_target_create_module_info_file builds the
moduleinfotool utility and runs it as a post-build step. The clap-wrapper
generates the folder layout itself but never runs that step, so shipped
archives carried no module information at all. This module reproduces the SDK's
own invocation against the pinned checkout instead of hand-writing metadata, so
the content is derived from the real binary's class registry.
"""

from __future__ import annotations

import subprocess
from pathlib import Path


def _run(command, cwd=None):
    print("+", " ".join(map(str, command)))
    subprocess.run(list(map(str, command)), cwd=cwd, check=True)


def build_module_info_tool(sdk_root: Path, build_directory: Path) -> Path:
    _run([
        "cmake", "-S", sdk_root, "-B", build_directory,
        "-DCMAKE_BUILD_TYPE=Release",
        "-DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF",
        "-DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF",
        "-DSMTG_CREATE_PLUGIN_LINK=OFF",
    ])
    _run([
        "cmake", "--build", build_directory,
        "--config", "Release", "--target", "moduleinfotool",
    ])
    candidates = [
        path for path in build_directory.rglob("*")
        if path.is_file() and path.stem == "moduleinfotool"
    ]
    if not candidates:
        raise RuntimeError("moduleinfotool was not produced below " + str(build_directory))
    return sorted(candidates, key=lambda path: (len(path.parts), str(path)))[0]


def create_module_info(tool: Path, bundle: Path, version: str, runtime_output: Path) -> Path:
    if not bundle.is_dir():
        raise RuntimeError("folder VST3 package does not exist: " + str(bundle))
    destination = bundle / "Contents" / "Resources" / "moduleinfo.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        destination.unlink()
    _run([
        tool, "-create", "-version", version, "-path", bundle,
        "-output", destination,
    ], cwd=runtime_output)
    if not destination.is_file() or destination.stat().st_size == 0:
        raise RuntimeError("moduleinfotool did not write " + str(destination))
    return destination
