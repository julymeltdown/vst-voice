import hashlib
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))
import host_certification  # noqa: E402


class HostCertificationTests(unittest.TestCase):
    def record(self):
        return {
            "targetId": "reaper",
            "runtimeResult": "PASS",
            "osVersion": "Windows 11 24H2",
            "hostVersion": "REAPER 7.30",
            "pluginFormat": "VST3",
            "pluginSha256": "b" * 64,
            "executedAt": "2026-08-18T12:00:00Z",
            "executor": "qa-engineer",
            "checks": {name: "PASS" for name in host_certification.CHECK_NAMES},
            "evidence": ["logs/reaper.txt", "screens/reaper.png", "audio/reaper.wav"],
        }

    def test_pass_record_requires_existing_evidence_and_records_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.record()
            errors = host_certification.validate_record(record, root)
            self.assertTrue(any("does not exist" in error for error in errors))
            for relative in record["evidence"]:
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"evidence-" + relative.encode())
            self.assertEqual([], host_certification.validate_record(record, root))
            self.assertEqual(set(record["evidence"]), set(record["evidenceSha256"]))
            for relative, digest in record["evidenceSha256"].items():
                self.assertEqual(hashlib.sha256((root / relative).read_bytes()).hexdigest(), digest)

    def test_not_run_record_cannot_contain_fake_pass_checks(self):
        record = self.record()
        record["runtimeResult"] = "NOT_RUN"
        record["evidence"] = []
        errors = host_certification.validate_record(record, Path("."))
        self.assertTrue(any("checks must be empty" in error for error in errors))

    def test_record_updates_only_matching_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = self.record()
            for relative in record["evidence"]:
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"evidence")
            self.assertEqual([], host_certification.validate_record(record, root))
            matrix = {"schemaVersion": 1, "policy": "MANDATORY", "targets": [
                {"id": "reaper", "implementationState": "SOURCE_READY", "runtimeResult": "NOT_RUN", "evidence": []},
                {"id": "logic-pro", "implementationState": "SOURCE_READY", "runtimeResult": "NOT_RUN", "evidence": []},
            ]}
            updated = host_certification.apply_record(matrix, record)
            self.assertEqual("PASS", updated["targets"][0]["runtimeResult"])
            self.assertEqual("TARGET_BUILD_PASS", updated["targets"][0]["implementationState"])
            self.assertEqual(record["evidence"], updated["targets"][0]["evidence"][0]["logs"])
            self.assertEqual("NOT_RUN", updated["targets"][1]["runtimeResult"])


if __name__ == "__main__":
    unittest.main()
