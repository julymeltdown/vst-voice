import copy
import hashlib
import unittest
from pathlib import Path
import tempfile
import json
import subprocess
import sys
from tools.voice_model_training.permissions import permission_report, inspect_permission_sources, TRAINING_PERMISSIONS
from tools.voice_model_training.test_audio_source import wav


class PermissionTests(unittest.TestCase):
    def test_permission_join_checks_actual_audio_and_exact_source_set(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            audio, evidence = wav(), b"fixture declaration only"
            digest = hashlib.sha256(audio).hexdigest()
            (root / "source.wav").write_bytes(audio)
            row = dict(sourceId="source", sourceSha256=digest, identityId="singer", kind="HUMAN_RECORDING",
                evidenceId="license", evidenceSha256=hashlib.sha256(evidence).hexdigest(), reviewRevision="supplied",
                permissions=dict.fromkeys(TRAINING_PERMISSIONS, True))
            manifest = dict(formatId="com.project-seam.training-permission-manifest", schemaVersion=1, sources=[row])
            source = dict(sourceId="source", songId="song", sessionId="session", lineageId="lineage",
                          path="source.wav", sourceSha256=digest)
            def run():
                return inspect_permission_sources(manifest, {"license": evidence}, root=root, sources=[source], sample_rate=48000)
            report = run()
            self.assertTrue(report["sourceBytesVerified"])
            self.assertFalse(report["trainingAdmitted"])
            self.assertEqual(report["sources"][0]["lineageId"], "lineage")
            (root / "license.txt").write_bytes(evidence)
            value = dict(formatId="com.project-seam.training-permission-config", schemaVersion=1,
                         manifest=manifest, sources=[source], sampleRate=48000, evidence={"license": "license.txt"})
            def command(name):
                payload = json.dumps(value).encode()
                (root / "config.json").write_bytes(payload)
                return subprocess.run([sys.executable, "-m", "tools.voice_model_training", "permission-report",
                    str(root / "config.json"), hashlib.sha256(payload).hexdigest(), str(root), str(root / name)],
                    capture_output=True, timeout=10)
            self.assertEqual(command("ready.json").returncode, 0)
            self.assertFalse(json.loads((root / "ready.json").read_bytes())["trainingAdmitted"])
            self.assertEqual(command("ready.json").returncode, 2)
            row["permissions"]["modelTraining"] = False
            self.assertEqual(command("missing-scope.json").returncode, 3)
            row["permissions"]["modelTraining"] = True
            (root / "license.txt").write_bytes(b"changed")
            self.assertEqual(command("wrong-evidence.json").returncode, 2)
            self.assertFalse((root / "wrong-evidence.json").exists())
            row["sourceSha256"] = "0" * 64
            with self.assertRaises(ValueError): run()
            row["sourceSha256"] = digest
            row["sourceId"] = "other"
            with self.assertRaises(ValueError): run()
            row["sourceId"] = "source"
            (root / "source.wav").write_bytes(audio[:-1])
            with self.assertRaises(ValueError): run()

    def test_bank_rights_do_not_imply_model_rights(self):
        blob = b"test-only supplied evidence; no real rights"
        row = dict(sourceId="source", sourceSha256="a" * 64, identityId="original", kind="PROCEDURAL_SYNTHESIS",
                   evidenceId="evidence", evidenceSha256=hashlib.sha256(blob).hexdigest(), reviewRevision="supplied",
                   permissions={p: not p.startswith("model") and p != "commercialModels" for p in TRAINING_PERMISSIONS})
        manifest = dict(formatId="com.project-seam.training-permission-manifest", schemaVersion=1, sources=[row])
        evidence = {"evidence": blob}
        report = permission_report(manifest, evidence)
        self.assertEqual(report["sources"][0]["missingScopes"], ["modelTraining", "modelRedistribution", "commercialModels"])
        self.assertFalse(report["assertionsComplete"])
        row["permissions"] = dict.fromkeys(TRAINING_PERMISSIONS, True)
        original = copy.deepcopy(manifest)
        report = permission_report(manifest, evidence)
        self.assertTrue(report["assertionsComplete"])
        self.assertFalse(report["trainingAdmitted"])
        self.assertFalse(report["reviewAuthenticated"])
        self.assertEqual(manifest, original)
        for invalid in ({}, {"evidence": b"changed"}):
            with self.assertRaises(ValueError): permission_report(manifest, invalid)
        del row["permissions"]["modelTraining"]
        with self.assertRaises(ValueError): permission_report(manifest, evidence)


if __name__ == "__main__":
    unittest.main()
