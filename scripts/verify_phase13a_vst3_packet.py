#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import TypeAlias

try:
    from tools.phase13a.distribution_manifest import tree_sha256
except ModuleNotFoundError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/phase13a"))
    from distribution_manifest import tree_sha256 as tree_sha256_fallback

    tree_sha256 = tree_sha256_fallback

JsonValue: TypeAlias = (
    str | int | float | bool | None | list["JsonValue"] | dict[str, "JsonValue"]
)

PACKET_TYPE = "phase13a-vst3-validator-packet"
VALID_STATUSES = {"PASS", "FAIL", "NOT_RUN", "BLOCKED"}
REQUIRED_ARTIFACTS = (
    "result",
    "stdoutLog",
    "stderrLog",
    "plugin",
    "clap",
    "validator",
    "runnerMetadata",
    "buildResult",
)


@dataclass(frozen=True, slots=True)
class Vst3PacketInputs:
    root: Path
    result: Path
    stdout_log: Path
    stderr_log: Path
    plugin: Path
    clap: Path
    validator: Path
    runner_metadata: Path
    build_result: Path


class PacketError(ValueError):
    pass


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _read_object(path: Path, label: str) -> dict[str, JsonValue]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PacketError(f"{label} cannot be read: {path}") from error
    if not isinstance(value, dict):
        raise PacketError(f"{label} must be a JSON object: {path}")
    return value


def _rooted(path: Path, root: Path) -> Path:
    return path if path.is_absolute() else root / path


def _entry(path: Path, root: Path, label: str, allow_empty: bool = False) -> dict[str, str]:
    if path.is_symlink():
        raise PacketError(f"{label} cannot be a symbolic link: {path}")
    resolved_root = root.resolve()
    resolved = path.resolve()
    try:
        relative = resolved.relative_to(resolved_root)
    except ValueError as error:
        raise PacketError(f"{label} escapes packet root: {path}") from error
    if resolved.is_file() and resolved.stat().st_size == 0 and not allow_empty:
        raise PacketError(f"{label} is empty: {path}")
    if not resolved.is_file() and not resolved.is_dir():
        raise PacketError(f"{label} is missing: {path}")
    if resolved.is_dir() and not any(resolved.rglob("*")):
        raise PacketError(f"{label} is empty: {path}")
    return {"path": relative.as_posix(), "sha256": tree_sha256(resolved)}


def _artifact_paths(inputs: Vst3PacketInputs) -> tuple[tuple[str, Path], ...]:
    return (
        ("result", inputs.result),
        ("stdoutLog", inputs.stdout_log),
        ("stderrLog", inputs.stderr_log),
        ("plugin", inputs.plugin),
        ("clap", inputs.clap),
        ("validator", inputs.validator),
        ("runnerMetadata", inputs.runner_metadata),
        ("buildResult", inputs.build_result),
    )


def _runner_errors(value: dict[str, JsonValue]) -> list[str]:
    return [
        "runner metadata must contain non-empty runnerOs and runnerArchitecture"
    ] if any(
        not isinstance(value.get(field), str) or not value[field]
        for field in ("runnerOs", "runnerArchitecture")
    ) else []


def _result_identity_errors(
    result: dict[str, JsonValue], plugin: Path, clap: Path, validator: Path
) -> list[str]:
    errors: list[str] = []
    plugin_hash = result.get("pluginSha256")
    if not isinstance(plugin_hash, str) or plugin_hash != tree_sha256(plugin):
        errors.append("validator result pluginSha256 does not match the plugin tree")
    canonical_clap = result.get("canonicalClapSha256")
    if isinstance(canonical_clap, str) and canonical_clap and canonical_clap != tree_sha256(clap):
        errors.append("validator result canonicalClapSha256 does not match the CLAP tree")
    tool = result.get("tool")
    tool_hash = tool.get("sha256") if isinstance(tool, dict) else None
    if not isinstance(tool_hash, str) or tool_hash != _sha256(validator):
        errors.append("validator result tool sha256 does not match the validator")
    return errors


