from __future__ import annotations

from dataclasses import dataclass
from typing import Final

try:
    from .full_product_contract_registry import BACKENDS, HOST_TUPLES, LANGUAGES, PLATFORMS, RESOURCE_KINDS
    from .full_product_contract_validation import exact_strings
    from .full_product_contract_empirical import empirical_criterion_errors
    from .full_product_contract_protocols import PROTOCOLS, canonical_definition_errors, catalog_definition_errors
    from .release_gate_validation import JsonObject, JsonValue
except ImportError:
    from full_product_contract_registry import BACKENDS, HOST_TUPLES, LANGUAGES, PLATFORMS, RESOURCE_KINDS
    from full_product_contract_validation import exact_strings
    from full_product_contract_empirical import empirical_criterion_errors
    from full_product_contract_protocols import PROTOCOLS, canonical_definition_errors, catalog_definition_errors
    from release_gate_validation import JsonObject, JsonValue


FIXED_CRITERIA: Final = {
    "phrases-per-language": ("minimum", 60, "phrases"),
    "song-count": ("minimum", 3, "songs"),
    "creator-count": ("minimum", 5, "independent-creators"),
    "pitch-median": ("maximum", 30, "cents"),
    "pitch-within-50": ("minimum", 90, "percent-steady-frames-within-50-cents"),
    "timing-displacement": ("maximum", 1, "sample-error-for-30ms-edit"),
    "classical-small-edit-p95": ("maximum", 500, "milliseconds"),
    "legacy-preview-p95": ("maximum", 400, "milliseconds"),
    "legacy-preview-median": ("maximum", 150, "milliseconds"),
    "reviewed-style-count": ("minimum", 2, "styles"),
}
EMPIRICAL_OWNERS: Final = {
    "acoustic-boundaries": ("U19", "U20", "U43"),
    "expression-tolerances": ("U19", "U20", "U36", "U39", "U43"),
    "pronunciation-scoring": ("U20", "U26", "U27", "U28", "U36", "U43"),
    "identity-rubric": ("U19", "U20", "U36", "U43"),
    "generation-budgets": ("U19", "U20"),
    "neural-budgets": ("U35", "U36"),
    "resource-limits": ("U19", "U20", "U35", "U36"),
    "cancellation-budgets": ("U19", "U20", "U35", "U36"),
    "reference-machines": ("U19", "U20", "U35", "U36"),
    "source-volume": ("U20", "U35", "U36"),
    "reproducibility-tolerances": ("U19", "U20", "U35", "U36"),
}
RULE_CRITERIA: Final = (
    "assisted-comparison", "producer-acceptance", "accessibility-acceptance",
    "interchange-acceptance", "host-acceptance", "release-acceptance",
    "gate-acceptance", "rights-acceptance",
)
CRITERION_IDS: Final = (*FIXED_CRITERIA, *EMPIRICAL_OWNERS, *RULE_CRITERIA)


@dataclass(frozen=True, slots=True)
class ProfileResult:
    errors: tuple[str, ...]
    pending: tuple[str, ...]


def _criteria_errors(rows: list[JsonValue]) -> ProfileResult:
    errors: list[str] = []
    pending: list[str] = []
    objects = [row for row in rows if isinstance(row, dict)]
    errors.extend(exact_strings([row.get("id") for row in objects], CRITERION_IDS, "full-product final criteria"))
    if len(objects) != len(rows):
        errors.append("full-product final criteria: every criterion must be an object")
    for row in objects:
        identifier = row.get("id")
        if not isinstance(identifier, str):
            continue
        label = f"full-product final criteria {identifier}"
        owners = EMPIRICAL_OWNERS.get(identifier, ("U2", "U43"))
        errors.extend(exact_strings(row.get("ownerUnits"), owners, f"{label}.ownerUnits"))
        fixed = FIXED_CRITERIA.get(identifier)
        if fixed is not None:
            comparison, number, unit = fixed
            value = row.get("value")
            if (row.get("kind"), row.get("comparison"), value, row.get("unit"), row.get("status")) != ("numeric", comparison, number, unit, "FIXED") or isinstance(value, bool):
                errors.append(f"{label}: fixed numerical floor changed or unresolved")
        if identifier in RULE_CRITERIA:
            if row.get("kind") != "protocol" or row.get("status") != "FIXED" or set(row) != {"id", "kind", "status", "ownerUnits", "protocol"}:
                errors.append(f"{label}: fixed protocol fields differ")
            errors.extend(canonical_definition_errors(row.get("protocol"), PROTOCOLS[identifier], f"{label} protocol"))
        if identifier in EMPIRICAL_OWNERS:
            if row.get("status") != "RESOLVED":
                pending.append(identifier)
            errors.extend(empirical_criterion_errors(row))
    return ProfileResult(tuple(errors), tuple(pending))


def validate_profile(contract: JsonObject) -> ProfileResult:
    profile = contract.get("evaluationProfile")
    if not isinstance(profile, dict) or not isinstance(profile.get("criteria"), list):
        return ProfileResult(("full-product final criteria: evaluation profile required",), ("profile",))
    result = _criteria_errors(profile["criteria"])
    errors = list(result.errors)
    errors.extend(catalog_definition_errors(contract.get("protocolCatalog"), PROTOCOLS, "full-product protocol catalog"))
    pending = list(result.pending)
    if profile.get("status") != "FROZEN":
        pending.append("evaluation-profile-freeze")
    scope = contract.get("scope")
    if not isinstance(scope, dict):
        return ProfileResult(tuple(errors + ["full-product scope required"]), tuple(pending))
    for key, expected in {"languages": LANGUAGES, "platforms": PLATFORMS, "hostTuples": HOST_TUPLES, "resourceKinds": RESOURCE_KINDS, "backends": BACKENDS}.items():
        errors.extend(exact_strings(scope.get(key), expected, f"full-product scope.{key}"))
    resources = scope.get("releasedResources")
    if scope.get("matrixStatus") != "FROZEN" or not isinstance(resources, list) or not resources:
        pending.append("resource matrix")
    legacy = profile.get("legacyPerformanceContract")
    expected_legacy = {"locator": "docs/product/external-beta-performance-workloads.json", "previewWorkloadId": "eb.render.preview.v1", "sampleRate": 48000, "phraseSeconds": 2, "burstMilliseconds": 20, "medianMilliseconds": 150, "p95Milliseconds": 400}
    if legacy != expected_legacy:
        errors.append("full-product final criteria: legacy 400 ms preview workload changed")
    cases = contract.get("cases")
    if isinstance(cases, list):
        for case in cases:
            if not isinstance(case, dict):
                continue
            criteria = case.get("criteriaIds")
            if not isinstance(criteria, list) or not criteria or any(not isinstance(item, str) or item not in CRITERION_IDS for item in criteria):
                errors.append(f"full-product cases {case.get('id')}: applicable final criteria required")
    return ProfileResult(tuple(errors), tuple(pending))
