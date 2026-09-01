from __future__ import annotations

import json
import math
import os
from pathlib import Path
import stat
from typing import Final, Protocol

import yaml
from yaml.nodes import MappingNode, Node, ScalarNode, SequenceNode
from yaml.tokens import (
    AliasToken,
    AnchorToken,
    BlockEndToken,
    BlockMappingStartToken,
    BlockSequenceStartToken,
    DirectiveToken,
    DocumentEndToken,
    DocumentStartToken,
    FlowMappingEndToken,
    FlowMappingStartToken,
    FlowSequenceEndToken,
    FlowSequenceStartToken,
    ScalarToken,
    StreamEndToken,
    StreamStartToken,
    TagToken,
)

from .evidence import JsonObject, JsonValue
from .ustx_study_contracts import (
    BridgeError,
    MAXIMUM_COLLECTION_ENTRIES,
    MAXIMUM_DEPTH,
    MAXIMUM_INPUT_BYTES,
    MAXIMUM_NODES,
    MAXIMUM_SCALAR_BYTES,
)


class YamlLoader(Protocol):
    def __call__(self, value: str, /) -> JsonValue: ...


YAML_LOAD: Final[YamlLoader] = yaml.safe_load


def read_bounded(path: Path) -> bytes:
    no_follow = getattr(os, "O_NOFOLLOW", 0)
    if no_follow == 0 and path.is_symlink():
        raise BridgeError(f"input path must not be a symbolic link: {path}")
    descriptor = -1
    try:
        descriptor = os.open(path, os.O_RDONLY | no_follow)
        status = os.fstat(descriptor)
        if not stat.S_ISREG(status.st_mode):
            raise BridgeError(f"input must be a regular file: {path}")
        if status.st_size > MAXIMUM_INPUT_BYTES:
            raise BridgeError(
                f"input exceeds study limit of {MAXIMUM_INPUT_BYTES} bytes"
            )
        with os.fdopen(descriptor, "rb") as handle:
            descriptor = -1
            payload = handle.read(MAXIMUM_INPUT_BYTES + 1)
    except OSError as error:
        raise BridgeError(f"unable to read input: {error}") from error
    finally:
        if descriptor >= 0:
            os.close(descriptor)
    if len(payload) > MAXIMUM_INPUT_BYTES:
        raise BridgeError(f"input exceeds study limit of {MAXIMUM_INPUT_BYTES} bytes")
    return payload


def _scalar_bytes(value: str, path: str) -> int:
    try:
        return len(value.encode("utf-8"))
    except UnicodeEncodeError as error:
        raise BridgeError(f"{path} contains an invalid Unicode scalar") from error


def _normalize(value: JsonValue, path: str) -> JsonValue:
    node_count = 0

    def visit(item: JsonValue, item_path: str, depth: int) -> JsonValue:
        nonlocal node_count
        if depth > MAXIMUM_DEPTH:
            raise BridgeError("input nesting exceeds the study limit")
        node_count += 1
        if node_count > MAXIMUM_NODES:
            raise BridgeError("input node count exceeds the study limit")
        if item is None or isinstance(item, bool | int | str):
            if (
                isinstance(item, str)
                and _scalar_bytes(item, item_path) > MAXIMUM_SCALAR_BYTES
            ):
                raise BridgeError(f"{item_path} exceeds the scalar byte limit")
            return item
        if isinstance(item, float):
            if not math.isfinite(item):
                raise BridgeError(f"{item_path} must be finite")
            return item
        if isinstance(item, list):
            if len(item) > MAXIMUM_COLLECTION_ENTRIES:
                raise BridgeError(f"{item_path} exceeds the collection limit")
            return [
                visit(child, f"{item_path}[{index}]", depth + 1)
                for index, child in enumerate(item)
            ]
        if isinstance(item, dict):
            if len(item) > MAXIMUM_COLLECTION_ENTRIES:
                raise BridgeError(f"{item_path} exceeds the collection limit")
            result: JsonObject = {}
            for key, child in item.items():
                if not isinstance(key, str):
                    raise BridgeError(f"{item_path} contains a non-string key")
                if key in result:
                    raise BridgeError(f"{item_path} contains a duplicate key: {key}")
                result[key] = visit(child, f"{item_path}.{key}", depth + 1)
            return result
        raise BridgeError(f"{item_path} contains an unsupported YAML value")

    return visit(value, path, 0)


