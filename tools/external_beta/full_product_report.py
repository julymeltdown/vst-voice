"""Fail-closed semantic validation for the full-product Beta evidence report.

JSON Schema proves that a report has the right *shape*.  This module proves
that the shape is bound to the candidate, the frozen contract registry, and
the raw evidence it claims to summarize.  It intentionally does not infer a
PASS from summary flags or from a report that is merely syntactically valid.
"""

from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import stat
from typing import Any

try:
    from .full_product_contract_registry import (
        ARTIFACT_KINDS,
        CASE_IDS,
        CRITERIA_BY_REQUIREMENT,
        HOST_TUPLES,
        LANGUAGES,
        PLATFORMS,
        REQUIREMENTS,
    )
    from .full_product_contract_protocols import CHECKS, required_check_ids
    from .full_product_contract_profile import FIXED_CRITERIA
    from .full_product_contract_empirical import empirical_result_errors
    from .release_gate_validation import HEX64, JsonObject, JsonValue
except ImportError:  # pragma: no cover - direct script import compatibility
    from full_product_contract_registry import (  # type: ignore
        ARTIFACT_KINDS,
        CASE_IDS,
        CRITERIA_BY_REQUIREMENT,
        HOST_TUPLES,
        LANGUAGES,
        PLATFORMS,
        REQUIREMENTS,
    )
    from full_product_contract_protocols import CHECKS, required_check_ids  # type: ignore
    from full_product_contract_profile import FIXED_CRITERIA  # type: ignore
    from full_product_contract_empirical import empirical_result_errors  # type: ignore
    from release_gate_validation import HEX64, JsonObject, JsonValue  # type: ignore


ROOT = Path(__file__).resolve().parents[2]
SCHEMA_PATH = ROOT / "docs/product/full-product-beta-evidence.schema.json"
MAXIMUM_REPORT_BYTES = 64 * 1024 * 1024
MAXIMUM_REFERENCE_BYTES = 256 * 1024 * 1024
MAXIMUM_JSON_DEPTH = 96
REFERENCE_READ_CHUNK_BYTES = 1024 * 1024


class FullProductReportError(ValueError):
    """Raised only for malformed report/reference input."""


def _canonical(value: JsonValue) -> str:
    return json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    )


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _sha256_json(value: JsonValue) -> str:
    return _sha256_bytes(_canonical(value).encode("utf-8"))


def _reject_constant(value: str) -> JsonValue:
    raise FullProductReportError(f"nonfinite numeric constant: {value}")


def _finite_float(value: str) -> float:
    result = float(value)
    if not math.isfinite(result):
        raise FullProductReportError("nonfinite numeric value")
    return result


def _unique_object(pairs: list[tuple[str, JsonValue]]) -> JsonObject:
    result: JsonObject = {}
    for key, value in pairs:
        if key in result:
            raise FullProductReportError("duplicate object key")
        result[key] = value
    return result


