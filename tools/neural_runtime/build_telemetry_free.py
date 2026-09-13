"""Build a telemetry-free CPU ORT wheel/shared library from an explicit trusted checkout.

This executes upstream build code and downloads its pinned dependencies. It is
not a sandbox or installed-runtime switch; qualification follows separately.
"""
import argparse
from pathlib import Path
import subprocess
import sys

REVISION = "f2c39fe2f838cf35ce7da92824f5a5e3ee6e88a7"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path)
    parser.add_argument("build_directory", type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 8:
        parser.error("--jobs must be between 1 and 8")
    checkout = args.checkout.resolve(strict=True)
    build = args.build_directory.absolute()
    if build.exists() or build.is_symlink() or not build.parent.is_dir():
        parser.error("Select a new build directory with an existing parent")
    def git(*arguments):
        return subprocess.check_output(["git", "-C", str(checkout), *arguments], text=True, timeout=15).strip()
    if git("rev-parse", "HEAD") != REVISION or git("status", "--porcelain"):
        parser.error("Select the clean trusted ONNX Runtime v1.30.0 source revision")
    command = [sys.executable, str(checkout / "tools/ci_build/build.py"), "--update", "--build", "--config", "Release",
               "--build_dir", str(build), "--build_shared_lib", "--build_wheel", "--skip_tests",
               "--parallel", str(args.jobs), "--cmake_generator", "Ninja",
               "--cmake_extra_defines", "onnxruntime_USE_TELEMETRY=OFF", "onnxruntime_BUILD_UNIT_TESTS=OFF",
               # FetchContent otherwise prefers matching Homebrew packages over
               # the source revision's dependency URLs and hashes.
               "FETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER",
               f"Python3_EXECUTABLE={sys.executable}"]
    result = subprocess.run(command, cwd=checkout)
    if result.returncode:
        return result.returncode
    cache = (build / "Release/CMakeCache.txt").read_text()
    if "onnxruntime_USE_TELEMETRY:BOOL=OFF" not in cache:
        raise RuntimeError("Completed build does not prove telemetry compilation was disabled")
    print("Telemetry-free build completed; wheel/shared-library installation and runtime qualification are still required.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
