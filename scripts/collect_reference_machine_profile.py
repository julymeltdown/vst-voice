"""Collect a typed reference-machine profile on the host that measures it.

The full-product evaluation profile requires two machines, one per declared
platform, and their values feed four empirical criteria: reference-machines
itself plus the machineProfile binding on generation-budgets, neural-budgets,
resource-limits, cancellation-budgets and reproducibility-tolerances. The
validator has always required a specific seven-field shape:

    cpuModel, logicalCpuCount, ramBytes, osId, osVersion, toolchainId, profile

Every one of those was only ever produced by a test fixture. Nothing collected
them from a real host, so every one of those criteria was unreachable by
construction rather than merely unmeasured. This writes the missing producer.

What this does NOT do, stated plainly because the surrounding tooling is strict
about it: this records hardware and toolchain identity only. It measures no
latency, no memory and no error tolerance, and it decides nothing. A machine
profile makes a measurement interpretable; it is not itself a result, and
resolving an empirical criterion still requires the measurements that reference
this file. It is also not an approval: the caller's identity and the host's are
recorded as declared facts, exactly as the validator treats them.

Two deliberate refusals:

- The platform is not inferred from uname. The contract's platform identifiers
  are macos-arm64 and windows-x86_64; guessing them from an OS string would let a
  mistyped or mislabelled run bind evidence to the wrong cell.
- The toolchain identity is not guessed either. It is read from the compiler the
  caller names, and any failure there is an error rather than a placeholder,
  because a profile with a fabricated toolchain is worse than no profile.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# The contract's own platform identifiers. Not derived from the OS name: see the
# module docstring.
PLATFORM_CELLS = {
    "macos-arm64": {"osId": "macos", "machine": "arm64"},
    "windows-x86_64": {"osId": "windows", "machine": "amd64"},
}

HEX64 = re.compile(r"^[0-9a-f]{64}$")
MAXIMUM_TOOLCHAIN_OUTPUT_BYTES = 64 * 1024


def canonical_json(value: object) -> str:
    """The repository's canonical form, matched to release_gate.sha256_json."""
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)