def _parse_json(contents: bytes) -> JsonValue:
    try:
        value = json.loads(
            contents,
            object_pairs_hook=_unique_object,
            parse_constant=_reject_constant,
            parse_float=_finite_float,
        )
    except (UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as error:
        raise FullProductReportError(f"invalid JSON: {error}") from error
    pending: list[tuple[JsonValue, int]] = [(value, 0)]
    while pending:
        current, depth = pending.pop()
        if depth > MAXIMUM_JSON_DEPTH:
            raise FullProductReportError("report exceeds the JSON depth limit")
        if isinstance(current, dict):
            pending.extend((child, depth + 1) for child in current.values())
        elif isinstance(current, list):
            pending.extend((child, depth + 1) for child in current)
    return value


def _safe_reference_path(locator: JsonValue, base: Path, label: str) -> Path:
    if not isinstance(locator, str) or not locator.strip():
        raise FullProductReportError(f"{label}.locator is required")
    candidate = Path(locator)
    is_absolute = candidate.is_absolute()
    if not is_absolute:
        candidate = base / candidate
    try:
        # Normalize ``..`` lexically, but do not resolve symlinks before the
        # no-follow lstat/open below.  Resolving here would turn a linked
        # evidence path into its target and defeat the boundary check.
        normalized = Path(os.path.abspath(os.fspath(candidate)))
        if not is_absolute:
            normalized_base = Path(os.path.abspath(os.fspath(base)))
            try:
                normalized.relative_to(normalized_base)
            except ValueError as error:
                raise FullProductReportError(
                    f"{label}.locator escapes its allowed relative root"
                ) from error
        return normalized
    except OSError as error:
        raise FullProductReportError(f"{label}.locator cannot be resolved: {error}") from error


def _read_regular_reference(
    reference: JsonValue,
    *,
    base: Path,
    label: str,
    maximum_bytes: int = MAXIMUM_REFERENCE_BYTES,
) -> bytes:
    if not isinstance(reference, dict) or set(reference) != {"locator", "sha256"}:
        raise FullProductReportError(f"{label} requires exactly locator and sha256")
    digest = reference.get("sha256")
    if not isinstance(digest, str) or HEX64.fullmatch(digest) is None:
        raise FullProductReportError(f"{label}.sha256 must be a lowercase SHA-256 digest")
    path = _safe_reference_path(reference.get("locator"), base, label)
    try:
        before = path.lstat()
    except OSError as error:
        raise FullProductReportError(f"{label} cannot be read: {error}") from error
    if not stat.S_ISREG(before.st_mode):
        raise FullProductReportError(f"{label} must reference a regular file")
    if before.st_size > maximum_bytes:
        raise FullProductReportError(f"{label} exceeds the {maximum_bytes} byte limit")
    flags = os.O_RDONLY | getattr(os, "O_NONBLOCK", 0) | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags)
        with os.fdopen(descriptor, "rb") as stream:
            opened = os.fstat(stream.fileno())
            if not stat.S_ISREG(opened.st_mode):
                raise FullProductReportError(f"{label} must reference a regular file")
            if (before.st_dev, before.st_ino) != (opened.st_dev, opened.st_ino):
                raise FullProductReportError(f"{label} changed while opening")
            # Do not ask the buffered reader to allocate the policy maximum for
            # every small evidence file.  A complete report validates tens of
            # thousands of references; Windows commits those oversized buffers
            # eagerly enough to turn the contract suite into memory thrashing.
            # Read one byte beyond the handle-bound size in bounded chunks so
            # growth and shrinkage still fail closed without speculative
            # 256-MiB allocations.
            contents = bytearray()
            remaining = before.st_size + 1
            while remaining > 0:
                chunk = stream.read(min(remaining, REFERENCE_READ_CHUNK_BYTES))
                if not chunk:
                    break
                contents.extend(chunk)
                remaining -= len(chunk)
            after = os.fstat(stream.fileno())
    except FullProductReportError:
        raise
    except OSError as error:
        raise FullProductReportError(f"{label} cannot be read: {error}") from error
    if (
        len(contents) != before.st_size
        or (opened.st_dev, opened.st_ino, opened.st_size)
        != (after.st_dev, after.st_ino, after.st_size)
        or after.st_size != before.st_size
    ):
        raise FullProductReportError(f"{label} changed while reading")
    contents = bytes(contents)
    if _sha256_bytes(contents) != digest:
        raise FullProductReportError(f"{label} content digest does not match its bytes")
    return contents


def _schema_errors(report: JsonValue) -> list[str]:
    try:
        from jsonschema import Draft202012Validator
    except ImportError:
        return ["semantic validator requires the jsonschema package"]
    try:
        schema = _parse_json(SCHEMA_PATH.read_bytes())
        Draft202012Validator.check_schema(schema)
        validator = Draft202012Validator(schema)
        errors = sorted(validator.iter_errors(report), key=lambda error: list(error.absolute_path))
    except (OSError, FullProductReportError, TypeError, ValueError) as error:
        return [f"full-product evidence schema cannot be loaded: {error}"]
    return [
        "schema " + ("/".join(str(part) for part in error.absolute_path) or "<root>") + ": " + error.message
        for error in errors[:64]
    ]


