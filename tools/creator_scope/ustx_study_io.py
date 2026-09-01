from __future__ import annotations

import hashlib
import json
import os
from dataclasses import dataclass
from pathlib import Path
import tempfile

import yaml

from .evidence import JsonObject
from .ustx_study_contracts import BridgeError


@dataclass(frozen=True, slots=True)
class TextOutput:
    path: Path
    text: str


@dataclass(frozen=True, slots=True)
class PreparedOutput:
    output: TextOutput
    temporary: Path
    device: int
    inode: int


def yaml_text(value: JsonObject) -> str:
    return yaml.safe_dump(
        value,
        allow_unicode=True,
        default_flow_style=False,
        sort_keys=False,
        width=100,
    )


def json_text(value: JsonObject) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2) + "\n"


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _remove(path: Path) -> str | None:
    try:
        path.unlink()
    except FileNotFoundError:
        return None
    except OSError as error:
        return str(error)
    return None


def _prepare(output: TextOutput) -> PreparedOutput:
    descriptor = -1
    temporary: Path | None = None
    try:
        output.path.parent.mkdir(parents=True, exist_ok=True)
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{output.path.name}.", dir=output.path.parent
        )
        temporary = Path(temporary_name)
        with os.fdopen(descriptor, "wb") as handle:
            descriptor = -1
            handle.write(output.text.encode("utf-8"))
            handle.flush()
            os.fsync(handle.fileno())
        status = os.lstat(temporary)
        return PreparedOutput(output, temporary, status.st_dev, status.st_ino)
    except OSError as error:
        if descriptor >= 0:
            os.close(descriptor)
        cleanup = _remove(temporary) if temporary is not None else None
        detail = f"; temporary cleanup failed: {cleanup}" if cleanup else ""
        raise BridgeError(
            f"unable to prepare output {output.path}: {error}{detail}"
        ) from error


def _publish(prepared: PreparedOutput) -> None:
    try:
        os.link(prepared.temporary, prepared.output.path, follow_symlinks=False)
    except FileExistsError as error:
        raise BridgeError(
            f"Refusing to overwrite existing output: {prepared.output.path}"
        ) from error
    except OSError as error:
        raise BridgeError(
            f"unable to publish output {prepared.output.path}: {error}"
        ) from error


def _rollback(prepared: PreparedOutput) -> str | None:
    try:
        status = os.lstat(prepared.output.path)
    except FileNotFoundError:
        return None
    except OSError as error:
        return str(error)
    if (status.st_dev, status.st_ino) != (prepared.device, prepared.inode):
        return "destination identity changed before rollback"
    return _remove(prepared.output.path)


def _write_outputs_new(outputs: tuple[TextOutput, ...]) -> None:
    for output in outputs:
        if output.path.exists() or output.path.is_symlink():
            raise BridgeError(f"Refusing to overwrite existing output: {output.path}")
    prepared: list[PreparedOutput] = []
    published: list[PreparedOutput] = []
    try:
        prepared.extend(_prepare(output) for output in outputs)
        for item in prepared:
            _publish(item)
            published.append(item)
        cleanup_errors = [
            f"{item.temporary}: {error}"
            for item in prepared
            if (error := _remove(item.temporary)) is not None
        ]
        if cleanup_errors:
            raise BridgeError("temporary cleanup failed: " + "; ".join(cleanup_errors))
    except BridgeError as error:
        rollback_errors = [
            f"{item.output.path}: {rollback_error}"
            for item in reversed(published)
            if (rollback_error := _rollback(item)) is not None
        ]
        temporary_errors = [
            f"{item.temporary}: {cleanup_error}"
            for item in prepared
            if (cleanup_error := _remove(item.temporary)) is not None
        ]
        recovery_errors = rollback_errors + temporary_errors
        if recovery_errors:
            raise BridgeError(
                f"{error}; publication recovery failed: {'; '.join(recovery_errors)}"
            ) from error
        raise


def write_new(path: Path, text: str) -> None:
    _write_outputs_new((TextOutput(path, text),))


def write_pair_new(first: TextOutput, second: TextOutput) -> None:
    _write_outputs_new((first, second))