def create_packet(inputs: Vst3PacketInputs, output: Path) -> dict[str, JsonValue]:
    root = inputs.root.resolve()
    resolved = Vst3PacketInputs(
        root=root,
        result=_rooted(inputs.result, root),
        stdout_log=_rooted(inputs.stdout_log, root),
        stderr_log=_rooted(inputs.stderr_log, root),
        plugin=_rooted(inputs.plugin, root),
        clap=_rooted(inputs.clap, root),
        validator=_rooted(inputs.validator, root),
        runner_metadata=_rooted(inputs.runner_metadata, root),
        build_result=_rooted(inputs.build_result, root),
    )
    result = _read_object(resolved.result, "validator result")
    status = result.get("status")
    platform = result.get("platform")
    if status not in VALID_STATUSES:
        raise PacketError("validator result status is invalid")
    if not isinstance(platform, str) or not platform:
        raise PacketError("validator result platform is missing")
    runner = _read_object(resolved.runner_metadata, "runner metadata")
    runner_errors = _runner_errors(runner)
    if runner_errors:
        raise PacketError("; ".join(runner_errors))
    errors = _result_identity_errors(result, resolved.plugin, resolved.clap, resolved.validator)
    if errors:
        raise PacketError("; ".join(errors))
    artifacts = {
        key: _entry(path, root, f"record {key}", allow_empty=key == "stderrLog")
        for key, path in _artifact_paths(resolved)
    }
    packet: dict[str, JsonValue] = {
        "schemaVersion": 1,
        "recordType": PACKET_TYPE,
        "status": status,
        "platform": platform,
        "pluginSha256": artifacts["plugin"]["sha256"],
        "validatorSha256": artifacts["validator"]["sha256"],
        "artifacts": artifacts,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(packet, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return packet


def _verify_entry(
    value: JsonValue, key: str, root: Path, allow_empty: bool = False
) -> tuple[list[str], Path | None]:
    if not isinstance(value, dict):
        return [f"record {key} entry is missing"], None
    relative = value.get("path")
    expected = value.get("sha256")
    if not isinstance(relative, str) or not relative:
        return [f"record {key} path is missing"], None
    errors: list[str] = []
    if not isinstance(expected, str) or len(expected) != 64:
        errors.append(f"record {key} sha256 is malformed")
    candidate = root / relative
    if candidate.is_symlink():
        return errors + [f"record {key} artifact is a symbolic link"], None
    resolved = candidate.resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError:
        return errors + [f"record {key} path escapes packet root"], None
    if not resolved.exists() or (
        resolved.is_file() and resolved.stat().st_size == 0 and not allow_empty
    ):
        return errors + [f"record {key} artifact is missing or empty"], None
    if isinstance(expected, str) and len(expected) == 64:
        if tree_sha256(resolved) != expected:
            errors.append(f"record {key} digest mismatch")
    return errors, resolved


def verify_packet(packet_path: Path, root: Path) -> list[str]:
    try:
        packet = _read_object(packet_path, "packet")
    except PacketError as error:
        return [str(error)]
    errors: list[str] = []
    if packet.get("schemaVersion") != 1:
        errors.append("packet schemaVersion must be 1")
    if packet.get("recordType") != PACKET_TYPE:
        errors.append("packet recordType is invalid")
    status = packet.get("status")
    if status not in VALID_STATUSES:
        errors.append("packet status is invalid")
    platform = packet.get("platform")
    if not isinstance(platform, str) or not platform:
        errors.append("packet platform is missing")
    artifacts = packet.get("artifacts")
    if not isinstance(artifacts, dict):
        return errors + ["packet artifacts must be an object"]
    paths: dict[str, Path] = {}
    for key in REQUIRED_ARTIFACTS:
        entry_errors, path = _verify_entry(
            artifacts.get(key), key, root.resolve(), allow_empty=key == "stderrLog"
        )
        errors.extend(entry_errors)
        if path is not None:
            paths[key] = path
    if "runnerMetadata" in paths:
        try:
            errors.extend(_runner_errors(_read_object(paths["runnerMetadata"], "runner metadata")))
        except PacketError as error:
            errors.append(str(error))
    if "result" in paths and "plugin" in paths and "clap" in paths and "validator" in paths:
        try:
            result = _read_object(paths["result"], "validator result")
            if result.get("status") != status or result.get("platform") != platform:
                errors.append("packet identity differs from validator result")
            errors.extend(_result_identity_errors(result, paths["plugin"], paths["clap"], paths["validator"]))
        except PacketError as error:
            errors.append(str(error))
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Create and verify hash-bound Phase 13A VST3 validator evidence")
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--packet", type=Path, required=True)
    parser.add_argument("--create", action="store_true")
    for name in ("result", "stdout_log", "stderr_log", "plugin", "clap", "validator", "runner_metadata", "build_result"):
        parser.add_argument(f"--{name.replace('_', '-')}", dest=name, type=Path)
    args = parser.parse_args(argv)
    try:
        root = args.root.resolve()
        packet = args.packet.resolve()
        if args.create:
            values = [getattr(args, name) for name in ("result", "stdout_log", "stderr_log", "plugin", "clap", "validator", "runner_metadata", "build_result")]
            if any(value is None for value in values):
                raise PacketError("all source artifact paths are required with --create")
            create_packet(Vst3PacketInputs(root, *values), packet)
        errors = verify_packet(packet, root)
    except (OSError, PacketError, ValueError, json.JSONDecodeError) as error:
        errors = [str(error)]
    if errors:
        for error in errors:
            print(f"[phase13a-vst3-packet] ERROR: {error}")
        return 1
    print("[phase13a-vst3-packet] PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
