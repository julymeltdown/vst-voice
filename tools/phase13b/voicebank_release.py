from pathlib import Path
from common import validate_requirement_map

REQUIRED_GATES = (
    "performer-contract", "rights-review", "recording-session-logs",
    "microphone-chain-calibration", "complete-unit-inventory", "retake-closure",
    "marker-pitch-loop-qa", "renderer-listening-qa", "signed-seambank",
    "installation-receipt", "character-marketing-rights",
    "performer-character-separation", "commercial-user-output-eula",
    "product-owner-approval", "legal-approval",
)
REQUIRED_RENDERERS = ("raw", "classic-psola", "spectral-classic", "stretch")


def evaluate_voicebank_dossier(dossier, root):
    errors=[]
    if dossier.get("schemaVersion") != 1: errors.append("schemaVersion must be 1")
    if dossier.get("voicebankId") != "official.voice.01": errors.append("official voicebank ID is required")
    if dossier.get("official") is not True: errors.append("official must be true")
    if dossier.get("contractedSinger") is not True: errors.append("contractedSinger must be true")
    gate_errors, blocked = validate_requirement_map(dossier.get("gates"), REQUIRED_GATES, Path(root))
    errors.extend(gate_errors)
    qa = dossier.get("gates", {}).get("renderer-listening-qa", {})
    results = qa.get("rendererResults", {})
    if qa.get("status", qa.get("result")) == "PASS":
        for renderer in REQUIRED_RENDERERS:
            if results.get(renderer) != "PASS": errors.append(f"renderer listening QA requires PASS for {renderer}")
    status = "FAIL" if errors and not any("official must" in e or "contractedSinger" in e for e in errors) else "BLOCKED" if blocked or errors else "ACCEPTED"
    return {"releaseStatus": status, "errors": errors, "unresolved": blocked}