def _case_dimensions(case_id: str) -> dict[str, tuple[str, ...]]:
    requirement, slug = case_id.split(".", 1)
    spec = REQUIREMENTS[requirement]
    languages: tuple[str, ...] = (*LANGUAGES, "language-independent")
    for language in LANGUAGES:
        if slug in {f"{language}-pronunciation", f"{language}-corpus"}:
            languages = (language,)
    if slug in {"finished-songs", "independent-creators", "edit-reconciliation"}:
        languages = LANGUAGES
    host = next((value for value in HOST_TUPLES if value.replace("/", "-").lower() == slug), None)
    return {
        "language": languages,
        "resource": ("sample-procedural", "recipe-original") if slug == "generated-input" else spec.resources,
        "backend": ("procedural", "classical") if slug == "generated-input" else spec.backends,
        "platform": (host.split("/")[0],) if host else PLATFORMS,
        "host": (host,) if host else (("standalone", *HOST_TUPLES) if slug == "soak-hosts" else ("standalone",)),
    }


def _exact_ids(value: JsonValue, expected: tuple[str, ...], label: str) -> list[str]:
    if not isinstance(value, (list, tuple)) or any(not isinstance(item, str) for item in value):
        return [f"{label}: exact non-empty ID coverage is required"]
    actual = tuple(value)
    errors: list[str] = []
    if len(actual) != len(set(actual)):
        errors.append(f"{label}: duplicate IDs")
    if set(actual) != set(expected):
        errors.append(f"{label}: missing or unknown IDs")
    return errors


def _reference_errors(reference: JsonValue, *, base: Path, label: str, verify: bool) -> list[str]:
    try:
        if isinstance(reference, dict):
            path = _safe_reference_path(reference.get("locator"), base, label)
            try:
                path.resolve().relative_to(base.resolve())
            except ValueError as error:
                raise FullProductReportError(f"{label}: reference escapes the restored report root") from error
        if verify:
            _read_regular_reference(reference, base=base, label=label)
        elif not isinstance(reference, dict) or set(reference) != {"locator", "sha256"}:
            raise FullProductReportError(f"{label} requires exactly locator and sha256")
    except FullProductReportError as error:
        return [str(error)]
    return []


def _bound_raw_record(reference: JsonValue, claim: JsonObject, *, base: Path, label: str) -> list[str]:
    """Compare typed claims with retained raw JSON; this is not acoustic reanalysis."""
    admission = _reference_errors(reference, base=base, label=label, verify=False)
    if admission:
        return admission
    try:
        raw = _parse_json(_read_regular_reference(reference, base=base, label=label))
        if not isinstance(raw, dict) or _canonical({key: raw.get(key) for key in claim}) != _canonical(claim):
            return [f"{label}: typed claim differs from retained raw record"]
    except FullProductReportError as error:
        return [str(error)]
    return []


