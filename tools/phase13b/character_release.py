from pathlib import Path
from common import validate_requirement_map

REQUIRED_GATES = (
    "public-name", "trademark-clearance", "domain-clearance", "social-handle-clearance",
    "ip-assignment", "source-provenance", "front-side-back-turnaround",
    "production-low-poly-model", "lod-set", "expression-set", "animation-set",
    "runtime-state-assets", "key-art", "merchandise-policy",
    "voice-character-separation", "product-owner-approval", "legal-approval",
)


def evaluate_character_dossier(dossier, root):
    errors=[]
    if dossier.get("schemaVersion") != 1: errors.append("schemaVersion must be 1")
    if dossier.get("characterId") != "official.character.01": errors.append("characterId is invalid")
    if dossier.get("publicName") in {None, "", "Character 01", "TBD", "TODO"}: errors.append("final public name must be non-placeholder")
    gate_errors, blocked = validate_requirement_map(dossier.get("gates"), REQUIRED_GATES, Path(root))
    errors.extend(gate_errors)
    status = "FAIL" if errors and not blocked else "BLOCKED" if blocked or dossier.get("developmentOnly") is True else "ACCEPTED" if not errors else "FAIL"
    return {"releaseStatus": status, "errors": errors, "unresolved": blocked}
