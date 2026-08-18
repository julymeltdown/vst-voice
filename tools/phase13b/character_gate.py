from __future__ import annotations

from pathlib import Path
from common import GateResult, resolve_component_root, validate_requirement_map
from character_audit import audit_character

REQUIRED_CATEGORIES = (
    "public-name",
    "trademark-clearance",
    "domain-clearance",
    "social-handle-clearance",
    "ip-assignment",
    "source-provenance",
    "front-side-back-turnaround",
    "production-low-poly-model",
    "lod-set",
    "expression-set",
    "animation-set",
    "runtime-state-assets",
    "key-art",
    "merchandise-policy",
    "voice-character-separation",
    "product-owner-approval",
    "legal-approval",
)

_PLACEHOLDERS = {"", "Character 01", "TBD", "TODO", "Test Character"}


def evaluate_character_dossier(dossier: dict, root: Path) -> GateResult:
    errors: list[str] = []
    if dossier.get("schemaVersion") != 1:
        errors.append("character dossier schemaVersion must be 1")
    if dossier.get("component") not in {None, "official-character"}:
        errors.append("character dossier component is invalid")
    if dossier.get("characterId") != "official.character.01":
        errors.append("characterId must be official.character.01")
    if dossier.get("commercialRelease") is not True:
        errors.append("commercialRelease must be true")
    public_name = dossier.get("finalPublicName", dossier.get("publicName", ""))
    if not isinstance(public_name, str) or public_name.strip() in _PLACEHOLDERS:
        errors.append("finalPublicName must be non-placeholder")
    character_root, root_errors = resolve_component_root(
        Path(root), dossier.get("characterRoot"), "characterRoot"
    )
    errors.extend(root_errors)
    if character_root is not None:
        report = audit_character(character_root, dossier.get("assetProfile", {}))
        errors.extend(report["errors"])
        if report.get("characterId") and report["characterId"] != dossier.get("characterId"):
            errors.append("character dossier ID does not match manifest")
        if report.get("version") and report["version"] != dossier.get("version"):
            errors.append("character dossier version does not match manifest")
    requirement_errors, blocked = validate_requirement_map(dossier.get("requirements"), REQUIRED_CATEGORIES, Path(root))
    errors.extend(requirement_errors)
    return GateResult(not errors and not blocked, errors, blocked)