def _observation_errors(
    observation: JsonObject,
    *,
    case_id: str,
    source_commit: str | None,
    build_id: str | None,
    resource_ids: set[str],
    report_base: Path,
    verify_references: bool,
    criterion_definitions: dict[str, JsonObject],
) -> list[str]:
    errors: list[str] = []
    label = f"full-product observation {case_id}"
    dimensions = _case_dimensions(case_id)
    for key, allowed in dimensions.items():
        if key == "resource":
            continue  # The evidence schema declares resourceIds, not resource.
        value = observation.get(key)
        if not isinstance(value, str) or value not in allowed:
            errors.append(f"{label}.{key}: value is outside the case dimension")
    platform = observation.get("platform")
    host = observation.get("host")
    if isinstance(host, str) and host == "standalone" and platform not in PLATFORMS:
        errors.append(f"{label}: standalone observation has no supported platform")
    if isinstance(host, str) and host != "standalone" and not host.startswith(f"{platform}/"):
        errors.append(f"{label}: host tuple does not belong to platform")
    if source_commit is not None and observation.get("sourceCommit") != source_commit:
        errors.append(f"{label}: sourceCommit differs across observations")
    if build_id is not None and observation.get("buildId") != build_id:
        errors.append(f"{label}: buildId differs across observations")
    if not resource_ids:
        errors.append(f"{label}: observation cannot be checked before the resource matrix is frozen")
    elif not set(observation.get("resourceIds", ())).issubset(resource_ids):
        errors.append(f"{label}: resourceIds include an undeclared released resource")
    artifacts = observation.get("artifacts")
    if isinstance(artifacts, list):
        actual = {item.get("kind") for item in artifacts if isinstance(item, dict)}
        expected = set(ARTIFACT_KINDS[REQUIREMENTS[case_id.split(".", 1)[0]].result_type])
        if not expected.issubset(actual):
            errors.append(f"{label}: required raw artifact kinds are missing")
        for index, artifact in enumerate(artifacts):
            if isinstance(artifact, dict):
                reference = {key: artifact.get(key) for key in ("locator", "sha256")}
                errors.extend(_reference_errors(reference, base=report_base, label=f"{label}.artifacts[{index}]", verify=verify_references))
    reviews = observation.get("reviews")
    if isinstance(reviews, list):
        roles = {review.get("role") for review in reviews if isinstance(review, dict)}
        required_roles = set(REQUIREMENTS[case_id.split(".", 1)[0]].review_roles)
        if not required_roles.issubset(roles):
            errors.append(f"{label}: required independent review roles are missing")
        for index, review in enumerate(reviews):
            if not isinstance(review, dict):
                continue
            if review.get("status") != "ACCEPTED":
                errors.append(f"{label}.reviews[{index}]: accepted review is required")
            if review.get("producerId") == review.get("reviewerId"):
                errors.append(f"{label}.reviews[{index}]: producer and reviewer must be distinct")
            errors.extend(_reference_errors(review.get("rawEvidence"), base=report_base, label=f"{label}.reviews[{index}].rawEvidence", verify=verify_references))
            if verify_references:
                errors.extend(_bound_raw_record(review.get("rawEvidence"), {key: value for key, value in review.items() if key != "rawEvidence"},
                    base=report_base, label=f"{label}.reviews[{index}]"))
    measurements = observation.get("measurements")
    if isinstance(measurements, list):
        criteria = set(CRITERIA_BY_REQUIREMENT[case_id.split(".", 1)[0]])
        observed_criteria: set[str] = set()
        for index, measurement in enumerate(measurements):
            if not isinstance(measurement, dict):
                continue
            criterion = measurement.get("criterionId")
            if isinstance(criterion, str):
                if criterion not in criteria:
                    errors.append(f"{label}.measurements[{index}]: criterion is not declared for this case")
                observed_criteria.add(criterion)
            value = measurement.get("value")
            if not isinstance(value, (int, float)) or isinstance(value, bool) or not math.isfinite(value):
                errors.append(f"{label}.measurements[{index}]: finite numeric value is required")
            elif criterion in FIXED_CRITERIA:
                comparison, threshold, unit = FIXED_CRITERIA[criterion]
                if measurement.get("unit") != unit:
                    errors.append(f"{label}.measurements[{index}]: unit differs from the frozen criterion")
                if (comparison == "maximum" and value > threshold) or (comparison == "minimum" and value < threshold):
                    errors.append(f"{label}.measurements[{index}]: measured value fails the frozen {comparison}")
            definition = criterion_definitions.get(criterion) if isinstance(criterion, str) else None
            if definition is not None and measurement.get("methodSha256") != _sha256_json(definition):
                errors.append(f"{label}.measurements[{index}]: method digest differs from the frozen criterion definition")
            errors.extend(_reference_errors(measurement.get("rawEvidence"), base=report_base, label=f"{label}.measurements[{index}].rawEvidence", verify=verify_references))
            if verify_references:
                errors.extend(_bound_raw_record(measurement.get("rawEvidence"), {key: value for key, value in measurement.items() if key != "rawEvidence"},
                    base=report_base, label=f"{label}.measurements[{index}]"))
        if not observed_criteria.intersection(criteria):
            errors.append(f"{label}: at least one declared criterion measurement is required")
    checks = observation.get("checkResults")
    if isinstance(checks, list):
        seen: set[str] = set()
        for index, check in enumerate(checks):
            if not isinstance(check, dict):
                continue
            check_id = check.get("id")
            if not isinstance(check_id, str):
                continue
            if check_id in seen:
                errors.append(f"{label}.checkResults: duplicate check {check_id}")
            seen.add(check_id)
            definition = CHECKS.get(check_id)
            if not isinstance(definition, dict):
                errors.append(f"{label}.checkResults: unknown check {check_id}")
                continue
            if check.get("protocolSha256") != _sha256_json(definition):
                errors.append(f"{label}.{check_id}: protocol digest differs from the canonical check definition")
            if definition.get("caseId") != case_id and check_id != "fp.check.chunk-continuity.v1":
                errors.append(f"{label}.checkResults: check {check_id} belongs to another case")
            operation_observations = check.get("operationObservations")
            if isinstance(operation_observations, list):
                operation_ids = [item.get("operationId") for item in operation_observations if isinstance(item, dict)]
                errors.extend(_exact_ids(operation_ids, tuple(definition.get("requiredOperations", ())), f"{label}.{check_id}.operationObservations"))
                for operation in operation_observations:
                    if not isinstance(operation, dict):
                        continue
                    for key in ("inputs", "outputs"):
                        refs = operation.get(key)
                        if isinstance(refs, list):
                            for ref_index, reference in enumerate(refs):
                                errors.extend(_reference_errors(reference, base=report_base, label=f"{label}.{check_id}.{key}[{ref_index}]", verify=verify_references))
            elif definition.get("requiredOperations"):
                errors.append(f"{label}.{check_id}: operation observations are required")
            raw = check.get("rawEvidence")
            if isinstance(raw, list):
                for raw_index, reference in enumerate(raw):
                    errors.extend(_reference_errors(reference, base=report_base, label=f"{label}.{check_id}.rawEvidence[{raw_index}]", verify=verify_references))
            continuity = check.get("continuity")
            if isinstance(continuity, dict):
                for key, reference in continuity.items():
                    errors.extend(_reference_errors(reference, base=report_base, label=f"{label}.{check_id}.continuity.{key}", verify=verify_references))
        expected_checks = set(required_check_ids(case_id))
        if not expected_checks.issubset(seen):
            errors.append(f"{label}: required checks are missing")
    return errors


