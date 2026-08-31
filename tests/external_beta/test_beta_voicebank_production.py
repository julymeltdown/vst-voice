from __future__ import annotations

import copy
import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path

from tools.external_beta.voicebank_production import (
    create_beta_lock,
    validate_beta_lock,
    validate_candidate_export,
    validate_recording_session,
    validate_retake_closure,
    validate_source_strategy_document,
)

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("seam_voicebank_script_generator", ROOT / "tools/voicebank-script-generator/main.py")
assert SPEC is not None and SPEC.loader is not None
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)


def _profile() -> dict:
    return {
        "profileId": "test-ja-v1",
        "vowels": ["a", "i"],
        "consonants": ["k"],
        "specialPhones": ["br"],
        "includeKinds": ["sustain", "release", "cv", "vc", "vv"],
        "pitchLayers": [60, 72],
        "rangeTest": {"method": "test-range", "minMidi": 60, "maxMidi": 72, "result": "PASS"},
        "alternateTakes": 1,
    }


def _artifact(root: Path, relative: str, payload: bytes, immutable: bool = True) -> dict:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
    return {"path": relative, "sha256": hashlib.sha256(payload).hexdigest(), "immutable": immutable}


def _fixture(root: Path):
    inventory = generator.generate_inventory(_profile())
    takes = []
    for index, unit in enumerate(inventory["units"]):
        source = _artifact(root, f"raw/{unit['takeId']}.wav", f"source-{index}".encode())
        derived = _artifact(root, f"derived/{unit['takeId']}.wav", f"derived-{index}".encode())
        takes.append(
            {
                "promptId": unit["promptId"],
                "takeId": unit["takeId"],
                "coverageKey": unit["coverageKey"],
                "pitchLayer": unit["pitchLayer"],
                "status": "ACCEPTED",
                "source": source,
                "derived": derived,
                "quality": {
                    "clipping": "PASS",
                    "dcOffset": "PASS",
                    "silence": "PASS",
                    "rootPitch": "PASS",
                    "markerOrder": "PASS",
                    "pitchMarks": "PASS",
                    "rootMidi": unit["pitchLayer"],
                    "analyzedMidi": unit["pitchLayer"],
                },
            }
        )
    session = {
        "schemaVersion": 1,
        "status": "COMPLETE",
        "sessionId": "session-001",
        "generatorVersion": inventory["generatorVersion"],
        "inventorySha256": inventory["inventorySha256"],
        "scriptSha256": inventory["scriptSha256"],
        "performerRef": "redacted-provider-reference",
        "startedAt": "2026-08-10T10:00:00Z",
        "endedAt": "2026-08-10T12:00:00Z",
        "sampleRate": 48000,
        "bitDepth": 24,
        "channels": 1,
        "rawImmutable": True,
        "takes": takes,
    }
    closure = {
        "schemaVersion": 1,
        "status": "PASS",
        "inventorySha256": inventory["inventorySha256"],
        "openRetakes": [],
        "closedRetakes": [],
    }
    bindings = []
    for unit in takes:
        pair = (unit["coverageKey"], unit["pitchLayer"])
        if any((item["coverageKey"], item["pitchLayer"]) == pair for item in bindings):
            continue
        bindings.append(
            {
                "coverageKey": unit["coverageKey"],
                "pitchLayer": unit["pitchLayer"],
                "takeId": unit["takeId"],
                "alias": unit["coverageKey"].replace(":", "-") + f"-p{unit['pitchLayer']}",
                "markers": {"start": 0.0, "loopStart": 0.1, "loopEnd": 0.5, "releaseEnd": 0.8},
                "pitchMarks": [0.0, 0.1, 0.2],
                "validator": {"result": "PASS", "pitchOctaveError": False},
            }
        )
    candidate = {"schemaVersion": 1, "status": "READY", "inventorySha256": inventory["inventorySha256"], "unitBindings": bindings, "rawRecordingsMutated": False}
    return inventory, session, closure, candidate


