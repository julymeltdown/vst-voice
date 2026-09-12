#!/usr/bin/env python3
"""Validate Phase 12C evidence against the canonical CLAP/bank contract.

The existing Phase 12C binaries are useful engineering harnesses, but a
prototype/fixture-only run must not satisfy the canonical CLAP gate.  This
module therefore has two modes: ``--root`` performs a source contract check;
adding ``--plugin``/``--bank`` and evidence files enables bound artifact and
per-row execution checks. A passing result establishes engineering evidence
only; installed-bank qualification and release eligibility remain separate.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import stat
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
CANONICAL_PLUGIN_ID = "com.project-seam.editor"
EXPECTED_VALIDATOR_VERSION = "0.4.1"
EXPECTED_MATRIX_CASES = 336
MATRIX_SAMPLE_RATES = (44100, 48000, 88200, 96000, 176400, 192000)
MATRIX_BLOCK_FRAMES = (16, 32, 64, 128, 256, 512, 1024)
MATRIX_CHANNEL_COUNTS = (1, 2, 4, 8)
MATRIX_DIALECTS = ("clap", "midi1")
CANONICAL_EXECUTION_PATH = "clap-plugin-process-v1"
MAX_JSON_BYTES = 16 * 1024 * 1024
MAX_BANK_BYTES = 256 * 1024 * 1024
MAX_JSON_DEPTH = 64


class ContractError(ValueError):
    pass


def _lexical_path(path: Path) -> Path:
    """Normalize ``..`` without following a link at the evidence boundary."""
    return Path(os.path.abspath(os.fspath(path)))


def _sha256(path: Path, maximum: int | None = None) -> str:
    try:
        info = path.lstat()
    except OSError as error:
        raise ContractError(f"artifact cannot be stat'ed: {path}: {error}") from error
    if not stat.S_ISREG(info.st_mode):
        raise ContractError(f"artifact must be a regular file: {path}")
    if maximum is not None and info.st_size > maximum:
        raise ContractError(f"artifact exceeds its size limit: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _tree_sha256(path: Path) -> str:
    path = Path(path)
    if path.is_symlink():
        raise ContractError(f"symbolic-link tree is forbidden: {path}")
    if path.is_file():
        return _sha256(path, MAX_BANK_BYTES)
    if not path.is_dir():
        raise ContractError(f"artifact tree is missing: {path}")
    digest = hashlib.sha256()
    total = 0
    for item in sorted(path.rglob("*")):
        if item.is_symlink():
            raise ContractError(f"symbolic-link tree member is forbidden: {item}")
        if not item.is_file():
            continue
        size = item.stat().st_size
        total += size
        if total > MAX_BANK_BYTES:
            raise ContractError(f"artifact tree exceeds {MAX_BANK_BYTES} bytes: {path}")
        relative = item.relative_to(path).as_posix().encode("utf-8")
        digest.update(len(relative).to_bytes(4, "little"))
        digest.update(relative)
        digest.update(bytes.fromhex(_sha256(item)))
    return digest.hexdigest()


def _read_json(path: Path) -> dict[str, Any]:
    if path.is_symlink() or not path.is_file():
        raise ContractError(f"JSON evidence must be a regular file: {path}")
    if path.stat().st_size > MAX_JSON_BYTES:
        raise ContractError(f"JSON evidence exceeds {MAX_JSON_BYTES} bytes: {path}")
    def unique(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise ContractError(f"duplicate JSON key in evidence: {key}")
            result[key] = value
        return result

    def reject_constant(value: str) -> Any:
        raise ContractError(f"non-finite JSON number in evidence: {value}")

    try:
        value = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=unique,
            parse_constant=reject_constant,
        )
    except (OSError, UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ContractError(f"JSON evidence cannot be read: {path}: {error}") from error
    if not isinstance(value, dict):
        raise ContractError(f"JSON evidence root must be an object: {path}")
    pending: list[tuple[Any, int]] = [(value, 0)]
    while pending:
        current, depth = pending.pop()
        if depth > MAX_JSON_DEPTH:
            raise ContractError(f"JSON evidence exceeds depth limit: {path}")
        if isinstance(current, dict):
            pending.extend((child, depth + 1) for child in current.values())
        elif isinstance(current, list):
            pending.extend((child, depth + 1) for child in current)
        elif isinstance(current, float) and not math.isfinite(current):
            raise ContractError(f"non-finite JSON number in evidence: {path}")
    return value


def validate_source(root: Path) -> list[str]:
    errors: list[str] = []
    def read(relative: str) -> str:
        path = root / relative
        try:
            return path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            errors.append(f"source file cannot be read: {relative}: {error}")
            return ""

    cmake = read("CMakeLists.txt")
    plugin = read("libs/seam-clap-editor/src/plugin_entry.cpp")
    phase_header = read("phase12c/include/seam/phase12c/live_voice.hpp")
    phase_engine = read("phase12c/src/live_voice.cpp")
    validator = read("scripts/run_clap_validator.sh")
    required = {
        "canonical CLAP target": "add_library(seam_clap_editor_plugin MODULE",
        "canonical CLAP production engine": "target_link_libraries(seam_clap_editor_plugin PRIVATE seam_live_voice",
        "canonical live engine source": "libs/seam-live-voice/src/voice_engine.cpp",
        "canonical plugin identity": 'kPluginId{"com.project-seam.editor"}',
        "canonical descriptor identity": '.id = "com.project-seam.editor"',
        "activation-time scratch": "liveScratch_",
        "32 voice limit": "kMaxVoices = 32",
        "1024 event limit": "kMaxEventsPerBlock = 1024",
        "256 MiB resource limit": "kMaxResourceBytes = 256u * 1024u * 1024u",
        "pinned validator version": EXPECTED_VALIDATOR_VERSION,
    }
    surfaces = {
        "canonical CLAP target": cmake,
        "canonical CLAP production engine": cmake,
        "canonical live engine source": cmake,
        "canonical plugin identity": plugin,
        "canonical descriptor identity": plugin,
        "activation-time scratch": plugin,
        "32 voice limit": phase_header,
        "1024 event limit": phase_header,
        "256 MiB resource limit": phase_header,
        "pinned validator version": validator,
    }
    for label, needle in required.items():
        if needle not in surfaces[label]:
            errors.append(f"{label} is missing from the canonical source contract")
    for relative in (
        "libs/seam-clap-editor/src/plugin_entry.cpp",
        "libs/seam-clap-editor/src/editor_runtime_preview.cpp",
        "phase12c/src/live_voice.cpp",
    ):
        text = plugin if relative.endswith("plugin_entry.cpp") else phase_engine if relative.endswith("live_voice.cpp") else read(relative)
        if "LiveSampleInstrument" in text or "human_vowel_data.hpp" in text:
            errors.append(f"legacy/generated fixture symbol remains in canonical source: {relative}")
    if "renderLiveSample()" in plugin:
        errors.append("canonical CLAP process still renders one sample at a time")
    if "CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI" not in plugin:
        errors.append("canonical CLAP input dialect contract is incomplete")
    return errors


def validate_plugin(path: Path) -> tuple[list[str], str]:
    errors: list[str] = []
    if path.is_symlink() or not path.exists():
        return ([f"canonical plugin is missing or linked: {path}"], "")
    if path.is_dir():
        if path.suffix.lower() != ".clap":
            errors.append("canonical plugin directory must have a .clap suffix")
        if not (path / "Contents" / "MacOS").is_dir() and not any(path.rglob("*.dll")) and not any(path.rglob("*.so")):
            errors.append("canonical plugin bundle contains no executable payload")
    elif path.suffix.lower() != ".clap" or path.stat().st_size == 0:
        errors.append("canonical plugin must be a non-empty .clap module or bundle")
    try:
        digest = _tree_sha256(path)
    except ContractError as error:
        errors.append(str(error))
        digest = ""
    return errors, digest


def validate_bank(path: Path) -> tuple[list[str], dict[str, str]]:
    errors: list[str] = []
    identity: dict[str, str] = {}
    if path.is_symlink() or not path.is_dir():
        return ([f"canonical voicebank root is missing or linked: {path}"], identity)
    manifest = path / "manifest.json"
    try:
        value = _read_json(manifest)
    except ContractError as error:
        return ([str(error)], identity)
    for field in ("id", "version"):
        if not isinstance(value.get(field), str) or not value[field]:
            errors.append(f"voicebank manifest {field} is required")
        else:
            identity[field] = value[field]
    if value.get("schemaVersion") != 3:
        errors.append("voicebank manifest schemaVersion must be 3")
    units = value.get("units")
    if not isinstance(units, list) or not units:
        errors.append("voicebank manifest must declare at least one unit")
    try:
        identity["treeSha256"] = _tree_sha256(path)
    except ContractError as error:
        errors.append(str(error))
    return errors, identity


def _require_digest(value: Any, label: str, errors: list[str]) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
        errors.append(f"{label} must be a lowercase SHA-256 digest")


def _require_execution_identity(value: dict[str, Any], label: str, errors: list[str]) -> None:
    if value.get("executionPath") != CANONICAL_EXECUTION_PATH:
        errors.append(f"{label} executionPath must be {CANONICAL_EXECUTION_PATH}; linked-engine evidence is not canonical plugin execution")
    if value.get("resourceMode") not in ("development-fixture", "installed-bank"):
        errors.append(f"{label} resourceMode must be development-fixture or installed-bank")
    commit = value.get("sourceCommit")
    if (
        not isinstance(commit, str)
        or len(commit) not in (40, 64)
        or any(character not in "0123456789abcdef" for character in commit)
        or set(commit) == {"0"}
    ):
        errors.append(f"{label} sourceCommit must be a nonzero full Git commit digest")
    build_id = value.get("buildId")
    if (
        not isinstance(build_id, str)
        or not build_id.strip()
        or build_id != build_id.strip()
        or set(build_id) == {"0"}
        or build_id.lower() in ("unknown", "unavailable", "none", "null")
    ):
        errors.append(f"{label} buildId must identify the actual build")


def _positive_integer(value: Any) -> bool:
    return type(value) is int and value > 0


def _positive_finite_number(value: Any) -> bool:
    return type(value) in (int, float) and value > 0 and (type(value) is int or math.isfinite(value))


def validate_matrix(path: Path, *, plugin_sha256: str, bank: dict[str, str]) -> list[str]:
    errors: list[str] = []
    try:
        value = _read_json(path)
    except ContractError as error:
        return [str(error)]
    if value.get("result") != "PASS":
        errors.append("canonical matrix result must be PASS")
    if type(value.get("cases")) is not int or type(value.get("expected")) is not int or value.get("cases") != EXPECTED_MATRIX_CASES or value.get("expected") != EXPECTED_MATRIX_CASES:
        errors.append("canonical matrix must contain exactly 336 cases")
    if type(value.get("failures")) is not int or value.get("failures") != 0 or value.get("finite") is not True:
        errors.append("canonical matrix must have zero failures and finite output")
    if value.get("pluginId") != CANONICAL_PLUGIN_ID:
        errors.append("canonical matrix pluginId must identify the editor")
    if value.get("pluginSha256") != plugin_sha256:
        errors.append("canonical matrix pluginSha256 is not bound to the supplied plugin")
    if value.get("voicebankId") != bank.get("id") or value.get("voicebankVersion") != bank.get("version"):
        errors.append("canonical matrix voicebank identity differs from the supplied bank")
    if value.get("voicebankTreeSha256") != bank.get("treeSha256"):
        errors.append("canonical matrix voicebank tree digest differs from the supplied bank")
    _require_execution_identity(value, "canonical matrix", errors)
    rows = value.get("rowResults")
    if not isinstance(rows, list) or len(rows) != EXPECTED_MATRIX_CASES:
        errors.append("canonical matrix rowResults must contain exactly 336 independently executed rows")
        return errors
    expected_rows = {
        (sample_rate, block_frames, channels, dialect)
        for sample_rate in MATRIX_SAMPLE_RATES
        for block_frames in MATRIX_BLOCK_FRAMES
        for channels in MATRIX_CHANNEL_COUNTS
        for dialect in MATRIX_DIALECTS
    }
    seen: set[tuple[int, int, int, str]] = set()
    for index, row in enumerate(rows):
        label = f"canonical matrix rowResults[{index}]"
        if not isinstance(row, dict):
            errors.append(f"{label} must be an object")
            continue
        dimensions = (row.get("sampleRate"), row.get("blockFrames"), row.get("channels"), row.get("dialect"))
        if not all(type(dimension) is int for dimension in dimensions[:3]) or not isinstance(dimensions[3], str) or dimensions not in expected_rows:
            errors.append(f"{label} has unsupported matrix dimensions")
        elif dimensions in seen:
            errors.append(f"{label} duplicates a matrix dimension combination")
        else:
            seen.add(dimensions)
        if row.get("result") != "PASS":
            errors.append(f"{label} result must be PASS")
        for field in ("finite", "stateRoundTrip", "preNoteSilent", "releaseSilent", "channelCountVerified", "pitchBendChanged", "panChannelIsolation", "eventAdmissionBounded", "processingStatusVerified"):
            if row.get(field) is not True:
                errors.append(f"{label} {field} must be true")
        if not _positive_integer(row.get("processCalls")):
            errors.append(f"{label} processCalls must be a positive integer")
        frames = row.get("renderedFrames")
        sample_rate = row.get("sampleRate")
        if not _positive_integer(frames) or type(sample_rate) is not int or frames * 10 < sample_rate:
            errors.append(f"{label} renderedFrames must cover at least one tenth of a second")
        if not _positive_finite_number(row.get("absoluteEnergy")):
            errors.append(f"{label} absoluteEnergy must be finite and positive")
    if seen != expected_rows:
        errors.append("canonical matrix rowResults must cover each of the 336 dimension combinations exactly once")
    return errors


def validate_validator_result(path: Path, *, plugin_sha256: str) -> list[str]:
    errors: list[str] = []
    try:
        value = _read_json(path)
    except ContractError as error:
        return [str(error)]
    if value.get("status") != "PASS":
        errors.append("canonical CLAP validator status must be PASS")
    if value.get("validatorVersion") != EXPECTED_VALIDATOR_VERSION:
        errors.append(f"canonical CLAP validator must be pinned to {EXPECTED_VALIDATOR_VERSION}")
    if value.get("pluginId") != CANONICAL_PLUGIN_ID:
        errors.append("canonical CLAP validator pluginId is not the editor")
    if value.get("pluginSha256") != plugin_sha256:
        errors.append("canonical CLAP validator result is bound to a different plugin")
    _require_digest(value.get("rawLogSha256"), "canonical CLAP validator rawLogSha256", errors)
    return errors


def validate_soak(path: Path, *, plugin_sha256: str, bank: dict[str, str], require_full: bool) -> list[str]:
    errors: list[str] = []
    try:
        value = _read_json(path)
    except ContractError as error:
        return [str(error)]
    profile = value.get("profile")
    if require_full and profile != "full":
        errors.append("canonical CLAP soak requires the full profile")
    if value.get("pluginId") != CANONICAL_PLUGIN_ID:
        errors.append("canonical soak pluginId is not the editor")
    if value.get("pluginSha256") != plugin_sha256:
        errors.append("canonical soak pluginSha256 is not bound to the supplied plugin")
    if value.get("voicebankId") != bank.get("id") or value.get("voicebankVersion") != bank.get("version"):
        errors.append("canonical soak voicebank identity differs from the supplied bank")
    if value.get("voicebankTreeSha256") != bank.get("treeSha256"):
        errors.append("canonical soak voicebank tree digest differs from the supplied bank")
    _require_execution_identity(value, "canonical soak", errors)
    # Reuse the strict workload invariants without accepting its fixture-only
    # identity as canonical evidence.
    if value.get("result") != "PASS" or value.get("finite") is not True:
        errors.append("canonical soak result must be PASS and finite")
    for key in ("eventBlocks", "resourcePublishes", "resourceClears", "noteOns", "noteOffs", "steals", "midiEvents", "expressionEvents", "renderedFrames", "transitionHits", "transitionFallbacks"):
        if not _positive_integer(value.get(key)):
            errors.append(f"canonical soak {key} must be positive")
    if type(value.get("eventOverflows")) is not int or value.get("eventOverflows") != 0:
        errors.append("canonical soak eventOverflows must be zero")
    if not _positive_integer(value.get("maxActiveVoices")) or value["maxActiveVoices"] > 32:
        errors.append("canonical soak maxActiveVoices must be an integer between 1 and 32")
    elapsed = value.get("elapsedSeconds")
    if not _positive_finite_number(elapsed):
        errors.append("canonical soak elapsedSeconds must be finite and positive")
    elif profile == "full" and elapsed < 7200:
        errors.append("canonical full soak must cover at least 7200 seconds")
    return errors


def verify(
    *,
    root: Path,
    plugin: Path | None = None,
    bank: Path | None = None,
    matrix: Path | None = None,
    validator_result: Path | None = None,
    soak: Path | None = None,
    require_full_soak: bool = False,
) -> tuple[list[str], dict[str, Any]]:
    errors = validate_source(root)
    result: dict[str, Any] = {
        "schemaVersion": 1,
        "contract": "phase12c-canonical-clap-v1",
        "pluginId": CANONICAL_PLUGIN_ID,
        "status": "PASS",
        "evidenceScope": "engineering",
        "releaseEligible": False,
        "source": {"root": str(root)},
    }
    plugin_sha256 = ""
    bank_identity: dict[str, str] = {}
    if plugin is not None:
        plugin_errors, plugin_sha256 = validate_plugin(plugin)
        errors.extend(plugin_errors)
        result["plugin"] = {"path": str(plugin), "sha256": plugin_sha256}
    if bank is not None:
        bank_errors, bank_identity = validate_bank(bank)
        errors.extend(bank_errors)
        result["voicebank"] = bank_identity | {"path": str(bank)}
    if matrix is not None:
        if not plugin_sha256 or not bank_identity:
            errors.append("matrix validation requires a validated canonical plugin and bank")
        else:
            errors.extend(validate_matrix(matrix, plugin_sha256=plugin_sha256, bank=bank_identity))
        result["matrix"] = str(matrix)
    if validator_result is not None:
        if not plugin_sha256:
            errors.append("validator validation requires a validated canonical plugin")
        else:
            errors.extend(validate_validator_result(validator_result, plugin_sha256=plugin_sha256))
        result["validatorResult"] = str(validator_result)
    if require_full_soak and soak is None:
        errors.append("--require-full-soak requires a soak evidence file")
    if soak is not None:
        if not plugin_sha256 or not bank_identity:
            errors.append("soak validation requires a validated canonical plugin and bank")
        else:
            errors.extend(validate_soak(soak, plugin_sha256=plugin_sha256, bank=bank_identity, require_full=require_full_soak))
        result["soak"] = str(soak)
    result["errors"] = list(dict.fromkeys(errors))
    result["status"] = "PASS" if not errors else "BLOCKED"
    return list(dict.fromkeys(errors)), result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Fail-closed Phase 12C canonical CLAP contract verifier")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--plugin", type=Path)
    parser.add_argument("--bank", type=Path)
    parser.add_argument("--matrix", type=Path)
    parser.add_argument("--validator-result", type=Path)
    parser.add_argument("--soak", type=Path)
    parser.add_argument("--require-full-soak", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        errors, result = verify(
            root=_lexical_path(args.root),
            plugin=_lexical_path(args.plugin) if args.plugin is not None else None,
            bank=_lexical_path(args.bank) if args.bank is not None else None,
            matrix=_lexical_path(args.matrix) if args.matrix is not None else None,
            validator_result=_lexical_path(args.validator_result) if args.validator_result is not None else None,
            soak=_lexical_path(args.soak) if args.soak is not None else None,
            require_full_soak=args.require_full_soak,
        )
    except (OSError, ValueError, ContractError, json.JSONDecodeError) as error:
        errors, result = [str(error)], {"schemaVersion": 1, "contract": "phase12c-canonical-clap-v1", "status": "BLOCKED", "evidenceScope": "engineering", "releaseEligible": False, "errors": [str(error)]}
    payload = json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload, encoding="utf-8")
    print(payload, end="")
    return 0 if not errors else 3


if __name__ == "__main__":
    raise SystemExit(main())
