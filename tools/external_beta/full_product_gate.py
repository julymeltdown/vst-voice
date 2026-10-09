"""Engineering reconciliation of the archived U45 typed evidence audit.

This adapter checks retained declarations, coverage, artifact syntax and exact
hash bindings. It does not prove acoustic quality, human identity, native
project replay or actual U14 installation. Canonical admission has an explicit
hold until those integrations are reviewed. The release gate still uses the
existing report reader; this module must not replace it without preserving the
shared soak replay context and resolving the remaining semantic boundaries.

Synthetic component fixtures exercise all 83 case rows, but their legacy soak
blobs fail the current full reader. Neither component success nor an engineering
packet authorizes a release. The canonical contract remains UNAVAILABLE.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
from pathlib import Path
import re
import statistics
import struct
from typing import Final
import zlib

try:
    from .full_product_contract import CANONICAL_CONTRACT_ID, SYNTHETIC_CONTRACT_ID
    from .full_product_contract_registry import BACKENDS, LANGUAGES, PLATFORMS, REQUIREMENTS, RESOURCE_KINDS
    from .full_product_report import (
        ROOT,
        FullProductReportError,
        MAXIMUM_REPORT_BYTES,
        _parse_json,
        _read_regular_reference,
        _safe_reference_path,
        _sha256_json,
        validate_full_product_report,
    )
    from .soak_session_validation import SoakReplayContext, reuse_reference
    from .release_gate_validation import JsonObject, JsonValue
except ImportError:  # pragma: no cover - direct script import compatibility
    from full_product_contract import CANONICAL_CONTRACT_ID, SYNTHETIC_CONTRACT_ID  # type: ignore
    from full_product_contract_registry import BACKENDS, LANGUAGES, PLATFORMS, REQUIREMENTS, RESOURCE_KINDS  # type: ignore
    from full_product_report import (  # type: ignore
        ROOT,
        FullProductReportError,
        MAXIMUM_REPORT_BYTES,
        _parse_json,
        _read_regular_reference,
        _safe_reference_path,
        _sha256_json,
        validate_full_product_report,
    )
    from soak_session_validation import SoakReplayContext, reuse_reference
    from release_gate_validation import JsonObject, JsonValue  # type: ignore


GATE_VERSION: Final = 1
CANDIDATE_EVIDENCE: Final = "CANDIDATE_EVIDENCE"
ENGINEERING_FIXTURE: Final = "ENGINEERING_FIXTURE"
ADMITTED_EVIDENCE: Final = {
    CANONICAL_CONTRACT_ID: CANDIDATE_EVIDENCE,
    SYNTHETIC_CONTRACT_ID: ENGINEERING_FIXTURE,
}
# The contract must record the validator that actually executes.  The
# canonical file still says UNAVAILABLE; changing it is a deliberate accepted
# contract revision (joint plan M6.P3), so until then the canonical contract
# stays blocked by this predicate as well as by its unresolved criteria.
SEMANTIC_VALIDATION: Final = {
    "ownerUnit": "U45",
    "status": "IMPLEMENTED",
    "admission": "TYPED_FAIL_CLOSED",
    "validator": "tools/external_beta/full_product_gate.py",
    "validatorVersion": GATE_VERSION,
}
U45_RECONCILIATION_HOLD: Final = (
    "U45_RECONCILIATION_HOLD: native resource, project and measurement replay integration is incomplete"
)

FIXTURE_AUTHORITY_ERROR: Final = (
    "EB-009-full-product: engineering-fixture evidence passed the typed audit under the "
    "synthetic contract but can never authorize a release state"
)

REVIEW_RECORD: Final = "seam.full-product.review.v1"
MEASUREMENT_RECORD: Final = "seam.full-product.measurement.v1"
REGISTRY_RECORD: Final = "seam.full-product.reviewer-registry.v1"
INSTALLED_RESOURCE_RECORD: Final = "seam.full-product.installed-resource.v1"
SESSION_LOG_RECORD: Final = "seam.full-product.session-log.v1"
CREATOR_RECORD: Final = "seam.full-product.creator-study.v1"
PROTOCOL_UNIT: Final = "protocol-conformance"

LANGUAGE_BEARING_RESULTS: Final = frozenset((
    "acoustic", "expression", "voice-design", "producer-journey", "resource-quality",
    "language-review", "neural-qualification", "creator-session", "quality-study",
))
AUDIO_MEASURED_RESULTS: Final = frozenset((
    "acoustic", "expression", "voice-design", "resource-quality", "language-review",
    "neural-qualification", "quality-study",
))
MUSICAL_REVIEW_ROLES: Final = frozenset((
    "musician", "voice-producer", "native-language-reviewer", "model-reviewer", "independent-creator",
))
REVIEW_ROLES: Final = frozenset(role for spec in REQUIREMENTS.values() for role in spec.review_roles)
SINGER_KINDS: Final = frozenset(("sample-real", "sample-procedural", "recipe-original", "neural-original"))
PACKAGE_KINDS: Final = {
    "sample-real": "sample",
    "sample-procedural": "sample",
    "recipe-original": "recipe",
    "neural-original": "model",
    "dictionary-original": "dictionary",
    "character-original": "character",
}
PRIMARY_BINDINGS: Final = {
    "sample-real": "bank",
    "sample-procedural": "bank",
    "recipe-original": "recipe",
    "neural-original": "model",
    "dictionary-original": "dictionary",
    "character-original": "character",
}
RESOURCE_BINDING_KINDS: Final = frozenset(("bank", "recipe", "model", "vocoder", "dictionary", "character", "training-split"))
BINDING_KINDS: Final = frozenset((
    "sample", "bank", "recipe", "model", "vocoder", "dictionary", "character", "training-split",
    "backend", "processing", "provider", "precision", "helper", "runtime",
))
RESOURCE_FIELDS: Final = frozenset((
    "id", "resourceKind", "packageKind", "version", "packageSha256", "bindings", "dependencies",
    "languages", "backends", "platforms",
))
OPTIONAL_RESOURCE_FIELDS: Final = frozenset(("styles",))
INCOMPATIBILITY_FIELDS: Final = frozenset(("caseId", "dimension", "value", "reason"))
INCOMPATIBLE_DIMENSIONS: Final = frozenset(("language", "resource", "backend"))
SCOPE_KEYS: Final = (
    "language", "backend", "platform", "host", "workloadSha256", "sourceCommit", "buildId",
    "signedDeliverableSha256", "installedTreeSha256",
)
LOWER_HEX64: Final = re.compile(r"^[0-9a-f]{64}$")
IDENTITY: Final = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
PNG_SIGNATURE: Final = b"\x89PNG\r\n\x1a\n"
PNG_END: Final = b"\x00\x00\x00\x00IEND\xae\x42\x60\x82"
MAXIMUM_IMAGE_EDGE: Final = 32768
MAXIMUM_AUDIO_CHANNELS: Final = 64
AUDIO_SAMPLE_RATES: Final = range(8000, 384001)
PCM_BITS: Final = frozenset((16, 24, 32))
FLOAT_BITS: Final = frozenset((32, 64))
BLOCKED_CASE: Final = re.compile(r"full-product (?:case|observation) (R\d+\.[a-z0-9_-]+)")


@dataclass(frozen=True, slots=True)
class FullProductGateResult:
    """Outcome of the typed audit.

    passed is the semantic predicate.  authorizes_release additionally
    requires the canonical contract and candidate evidence; a complete
    engineering fixture can pass semantically but never authorizes release.
    """

    passed: bool
    errors: tuple[str, ...]
    blocked_case_ids: tuple[str, ...] = ()
    contract_mode: str = "INVALID"
    evidence_class: str | None = None

    @property
    def authorizes_release(self) -> bool:
        return self.passed and self.contract_mode == "CANONICAL" and self.evidence_class == CANDIDATE_EVIDENCE

    def as_dict(self) -> JsonObject:
        return {
            "passed": self.passed,
            "authorizesRelease": self.authorizes_release,
            "contractMode": self.contract_mode,
            "evidenceClass": self.evidence_class,
            "gateVersion": GATE_VERSION,
            "errors": list(self.errors),
            "blockedCaseIds": list(self.blocked_case_ids),
        }


def _hex(value: JsonValue) -> bool:
    return isinstance(value, str) and LOWER_HEX64.fullmatch(value) is not None


def _identity(value: JsonValue) -> bool:
    return isinstance(value, str) and IDENTITY.fullmatch(value) is not None


def _strings(value: JsonValue) -> list[str] | None:
    if not isinstance(value, list) or any(not isinstance(item, str) or not item for item in value):
        return None
    if len(value) != len(set(value)):
        return None
    return list(value)


def audio_errors(data: bytes, label: str) -> list[str]:
    """Reject anything that is not decodable, finite, non-silent RIFF/WAVE PCM."""

    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return [f"{label}: audio must be a RIFF/WAVE file"]
    if int.from_bytes(data[4:8], "little") + 8 != len(data):
        return [f"{label}: RIFF size differs from the retained bytes"]
    offset, fmt, payload = 12, None, None
    while offset + 8 <= len(data):
        chunk = data[offset:offset + 4]
        size = int.from_bytes(data[offset + 4:offset + 8], "little")
        start, end = offset + 8, offset + 8 + size
        if end > len(data):
            return [f"{label}: WAV chunk is truncated"]
        if chunk in (b"fmt ", b"data"):
            if (fmt if chunk == b"fmt " else payload) is not None:
                return [f"{label}: WAV repeats a {chunk.decode().strip()} chunk"]
            if chunk == b"fmt ":
                fmt = data[start:end]
            else:
                payload = memoryview(data)[start:end]
        offset = end + (size & 1)
    if offset != len(data):
        return [f"{label}: WAV has trailing bytes outside its chunks"]
    if fmt is None or len(fmt) < 16 or payload is None:
        return [f"{label}: WAV requires fmt and data chunks"]
    encoding, channels, rate, byte_rate, block_align, bits = struct.unpack("<HHIIHH", fmt[:16])
    if encoding == 0xFFFE:
        if len(fmt) != 40 or int.from_bytes(fmt[16:18], "little") != 22:
            return [f"{label}: WAVE_FORMAT_EXTENSIBLE header is invalid"]
        encoding = int.from_bytes(fmt[24:28], "little")
        if fmt[28:40] != bytes.fromhex("00001000800000aa00389b71") or int.from_bytes(fmt[18:20], "little") != bits:
            return [f"{label}: WAVE_FORMAT_EXTENSIBLE subtype or valid bits is unsupported"]
    if encoding not in (1, 3) or bits not in (PCM_BITS if encoding == 1 else FLOAT_BITS):
        return [f"{label}: audio must be integer PCM or IEEE float samples"]
    if not 1 <= channels <= MAXIMUM_AUDIO_CHANNELS or rate not in AUDIO_SAMPLE_RATES:
        return [f"{label}: audio channel count or sample rate is outside the supported range"]
    if block_align != channels * bits // 8 or byte_rate != rate * block_align:
        return [f"{label}: WAV block alignment or byte rate is inconsistent"]
    if not len(payload) or len(payload) % block_align:
        return [f"{label}: audio has no complete sample frames"]
    if encoding == 3:
        nonzero = False
        for (sample,) in struct.iter_unpack("<f" if bits == 32 else "<d", payload):
            if not math.isfinite(sample):
                return [f"{label}: float audio contains non-finite samples"]
            nonzero = nonzero or sample != 0.0
    else:
        nonzero = any(payload)
    if not nonzero:
        return [f"{label}: audio is digital silence and cannot demonstrate a capability"]
    return []


def ui_capture_errors(data: bytes, label: str) -> list[str]:
    """Validate bounded, noninterlaced 8-bit RGB/RGBA screenshot PNGs.

    Other PNG encodings are explicitly unsupported. This checks the complete
    compressed scanline stream, not whether the pixels depict product behavior.
    """
    if len(data) < 45 or data[:8] != PNG_SIGNATURE or data[12:16] != b"IHDR" or int.from_bytes(data[8:12], "big") != 13:
        return [f"{label}: UI capture must be a PNG image"]
    width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", data[16:29])
    if not (1 <= width <= MAXIMUM_IMAGE_EDGE and 1 <= height <= MAXIMUM_IMAGE_EDGE):
        return [f"{label}: UI capture dimensions are outside the supported range"]
    if zlib.crc32(data[12:29]) != int.from_bytes(data[29:33], "big"):
        return [f"{label}: UI capture header checksum is invalid"]
    if depth != 8 or color not in (2, 6) or (compression, filtering, interlace) != (0, 0, 0):
        return [f"{label}: UI capture requires noninterlaced 8-bit RGB or RGBA PNG"]
    stride = width * (3 if color == 2 else 4) + 1
    expected = height * stride
    if expected > 64 * 1024 * 1024:
        return [f"{label}: UI capture decoded bytes exceed the 64 MiB limit"]
    offset, started, ended, finished = 33, False, False, False
    decoder = zlib.decompressobj()
    decoded = bytearray()
    while offset + 12 <= len(data):
        size = int.from_bytes(data[offset:offset + 4], "big")
        kind = data[offset + 4:offset + 8]
        end = offset + 8 + size
        if end + 4 > len(data):
            return [f"{label}: UI capture chunk is truncated"]
        if zlib.crc32(data[offset + 4:end]) != int.from_bytes(data[end:end + 4], "big"):
            return [f"{label}: UI capture chunk checksum is invalid"]
        if kind == b"IDAT":
            if ended:
                return [f"{label}: UI capture IDAT chunks must be consecutive"]
            started = True
            try:
                decoded.extend(decoder.decompress(data[offset + 8:end], expected - len(decoded) + 1))
            except zlib.error:
                return [f"{label}: UI capture compressed scanlines are invalid"]
            if len(decoded) > expected or decoder.unconsumed_tail or decoder.unused_data:
                return [f"{label}: UI capture compressed stream exceeds its declared image"]
        else:
            ended = started
            if kind == b"IEND":
                if size or end + 4 != len(data):
                    return [f"{label}: UI capture has an invalid end or trailing bytes"]
                finished = True
                break
            if kind == b"PLTE":
                if started or not size or size % 3 or size > 768:
                    return [f"{label}: UI capture palette is invalid"]
            elif not all(65 <= ch <= 90 or 97 <= ch <= 122 for ch in kind) or not kind[0] & 32:
                return [f"{label}: UI capture has an unsupported critical chunk"]
        offset = end + 4
    if not finished or not started or not decoder.eof or len(decoded) != expected:
        return [f"{label}: UI capture is truncated or has missing scanlines"]
    if any(decoded[row * stride] > 4 for row in range(height)):
        return [f"{label}: UI capture has an invalid scanline filter"]
    return []


def project_document_errors(data: bytes, label: str) -> list[str]:
    """Check the project envelope only; native codec replay is still required."""

    try:
        project = _parse_json(data)
    except FullProductReportError as error:
        return [f"{label}: project is not valid JSON: {error}"]
    if not isinstance(project, dict) or project.get("formatId") != "com.project-seam.project":
        return [f"{label}: project must be a com.project-seam.project document"]
    version, ppq = project.get("schemaVersion"), project.get("ppq")
    if not isinstance(version, int) or isinstance(version, bool) or version < 1:
        return [f"{label}: project schemaVersion is invalid"]
    if not isinstance(ppq, int) or isinstance(ppq, bool) or not 1 <= ppq <= 32767:
        return [f"{label}: project ppq is invalid"]
    if not isinstance(project.get("projectId"), str) or not project["projectId"]:
        return [f"{label}: project projectId is required"]
    if not isinstance(project.get("tempoMap"), list) or not project["tempoMap"]:
        return [f"{label}: project tempoMap is required"]
    return []


def approval_scope(observation: JsonObject, case_id: str) -> JsonObject:
    """Exact resource/workload/revision scope that a retained review approves."""

    bindings = observation.get("bindings")
    ordered = sorted(
        (item for item in bindings if isinstance(item, dict)),
        key=lambda item: (str(item.get("kind")), str(item.get("id"))),
    ) if isinstance(bindings, list) else []
    resource_ids = observation.get("resourceIds")
    scope: JsonObject = {
        "caseId": case_id,
        "resourceIds": sorted(item for item in resource_ids if isinstance(item, str)) if isinstance(resource_ids, list) else [],
        "bindingsSha256": _sha256_json(ordered),
    }
    scope.update({key: observation.get(key) for key in SCOPE_KEYS})
    return scope


def released_resources(contract: JsonValue) -> tuple[dict[str, JsonObject], list[str]]:
    """Validate the frozen typed resource matrix.

    This is the descriptor interface for U14's typed candidates: resource
    kind, package kind (sample, recipe or model, plus dictionary and
    character), package digest, exact content bindings and dependency set.
    """

    scope = contract.get("scope") if isinstance(contract, dict) else None
    entries = scope.get("releasedResources") if isinstance(scope, dict) else None
    if not isinstance(entries, list) or not entries:
        return {}, ["full-product resource matrix declares no released resources"]
    errors: list[str] = []
    resources: dict[str, JsonObject] = {}
    for index, entry in enumerate(entries):
        label = f"full-product released resource [{index}]"
        if not isinstance(entry, dict):
            errors.append(f"{label} must be an object")
            continue
        identifier = entry.get("id")
        if not _identity(identifier):
            errors.append(f"{label}.id is not a valid resource identity")
            continue
        label = f"full-product released resource {identifier}"
        keys = set(entry)
        if not RESOURCE_FIELDS <= keys or keys - RESOURCE_FIELDS - OPTIONAL_RESOURCE_FIELDS:
            errors.append(f"{label}: fields differ from the typed resource descriptor")
        if identifier in resources:
            errors.append(f"{label}: duplicate released resource")
            continue
        kind = entry.get("resourceKind")
        if not isinstance(kind, str) or kind not in RESOURCE_KINDS:
            errors.append(f"{label}: resourceKind is unknown")
        elif entry.get("packageKind") != PACKAGE_KINDS[kind]:
            errors.append(f"{label}: packageKind must be {PACKAGE_KINDS[kind]} for {kind}")
        if not isinstance(entry.get("version"), str) or not entry["version"]:
            errors.append(f"{label}: version is required")
        if not _hex(entry.get("packageSha256")):
            errors.append(f"{label}: packageSha256 must be a lowercase SHA-256 digest")
        bindings = entry.get("bindings")
        binding_keys: set[tuple[str, str]] = set()
        if not isinstance(bindings, list) or not bindings:
            errors.append(f"{label}: exact content bindings are required")
            bindings = []
        for binding in bindings:
            if (
                not isinstance(binding, dict)
                or set(binding) != {"kind", "id", "version", "sha256"}
                or not isinstance(binding.get("kind"), str)
                or binding.get("kind") not in BINDING_KINDS
                or not isinstance(binding.get("id"), str) or not binding["id"]
                or not isinstance(binding.get("version"), str) or not binding["version"]
                or not _hex(binding.get("sha256"))
            ):
                errors.append(f"{label}: every binding requires a known kind, id, version and digest")
                continue
            key = (binding["kind"], binding["id"])
            if key in binding_keys:
                errors.append(f"{label}: binding {key[0]} {key[1]} is duplicated")
            binding_keys.add(key)
        if isinstance(kind, str) and kind in PRIMARY_BINDINGS and PRIMARY_BINDINGS[kind] not in {key[0] for key in binding_keys}:
            errors.append(f"{label}: does not bind its primary {PRIMARY_BINDINGS[kind]} content")
        dependencies = entry.get("dependencies")
        if not isinstance(dependencies, list):
            errors.append(f"{label}: dependency set must be an array")
        else:
            seen: set[str] = set()
            for dependency in dependencies:
                if (
                    not isinstance(dependency, dict)
                    or set(dependency) != {"id", "version", "sha256"}
                    or not _identity(dependency.get("id"))
                    or not isinstance(dependency.get("version"), str) or not dependency["version"]
                    or not _hex(dependency.get("sha256"))
                ):
                    errors.append(f"{label}: every dependency requires id, version and digest")
                    continue
                if dependency["id"] in seen:
                    errors.append(f"{label}: dependency {dependency['id']} is duplicated")
                seen.add(dependency["id"])
        languages = _strings(entry.get("languages"))
        if languages is None or not set(languages) <= set(LANGUAGES) or (not languages and kind != "character-original"):
            errors.append(f"{label}: languages must be a unique subset of the frozen scope languages")
        backends = _strings(entry.get("backends"))
        if not backends or not set(backends) <= set(BACKENDS):
            errors.append(f"{label}: backends must be a non-empty subset of the frozen scope backends")
        platforms = _strings(entry.get("platforms"))
        if platforms is None or set(platforms) != set(PLATFORMS):
            errors.append(f"{label}: every released resource must be released on every supported platform")
        if "styles" in entry and _strings(entry.get("styles")) is None:
            errors.append(f"{label}: styles must be unique non-empty strings")
        resources[identifier] = entry
    for identifier, entry in resources.items():
        dependencies = entry.get("dependencies")
        for dependency in dependencies if isinstance(dependencies, list) else []:
            if isinstance(dependency, dict) and isinstance(dependency.get("id"), str) and dependency.get("id") in resources:
                if dependency.get("sha256") != resources[dependency["id"]].get("packageSha256"):
                    errors.append(
                        f"full-product released resource {identifier}: dependency {dependency['id']} "
                        "differs from its released package digest"
                    )
    return resources, errors


def declared_incompatibilities(contract: JsonValue) -> tuple[dict[str, dict[str, set[str]]], list[str]]:
    """Read the frozen matrix's feature-specific incompatibility declarations.

    Only a case's language, resource-kind or backend value may be declared
    unsupported, with a reason, and at least one supported value must remain.
    Platform and host support can never be waived.
    """

    scope = contract.get("scope") if isinstance(contract, dict) else None
    entries = scope.get("declaredIncompatibilities", []) if isinstance(scope, dict) else []
    rows = contract.get("cases", []) if isinstance(contract, dict) else []
    cases = {row["id"]: row for row in rows if isinstance(row, dict) and isinstance(row.get("id"), str)}
    result: dict[str, dict[str, set[str]]] = {}
    errors: list[str] = []
    if not isinstance(entries, list):
        return {}, ["full-product declaredIncompatibilities must be an array"]
    for index, entry in enumerate(entries):
        label = f"full-product declared incompatibility [{index}]"
        if not isinstance(entry, dict) or set(entry) != INCOMPATIBILITY_FIELDS:
            errors.append(f"{label}: caseId, dimension, value and reason are required")
            continue
        case = cases.get(entry.get("caseId")) if isinstance(entry.get("caseId"), str) else None
        dimension, value = entry.get("dimension"), entry.get("value")
        if case is None or not isinstance(dimension, str) or dimension not in INCOMPATIBLE_DIMENSIONS or not isinstance(value, str):
            errors.append(f"{label}: only a known case language, resource or backend may be declared unsupported")
            continue
        if not isinstance(entry.get("reason"), str) or not entry["reason"].strip():
            errors.append(f"{label}: a reviewed reason is required")
        dimensions = case.get("dimensions") if isinstance(case.get("dimensions"), dict) else {}
        declared = dimensions.get(dimension, [])
        declared = [item for item in declared if isinstance(item, str)] if isinstance(declared, list) else []
        allowed = set(LANGUAGES) if dimension == "language" and "each-declared-language" in declared else set(declared) - {"all-released"}
        if value not in allowed:
            errors.append(f"{label}: {value} is not a declared {dimension} of {case['id']}")
            continue
        result.setdefault(case["id"], {}).setdefault(dimension, set()).add(value)
        if result[case["id"]][dimension] >= allowed and "all-released" not in declared:
            errors.append(f"{label}: {case['id']} cannot declare every {dimension} unsupported; a mandatory feature needs a supported tested combination")
    return result, errors


class _Evidence:
    """Bounded no-follow reads of retained evidence, memoized for one audit."""

    def __init__(self, base: Path, soak_context: SoakReplayContext | None = None) -> None:
        self.soak_context = soak_context
        self.base = base
        self.resolved_base = base.resolve()
        self._cache: dict[tuple[str, str], bytes] = {}
        self._failures: dict[tuple[str, str], str] = {}
        self._cached_bytes = 0

    def read(self, reference: JsonValue, label: str) -> tuple[bytes | None, str | None]:
        if not isinstance(reference, dict) or set(reference) != {"locator", "sha256"}:
            return None, f"{label} requires exactly locator and sha256"
        if not isinstance(reference.get("locator"), str) or not reference["locator"] or not _hex(reference.get("sha256")):
            return None, f"{label} requires a string locator and lowercase SHA-256 digest"
        key = (reference["locator"], reference["sha256"])
        try:
            # The typed soak dispatcher owns aliases, read failures and snapshots.
            # Consult it even if this adapter previously cached a generic read.
            reused = reuse_reference(reference, base=self.base,
                maximum_bytes=256 * 1024 * 1024, replay_context=self.soak_context)
            if reused is not None:
                return reused, None
            if key in self._failures:
                return None, self._failures[key]
            cached = self._cache.get(key)
            if cached is not None:
                return cached, None
            path = _safe_reference_path(reference.get("locator"), self.base, label)
            try:
                path.resolve().relative_to(self.resolved_base)
            except ValueError as error:
                raise FullProductReportError(f"{label}: reference escapes the restored report root") from error
            contents = _read_regular_reference(reference, base=self.base, label=label,
                maximum_bytes=256 * 1024 * 1024 - self._cached_bytes)
        except (FullProductReportError, OSError, ValueError) as error:
            self._failures[key] = str(error)
            return None, str(error)
        self._cached_bytes += len(contents)
        self._cache[key] = contents
        return contents, None

    def record(self, reference: JsonValue, label: str, record_type: str) -> tuple[JsonObject | None, str | None]:
        contents, error = self.read(reference, label)
        if contents is None:
            return None, error
        try:
            value = _parse_json(contents)
        except FullProductReportError as parse_error:
            return None, f"{label}: {parse_error}"
        if not isinstance(value, dict) or value.get("recordType") != record_type:
            return None, f"{label}: retained record must be a {record_type} object"
        return value, None


def _list(value: JsonValue) -> list[JsonValue]:
    return value if isinstance(value, list) else []


def _number(value: JsonValue) -> bool:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return False
    try:
        return math.isfinite(value)
    except OverflowError:
        return False


class _TypedAudit:
    """Executes the typed validators over one report that passed the reader."""

    def __init__(self, report: JsonObject, contract: JsonObject, candidate: JsonObject | None, base: Path, soak_context: SoakReplayContext | None = None) -> None:
        self.report = report
        self.contract = contract
        self.candidate = candidate
        self.evidence = _Evidence(base, soak_context)
        self.errors: list[str] = []
        self.blocked: set[str] = set()
        self.evidence_class = report.get("evidenceClass")
        self.root_id = report.get("candidateRootId")
        self.cases = {row["id"]: row for row in _list(contract.get("cases")) if isinstance(row, dict) and isinstance(row.get("id"), str)}
        profile = contract.get("evaluationProfile")
        criteria = profile.get("criteria") if isinstance(profile, dict) else None
        self.criteria = {row["id"]: row for row in _list(criteria) if isinstance(row, dict) and isinstance(row.get("id"), str)}
        catalog = contract.get("protocolCatalog")
        review_protocol = catalog.get("independent-review") if isinstance(catalog, dict) else None
        self.review_protocol_sha256 = _sha256_json(review_protocol) if isinstance(review_protocol, dict) else None
        self.resources: dict[str, JsonObject] = {}
        self.incompatible: dict[str, dict[str, set[str]]] = {}
        self.reviewers: dict[str, JsonObject] = {}
        self.producers: dict[str, JsonObject] = {}
        self.installed: dict[str, tuple[JsonValue, JsonValue]] | None = None
        self.resource_use: dict[str, dict[str, set[str]]] = {}
        self.creators: dict[str, dict[str, JsonObject]] = {}
        self.creator_claims: list[tuple[str, str, JsonValue]] = []
        self.assisted_claims: list[tuple[str, str]] = []

    def fail(self, message: str, case_id: str | None = None) -> None:
        self.errors.append(message)
        if case_id is not None:
            self.blocked.add(case_id)

    def run(self) -> None:
        self._contract_identity()
        if _contract_mode(self.contract) == "CANONICAL":
            self.fail(U45_RECONCILIATION_HOLD)
        self.resources, errors = released_resources(self.contract)
        self.errors.extend(errors)
        self.incompatible, errors = declared_incompatibilities(self.contract)
        self.errors.extend(errors)
        self._resource_kind_availability()
        self._registry()
        self._installed_targets()
        for row in _list(self.report.get("cases")):
            if isinstance(row, dict) and row.get("id") in self.cases:
                self._case(row)
        self._resource_coverage()
        self._creator_derivations()

    def _contract_identity(self) -> None:
        contract_id = self.contract.get("contractId")
        admitted = ADMITTED_EVIDENCE.get(contract_id) if isinstance(contract_id, str) else None
        if admitted is None:
            self.fail("full-product contract identity is neither the canonical nor the synthetic fixture contract")
        elif self.evidence_class != admitted:
            self.fail(
                f"full-product report evidenceClass {self.evidence_class} is not admitted by {contract_id}; "
                "the canonical contract admits only CANDIDATE_EVIDENCE and the synthetic contract only ENGINEERING_FIXTURE"
            )
        if self.contract.get("semanticValidation") != SEMANTIC_VALIDATION:
            self.fail(
                "full-product contract semanticValidation does not record the implemented U45 typed validator; "
                "the canonical metadata needs a deliberate accepted contract revision"
            )

    def _envelope(self, record: JsonObject, label: str, case_id: str | None = None) -> None:
        if record.get("evidenceClass") != self.evidence_class:
            self.fail(f"{label}: evidenceClass differs from the report", case_id)
        if record.get("candidateRootId") != self.root_id:
            self.fail(f"{label}: candidateRootId differs from the report", case_id)
        if case_id is not None and record.get("caseId") != case_id:
            self.fail(f"{label}: caseId differs from its case", case_id)

    def _resource_kind_availability(self) -> None:
        available = {entry.get("resourceKind") for entry in self.resources.values()}
        for case_id, case in self.cases.items():
            dimensions = case.get("dimensions") if isinstance(case.get("dimensions"), dict) else {}
            excused = self.incompatible.get(case_id, {}).get("resource", set())
            for kind in _list(dimensions.get("resource")):
                if kind != "all-released" and kind not in available and kind not in excused:
                    self.fail(f"full-product case {case_id}: the resource matrix releases no {kind} resource", case_id)

    def _registry(self) -> None:
        record, error = self.evidence.record(self.report.get("reviewerRegistry"), "full-product reviewerRegistry", REGISTRY_RECORD)
        if record is None:
            self.fail(str(error))
            return
        self._envelope(record, "full-product reviewerRegistry")
        reviewers, producers = record.get("reviewers"), record.get("producers")
        if not isinstance(reviewers, list) or not reviewers or not isinstance(producers, list) or not producers:
            self.fail("full-product reviewerRegistry requires registered reviewers and producers")
            return
        for entry in reviewers:
            roles = _strings(entry.get("roles")) if isinstance(entry, dict) else None
            languages = _strings(entry.get("nativeLanguages")) if isinstance(entry, dict) else None
            if (
                not isinstance(entry, dict)
                or set(entry) != {"reviewerId", "kind", "roles", "nativeLanguages"}
                or not _identity(entry.get("reviewerId"))
                or entry.get("kind") not in {"HUMAN", "GENERATOR"}
                or not roles or not set(roles) <= REVIEW_ROLES
                or languages is None or not set(languages) <= set(LANGUAGES)
            ):
                self.fail("full-product reviewerRegistry: every reviewer needs a pseudonymous id, kind, qualified roles and native languages")
                continue
            if entry["reviewerId"] in self.reviewers:
                self.fail(f"full-product reviewerRegistry: reviewer {entry['reviewerId']} is duplicated")
            self.reviewers[entry["reviewerId"]] = entry
        for entry in producers:
            if (
                not isinstance(entry, dict)
                or set(entry) != {"producerId", "kind", "affiliation"}
                or not _identity(entry.get("producerId"))
                or entry.get("kind") not in {"HUMAN", "GENERATOR"}
                or entry.get("affiliation") not in {"IMPLEMENTER", "INDEPENDENT"}
            ):
                self.fail("full-product reviewerRegistry: every producer needs a pseudonymous id, kind and affiliation")
                continue
            if entry["producerId"] in self.producers:
                self.fail(f"full-product reviewerRegistry: producer {entry['producerId']} is duplicated")
            self.producers[entry["producerId"]] = entry
        overlap = sorted(set(self.reviewers) & set(self.producers))
        if overlap:
            self.fail(
                "full-product reviewerRegistry: " + ", ".join(overlap)
                + " cannot be both producer and reviewer; producer and reviewer must be distinct people"
            )

    def _installed_targets(self) -> None:
        if not isinstance(self.candidate, dict):
            return
        self.installed = {}
        for record in _list(self.candidate.get("evidence")):
            if not isinstance(record, dict) or record.get("requirementId") != "EB-009-full-product" or record.get("status") != "PASS":
                continue
            platform = f"{record.get('platform')}-{record.get('architecture')}"
            value = (record.get("installedTreeSha256"), record.get("finalDeliverableSha256"))
            if self.installed.get(platform, value) != value:
                self.fail(f"candidate EB-009 evidence binds conflicting installed bytes for {platform}")
            self.installed[platform] = value

    def _case(self, row: JsonObject) -> None:
        case_id = row["id"]
        case = self.cases[case_id]
        result_type = REQUIREMENTS[case_id.split(".", 1)[0]].result_type
        coverage: dict[str, set[str]] = {key: set() for key in ("language", "resource", "backend", "platform", "host")}
        measured: set[str] = set()
        for index, observation in enumerate(_list(row.get("observations"))):
            if isinstance(observation, dict):
                label = f"full-product observation {case_id}[{index}]"
                self._observation(case_id, case, result_type, observation, label, coverage, measured)
        self._case_coverage(case_id, case, result_type, coverage)
        declared = {item for item in _list(case.get("criteriaIds")) if isinstance(item, str)}
        unfilled = sorted(declared - measured)
        if unfilled:
            self.fail(f"full-product case {case_id}: acceptance criteria are unfilled: {', '.join(unfilled)}", case_id)

    def _observation(
        self,
        case_id: str,
        case: JsonObject,
        result_type: str,
        observation: JsonObject,
        label: str,
        coverage: dict[str, set[str]],
        measured: set[str],
    ) -> None:
        workload = case.get("workload")
        workload_id = workload.get("id") if isinstance(workload, dict) else None
        if observation.get("workloadId") != workload_id or observation.get("workloadSha256") != _sha256_json(workload):
            self.fail(f"{label}: workload identity differs from the frozen case workload", case_id)
        language, backend, platform, host = (observation.get(key) for key in ("language", "backend", "platform", "host"))
        resource_ids = [item for item in _list(observation.get("resourceIds")) if isinstance(item, str)]
        if len(set(resource_ids)) != len(resource_ids):
            self.fail(f"{label}: resourceIds repeat a resource", case_id)
        resources = [self.resources[item] for item in dict.fromkeys(resource_ids) if item in self.resources]
        dimensions = case.get("dimensions") if isinstance(case.get("dimensions"), dict) else {}
        allowed = set(_list(dimensions.get("resource")))
        primary = [entry for entry in resources if "all-released" in allowed or entry.get("resourceKind") in allowed]
        if not primary:
            self.fail(f"{label}: exercises no released resource of the case's resource kinds", case_id)
        for entry in resources:
            if platform not in _list(entry.get("platforms")):
                self.fail(f"{label}: resource {entry.get('id')} is not released for {platform}", case_id)
        if backend != "native" and primary and not any(backend in _list(entry.get("backends")) for entry in primary):
            self.fail(f"{label}: no exercised resource supports the {backend} backend", case_id)
        singers = [entry for entry in primary if entry.get("resourceKind") in SINGER_KINDS]
        if language == "language-independent":
            if result_type in LANGUAGE_BEARING_RESULTS:
                self.fail(f"{label}: a sung {result_type} result requires a concrete language", case_id)
        elif singers and not any(language in _list(entry.get("languages")) for entry in singers):
            self.fail(f"{label}: no exercised singer resource declares language {language}", case_id)
        for key, value in (("language", language), ("backend", backend), ("platform", platform), ("host", host)):
            coverage[key].add(str(value))
        coverage["resource"].update(str(entry.get("resourceKind")) for entry in primary)
        for entry in resources:
            use = self.resource_use.setdefault(str(entry.get("id")), {"languages": set(), "platforms": set()})
            use["languages"].add(str(language))
            use["platforms"].add(str(platform))
        self._bindings(observation, resources, label, case_id)
        self._installed_identity(observation, label, case_id)
        digests = self._artifacts(observation, resources, label, case_id)
        self._reviews(observation, digests, label, case_id)
        self._measurements(observation, result_type, digests, measured, label, case_id)
        self._creator_study(observation, label, case_id)
        self._checks(observation, label, case_id)

    def _bindings(self, observation: JsonObject, resources: list[JsonObject], label: str, case_id: str) -> None:
        actual = {
            (binding.get("kind"), binding.get("id")): binding
            for binding in _list(observation.get("bindings")) if isinstance(binding, dict)
        }
        declared = {
            (binding.get("kind"), binding.get("id")): binding
            for entry in resources for binding in _list(entry.get("bindings")) if isinstance(binding, dict)
        }
        for key, expected in declared.items():
            found = actual.get(key)
            if found is None:
                self.fail(f"{label}: does not bind released {key[0]} {key[1]}", case_id)
            elif (found.get("version"), found.get("sha256")) != (expected.get("version"), expected.get("sha256")):
                self.fail(f"{label}: stale or substituted {key[0]} {key[1]} differs from the frozen resource matrix", case_id)
        for key in actual:
            if key[0] in RESOURCE_BINDING_KINDS and key not in declared:
                self.fail(f"{label}: binds {key[0]} {key[1]} that no exercised released resource declares", case_id)

    def _installed_identity(self, observation: JsonObject, label: str, case_id: str) -> None:
        if self.installed is None:
            return
        platform = str(observation.get("platform"))
        expected = self.installed.get(platform)
        if expected is None:
            self.fail(f"{label}: candidate has no EB-009 installed evidence for {platform}", case_id)
        elif (observation.get("installedTreeSha256"), observation.get("signedDeliverableSha256")) != expected:
            self.fail(f"{label}: installed or signed bytes differ from the candidate's {platform} installation", case_id)

    def _artifacts(self, observation: JsonObject, resources: list[JsonObject], label: str, case_id: str) -> dict[str, set[str]]:
        digests: dict[str, set[str]] = {}
        installed: list[tuple[str, JsonObject]] = []
        logs: list[tuple[str, JsonObject]] = []
        validators = {"audio": audio_errors, "ui-capture": ui_capture_errors, "project": project_document_errors}
        for index, artifact in enumerate(_list(observation.get("artifacts"))):
            if not isinstance(artifact, dict):
                continue
            kind = str(artifact.get("kind"))
            digests.setdefault(kind, set()).add(str(artifact.get("sha256")))
            reference = {"locator": artifact.get("locator"), "sha256": artifact.get("sha256")}
            item_label = f"{label}.artifacts[{index}] {kind}"
            if kind in validators:
                contents, error = self.evidence.read(reference, item_label)
                if contents is None:
                    self.fail(str(error), case_id)
                else:
                    for problem in validators[kind](contents, item_label):
                        self.fail(problem, case_id)
            elif kind in ("installed-resource", "session-log"):
                record_type = INSTALLED_RESOURCE_RECORD if kind == "installed-resource" else SESSION_LOG_RECORD
                record, error = self.evidence.record(reference, item_label, record_type)
                if record is None:
                    self.fail(str(error), case_id)
                else:
                    (installed if kind == "installed-resource" else logs).append((item_label, record))
        digests["*"] = set().union(*digests.values()) if digests else set()
        self._installed_resources(observation, installed, resources, label, case_id)
        self._session_logs(logs, digests, label, case_id)
        return digests

    def _installed_resources(
        self,
        observation: JsonObject,
        records: list[tuple[str, JsonObject]],
        resources: list[JsonObject],
        label: str,
        case_id: str,
    ) -> None:
        installed: set[str] = set()
        for item_label, record in records:
            self._envelope(record, item_label, case_id)
            resource_id = record.get("resourceId")
            resource = self.resources.get(resource_id) if isinstance(resource_id, str) else None
            if resource is None or resource_id not in _list(observation.get("resourceIds")):
                self.fail(f"{item_label}: installs a resource the observation does not exercise", case_id)
                continue
            if record.get("packageSha256") != resource.get("packageSha256"):
                self.fail(f"{item_label}: installed package digest differs from the frozen resource matrix", case_id)
            if record.get("platform") != observation.get("platform") or record.get("installedTreeSha256") != observation.get("installedTreeSha256"):
                self.fail(f"{item_label}: installation belongs to another platform or installed tree", case_id)
            if not _hex(record.get("installReceiptSha256")):
                self.fail(f"{item_label}: installReceiptSha256 is required", case_id)
            installed.add(resource_id)
        if records:
            missing = sorted(str(entry.get("id")) for entry in resources if entry.get("id") not in installed)
            if missing:
                self.fail(f"{label}: exercised resources lack installed-resource receipts: {', '.join(missing)}", case_id)

    def _session_logs(self, logs: list[tuple[str, JsonObject]], digests: dict[str, set[str]], label: str, case_id: str) -> None:
        projects, captures = digests.get("project", set()), digests.get("ui-capture", set())
        bound: set[str] = set()
        for item_label, record in logs:
            self._envelope(record, item_label, case_id)
            operations = record.get("operations")
            if not isinstance(operations, list) or not operations:
                self.fail(f"{item_label}: session log records no operations", case_id)
                continue
            for operation in operations:
                operation_id = operation.get("operationId") if isinstance(operation, dict) else None
                if not isinstance(operation_id, str) or not operation_id:
                    self.fail(f"{item_label}: every operation requires an operationId", case_id)
                    continue
                before, after = operation.get("beforeProjectSha256"), operation.get("afterProjectSha256")
                shots = _strings(operation.get("captureSha256s"))
                if shots is None or not set(shots) <= captures:
                    self.fail(f"{item_label}: operation {operation_id} names captures that were not retained", case_id)
                elif before is None and after is None:
                    # A run without project state (for example a soak) may be
                    # logged, but it cannot vouch for any UI capture.
                    if shots:
                        self.fail(f"{item_label}: operation {operation_id} binds captures without a project state change", case_id)
                elif before not in projects or after not in projects:
                    self.fail(f"{item_label}: operation {operation_id} is not bound to retained project states", case_id)
                elif before != after:
                    bound.update(shots)
        for capture in sorted(captures - bound):
            self.fail(
                f"{label}: UI capture {capture[:12]} is not bound to an observed project state change; "
                "a screenshot alone is not proof",
                case_id,
            )

    def _reviews(self, observation: JsonObject, digests: dict[str, set[str]], label: str, case_id: str) -> None:
        reviews = [item for item in _list(observation.get("reviews")) if isinstance(item, dict)]
        producers = {review.get("producerId") for review in reviews}
        if len(producers) > 1:
            self.fail(f"{label}: reviews name different producers for the same material", case_id)
        scope = approval_scope(observation, case_id)
        audio = digests.get("audio", set())
        retained: set[str] = set()
        for index, review in enumerate(reviews):
            item_label = f"{label}.reviews[{index}]"
            reviewer_id, role = review.get("reviewerId"), review.get("role")
            reviewer = self.reviewers.get(reviewer_id) if isinstance(reviewer_id, str) else None
            if reviewer is None:
                self.fail(f"{item_label}: reviewer is not in the reviewer registry", case_id)
            elif reviewer.get("kind") != "HUMAN":
                self.fail(f"{item_label}: a generator identity cannot approve generated material", case_id)
            elif role not in _list(reviewer.get("roles")):
                self.fail(f"{item_label}: reviewer is not qualified for role {role}", case_id)
            if reviewer_id in self.producers or reviewer_id in producers:
                self.fail(f"{item_label}: self-approved claim; the reviewer also produced candidate material", case_id)
            if review.get("producerId") not in self.producers:
                self.fail(f"{item_label}: producer is not in the reviewer registry", case_id)
            if role == "native-language-reviewer" and (
                review.get("language") != observation.get("language")
                or reviewer is None
                or observation.get("language") not in _list(reviewer.get("nativeLanguages"))
            ):
                self.fail(f"{item_label}: native-language review requires a reviewer native in the observed language", case_id)
            if review.get("rubricSha256") != self.review_protocol_sha256:
                self.fail(f"{item_label}: rubric differs from the frozen independent-review protocol", case_id)
            reference = review.get("rawEvidence")
            if isinstance(reference, dict):
                retained.add(str(reference.get("sha256")))
            record, error = self.evidence.record(reference, f"{item_label}.rawEvidence", REVIEW_RECORD)
            if record is None:
                self.fail(str(error), case_id)
                continue
            self._envelope(record, f"{item_label}.rawEvidence", case_id)
            if record.get("approvalScope") != scope:
                self.fail(f"{item_label}: approval scope differs from the exact resource, workload and revision observed", case_id)
            reviewed = _strings(record.get("reviewedArtifacts"))
            if not reviewed or not set(reviewed) <= digests["*"]:
                self.fail(f"{item_label}: review must name the retained material it judged", case_id)
            elif role in MUSICAL_REVIEW_ROLES and not audio <= set(reviewed):
                self.fail(f"{item_label}: musical judgment does not cover every retained audio artifact", case_id)
        if retained != digests.get("independent-review", set()):
            self.fail(f"{label}: independent-review artifacts must be exactly the retained review records", case_id)

    def _measurements(
        self,
        observation: JsonObject,
        result_type: str,
        digests: dict[str, set[str]],
        measured: set[str],
        label: str,
        case_id: str,
    ) -> None:
        requirement = case_id.split(".", 1)[0]
        audio = digests.get("audio", set())
        for index, measurement in enumerate(_list(observation.get("measurements"))):
            if not isinstance(measurement, dict):
                continue
            item_label = f"{label}.measurements[{index}]"
            criterion = measurement.get("criterionId")
            reference = measurement.get("rawEvidence")
            record, error = self.evidence.record(reference, f"{item_label}.rawEvidence", MEASUREMENT_RECORD)
            if record is None:
                self.fail(str(error), case_id)
                continue
            self._envelope(record, f"{item_label}.rawEvidence", case_id)
            measured.add(str(criterion))
            if isinstance(reference, dict) and reference.get("sha256") not in digests.get("measurement", set()):
                self.fail(f"{item_label}: measurement record is not retained as a measurement artifact", case_id)
            inputs = _strings(record.get("inputs"))
            if not inputs or not set(inputs) <= digests["*"]:
                self.fail(f"{item_label}: measurement must name the retained artifacts it measured", case_id)
            elif result_type in AUDIO_MEASURED_RESULTS and not set(inputs) & audio:
                self.fail(f"{item_label}: acoustic measurement does not measure any retained audio", case_id)
            definition = self.criteria.get(criterion) if isinstance(criterion, str) else None
            if not isinstance(definition, dict):
                continue
            if definition.get("kind") == "protocol":
                self._protocol(measurement, record, definition, digests, item_label, case_id)
            elif definition.get("kind") == "empirical":
                self._empirical(observation, measurement, record, definition, item_label, case_id)
            if criterion == "creator-count":
                self.creator_claims.append((case_id, requirement, measurement.get("value")))
            if criterion == "assisted-comparison":
                self.assisted_claims.append((case_id, requirement))

    def _protocol(
        self,
        measurement: JsonObject,
        record: JsonObject,
        definition: JsonObject,
        digests: dict[str, set[str]],
        label: str,
        case_id: str,
    ) -> None:
        value = measurement.get("value")
        if measurement.get("unit") != PROTOCOL_UNIT or isinstance(value, bool) or value != 1:
            self.fail(f"{label}: protocol conformance must be reported as value 1 {PROTOCOL_UNIT}", case_id)
        protocol = definition.get("protocol")
        if not isinstance(protocol, dict) or record.get("protocol") != {"id": protocol.get("id"), "sha256": _sha256_json(protocol)}:
            self.fail(f"{label}: conformance record is not bound to the frozen protocol definition", case_id)
            return
        constraints = protocol.get("constraints") if isinstance(protocol.get("constraints"), dict) else {}
        results = record.get("constraintResults")
        if not isinstance(results, dict) or set(results) != set(constraints):
            self.fail(f"{label}: every protocol constraint must be evaluated exactly once", case_id)
            return
        for key, result in results.items():
            evidence = _strings(result.get("evidence")) if isinstance(result, dict) else None
            if not isinstance(result, dict) or set(result) != {"status", "evidence"} or result.get("status") != "MET":
                self.fail(f"{label}: protocol constraint {key} is not MET", case_id)
            elif not evidence or not set(evidence) <= digests["*"]:
                self.fail(f"{label}: protocol constraint {key} is not backed by retained artifacts", case_id)

    def _empirical(
        self,
        observation: JsonObject,
        measurement: JsonObject,
        record: JsonObject,
        definition: JsonObject,
        label: str,
        case_id: str,
    ) -> None:
        frozen = definition.get("value")
        cells = {cell.get("id"): cell for cell in _list(frozen.get("cells")) if isinstance(cell, dict)} if isinstance(frozen, dict) else {}
        cell = cells.get(record.get("cellId"))
        if definition.get("status") != "RESOLVED" or cell is None:
            self.fail(f"{label}: measurement is not bound to a frozen {definition.get('id')} threshold cell", case_id)
            return
        dimensions = cell.get("dimensions") if isinstance(cell.get("dimensions"), dict) else {}
        for key in ("platform", "backend", "language"):
            if key in dimensions and dimensions[key] != observation.get(key):
                self.fail(f"{label}: threshold cell {cell.get('id')} belongs to another {key}", case_id)
                return
        if cell.get("valueType") == "machine-profile":
            value = cell.get("value")
            profile = value.get("profile") if isinstance(value, dict) else None
            expected = profile.get("sha256") if isinstance(profile, dict) else None
            if expected is None or not record.get("machineProfileSha256") == observation.get("machineProfileSha256") == expected:
                self.fail(f"{label}: observation did not run on the frozen reference machine", case_id)
            return
        value, limit = measurement.get("value"), cell.get("value")
        if measurement.get("unit") != cell.get("unit"):
            self.fail(f"{label}: unit differs from frozen threshold cell {cell.get('id')}", case_id)
        if not _number(value) or not _number(limit) or cell.get("comparison") not in {"maximum", "minimum"}:
            self.fail(f"{label}: threshold cell {cell.get('id')} cannot be compared numerically", case_id)
        elif (cell["comparison"] == "maximum" and value > limit) or (cell["comparison"] == "minimum" and value < limit):
            self.fail(f"{label}: measured value fails frozen threshold cell {cell.get('id')}", case_id)

    def _creator_study(self, observation: JsonObject, label: str, case_id: str) -> None:
        study = observation.get("creatorStudy")
        if not isinstance(study, dict):
            return
        record, error = self.evidence.record(study.get("rawEvidence"), f"{label}.creatorStudy.rawEvidence", CREATOR_RECORD)
        if record is None:
            self.fail(str(error), case_id)
            return
        self._envelope(record, f"{label}.creatorStudy.rawEvidence", case_id)
        claim = {key: value for key, value in study.items() if key != "rawEvidence"}
        if _sha256_json({key: record.get(key) for key in claim}) != _sha256_json(claim):
            self.fail(f"{label}: creatorStudy differs from its retained creator record", case_id)
            return
        participant = study.get("participantId")
        producer = self.producers.get(participant) if isinstance(participant, str) else None
        if producer is None or producer.get("kind") != "HUMAN" or producer.get("affiliation") != "INDEPENDENT":
            self.fail(
                f"{label}: creator {participant} is not a registered independent human participant; "
                "implementer identities are never independent creators",
                case_id,
            )
            return
        self.creators.setdefault(case_id.split(".", 1)[0], {})[participant] = study

    def _checks(self, observation: JsonObject, label: str, case_id: str) -> None:
        for check in _list(observation.get("checkResults")):
            if not isinstance(check, dict):
                continue
            check_label = f"{label}.{check.get('id')}"
            for operation in _list(check.get("operationObservations")):
                if not isinstance(operation, dict):
                    continue
                inputs = {str(item.get("sha256")) for item in _list(operation.get("inputs")) if isinstance(item, dict)}
                outputs = {str(item.get("sha256")) for item in _list(operation.get("outputs")) if isinstance(item, dict)}
                if not inputs or not outputs or inputs & outputs:
                    self.fail(
                        f"{check_label}: operation {operation.get('operationId')} must show distinct retained inputs and outputs",
                        case_id,
                    )
            continuity = check.get("continuity")
            if isinstance(continuity, dict):
                for key in ("wholeRender", "chunkedRender"):
                    item_label = f"{check_label}.continuity.{key}"
                    contents, error = self.evidence.read(continuity.get(key), item_label)
                    if contents is None:
                        self.fail(str(error), case_id)
                    else:
                        for problem in audio_errors(contents, item_label):
                            self.fail(problem, case_id)

    def _case_coverage(self, case_id: str, case: JsonObject, result_type: str, coverage: dict[str, set[str]]) -> None:
        dimensions = case.get("dimensions") if isinstance(case.get("dimensions"), dict) else {}
        excused = self.incompatible.get(case_id, {})
        for key in ("platform", "host"):
            for value in _list(dimensions.get(key)):
                if value not in coverage[key]:
                    self.fail(f"full-product case {case_id}: required {key} {value} has no observation", case_id)
        for value in _list(dimensions.get("backend")):
            if value not in coverage["backend"] and value not in excused.get("backend", set()):
                self.fail(f"full-product case {case_id}: required backend {value} has no observation", case_id)
        kinds = [item for item in _list(dimensions.get("resource")) if isinstance(item, str)]
        for value in kinds:
            if value == "all-released":
                if not coverage["resource"]:
                    self.fail(f"full-product case {case_id}: no released resource was exercised", case_id)
            elif value not in coverage["resource"] and value not in excused.get("resource", set()):
                self.fail(f"full-product case {case_id}: required resource kind {value} has no observation", case_id)
        languages = [item for item in _list(dimensions.get("language")) if isinstance(item, str)]
        if "each-declared-language" in languages:
            required: set[str] = set()
            if result_type in LANGUAGE_BEARING_RESULTS:
                for entry in self.resources.values():
                    if entry.get("resourceKind") in SINGER_KINDS and ("all-released" in kinds or entry.get("resourceKind") in kinds):
                        required.update(item for item in _list(entry.get("languages")) if isinstance(item, str))
        else:
            required = set(languages)
        for value in sorted(required - excused.get("language", set())):
            if value not in coverage["language"]:
                self.fail(f"full-product case {case_id}: required language {value} has no observation", case_id)

    def _resource_coverage(self) -> None:
        for identifier, entry in self.resources.items():
            use = self.resource_use.get(identifier)
            if use is None:
                self.fail(f"full-product released resource {identifier} is never exercised by an observation")
                continue
            missing = sorted(set(_list(entry.get("platforms"))) - use["platforms"])
            if missing:
                self.fail(f"full-product released resource {identifier} is never exercised on {', '.join(map(str, missing))}")
            if entry.get("resourceKind") in SINGER_KINDS:
                missing = sorted(set(_list(entry.get("languages"))) - use["languages"])
                if missing:
                    self.fail(f"full-product released resource {identifier} never sings its declared language(s) {', '.join(map(str, missing))}")

    def _creator_derivations(self) -> None:
        for case_id, requirement, value in self.creator_claims:
            count = len(self.creators.get(requirement, {}))
            if not _number(value) or value > count:
                self.fail(
                    f"full-product case {case_id}: creator-count claims {value} but {requirement} retains "
                    f"only {count} distinct independent creator records",
                    case_id,
                )
        for case_id, requirement in self.assisted_claims:
            studies = list(self.creators.get(requirement, {}).values())
            orders = {study.get("taskOrder") for study in studies}
            differences = [
                study["assistedCorrectionSeconds"] - study["manualCorrectionSeconds"]
                for study in studies
                if _number(study.get("assistedCorrectionSeconds")) and _number(study.get("manualCorrectionSeconds"))
            ]
            if orders != {"MANUAL_THEN_ASSISTED", "ASSISTED_THEN_MANUAL"}:
                self.fail(f"full-product case {case_id}: assisted comparison is not counterbalanced across creators", case_id)
            if not differences or statistics.median(differences) >= 0:
                self.fail(f"full-product case {case_id}: median within-creator assisted correction is not strictly faster than manual", case_id)
            if any(study.get("remainingPronunciationDefects") != 0 or study.get("remainingTimingDefects") != 0 for study in studies):
                self.fail(f"full-product case {case_id}: assisted comparison leaves pronunciation or timing defects", case_id)


SHAPE_ERROR_PREFIXES: Final = ("schema ", "semantic validator requires", "full-product evidence schema cannot be loaded")


def _contract_mode(contract: JsonValue) -> str:
    contract_id = contract.get("contractId") if isinstance(contract, dict) else None
    return {CANONICAL_CONTRACT_ID: "CANONICAL", SYNTHETIC_CONTRACT_ID: "SYNTHETIC"}.get(contract_id, "INVALID") if isinstance(contract_id, str) else "INVALID"


def _blocked(errors: list[str]) -> set[str]:
    return {match.group(1) for error in errors for match in [BLOCKED_CASE.search(error)] if match is not None}


def audit_full_product_report(
    report: JsonValue,
    *,
    full_product_contract: JsonValue,
    candidate: JsonObject | None = None,
    acceptance_contract: JsonObject | None = None,
    report_path: Path | None = None,
    evidence_root: Path | None = None,
    soak_replay_context: SoakReplayContext | None = None,
) -> FullProductGateResult:
    """Run the reader and every typed validator over one decoded report."""

    mode = _contract_mode(full_product_contract)
    if not isinstance(report, dict):
        return FullProductGateResult(False, ("full-product report root must be an object",), (), mode)
    evidence_class = report.get("evidenceClass") if isinstance(report.get("evidenceClass"), str) else None
    contract = full_product_contract if isinstance(full_product_contract, dict) else None
    context = soak_replay_context if soak_replay_context is not None else SoakReplayContext()
    reader_errors = list(validate_full_product_report(
        report,
        candidate=candidate,
        acceptance_contract=acceptance_contract,
        full_product_contract=contract,
        report_path=report_path,
        verify_references=True,
        evidence_root=evidence_root,
        soak_replay_context=context,
    ))
    if any(error.startswith(SHAPE_ERROR_PREFIXES) for error in reader_errors):
        return FullProductGateResult(False, tuple(reader_errors), tuple(sorted(_blocked(reader_errors))), mode, evidence_class)
    if contract is None:
        errors = tuple(reader_errors + ["typed full-product audit requires the frozen full-product contract"])
        return FullProductGateResult(False, errors, tuple(sorted(_blocked(reader_errors))), mode, evidence_class)
    audit = _TypedAudit(report, contract, candidate, (report_path.parent if report_path is not None else ROOT).resolve(), context)
    try:
        audit.run()
    except (TypeError, KeyError, ValueError, OverflowError) as error:
        # Nested raw records are not governed by the report JSON Schema.
        # Malformed shapes must produce a refusal, never an uncaught crash.
        audit.fail(f"typed full-product evidence is malformed ({type(error).__name__})")
    errors = tuple(dict.fromkeys(reader_errors + audit.errors))
    blocked = tuple(sorted(_blocked(reader_errors) | audit.blocked))
    return FullProductGateResult(not errors, errors, blocked, mode, evidence_class)


def audit_full_product_reference(
    reference: JsonValue,
    *,
    candidate: JsonObject,
    acceptance_contract: JsonObject,
    full_product_contract: JsonValue,
    evidence_root: Path | None = None,
    soak_replay_context: SoakReplayContext | None = None,
) -> FullProductGateResult:
    """Read, rehash, parse and audit the EB-009 report reference."""

    mode = _contract_mode(full_product_contract)
    base = evidence_root if evidence_root is not None else ROOT
    context = soak_replay_context if soak_replay_context is not None else SoakReplayContext()
    try:
        reused = reuse_reference(reference, base=base, maximum_bytes=MAXIMUM_REPORT_BYTES, replay_context=context)
        locator = reference.get("locator") if isinstance(reference, dict) else None
        path = _safe_reference_path(locator, base, "fullProductReport")
        if evidence_root is not None:
            try:
                path.resolve().relative_to(base.resolve())
            except ValueError:
                return FullProductGateResult(False, ("fullProductReport escapes the restored archive root",), (), mode)
        report = _parse_json(reused if reused is not None else _read_regular_reference(reference, base=base, label="fullProductReport", maximum_bytes=MAXIMUM_REPORT_BYTES))
    except (FullProductReportError, OSError, UnicodeError, ValueError) as error:
        return FullProductGateResult(False, (str(error),), (), mode)
    return audit_full_product_report(
        report,
        full_product_contract=full_product_contract,
        candidate=candidate,
        acceptance_contract=acceptance_contract,
        report_path=path,
        evidence_root=evidence_root,
        soak_replay_context=context,
    )
