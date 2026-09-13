"""Build the static Protobuf/Abseil closure the shipped neural helper needs.

The packaging intake requires a helper whose runtime resolves from the directory
it is launched out of. Homebrew publishes protobuf and abseil as shared libraries
only, so a development worker links 80 absolute Homebrew paths (78 abseil, 2
protobuf) that cannot exist on a user's machine. This tool builds the pinned
Protobuf release with its pinned Abseil as static archives, verifies that the
install tree really is static, and links a probe against it so the repository's own
closure reader can prove the closure closes.

It downloads the pinned release tarballs when they are not already present, checks
their SHA-256 digests before extraction, and refuses a source tree whose declared
versions differ from the pins. It does not qualify the archive for distribution, it
does not relink the worker, and it does not change any release gate: a separate
distribution build must still link the worker against this archive and the pinned
static OpenSSL before a payload can be assembled.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True, slots=True)
class StaticProtobufSdk:
    """The pinned static closure and its build-time code generator."""

    prefix: Path
    protoc: Path
    probe: Path


PROTOBUF_VERSION = "33.4"
PROTOBUF_TARBALL = f"protobuf-{PROTOBUF_VERSION}.tar.gz"
PROTOBUF_URL = (
    "https://github.com/protocolbuffers/protobuf/releases/download/"
    f"v{PROTOBUF_VERSION}/{PROTOBUF_TARBALL}"
)
PROTOBUF_SHA256 = "bc670a4e34992c175137ddda24e76562bb928f849d712a0e3c2fb2e19249bea1"
ABSEIL_VERSION = "20250512.1"
ABSEIL_TARBALL = f"abseil-cpp-{ABSEIL_VERSION}.tar.gz"
ABSEIL_URL = (
    "https://github.com/abseil/abseil-cpp/releases/download/"
    f"{ABSEIL_VERSION}/{ABSEIL_TARBALL}"
)
ABSEIL_SHA256 = "9b7a064305e9fd94d124ffa6cc358592eb42b5da588fb4e07d09254aa40086db"
# The code generator is a build-time tool and is not part of the shipped closure.
# Pinning it by release asset keeps the generated ONNX sources matched to the
# static runtime the helper links; only this host pin is currently recorded.
PROTOC_ASSETS = {
    ("darwin", "arm64"): (
        "protoc-33.4-osx-aarch_64.zip",
        "https://github.com/protocolbuffers/protobuf/releases/download/v33.4/protoc-33.4-osx-aarch_64.zip",
        "726297dcfed58592fd35620a5a6246ae020c39e88f3fd4cb1827df7bcf3dfcf1",
    ),
}
PROBE_CMAKE = """cmake_minimum_required(VERSION 3.24)
project(seam_static_protobuf_probe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Protobuf CONFIG REQUIRED)
add_executable(seam_static_protobuf_probe \"{probe}\")
target_link_libraries(seam_static_protobuf_probe PRIVATE protobuf::libprotobuf)
set_target_properties(seam_static_protobuf_probe PROPERTIES
  BUILD_RPATH "" INSTALL_RPATH "")
"""

# The host package prefix that previously leaked shared Abseil and zlib into the
# closure. Ignoring it makes a missing pin a build error instead of a silent
# dependency on whatever the development machine happens to have installed.
HOST_PREFIX_TO_IGNORE = "/opt/homebrew"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def fetch(destination: Path, url: str, expected: str) -> Path:
    """Fetch a pinned release tarball, or verify the existing one."""
    if destination.is_file():
        actual = sha256_file(destination)
        if actual != expected:
            raise SystemExit(
                f"{destination} has digest {actual}, expected {expected}; remove it and retry"
            )
        return destination
    print(f"fetching {url}")
    with urllib.request.urlopen(url, timeout=120) as response:  # noqa: S310 - pinned https URL
        payload = response.read()
    actual = hashlib.sha256(payload).hexdigest()
    if actual != expected:
        raise SystemExit(f"{url} has digest {actual}, expected {expected}")
    destination.write_bytes(payload)
    return destination


def fetch_protoc(source_dir: Path) -> Path:
    """Fetch and verify the pinned build-time code generator for this host."""
    import platform  # noqa: PLC0415 - host identity is only needed here

    asset = PROTOC_ASSETS.get((sys.platform, platform.machine().casefold()))
    if asset is None:
        raise SystemExit(
            "no pinned protoc release asset is recorded for "
            f"{sys.platform}/{platform.machine()}; add one before building here"
        )
    name, url, digest = asset
    archive = fetch(source_dir / name, url, digest)
    target = source_dir / f"protoc-{PROTOBUF_VERSION}"
    executable = target / "bin/protoc"
    if not executable.is_file():
        target.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(archive) as bundle:
            for member in bundle.namelist():
                if member.startswith("/") or ".." in Path(member).parts:
                    raise SystemExit(f"{archive} contains an unsafe path: {member}")
            bundle.extractall(target)
        executable.chmod(0o755)
    if not executable.is_file():
        raise SystemExit(f"{archive} did not contain bin/protoc")
    reported = subprocess.run([str(executable), "--version"], capture_output=True, text=True, timeout=30)
    if reported.returncode or f" {PROTOBUF_VERSION}" not in reported.stdout:
        raise SystemExit(f"pinned protoc reports {reported.stdout.strip()!r}, expected {PROTOBUF_VERSION}")
    return executable


def extract(archive: Path, destination: Path, expected_root: str) -> Path:
    """Extract one archive whose top-level directory must match the pin."""
    root = destination / expected_root
    if root.is_dir():
        return root
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as bundle:
        for member in bundle.getmembers():
            if member.name.startswith("/") or ".." in Path(member.name).parts:
                raise SystemExit(f"{archive} contains an unsafe path: {member.name}")
            if member.issym() or member.islnk():
                raise SystemExit(f"{archive} contains a link: {member.name}")
        bundle.extractall(destination)
    if not root.is_dir():
        raise SystemExit(f"{archive} did not contain {expected_root}")
    return root


def verify_source(source: Path) -> None:
    """Refuse a source tree whose declared versions differ from the pins."""
    version_file = source / "version.json"
    if not version_file.is_file():
        raise SystemExit(f"{source} has no version.json")
    declared = json.loads(version_file.read_text(encoding="utf-8"))
    version = declared.get(PROTOBUF_VERSION.split(".")[0] + ".x", {}).get("protoc_version")
    if version != PROTOBUF_VERSION:
        raise SystemExit(f"{source} declares protoc {version}, expected {PROTOBUF_VERSION}")
    cmake = source / "third_party/abseil-cpp/CMakeLists.txt"
    if not cmake.is_file():
        raise SystemExit(
            f"{source} has no third_party/abseil-cpp; the static closure must vendor "
            f"Abseil {ABSEIL_VERSION} rather than resolve it from the host"
        )
    header = cmake.read_text(encoding="utf-8", errors="replace")[:2048]
    if ABSEIL_VERSION.split(".")[0] not in header:
        raise SystemExit(f"third_party/abseil-cpp does not declare {ABSEIL_VERSION}")


def run(command: list[str], *, cwd: Path | None = None) -> None:
    print(" ".join(command))
    result = subprocess.run(command, cwd=cwd)
    if result.returncode:
        raise SystemExit(result.returncode)


def parse_std_options(header: str) -> dict[str, str]:
    """Return the ABSL_OPTION_USE_STD_* pins declared by an Abseil options.h."""
    return {
        feature: value
        for feature, value in re.findall(
            r"^#define ABSL_OPTION_USE_STD_([A-Z_]+) (\d+)$", header, re.MULTILINE
        )
    }


def verify_static_install(prefix: Path) -> None:
    """Prove the install tree is static and complete for a C++ consumer."""
    # A single shared library anywhere in this prefix would be found by a
    # consumer's find_package and would put the helper straight back to linking an
    # absolute path, so the whole tree is checked rather than just Protobuf.
    shared = sorted(
        path.name
        for path in (prefix / "lib").rglob("*")
        if path.is_file() and (path.suffix == ".dylib" or path.suffix == ".so" or ".so." in path.name)
    )
    if shared:
        raise SystemExit(f"static Protobuf install contains shared libraries: {shared}")
    libraries = sorted((prefix / "lib").glob("libprotobuf*"))
    if not any(path.name in {"libprotobuf.a", "libprotobuf.lib"} for path in libraries):
        raise SystemExit("static Protobuf install has no Protobuf archive")
    if not (prefix / "lib/cmake/protobuf/protobuf-config.cmake").is_file():
        raise SystemExit("static Protobuf install has no CMake package")
    if not (prefix / "lib/cmake/absl").is_dir():
        raise SystemExit("static Protobuf install has no Abseil CMake package")
    # Abseil installs a *different* options.h than the one it compiles with: the
    # source header leaves ABSL_OPTION_USE_STD_* at 2 and the install step pins
    # each feature to the ABI the archives were built with. If the pinned header
    # says 0 for a feature the archives aliased, every consumer of this prefix
    # compiles against a different absl::string_view (and friends) than the
    # archives define, and the link fails with unresolved absl symbols that the
    # archive demonstrably contains. That must never ship as a static SDK, so it
    # is checked here rather than discovered by a user's first link.
    options_header = prefix / "include/absl/base/options.h"
    if not options_header.is_file():
        raise SystemExit("static Abseil install has no pinned absl/base/options.h")
    pinned = parse_std_options(options_header.read_text(encoding="utf-8", errors="replace"))
    # Abseil 20250512.1 declares two pin-able features. STRING_VIEW is a std alias
    # from C++17, which is the standard this tool configures, so its archives and
    # the pinned header must both alias std::string_view. ORDERING needs C++20, so
    # the archives at C++17 used Abseil's own type and 0 is the matching pin.
    # A different feature set means the pinned Abseil version changed and this
    # table must be revisited rather than silently skipped.
    expected = {"STRING_VIEW": "1", "ORDERING": "0"}
    if pinned != expected:
        raise SystemExit(
            f"Abseil pinned {pinned} but the {ABSEIL_VERSION} archives built at "
            f"C++17 ABI require {expected}; the compiled ABI and the installed "
            "header disagree, so every consumer would fail to link "
            "absl::string_view and friends"
        )


def build_abseil(source: Path, build: Path, prefix: Path, jobs: int) -> None:
    """Build and install Abseil as static archives into the same prefix.

    Abseil is built first and its install prefix is the only one Protobuf may
    search, because ``find_package(absl CONFIG)`` otherwise finds the host's
    shared Abseil and the resulting Protobuf stops being self-contained.

    The explicit C++ standard is not cosmetic. Abseil installs an ABI-pinned
    ``absl/base/options.h`` derived from the C++ standard known at configure time,
    while its own sources are compiled from the unpinned header. Leaving the
    standard to the compiler default pins the header to a *different* ABI than
    the archives were built with, and every consumer then fails to link against
    ``absl::string_view`` and its relatives. Both the standard and the resulting
    pin are therefore fixed here and re-checked after install.
    """
    abseil_build = build / "abseil"
    run(["cmake", "-S", str(source / "third_party/abseil-cpp"), "-B", str(abseil_build), "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release",
         "-DCMAKE_CXX_STANDARD=17",
         "-DCMAKE_CXX_STANDARD_REQUIRED=ON",
         "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
         f"-DCMAKE_INSTALL_PREFIX={prefix}",
         "-DBUILD_SHARED_LIBS=OFF",
         "-DABSL_ENABLE_INSTALL=ON",
         "-DABSL_PROPAGATE_CXX_STD=ON",
         "-DABSL_BUILD_TESTING=OFF",
         "-DABSL_FIND_GOOGLETEST=OFF",
         f"-DCMAKE_IGNORE_PREFIX_PATH={HOST_PREFIX_TO_IGNORE}"])
    run(["cmake", "--build", str(abseil_build), "--target", "install", "--parallel", str(jobs)])


def build_probe(build: Path, prefix: Path) -> Path:
    """Link the linkage probe against the static archive and return its path."""
    probe_source = Path(__file__).resolve().parent / "static_protobuf_probe.cpp"
    if not probe_source.is_file():
        raise SystemExit(f"linkage probe source is missing: {probe_source}")
    probe_build = build / "probe"
    probe_build.mkdir(parents=True, exist_ok=True)
    (probe_build / "CMakeLists.txt").write_text(
        PROBE_CMAKE.format(probe=probe_source.as_posix()), encoding="utf-8"
    )
    run(["cmake", "-S", str(probe_build), "-B", str(probe_build / "build"), "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_PREFIX_PATH={prefix}"])
    run(["cmake", "--build", str(probe_build / "build"), "--parallel", "4"])
    return probe_build / "build/seam_static_protobuf_probe"


def report_closure(probe: Path) -> None:
    """Read the probe's own linkage with the packaging closure reader."""
    root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(root))
    from tools.phase13a.runtime_closure import derive_runtime_closure  # noqa: PLC0415

    closure = derive_runtime_closure(probe, ())
    if closure.unresolved:
        for entry in closure.unresolved:
            print(f"  unresolved: {entry}")
        raise SystemExit(
            "the statically linked probe still has unresolved runtime references"
        )
    print(
        "static closure verified: the probe resolves every non-system reference "
        f"from beside itself ({len(closure.entries)} package entries)"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path,
                        help="Directory holding the pinned tarballs and extracted sources")
    parser.add_argument("--build-dir", type=Path,
                        help="New build directory with an existing parent")
    parser.add_argument("--probe-build-dir", type=Path,
                        help="Probe build directory used by --probe-only")
    parser.add_argument("--prefix", type=Path, required=True,
                        help="New install prefix that distribution builds point at")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--probe-only", action="store_true",
                        help="Skip the build; only re-verify the existing install")
    arguments = parser.parse_args(argv)
    if not 1 <= arguments.jobs <= 12:
        parser.error("--jobs must be between 1 and 12")
    if not arguments.probe_only and arguments.source_dir is None:
        parser.error("--source-dir is required unless --probe-only is used")
    sdk = prepare_static_protobuf(
        arguments.prefix,
        source_dir=arguments.source_dir,
        build_dir=arguments.build_dir or arguments.probe_build_dir,
        jobs=arguments.jobs,
        build=not arguments.probe_only,
    )
    if arguments.probe_only:
        print(
            f"prefix {sdk.prefix} is static, ABI-consistent and closed"
        )
    else:
        print(
            "A distribution build must now link the worker with "
            f"-DSEAM_STATIC_PROTOBUF_ROOT={sdk.prefix} -DSEAM_PROTOC_EXECUTABLE={sdk.protoc} "
            "and the pinned static OpenSSL, then "
            "stage the payload; qualification of the archive itself follows separately."
        )
    return 0


