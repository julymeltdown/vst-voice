"""Prepare the pinned static Protobuf/Abseil SDK a payload build links against.

Homebrew publishes Protobuf and Abseil as shared libraries only, so a development
worker records about eighty absolute ``/opt/homebrew`` paths in its load commands
and the packaging owner correctly refuses to stage it. The payload therefore needs
a closure whose runtime resolves from the directory the helper is launched out of.

The builder in ``tools/neural_runtime/build_static_protobuf.py`` owns the pins, the
C++ standard the closure is compiled at, the ABI-consistency check on the installed
Abseil header and the probe that proves the closure closes. This module is only the
distribution-facing placement decision: where the sources live beside the other
locked Phase 13A checkouts, and where the install prefix and build tree go for a
payload run.
"""

from __future__ import annotations

from pathlib import Path

from tools.neural_runtime.build_static_protobuf import (
    StaticProtobufSdk,
    prepare_static_protobuf,
)

# The payload build already serialises its compiler work; a small job count keeps
# the dependency build from starving it without serialising the SDK for minutes.
DEFAULT_JOBS = 4


def prepare_static_protobuf_sdk(
    dependencies: Path,
    build_root: Path,
    *,
    jobs: int = DEFAULT_JOBS,
    build: bool = True,
) -> StaticProtobufSdk:
    """Build, or re-verify, the static Protobuf/Abseil closure for a payload run."""
    return prepare_static_protobuf(
        build_root.resolve() / "static-protobuf-install",
        source_dir=dependencies.resolve() / "protobuf",
        build_dir=build_root.resolve() / "static-protobuf-build",
        jobs=jobs,
        build=build,
    )
