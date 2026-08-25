#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


REQUIRED_ROOTS = (
    ".github/workflows",
    "apps",
    "assets",
    "cmake",
    "docs",
    "libs",
    "packaging",
    "phase12c",
    "phase13a",
    "scripts",
    "tests",
    "tools",
)
REQUIRED_ROOT_FILES = (
    ".gitignore",
    "CMakeLists.txt",
    "CMakePresets.json",
    "README.md",
    "SBOM.spdx.json",
    "THIRD_PARTY_NOTICES.md",
)
TEXT_SUFFIXES = {
    ".c",
    ".cc",
    ".cmake",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".in",
    ".json",
    ".m",
    ".md",
    ".mm",
    ".nsi",
    ".patch",
    ".plist",
    ".ps1",
    ".py",
    ".rc",
    ".seam",
    ".sh",
    ".txt",
    ".xml",
    ".yaml",
    ".yml",
}
IGNORED_DIRECTORY_NAMES = {".cache", "__pycache__"}
REFERENCE_PATTERN = re.compile(
    r"(?P<path>(?:\.github/workflows|apps|assets|cmake|docs|libs|packaging|"
    r"phase12c|phase13a|scripts|tests|tools)/"
    r"[A-Za-z0-9_./+@-]+\.(?:c|cc|cmake|cpp|cxx|h|hh|hpp|in|json|m|md|mm|"
    r"nsi|patch|plist|ps1|py|rc|seam|sh|txt|xml|yaml|yml))"
    r"(?![A-Za-z0-9_.])"
)
MAXIMUM_TEXT_INPUT_BYTES = 4 * 1024 * 1024


class SourceClosureAuditError(RuntimeError):
    pass


@dataclass(frozen=True, slots=True)
class ClosureIssue:
    path: str
    consumer: str
    problem: str


def _git_index(root: Path) -> set[str]:
    process = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z", "--cached"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if process.returncode != 0:
        diagnostic = process.stderr.decode("utf-8", errors="replace").strip()
        raise SourceClosureAuditError(
            f"unable to read Git index for {root}: {diagnostic}"
        )
    return {
        entry.decode("utf-8", errors="strict")
        for entry in process.stdout.split(b"\0")
        if entry
    }


def _required_root(path: str) -> str | None:
    candidate = PurePosixPath(path)
    for root in REQUIRED_ROOTS:
        base = PurePosixPath(root)
        if candidate == base or base in candidate.parents:
            return root
    return None


def _present_source_files(root: Path) -> set[str]:
    paths: set[str] = set()
    for relative_root in REQUIRED_ROOTS:
        source_root = root / relative_root
        if not source_root.exists():
            continue
        for candidate in source_root.rglob("*"):
            if any(
                component in IGNORED_DIRECTORY_NAMES
                for component in candidate.relative_to(root).parts
            ):
                continue
            if candidate.is_file() or candidate.is_symlink():
                paths.add(candidate.relative_to(root).as_posix())
    return paths


def _read_consumer(path: Path) -> str | None:
    if path.suffix.lower() not in TEXT_SUFFIXES or path.is_symlink():
        return None
    try:
        if path.stat().st_size > MAXIMUM_TEXT_INPUT_BYTES:
            return None
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeError):
        return None


def _requirements(root: Path, indexed: set[str]) -> dict[str, set[str]]:
    requirements: dict[str, set[str]] = {}

    def require(path: str, consumer: str) -> None:
        normalized = PurePosixPath(path).as_posix()
        requirements.setdefault(normalized, set()).add(consumer)

    present = _present_source_files(root)
    for path in sorted(present | indexed):
        required_root = _required_root(path)
        if required_root is not None:
            require(path, f"source-tree policy:{required_root}")
    for path in REQUIRED_ROOT_FILES:
        if path in indexed or (root / path).exists():
            require(path, "repository root contract")

    consumers = sorted(present | set(REQUIRED_ROOT_FILES))
    for consumer in consumers:
        is_cmake = consumer == "CMakeLists.txt" or consumer.endswith(
            ("/CMakeLists.txt", ".cmake")
        )
        is_workflow = consumer.startswith(".github/workflows/")
        if not is_cmake and not is_workflow:
            continue
        text = _read_consumer(root / consumer)
        if text is None:
            continue
        for match in REFERENCE_PATTERN.finditer(text):
            path = match.group("path")
            if is_cmake or path in indexed or (root / path).exists():
                require(path, f"referenced by {consumer}")
    return requirements


def audit_source_closure(root: Path) -> list[ClosureIssue]:
    source_root = root.resolve()
    indexed = _git_index(source_root)
    requirements = _requirements(source_root, indexed)
    issues: list[ClosureIssue] = []
    for path, consumers in sorted(requirements.items()):
        candidate = source_root / path
        consumer = "; ".join(sorted(consumers))
        if not candidate.is_file() and not candidate.is_symlink():
            issues.append(ClosureIssue(path, consumer, "missing"))
        elif path not in indexed:
            issues.append(ClosureIssue(path, consumer, "not indexed"))
    return issues


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    args = parser.parse_args(argv)
    try:
        issues = audit_source_closure(args.root)
    except SourceClosureAuditError as error:
        print(f"SOURCE_CLOSURE=ERROR {error}")
        return 2
    if issues:
        print(f"SOURCE_CLOSURE=FAIL required_inputs={len(issues)}")
        for issue in issues:
            print(f"{issue.problem}: {issue.path} ({issue.consumer})")
        return 1
    print("SOURCE_CLOSURE=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
