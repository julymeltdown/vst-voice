from __future__ import annotations

from dataclasses import dataclass

try:
    from .full_product_contract_registry import ARTIFACT_KINDS, CASE_IDS, CRITERIA_BY_REQUIREMENT, HOST_TUPLES, LANGUAGES, PLATFORMS, REQUIREMENTS, WORK_PACKAGES
    from .release_gate_validation import JsonObject, JsonValue
    from .full_product_contract_protocols import CHECKS, CONTINUITY_CASES, catalog_definition_errors, required_check_ids
except ImportError:
    from full_product_contract_registry import ARTIFACT_KINDS, CASE_IDS, CRITERIA_BY_REQUIREMENT, HOST_TUPLES, LANGUAGES, PLATFORMS, REQUIREMENTS, WORK_PACKAGES
    from release_gate_validation import JsonObject, JsonValue
    from full_product_contract_protocols import CHECKS, CONTINUITY_CASES, catalog_definition_errors, required_check_ids


@dataclass(frozen=True, slots=True)
class DefinitionResult:
    errors: tuple[str, ...]
    case_ids: tuple[str, ...] = ()

    @property
    def passed(self) -> bool:
        return not self.errors


def exact_strings(value: JsonValue, expected: tuple[str, ...], label: str) -> list[str]:
    if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
        return [f"{label}: expected exact non-empty string coverage"]
    strings = [item for item in value if isinstance(item, str)]
    errors: list[str] = []
    if len(strings) != len(set(strings)):
        errors.append(f"{label}: duplicate IDs")
    if set(strings) != set(expected):
        errors.append(f"{label}: missing or unknown IDs; expected {', '.join(expected)}")
    return errors


def _rows(value: JsonValue, label: str, errors: list[str]) -> tuple[JsonObject, ...]:
    if not isinstance(value, list) or not value:
        errors.append(f"{label}: non-empty registry required")
        return ()
    rows = tuple(item for item in value if isinstance(item, dict))
    if len(rows) != len(value):
        errors.append(f"{label}: every row must be an object")
    for row in rows:
        if row.get("beforeBetaGO") is not True or "status" in row:
            errors.append(f"{label}: {row.get('id')} cannot be deferred or marked PASS/N/A")
    return rows


def _case_errors(case: JsonObject) -> list[str]:
    identifier = case.get("id")
    if not isinstance(identifier, str) or identifier not in CASE_IDS:
        return []
    requirement, slug = identifier.split(".", 1)
    spec = REQUIREMENTS[requirement]
    label = f"full-product cases {identifier}"
    errors: list[str] = []
    if case.get("requirementId") != requirement or case.get("resultType") != spec.result_type:
        errors.append(f"{label}: wrong requirement binding or result type")
    criteria = CRITERIA_BY_REQUIREMENT[requirement]
    criteria += {"classical-small-edit": ("classical-small-edit-p95",), "legacy-preview": ("legacy-preview-p95", "legacy-preview-median"), "two-styles": ("reviewed-style-count",)}.get(slug, ())
    errors.extend(exact_strings(case.get("criteriaIds"), criteria, f"{label}.criteriaIds"))
    errors.extend(exact_strings(case.get("independentReviewRoles"), spec.review_roles, f"{label}.independentReviewRoles"))
    dimensions = case.get("dimensions")
    if not isinstance(dimensions, dict):
        return errors + [f"{label}: dimensions required"]
    languages = ("each-declared-language",)
    for language in LANGUAGES:
        if slug in {f"{language}-pronunciation", f"{language}-corpus"}:
            languages = (language,)
    if slug in {"finished-songs", "independent-creators", "edit-reconciliation"}:
        languages = LANGUAGES
    host = next((value for value in HOST_TUPLES if value.replace("/", "-").lower() == slug), None)
    expected = {
        "language": languages,
        "resource": ("sample-procedural", "recipe-original") if slug == "generated-input" else spec.resources,
        "backend": ("procedural", "classical") if slug == "generated-input" else spec.backends,
        "platform": (host.split("/")[0],) if host else PLATFORMS,
        "host": (host,) if host else ("standalone", *HOST_TUPLES) if slug == "soak-hosts" else ("standalone",),
    }
    if set(dimensions) != set(expected):
        errors.append(f"{label}: unknown or missing dimension")
    for key, values in expected.items():
        errors.extend(exact_strings(dimensions.get(key), values, f"{label}.{key}"))
    workload = case.get("workload")
    if not isinstance(workload, dict):
        return errors + [f"{label}: workload definition required"]
    if workload.get("id") != f"fp.{identifier.lower()}.v1":
        errors.append(f"{label}: workload identity differs")
    if set(workload) != {"id", "description", "rawArtifactKinds", "checkIds"}:
        errors.append(f"{label}: workload protocol fields differ")
    errors.extend(exact_strings(workload.get("checkIds"), required_check_ids(identifier), f"{label}.checkIds"))
    procedure = workload.get("description")
    kinds = workload.get("rawArtifactKinds")
    if not isinstance(procedure, str) or not procedure.strip():
        errors.append(f"{label}: workload description required")
    artifact_kinds = ARTIFACT_KINDS[spec.result_type]
    if identifier in CONTINUITY_CASES:
        artifact_kinds += ("partition-manifest", "phoneme-alignment", "modulation-phase", "dependency-invalidation")
    errors.extend(exact_strings(kinds, artifact_kinds, f"{label}.rawArtifactKinds"))
    return errors


def validate_registry(value: JsonValue) -> DefinitionResult:
    if not isinstance(value, dict):
        return DefinitionResult(("full-product contract must be an object",))
    errors: list[str] = []
    errors.extend(catalog_definition_errors(value.get("checkCatalog"), CHECKS, "full-product workload check catalog"))
    sections = {
        "requirements": tuple(REQUIREMENTS),
        "cases": CASE_IDS,
        "workPackages": WORK_PACKAGES,
    }
    for section, expected in sections.items():
        label = f"full-product {section}"
        rows = _rows(value.get(section), label, errors)
        errors.extend(exact_strings([row.get("id") for row in rows], expected, label))
        for row in rows:
            allowed = {"requirements": {"id", "beforeBetaGO", "workPackageIds", "caseIds"}, "cases": {"id", "requirementId", "beforeBetaGO", "resultType", "dimensions", "workload", "independentReviewRoles", "criteriaIds"}, "workPackages": {"id", "beforeBetaGO"}}[section]
            if set(row) != allowed:
                errors.append(f"{label} {row.get('id')}: unknown or missing fields")
            if section == "cases":
                errors.extend(_case_errors(row))
            if section == "requirements":
                identifier = row.get("id")
                if not isinstance(identifier, str) or identifier not in REQUIREMENTS:
                    continue
                spec = REQUIREMENTS[identifier]
                case_ids = tuple(f"{identifier}.{slug}" for slug in spec.case_slugs)
                errors.extend(exact_strings(row.get("caseIds"), case_ids, f"{label} {identifier}.caseIds"))
                errors.extend(exact_strings(row.get("workPackageIds"), spec.work_packages, f"{label} {identifier}.workPackageIds"))
    return DefinitionResult(tuple(errors), CASE_IDS if not errors else ())