def _preflight_ustx(text: str) -> None:
    document_started = False
    content_seen = False
    depth = 0
    nodes = 0
    starts = (
        BlockMappingStartToken,
        BlockSequenceStartToken,
        FlowMappingStartToken,
        FlowSequenceStartToken,
    )
    ends = (BlockEndToken, FlowMappingEndToken, FlowSequenceEndToken)
    structural = (
        StreamStartToken,
        StreamEndToken,
        DirectiveToken,
        DocumentEndToken,
    )
    for token in yaml.scan(text, Loader=yaml.SafeLoader):
        if isinstance(token, AliasToken | AnchorToken | TagToken):
            raise BridgeError("USTX aliases, anchors, and tags are not supported")
        if isinstance(token, DocumentStartToken):
            if document_started or content_seen:
                raise BridgeError("multiple YAML documents are not supported")
            document_started = True
            continue
        if isinstance(token, starts):
            depth += 1
            nodes += 1
            if depth > MAXIMUM_DEPTH:
                raise BridgeError("USTX nesting exceeds the study limit")
        if isinstance(token, ends):
            depth -= 1
        if isinstance(token, ScalarToken):
            nodes += 1
            if _scalar_bytes(token.value, "USTX scalar") > MAXIMUM_SCALAR_BYTES:
                raise BridgeError("USTX scalar exceeds the scalar byte limit")
        if nodes > MAXIMUM_NODES:
            raise BridgeError("USTX node count exceeds the study limit")
        if not isinstance(token, structural):
            content_seen = True


def _reject_duplicate_keys(node: Node | None) -> None:
    pending: list[Node] = [] if node is None else [node]
    while pending:
        current = pending.pop()
        if isinstance(current, MappingNode):
            keys: set[str] = set()
            for key, value in current.value:
                if isinstance(key, ScalarNode):
                    if key.value in keys:
                        raise BridgeError(
                            f"duplicate YAML key {key.value!r} is not supported"
                        )
                    keys.add(key.value)
                pending.extend((key, value))
        if isinstance(current, SequenceNode):
            pending.extend(current.value)


def _preflight_json(payload: bytes) -> None:
    depth = 0
    in_string = False
    escaped = False
    for byte in payload:
        if in_string:
            if escaped:
                escaped = False
                continue
            if byte == ord("\\"):
                escaped = True
                continue
            if byte == ord('"'):
                in_string = False
            continue
        if byte == ord('"'):
            in_string = True
            continue
        if byte in (ord("["), ord("{")):
            depth += 1
            if depth > MAXIMUM_DEPTH:
                raise BridgeError("SEAM JSON nesting exceeds the study limit")
            continue
        if byte in (ord("]"), ord("}")):
            depth -= 1


def load_ustx(path: Path) -> tuple[JsonObject, bytes]:
    payload = read_bounded(path)
    try:
        text = payload.decode("utf-8-sig")
    except UnicodeDecodeError as error:
        raise BridgeError(f"USTX must be valid UTF-8: {error}") from error
    try:
        _preflight_ustx(text)
        _reject_duplicate_keys(yaml.compose(text, Loader=yaml.SafeLoader))
        normalized = _normalize(YAML_LOAD(text), "ustx")
    except BridgeError:
        raise
    except (RecursionError, UnicodeError, ValueError, yaml.YAMLError) as error:
        raise BridgeError(f"invalid USTX YAML: {error}") from error
    if not isinstance(normalized, dict):
        raise BridgeError("USTX root must be an object")
    return normalized, payload


def load_json(path: Path) -> tuple[JsonObject, bytes]:
    payload = read_bounded(path)
    try:
        _preflight_json(payload)
        normalized = _normalize(json.loads(payload), "project")
    except BridgeError:
        raise
    except (RecursionError, UnicodeError, ValueError) as error:
        raise BridgeError(f"invalid SEAM JSON: {error}") from error
    if not isinstance(normalized, dict):
        raise BridgeError("SEAM project root must be an object")
    return normalized, payload
