#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import TypeAlias, TypedDict

try:
    from scripts.verify_phase12c_evidence import (
        read_json,
        validate_live_summary,
        validate_soak_result,
    )
except ModuleNotFoundError:
    from verify_phase12c_evidence import (
        read_json,
        validate_live_summary,
        validate_soak_result,
    )

JsonValue: TypeAlias = (
    str | int | float | bool | None | list["JsonValue"] | dict[str, "JsonValue"]
)


class ArtifactEntry(TypedDict):
    path: str
    sha256: str


class SoakPacket(TypedDict):
    schemaVersion: int
    recordType: str
    profile: str
    requiredSeconds: int
    elapsedSeconds: int
    result: str
    artifacts: dict[str, ArtifactEntry]


@dataclass(frozen=True, slots=True)
class SoakPacketInputs:
    root: Path
    summary: Path
    audio: Path
    soak: Path
    runner_metadata: Path
    soak_binary: Path


class PacketError(ValueError):
    def __init__(self, message: str) -> None:
        super().__init__(message)
        self.message = message

    def __str__(self) -> str:
        return self.message


REQUIRED_ARTIFACTS = (
    "liveSummary",
    "liveAudio",
    "soak",
    "runnerMetadata",
    "soakBinary",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _artifact_paths(inputs: SoakPacketInputs) -> tuple[tuple[str, Path], ...]:
    return (
        ("liveSummary", inputs.summary),
        ("liveAudio", inputs.audio),
        ("soak", inputs.soak),
        ("runnerMetadata", inputs.runner_metadata),
        ("soakBinary", inputs.soak_binary),
    )


def _rooted(path: Path, root: Path) -> Path:
    return path if path.is_absolute() else root / path


def _relative_file(path: Path, root: Path, label: str) -> str:
    candidate = path if path.is_absolute() else root / path
    if candidate.is_symlink():
        raise PacketError(f"{label} cannot be a symbolic link: {path}")
    resolved_root = root.resolve()
    resolved = candidate.resolve()
    try:
        relative = resolved.relative_to(resolved_root)
    except ValueError as error:
        raise PacketError(f"{label} escapes packet root: {path}") from error
    if not resolved.is_file() or resolved.stat().st_size == 0:
        raise PacketError(f"{label} is missing or empty: {path}")
    return relative.as_posix()


def _entry(path: Path, root: Path, label: str) -> ArtifactEntry:
    relative = _relative_file(path, root, label)
    return {"path": relative, "sha256": sha256((root / relative).resolve())}


def create_packet(inputs: SoakPacketInputs, output: Path) -> SoakPacket:
    root = inputs.root.resolve()
    resolved_inputs = SoakPacketInputs(
        root=root,
        summary=_rooted(inputs.summary, root),
        audio=_rooted(inputs.audio, root),
        soak=_rooted(inputs.soak, root),
        runner_metadata=_rooted(inputs.runner_metadata, root),
        soak_binary=_rooted(inputs.soak_binary, root),
    )
    summary = read_json(resolved_inputs.summary)
    soak = read_json(resolved_inputs.soak)
    summary_errors = validate_live_summary(summary)
    soak_errors = validate_soak_result(soak, True)
    if summary_errors or soak_errors:
        details = summary_errors + soak_errors
        raise PacketError("invalid source evidence: " + "; ".join(details))
    artifacts = {
        key: _entry(path, root, f"record {key}")
        for key, path in _artifact_paths(resolved_inputs)
    }
    packet: SoakPacket = {
        "schemaVersion": 1,
        "recordType": "phase12c-full-soak-packet",
        "profile": "full",
        "requiredSeconds": 7200,
        "elapsedSeconds": soak["elapsedSeconds"],
        "result": "PASS",
        "artifacts": artifacts,
    }
    output_root = output.resolve().parent
    output_root.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(packet, indent=2) + "\n", encoding="utf-8")
    return packet


def _verify_entry(value: JsonValue, key: str, root: Path) -> list[str]:
    if not isinstance(value, dict):
        return [f"record {key} entry is missing"]
    relative = value.get("path")
    expected = value.get("sha256")
    errors: list[str] = []
    if not isinstance(relative, str) or not relative:
        return [f"record {key} path is missing"]
    if not isinstance(expected, str) or len(expected) != 64:
        errors.append(f"record {key} sha256 is missing or malformed")
    candidate = root / relative
    if candidate.is_symlink():
        return errors + [f"record {key} artifact is a symbolic link: {relative}"]
    resolved = candidate.resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError:
        return errors + [f"record {key} path escapes packet root: {relative}"]
    if not resolved.is_file() or resolved.stat().st_size == 0:
        return errors + [f"record {key} artifact is missing or empty: {relative}"]
    if isinstance(expected, str) and len(expected) == 64:
        actual = sha256(resolved)
        if actual != expected:
            errors.append(
                f"record {key} digest mismatch: expected {expected}, got {actual}"
            )
    return errors


def verify_packet(packet_path: Path, root: Path) -> list[str]:
    try:
        packet = read_json(packet_path)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        return [f"unable to read packet: {error}"]
    errors: list[str] = []
    if packet.get("schemaVersion") != 1:
        errors.append("packet schemaVersion must equal 1")
    if packet.get("recordType") != "phase12c-full-soak-packet":
        errors.append("packet recordType is invalid")
    if packet.get("profile") != "full":
        errors.append("packet profile must be full")
    if packet.get("requiredSeconds") != 7200:
        errors.append("packet requiredSeconds must be 7200")
    elapsed = packet.get("elapsedSeconds")
    if not isinstance(elapsed, int) or isinstance(elapsed, bool) or elapsed < 7200:
        errors.append("packet elapsedSeconds must be at least 7200")
    if packet.get("result") != "PASS":
        errors.append("packet result must be PASS")
    artifacts = packet.get("artifacts")
    if not isinstance(artifacts, dict):
        return errors + ["packet artifacts must be an object"]
    for key in REQUIRED_ARTIFACTS:
        errors.extend(_verify_entry(artifacts.get(key), key, root.resolve()))
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--packet", type=Path, required=True)
    parser.add_argument("--create", action="store_true")
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--audio", type=Path)
    parser.add_argument("--soak", type=Path)
    parser.add_argument("--runner-metadata", type=Path)
    parser.add_argument("--soak-binary", type=Path)
    args = parser.parse_args()
    try:
        root = args.root.resolve()
        packet = args.packet.resolve()
        if args.create:
            if (args.summary is None or args.audio is None or args.soak is None or
                    args.runner_metadata is None or args.soak_binary is None):
                raise PacketError("all source artifact paths are required with --create")
            create_packet(
                SoakPacketInputs(
                    root=root,
                    summary=args.summary.resolve(),
                    audio=args.audio.resolve(),
                    soak=args.soak.resolve(),
                    runner_metadata=args.runner_metadata.resolve(),
                    soak_binary=args.soak_binary.resolve(),
                ),
                packet,
            )
        errors = verify_packet(packet, root)
    except (OSError, PacketError, ValueError, json.JSONDecodeError) as error:
        errors = [str(error)]
    if errors:
        for error in errors:
            print(f"[phase12c-soak-packet] ERROR: {error}")
        return 1
    print("[phase12c-soak-packet] PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