def sha256_json(value: object) -> str:
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def _bounded_toolchain_version(compiler: str) -> str:
    """First line of the compiler's version output, bounded and single-line.

    A version banner is a fact about the toolchain; if the compiler cannot be
    run, that is an error. Substituting a placeholder would produce a profile
    that looks complete and describes nothing.
    """
    resolved = shutil.which(compiler)
    if resolved is None:
        raise ValueError(f"toolchain executable is unavailable: {compiler}")
    completed = subprocess.run(
        [resolved, "--version"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=30,
        check=False,
    )
    if completed.returncode != 0:
        raise ValueError(f"toolchain version query failed: {compiler}")
    output = completed.stdout[:MAXIMUM_TOOLCHAIN_OUTPUT_BYTES].decode("utf-8", "replace")
    first = output.splitlines()[0].strip() if output.strip() else ""
    if not first:
        raise ValueError(f"toolchain version output was empty: {compiler}")
    return first


def _cpu_model() -> str:
    """The marketing name of the CPU, not its architecture.

    platform.processor() answers 'arm' on an Apple silicon host and 'x86_64' on
    Intel, which identifies an instruction set rather than a model. Two different
    machines would then produce the same cpuModel and the profile could not
    distinguish them, which is the one thing a reference machine has to do.
    """
    if sys.platform == "darwin":
        completed = subprocess.run(
            ["sysctl", "-n", "machdep.cpu.brand_string"],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, timeout=30, check=False)
        if completed.returncode == 0:
            name = completed.stdout[:MAXIMUM_TOOLCHAIN_OUTPUT_BYTES].decode("utf-8", "replace").strip()
            if name:
                return name
    elif Path("/proc/cpuinfo").is_file():
        for line in Path("/proc/cpuinfo").read_text(errors="replace").splitlines():
            if line.lower().startswith("model name"):
                _, _, value = line.partition(":")
                if value.strip():
                    return value.strip()
    # Fall back to the architecture, which is weaker but not fabricated.
    return platform.processor().strip() or platform.machine().strip()


def _total_ram_bytes() -> int:
    """Physical memory, from the OS rather than an environment variable.

    /proc/meminfo is authoritative on Linux. sysconf answers on macOS and the
    BSDs in pages, which is what SC_PHYS_PAGES documents. Windows has neither:
    os.sysconf does not exist there, and the first version of this function
    called it unconditionally, which took down the Windows leg of the contract
    suite the moment this collector landed. Each platform is now asked in the
    way that platform answers.

    No API here reports installed-usable rather than installed-total memory on
    macOS, so this is total physical RAM, which is what a reference machine
    records. Every branch returns bytes.
    """
    meminfo = Path("/proc/meminfo")
    if meminfo.is_file():
        for line in meminfo.read_text().splitlines():
            if line.startswith("MemTotal:"):
                fields = line.split()
                if len(fields) >= 2 and fields[1].isdigit():
                    return int(fields[1]) * 1024
        raise ValueError("MemTotal is absent from /proc/meminfo")
    if hasattr(os, "sysconf"):
        return os_sysconf("SC_PAGE_SIZE") * os_sysconf("SC_PHYS_PAGES")
    if sys.platform == "win32":
        return _windows_total_ram_bytes()
    raise ValueError("host does not expose a total-memory interface")


def _windows_total_ram_bytes() -> int:
    """Installed memory on Windows, via the documented kernel query.

    GlobalMemoryStatusEx reports ullTotalPhys in bytes through a MEMORYSTATUSEX
    structure. It is called through ctypes rather than read from an environment
    variable, because a profile has to describe the machine and not its setup.
    """
    import ctypes

    class MemoryStatusEx(ctypes.Structure):
        _fields_ = [
            ("dwLength", ctypes.c_ulong),
            ("dwMemoryLoad", ctypes.c_ulong),
            ("ullTotalPhys", ctypes.c_ulonglong),
            ("ullAvailPhys", ctypes.c_ulonglong),
            ("ullTotalPageFile", ctypes.c_ulonglong),
            ("ullAvailPageFile", ctypes.c_ulonglong),
            ("ullTotalVirtual", ctypes.c_ulonglong),
            ("ullAvailVirtual", ctypes.c_ulonglong),
            ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
        ]

    status = MemoryStatusEx()
    status.dwLength = ctypes.sizeof(MemoryStatusEx)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
        raise ValueError("GlobalMemoryStatusEx failed on this host")
    total = int(status.ullTotalPhys)
    if total <= 0:
        raise ValueError("GlobalMemoryStatusEx reported no physical memory")
    return total


def os_sysconf(name: str) -> int:
    value = os.sysconf(name)
    if not isinstance(value, int) or value <= 0:
        raise ValueError(f"sysconf({name}) did not report a positive value")
    return value


def collect(platform_cell: str, compiler: str, actor: str) -> dict:
    expected = PLATFORM_CELLS.get(platform_cell)
    if expected is None:
        raise ValueError(
            "platform must be one of " + ", ".join(sorted(PLATFORM_CELLS)))
    machine = platform.machine().lower()
    if machine != expected["machine"]:
        raise ValueError(
            f"host machine {machine!r} does not match platform cell {platform_cell!r}")
    if not actor.strip():
        raise ValueError("collector identity must not be empty")
    cpu_model = _cpu_model()
    if not cpu_model:
        raise ValueError("host did not report a CPU model")
    profile_body = {
        "cpuModel": cpu_model,
        "logicalCpuCount": _logical_cpu_count(),
        "ramBytes": _total_ram_bytes(),
        "osId": expected["osId"],
        "osVersion": platform.release().strip(),
        "toolchainId": _bounded_toolchain_version(compiler),
    }
    if not all(isinstance(profile_body[key], (str, int)) and profile_body[key] != ""
               for key in ("cpuModel", "osId", "osVersion", "toolchainId")):
        raise ValueError("host identity fields must all be non-empty")
    if not profile_body["osVersion"]:
        raise ValueError("host did not report an OS version")
    return {
        "formatId": "com.project-seam.reference-machine-profile",
        "schemaVersion": 1,
        "platform": platform_cell,
        "collectedBy": actor,
        "declared": True,
        "measured": False,
        "profile": profile_body,
        "profileSha256": sha256_json(profile_body),
        # A profile is hardware identity, never a result. These stay false so this
        # file cannot be mistaken for a resolved criterion by a later reader.
        "singerQualified": False,
        "releaseEligible": False,
        "trainingAdmitted": False,
    }


def _logical_cpu_count() -> int:
    value = os.cpu_count()
    if value is None or value <= 0:
        raise ValueError("host did not report a logical CPU count")
    return int(value)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Collect a typed reference-machine profile for the full-product evaluation profile")
    parser.add_argument("--platform", required=True, choices=sorted(PLATFORM_CELLS))
    parser.add_argument("--compiler", default="c++", help="Toolchain executable whose version identifies the build")
    parser.add_argument("--actor", required=True, help="Identity collecting this profile; recorded, not verified")
    parser.add_argument("--output", type=Path, required=True, help="Destination; must not already exist")
    args = parser.parse_args(argv)
    try:
        payload = collect(args.platform, args.compiler, args.actor)
    except (OSError, subprocess.SubprocessError, UnicodeError, ValueError) as exc:
        print(f"machine-profile collection failed: {exc}", file=sys.stderr)
        return 2
    if args.output.exists():
        print(f"machine-profile destination already exists: {args.output}", file=sys.stderr)
        return 3
    encoded = json.dumps(payload, indent=2, allow_nan=False) + "\n"
    args.output.write_text(encoded, encoding="utf-8")
    print(f"platform={payload['platform']}")
    print(f"cpuModel={payload['profile']['cpuModel']}")
    print(f"logicalCpuCount={payload['profile']['logicalCpuCount']}")
    print(f"ramBytes={payload['profile']['ramBytes']}")
    print(f"toolchainId={payload['profile']['toolchainId']}")
    print(f"profileSha256={payload['profileSha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
