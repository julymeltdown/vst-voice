import hashlib
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13b"))

import voicebank_release  # noqa: E402


class VoicebankReleaseTests(unittest.TestCase):
    def _evidence(self, root: Path, name: str):
        path = root / "evidence" / f"{name}.txt"
        path.parent.mkdir(exist_ok=True)
        path.write_text(name, encoding="utf-8")
        return {
            "path": f"evidence/{name}.txt",
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "kind": name,
            "executedAt": "2026-08-18T12:00:00Z",
            "reviewer": "phase13b-test",
        }

    def _accepted(self, root: Path):
        gates = {}
        for gate in voicebank_release.REQUIRED_GATES:
            gates[gate] = {"status": "PASS", "evidence": [self._evidence(root, gate)]}
        gates["renderer-listening-qa"]["rendererResults"] = {
            renderer: "PASS" for renderer in voicebank_release.REQUIRED_RENDERERS
        }
        return {
            "schemaVersion": 1,
            "voicebankId": "official.voice.01",
            "version": "1.0.0",
            "official": True,
            "contractedSinger": True,
            "gates": gates,
        }

    def test_all_evidence_backed_gates_accept_official_voicebank(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = voicebank_release.evaluate_voicebank_dossier(self._accepted(root), root)
            self.assertEqual("ACCEPTED", result["releaseStatus"], result)
            self.assertEqual([], result["unresolved"])

    def test_public_domain_demo_cannot_be_promoted_to_official_voicebank(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dossier = self._accepted(root)
            dossier["official"] = False
            dossier["contractedSinger"] = False
            result = voicebank_release.evaluate_voicebank_dossier(dossier, root)
            self.assertEqual("BLOCKED", result["releaseStatus"])
            self.assertIn("official", " ".join(result["errors"]).lower())

    def test_renderer_listening_qa_requires_all_four_renderers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dossier = self._accepted(root)
            del dossier["gates"]["renderer-listening-qa"]["rendererResults"]["stretch"]
            result = voicebank_release.evaluate_voicebank_dossier(dossier, root)
            self.assertEqual("FAIL", result["releaseStatus"])
            self.assertTrue(any("stretch" in error for error in result["errors"]))

    def test_not_run_gate_remains_blocked_without_fake_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dossier = self._accepted(root)
            dossier["gates"]["performer-contract"] = {"status": "NOT_RUN", "evidence": []}
            result = voicebank_release.evaluate_voicebank_dossier(dossier, root)
            self.assertEqual("BLOCKED", result["releaseStatus"])
            self.assertIn("performer-contract", result["unresolved"])


if __name__ == "__main__":
    unittest.main()
