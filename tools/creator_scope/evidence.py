from __future__ import annotations

import errno
import hashlib
import json
import os
from dataclasses import dataclass
from pathlib import Path
import stat
from typing import Final, Protocol


JsonValue = str | int | float | bool | None | list["JsonValue"] | dict[str, "JsonValue"]
JsonObject = dict[str, JsonValue]


class JsonLoader(Protocol):
    def __call__(
        self,
        value: str,
        /,
        *,
        object_pairs_hook: JsonObjectHook,
    ) -> JsonValue: ...


class JsonObjectHook(Protocol):
    def __call__(self, pairs: list[tuple[str, JsonValue]], /) -> JsonObject: ...


@dataclass(frozen=True, slots=True)
class DuplicateJsonKeyError(ValueError):
    key: str

    def __str__(self) -> str:
        return f"duplicate JSON key {self.key!r} is not supported"


def _unique_object(pairs: list[tuple[str, JsonValue]]) -> JsonObject:
    result: JsonObject = {}
    for key, value in pairs:
        if key in result:
            raise DuplicateJsonKeyError(key)
        result[key] = value
    return result


JSON_LOAD: Final[JsonLoader] = json.loads
OPEN_SUPPORTS_DIR_FD: Final = os.open in os.supports_dir_fd


def as_object(value: JsonValue, label: str, errors: list[str]) -> JsonObject | None:
    if isinstance(value, dict):
        return value
    errors.append(f"{label} must be an object")
    return None


def as_array(value: JsonValue, label: str, errors: list[str]) -> list[JsonValue] | None:
    if isinstance(value, list):
        return value
    errors.append(f"{label} must be an array")
    return None


def load_object(path: Path, errors: list[str]) -> JsonObject | None:
    try:
        value = JSON_LOAD(
            path.read_text(encoding="utf-8"), object_pairs_hook=_unique_object
        )
    except FileNotFoundError:
        errors.append(f"missing JSON file: {path}")
        return None
    except (OSError, UnicodeError, ValueError) as error:
        errors.append(f"invalid JSON file {path}: {error}")
        return None
    return as_object(value, str(path), errors)


def _sha256_regular_beneath(root: Path, relative_path: Path) -> str:
    no_follow = getattr(os, "O_NOFOLLOW", 0)
    directory_flag = getattr(os, "O_DIRECTORY", 0)
    close_on_exec = getattr(os, "O_CLOEXEC", 0)
    if no_follow == 0 or directory_flag == 0 or not OPEN_SUPPORTS_DIR_FD:
        raise OSError(
            errno.ENOTSUP,
            "platform does not support handle-bound no-follow artifact reads",
        )
    directory_descriptor = -1
    file_descriptor = -1
    digest = hashlib.sha256()
    try:
        directory_descriptor = os.open(
            root,
            os.O_RDONLY | directory_flag | no_follow | close_on_exec,
        )
        for part in relative_path.parts[:-1]:
            next_descriptor = os.open(
                part,
                os.O_RDONLY | directory_flag | no_follow | close_on_exec,
                dir_fd=directory_descriptor,
            )
            os.close(directory_descriptor)
            directory_descriptor = next_descriptor
        file_descriptor = os.open(
            relative_path.parts[-1],
            os.O_RDONLY | no_follow | close_on_exec,
            dir_fd=directory_descriptor,
        )
        status = os.fstat(file_descriptor)
        if not stat.S_ISREG(status.st_mode):
            raise OSError(errno.EINVAL, "artifact is not a regular file")
        with os.fdopen(file_descriptor, "rb") as handle:
            file_descriptor = -1
            for block in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(block)
        return digest.hexdigest()
    finally:
        if file_descriptor >= 0:
            os.close(file_descriptor)
        if directory_descriptor >= 0:
            os.close(directory_descriptor)


def artifact_errors(root: Path, value: JsonValue, label: str) -> tuple[str, ...]:
    errors: list[str] = []
    artifact = as_object(value, label, errors)
    if artifact is None:
        return tuple(errors)
    relative_value = artifact.get("path")
    digest_value = artifact.get("sha256")
    if not isinstance(relative_value, str) or not relative_value:
        errors.append(f"{label}.path must be a non-empty string")
        return tuple(errors)
    relative_path = Path(relative_value)
    if (
        not relative_path.parts
        or relative_path.is_absolute()
        or ".." in relative_path.parts
    ):
        errors.append(f"{label}.path must be repository-relative and non-traversing")
        return tuple(errors)
    if not isinstance(digest_value, str) or len(digest_value) != 64:
        errors.append(f"{label}.sha256 must be 64 lowercase hexadecimal characters")
        return tuple(errors)
    try:
        digest = _sha256_regular_beneath(root, relative_path)
    except (OSError, ValueError) as error:
        errors.append(f"{label}.path cannot be securely read: {error}")
        return tuple(errors)
    if digest != digest_value:
        errors.append(f"{label}.sha256 does not match")
    return tuple(errors)


def evidence_errors(root: Path, value: JsonValue, label: str) -> tuple[str, ...]:
    errors: list[str] = []
    evidence = as_array(value, label, errors)
    if evidence is None:
        return tuple(errors)
    if not evidence:
        return (f"{label} must contain evidence",)
    for index, artifact in enumerate(evidence):
        errors.extend(artifact_errors(root, artifact, f"{label}[{index}]"))
    return tuple(errors)