def prepare_static_protobuf(
    prefix: Path,
    *,
    source_dir: Path | None = None,
    build_dir: Path | None = None,
    jobs: int = 8,
    build: bool = True,
) -> StaticProtobufSdk:
    """Fetch the pins, build the static closure, and prove it closes.

    This is the single owner of the Protobuf/Abseil pins and of the C++ standard
    the closure is compiled at. The distribution driver calls it so a payload
    build and a developer's manual run cannot disagree about either.
    """
    if not 1 <= jobs <= 12:
        raise SystemExit("--jobs must be between 1 and 12")
    prefix = prefix.absolute()
    if not build:
        # Verification-only mode is hermetic: it proves the *installed* prefix is
        # static, ABI-consistent and closed without needing the pinned tarballs,
        # the network or a code generator. That is what a build that already
        # points at the SDK should re-check on every run.
        if build_dir is None:
            raise SystemExit("--probe-only needs --probe-build-dir for the probe build")
        if not prefix.is_dir():
            raise SystemExit(f"static Protobuf prefix does not exist: {prefix}")
        verify_static_install(prefix)
        probe = build_probe(build_dir.absolute(), prefix)
        run([str(probe)])
        report_closure(probe)
        return StaticProtobufSdk(prefix=prefix, protoc=Path(), probe=probe)
    if source_dir is None or build_dir is None:
        raise SystemExit("building the static closure needs --source-dir and --build-dir")
    source_dir = source_dir.absolute()
    build_root = build_dir.absolute()
    source_dir.mkdir(parents=True, exist_ok=True)
    protobuf_archive = fetch(source_dir / PROTOBUF_TARBALL, PROTOBUF_URL, PROTOBUF_SHA256)
    abseil_archive = fetch(source_dir / ABSEIL_TARBALL, ABSEIL_URL, ABSEIL_SHA256)
    protoc = fetch_protoc(source_dir)
    protobuf = extract(protobuf_archive, source_dir, f"protobuf-{PROTOBUF_VERSION}")
    abseil_target = protobuf / "third_party/abseil-cpp"
    if not abseil_target.is_dir():
        extracted = extract(abseil_archive, protobuf / "third_party", f"abseil-cpp-{ABSEIL_VERSION}")
        extracted.rename(abseil_target)
    verify_source(protobuf)
    if build:
        if build_root.is_symlink() or not build_root.parent.is_dir():
            raise SystemExit("Select a build directory with an existing parent and no link")
        if build_root.exists():
            # Re-running must be safe, so an existing build is only reused when it
            # is the same Protobuf configuration for the same install prefix.
            cache = build_root / "protobuf/CMakeCache.txt"
            if not cache.is_file():
                raise SystemExit(f"{build_root} exists but is not a Protobuf build directory")
            if f"CMAKE_INSTALL_PREFIX:PATH={prefix}" not in cache.read_text(encoding="utf-8", errors="replace"):
                raise SystemExit(f"{build_root} was configured for a different install prefix")
        build_abseil(protobuf, build_root, prefix, jobs)
        # The host prefix is ignored so no Homebrew package can satisfy a
        # dependency: every archive in this closure must come from the pins.
        run(["cmake", "-S", str(protobuf), "-B", str(build_root / "protobuf"), "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Release",
             "-DCMAKE_CXX_STANDARD=17",
             "-DCMAKE_CXX_STANDARD_REQUIRED=ON",
             "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
             f"-DCMAKE_INSTALL_PREFIX={prefix}",
             f"-DCMAKE_PREFIX_PATH={prefix}",
             f"-DCMAKE_IGNORE_PREFIX_PATH={HOST_PREFIX_TO_IGNORE}",
             "-DBUILD_SHARED_LIBS=OFF",
             "-Dprotobuf_BUILD_SHARED_LIBS=OFF",
             "-Dprotobuf_BUILD_TESTS=OFF",
             "-Dprotobuf_BUILD_CONFORMANCE=OFF",
             "-Dprotobuf_BUILD_EXAMPLES=OFF",
             # The static SDK ships the runtime closure only. The compiler binary
             # is the pinned build-time tool fetched above; libprotoc, protoc and
             # the upb generators are all excluded because the runtime archives do
             # not reference upb (verified below) and the generator binaries do
             # not link against static Abseil on macOS.
             "-Dprotobuf_BUILD_PROTOC_BINARIES=OFF",
             "-Dprotobuf_BUILD_LIBPROTOC=OFF",
             "-Dprotobuf_BUILD_LIBUPB=OFF",
             "-Dprotobuf_WITH_ZLIB=OFF",
             "-Dprotobuf_ABSL_PROVIDER=package",
             "-DABSL_PROPAGATE_CXX_STD=ON"])
        run(["cmake", "--build", str(build_root / "protobuf"), "--target", "install", "--parallel", str(jobs)])
    verify_static_install(prefix)
    probe = build_probe(build_root, prefix)
    run([str(probe)])
    report_closure(probe)
    return StaticProtobufSdk(prefix=prefix, protoc=protoc, probe=probe)


if __name__ == "__main__":
    raise SystemExit(main())
