from __future__ import annotations

from pathlib import Path
from common import GateResult, resolve_component_root, validate_requirement_map
from voicebank_audit import audit_voicebank

REQUIRED_CATEGORIES = (
    "performer-contract",
    "rights-review",
    "recording-session-logs",
    "microphone-chain-calibration",
    "complete-unit-inventory",
    "retake-closure",
    "marker-pitch-loop-qa",
    "renderer-listening-qa",
    "signed-seambank",
    "installation-receipt",
    "character-marketing-rights",
    "performer-character-separation",
    "commercial-user-output-eula",
    "product-owner-approval",
    "legal-approval",
)


def evaluate_voicebank_dossier(dossier: dict, root: Path) -> GateResult:
    errors: list[str] = []
    if dossier.get("schemaVersion") != 1:
        errors.append("voicebank dossier schemaVersion must be 1")
    if dossier.get("component") not in {None, "official-voicebank"}:
        errors.append("voicebank dossier component is invalid")
    if dossier.get("voicebankId") != "official.voice.01":
        errors.append("voicebankId must be official.voice.01")
    if dossier.get("official") is not True:
        errors.append("official must be true")
    if dossier.get("contractedSinger") is not True:
        errors.append("contractedSinger must be true")
    bank_root, root_errors = resolve_component_root(Path(root), dossier.get("bankRoot"), "bankRoot")
    errors.extend(root_errors)
    if bank_root is not None:
        audit = audit_voicebank(bank_root, dossier.get("inventoryProfile", {}))
        errors.extend(audit["errors"])
        if audit.get("voicebankId") and audit["voicebankId"] != dossier.get("voicebankId"):
            errors.append("voicebank dossier ID does not match manifest")
        if audit.get("version") and audit["version"] != dossier.get("version"):
            errors.append("voicebank dossier version does not match manifest")
    requirement_errors, blocked = validate_requirement_map(dossier.get("requirements"), REQUIRED_CATEGORIES, Path(root))
    errors.extend(requirement_errors)
    return GateResult(not errors and not blocked, errors, blocked)