class BetaVoicebankProductionTests(unittest.TestCase):
    def test_complete_session_closure_and_candidate_pass(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inventory, session, closure, candidate = _fixture(root)
            session_result = validate_recording_session(session, inventory, root)
            self.assertTrue(session_result.passed, session_result.errors)
            closure_result = validate_retake_closure(closure, inventory, [session])
            self.assertTrue(closure_result.passed, closure_result.errors)
            candidate_result = validate_candidate_export(candidate, inventory, [session], closure)
            self.assertTrue(candidate_result.passed, candidate_result.errors)

    def test_wrong_format_clipping_and_immutable_source_fail(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inventory, session, _, _ = _fixture(root)
            session["sampleRate"] = 44100
            session["takes"][0]["quality"]["clipping"] = "FAIL"
            session["takes"][0]["source"]["immutable"] = False
            result = validate_recording_session(session, inventory, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("48000" in error for error in result.errors))
            self.assertTrue(any("clipping" in error for error in result.errors))
            self.assertTrue(any("immutable" in error for error in result.errors))

    def test_marker_pitch_and_duplicate_alias_block_candidate_export(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inventory, session, closure, candidate = _fixture(root)
            candidate["unitBindings"][0]["markers"]["loopEnd"] = 0.05
            candidate["unitBindings"][1]["alias"] = candidate["unitBindings"][0]["alias"]
            result = validate_candidate_export(candidate, inventory, [session], closure)
            self.assertFalse(result.passed)
            self.assertTrue(any("out of order" in error for error in result.errors))
            self.assertTrue(any("duplicated" in error for error in result.errors))

    def test_open_retake_and_replacement_from_wrong_prompt_are_blocking(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inventory, session, closure, _ = _fixture(root)
            closure["openRetakes"] = [{"retakeId": "rt-1", "promptId": session["takes"][0]["promptId"]}]
            closure["closedRetakes"] = [{
                "retakeId": "rt-2",
                "promptId": session["takes"][0]["promptId"],
                "originalTakeId": session["takes"][0]["takeId"],
                "replacementTakeId": session["takes"][1]["takeId"],
                "reason": "pitch",
                "closedAt": "2026-08-11T10:00:00Z",
                "reviewer": "reviewer",
                "status": "CLOSED",
            }]
            result = validate_retake_closure(closure, inventory, [session])
            self.assertFalse(result.passed)
            self.assertIn("open-retakes", result.blocked)
            self.assertTrue(any("belongs to another prompt" in error for error in result.errors))

    def test_lock_binds_exact_candidate_package_inventory_and_song(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inventory, session, closure, candidate = _fixture(root)
            self.assertTrue(validate_candidate_export(candidate, inventory, [session], closure).passed)
            package = {"id": "beta.voice.01", "version": "0.1.0", "contentSha256": "a" * 64, "entryManifestSha256": "b" * 64}
            song = {"projectSha256": "c" * 64, "mediaSha256": "d" * 64}
            lock = create_beta_lock(candidate, package, song, inventory, generated_at="2026-08-12T10:00:00Z")
            result = validate_beta_lock(lock, candidate, package, inventory, song)
            self.assertTrue(result.passed, result.errors)
            changed = copy.deepcopy(candidate)
            changed["unitBindings"][0]["pitchMarks"].append(0.3)
            result = validate_beta_lock(lock, changed, package, inventory, song)
            self.assertFalse(result.passed)
            self.assertTrue(any("candidateSha256" in error for error in result.errors))

    def test_interrupted_candidate_export_never_reports_ready(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inventory, session, closure, candidate = _fixture(root)
            candidate["status"] = "IN_PROGRESS"
            result = validate_candidate_export(candidate, inventory, [session], closure)
            self.assertFalse(result.passed)
            self.assertIn("candidate-export", result.blocked)

    def test_selected_source_strategy_requires_all_four_rights(self) -> None:
        strategies = {
            "schemaVersion": 1,
            "status": "READY_FOR_ACQUISITION",
            "assetAdmissionStatus": "NOT_RUN",
            "selectedStrategyId": "tts",
            "strategies": [{
                "id": "tts",
                "kind": "TTS_DERIVED",
                "rights": "PASS",
                "coverage": "PASS",
                "listening": "PASS",
                "permissions": {
                    "sourceUse": True,
                    "transformation": True,
                    "singingBankRedistribution": False,
                    "commercialRenders": True,
                },
                "licenseLocator": "docs/legal/VOICE_PROVIDER_CONTRACT_REQUIREMENTS.md",
                "licenseSha256": "0" * 64,
                "evidenceState": "LICENSE_REVIEW_INCOMPLETE",
            }],
        }
        result = validate_source_strategy_document(strategies, ROOT)
        self.assertFalse(result.passed)
        self.assertTrue(any("singingBankRedistribution" in error for error in result.errors))


if __name__ == "__main__":
    unittest.main()