def validate_full_product_report(
    report: JsonObject,
    *,
    candidate: JsonObject | None = None,
    acceptance_contract: JsonObject | None = None,
    full_product_contract: JsonObject | None = None,
    report_path: Path | None = None,
    verify_references: bool = True,
) -> tuple[str, ...]:
    """Return semantic errors for one decoded full-product report.

    The function is deliberately pure with respect to the report: it never
    mutates candidate or contract data.  A report can be structurally valid
    while still being rejected because it is stale, incomplete, or backed by
    unresolved contract criteria.
    """

    errors = _schema_errors(report)
    if errors:
        return tuple(errors)
    assert isinstance(report, dict)
    report_base = (report_path.parent if report_path is not None else ROOT).resolve()
    if report.get("status") != "PASS":
        errors.append("full-product report status must be PASS for EB-009")

    if candidate is not None:
        root = candidate.get("candidateRoot")
        if not isinstance(root, dict):
            errors.append("full-product report cannot bind to a missing candidate root")
        else:
            for report_key, root_key in (("candidateRootId", "id"), ("candidateRootSha256", "sha256")):
                if report.get(report_key) != root.get(root_key):
                    errors.append(f"full-product report {report_key} differs from candidate root")
        acceptance_sha = candidate.get("acceptanceContractSha256")
        if report.get("acceptanceContractSha256") != acceptance_sha:
            errors.append("full-product report acceptance contract digest differs from candidate")

    if acceptance_contract is not None:
        reference = acceptance_contract.get("fullProductContract")
        if isinstance(reference, dict):
            if report.get("fullProductContractSha256") != reference.get("sha256"):
                errors.append("full-product report full-product contract digest differs from acceptance contract")
        else:
            errors.append("acceptance contract has no full-product contract reference")

    if full_product_contract is not None and isinstance(full_product_contract, dict):
        profile = full_product_contract.get("evaluationProfile")
        scope = full_product_contract.get("scope")
        if report.get("evaluationProfileSha256") != _sha256_json(profile):
            errors.append("full-product report evaluation profile digest differs from contract bytes")
        resource_matrix = scope.get("releasedResources") if isinstance(scope, dict) else None
        if report.get("resourceMatrixSha256") != _sha256_json(resource_matrix):
            errors.append("full-product report resource matrix digest differs from contract bytes")
        if isinstance(scope, dict) and scope.get("matrixStatus") != "FROZEN":
            errors.append("full-product resource matrix is not frozen")
        profile_status = profile.get("status") if isinstance(profile, dict) else None
        if profile_status != "FROZEN":
            errors.append("full-product evaluation profile is not frozen")

    archive = report.get("rawArchive")
    errors.extend(_reference_errors(archive, base=report_base, label="full-product rawArchive", verify=verify_references))

    requirements = report.get("requirements")
    requirement_rows: dict[str, JsonObject] = {}
    if isinstance(requirements, list):
        for row in requirements:
            if isinstance(row, dict) and isinstance(row.get("id"), str):
                identifier = row["id"]
                if identifier in requirement_rows:
                    errors.append(f"full-product requirements: duplicate {identifier}")
                requirement_rows[identifier] = row
                if row.get("status") != "PASS":
                    errors.append(f"full-product requirement {identifier} is not PASS")
                if identifier in REQUIREMENTS:
                    expected_cases = tuple(f"{identifier}.{slug}" for slug in REQUIREMENTS[identifier].case_slugs)
                    errors.extend(_exact_ids(row.get("caseIds"), expected_cases, f"full-product requirement {identifier}.caseIds"))
        errors.extend(_exact_ids(tuple(requirement_rows), tuple(REQUIREMENTS), "full-product requirements"))

    scope_resources: set[str] = set()
    if isinstance(full_product_contract, dict):
        scope = full_product_contract.get("scope")
        if isinstance(scope, dict) and isinstance(scope.get("releasedResources"), list):
            scope_resources = {item.get("id") for item in scope["releasedResources"] if isinstance(item, dict) and isinstance(item.get("id"), str)}

    cases = report.get("cases")
    case_rows: dict[str, JsonObject] = {}
    release_identity = candidate.get("releaseIdentity", {}) if isinstance(candidate, dict) else {}
    source_commit = release_identity.get("sourceCommit") if isinstance(release_identity, dict) else None
    build_id = release_identity.get("buildId") if isinstance(release_identity, dict) else None
    criterion_definitions = {}
    if isinstance(full_product_contract, dict):
        profile = full_product_contract.get("evaluationProfile", {})
        if isinstance(profile, dict):
            criterion_definitions = {row["id"]: row for row in profile.get("criteria", []) if isinstance(row, dict) and isinstance(row.get("id"), str)}
    if isinstance(cases, list):
        for row in cases:
            if not isinstance(row, dict) or not isinstance(row.get("id"), str):
                continue
            case_id = row["id"]
            if case_id in case_rows:
                errors.append(f"full-product cases: duplicate {case_id}")
            case_rows[case_id] = row
            if case_id not in CASE_IDS:
                continue
            requirement, _ = case_id.split(".", 1)
            spec = REQUIREMENTS[requirement]
            if row.get("requirementId") != requirement:
                errors.append(f"full-product case {case_id}: requirement binding differs")
            if row.get("resultType") != spec.result_type:
                errors.append(f"full-product case {case_id}: result type differs")
            if row.get("status") != "PASS":
                errors.append(f"full-product case {case_id} is not PASS")
            observations = row.get("observations")
            if not isinstance(observations, list) or not observations:
                errors.append(f"full-product case {case_id}: observations are required")
                continue
            observed_checks: set[str] = set()
            for observation in observations:
                if not isinstance(observation, dict):
                    continue
                if source_commit is None and isinstance(observation.get("sourceCommit"), str):
                    source_commit = observation["sourceCommit"]
                if build_id is None and isinstance(observation.get("buildId"), str):
                    build_id = observation["buildId"]
                checks = observation.get("checkResults")
                if isinstance(checks, list):
                    observed_checks.update(item.get("id") for item in checks if isinstance(item, dict) and isinstance(item.get("id"), str))
                errors.extend(_observation_errors(observation, case_id=case_id, source_commit=source_commit, build_id=build_id, resource_ids=scope_resources, report_base=report_base, verify_references=verify_references, criterion_definitions=criterion_definitions))
            expected_checks = set(required_check_ids(case_id))
            if observed_checks != expected_checks:
                errors.append(f"full-product case {case_id}: check coverage differs from canonical workload")
        errors.extend(_exact_ids(tuple(case_rows), CASE_IDS, "full-product cases"))

    empirical = report.get("empiricalResults")
    if isinstance(full_product_contract, dict) and isinstance(empirical, dict):
        profile = full_product_contract.get("evaluationProfile")
        criteria = profile.get("criteria") if isinstance(profile, dict) else None
        if isinstance(criteria, list):
            for criterion in criteria:
                if not isinstance(criterion, dict) or criterion.get("kind") != "empirical":
                    continue
                identifier = criterion.get("id")
                if not isinstance(identifier, str):
                    continue
                if criterion.get("status") != "RESOLVED":
                    errors.append(f"full-product empirical criterion {identifier} is unresolved in the canonical contract")
                result = empirical.get(identifier)
                if not isinstance(result, dict) or result.get("schemaVersion") != 1 or result.get("resultType") != criterion.get("resultSpec", {}).get("resultType"):
                    errors.append(f"full-product empirical result {identifier} is missing or has the wrong result type")
                    continue
                expected_cells = criterion.get("resultSpec", {}).get("cells")
                actual_cells = result.get("cells")
                expected_ids = tuple(item.get("id") for item in expected_cells if isinstance(item, dict) and isinstance(item.get("id"), str)) if isinstance(expected_cells, list) else ()
                actual_ids = tuple(item.get("id") for item in actual_cells if isinstance(item, dict) and isinstance(item.get("id"), str)) if isinstance(actual_cells, list) else ()
                errors.extend(_exact_ids(actual_ids, expected_ids, f"full-product empirical result {identifier}.cells"))
                errors.extend(empirical_result_errors(result, criterion.get("resultSpec", {}), f"full-product empirical result {identifier}"))
                frozen = criterion.get("value")
                if not isinstance(frozen, dict) or not isinstance(frozen.get("cells"), list):
                    errors.append(f"full-product empirical criterion {identifier} has no frozen threshold grid")
                    continue
                thresholds = {row.get("id"): row for row in frozen["cells"] if isinstance(row, dict)}
                for cell in actual_cells if isinstance(actual_cells, list) else []:
                    if not isinstance(cell, dict):
                        continue
                    target = thresholds.get(cell.get("id"))
                    if not isinstance(target, dict):
                        continue
                    value, limit = cell.get("value"), target.get("value")
                    if cell.get("bindings") != target.get("bindings"):
                        errors.append(f"full-product empirical {cell.get('id')}: qualification bindings differ from frozen threshold")
                    if cell.get("valueType") == "machine-profile":
                        passed = value == limit
                    elif isinstance(value, (int, float)) and not isinstance(value, bool) and isinstance(limit, (int, float)) and not isinstance(limit, bool):
                        passed = value <= limit if cell.get("comparison") == "maximum" else value >= limit
                    else:
                        passed = False
                    if not passed:
                        errors.append(f"full-product empirical {cell.get('id')}: measured value fails frozen threshold")
                    bindings = cell.get("bindings")
                    if isinstance(bindings, dict):
                        for key in ("machineProfile", "workload", "resourceMatrix"):
                            errors.extend(_reference_errors(bindings.get(key), base=report_base, label=f"full-product empirical {cell.get('id')}.{key}", verify=verify_references))
                if verify_references:
                    errors.extend(_bound_raw_record(criterion.get("measurement"), frozen, base=report_base, label=f"frozen qualification {identifier}"))
                    errors.extend(_reference_errors(criterion.get("independentReview"), base=report_base, label=f"frozen qualification {identifier}.independentReview", verify=True))

    return tuple(dict.fromkeys(errors))


def validate_full_product_report_reference(
    reference: JsonObject,
    *,
    candidate: JsonObject,
    acceptance_contract: JsonObject,
    full_product_contract: JsonObject | None = None,
    evidence_root: Path | None = None,
) -> tuple[str, ...]:
    """Read, hash-check, parse, and semantically validate a report reference."""

    locator = reference.get("locator")
    try:
        base = evidence_root if evidence_root is not None else ROOT
        path = _safe_reference_path(locator, base, "fullProductReport")
        if evidence_root is not None:
            try:
                path.resolve().relative_to(base.resolve())
            except ValueError:
                return ("fullProductReport escapes the restored archive root",)
        contents = _read_regular_reference(reference, base=base, label="fullProductReport", maximum_bytes=MAXIMUM_REPORT_BYTES)
        report = _parse_json(contents)
        if not isinstance(report, dict):
            return ("fullProductReport root must be an object",)
        return validate_full_product_report(
            report,
            candidate=candidate,
            acceptance_contract=acceptance_contract,
            full_product_contract=full_product_contract,
            report_path=path,
            verify_references=True,
        )
    except (FullProductReportError, OSError, UnicodeError, json.JSONDecodeError) as error:
        return (str(error),)
