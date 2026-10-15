"""Policy/held-file orchestration tests use a mocked native process.

Real sample/recipe rendering, coordinated forgeries and retained-output
preservation are covered by seam_sample_review_cli_tests.
"""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from tools.external_beta import installed_export_record as audit
from tests.external_beta.test_installed_project_binding_record import fixture as binding_fixture


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Fixture:
    def __init__(self, root, recipe=False):
        self.root = root
        self.export = root / "export"
        self.export.mkdir()
        self.master = self.export / "master.wav"
        self.master.write_bytes(b"mocked native audio")
        linked = binding_fixture(recipe)
        family = linked["installed"]["payloadFamily"]
        self.record = audit.FIXED | dict(recordType=audit.TYPES[family], resourceCapture=audit.CAPTURE[family],
            installed=linked["installed"], project=linked["project"], projectLanguages=["ja"],
            platform="macos", buildId="unit-test", sourceCommit="a" * 40, sampleRate=48000,
            artifacts=[dict(path="master.wav", sha256=sha(self.master), frames=10, channels=1)],
            exportReceiptSha256="0" * 64, masterSha256=sha(self.master), masterPeakAbs=0.1, masterClipped=False)
        self.receipt = dict(schemaVersion=2, state="COMMITTED", renderQuality="Final",
            transactionId="a" * 24, executionDateUnixMs=1, files=copy.deepcopy(self.record["artifacts"]))
        self.receipt_path = self.export / "receipt.json"
        self.receipt_path.write_text(json.dumps(self.receipt))
        self.record["exportReceiptSha256"] = sha(self.receipt_path)
        self.path = root / "record.json"
        self.path.write_text(json.dumps(self.record))
        self.cli = root / "caller-cli"; self.cli.write_bytes(b"mock CLI")
        self.key = root / "caller-key"; self.key.write_bytes(b"mock public key")
        self.args = dict(record_path=self.path, record_sha256=sha(self.path),
            project_path=root / "project", package_path=root / "package", installed_directory=root / "installed",
            export_directory=self.export, cli_path=self.cli, cli_sha256=sha(self.cli),
            public_key_path=self.key, public_key_sha256=sha(self.key))
        self.outputs = []

    def update(self):
        self.path.write_text(json.dumps(self.record))
        self.args["record_sha256"] = sha(self.path)

    def native(self, command, timeout):
        assert command[0] == str(self.cli)
        assert command[1] == "export-installed-" + self.record["installed"]["payloadFamily"] + "-project"
        assert command[2] == str(self.args["project_path"])
        assert command[7] == str(self.args["package_path"])
        assert command[10] == str(self.args["installed_directory"])
        assert command[11] == str(self.key)
        output = Path(command[12]); output.mkdir()
        self.outputs.append(output)
        (output / "master.wav").write_bytes(b"mocked native audio")
        for artifact in self.record["artifacts"]:
            if artifact["path"] != "master.wav":
                target = output / artifact["path"]; target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((self.export / artifact["path"]).read_bytes())
        receipt = copy.deepcopy(self.receipt)
        receipt.update(transactionId="b" * 24, executionDateUnixMs=2)
        (output / "receipt.json").write_text(json.dumps(receipt))
        result = copy.deepcopy(self.record)
        result["exportReceiptSha256"] = sha(output / "receipt.json")
        return result


class InstalledExportRecordTests(unittest.TestCase):
    def backing_fixture(self, root, recipe=False):
        f = Fixture(root, recipe)
        data = b"mocked backing media"
        digest = hashlib.sha256(data).hexdigest(); name = "media/" + digest + ".wav"
        path = f.export / name; path.parent.mkdir(); path.write_bytes(data)
        f.record.update(schemaVersion=2, backingMediaCapture="PROJECT_HASH_MATCHED_STAGED_WAV_BYTES", recordType="seam.u45.installed-" +
            f.record["installed"]["payloadFamily"] + "-export.v2", backingMedia=[dict(
            trackId=f"{index:016x}", artifactPath=name, mediaSha256=digest,
            sourceFrames=3, sourceSampleRate=44100, sourceChannels=1) for index in (1, 2)])
        f.record["artifacts"].append(dict(path=name, sha256=digest, frames=3, channels=1))
        f.receipt["files"] = copy.deepcopy(f.record["artifacts"])
        f.receipt_path.write_text(json.dumps(f.receipt)); f.record["exportReceiptSha256"] = sha(f.receipt_path)
        f.update(); return f

    def test_backing_v2_binds_duplicate_tracks_to_one_owned_artifact_and_replays(self):
        for recipe in (False, True):
            with tempfile.TemporaryDirectory() as tmp:
                f = self.backing_fixture(Path(tmp), recipe)
                audit.validate_record(f.record)
                before = {str(p.relative_to(f.export)): sha(p) for p in f.export.rglob("*") if p.is_file()}
                with mock.patch.object(audit.installed, "_run", side_effect=f.native):
                    result = audit.audit_installed_export_record(**f.args)
                self.assertTrue(result["passed"], result)
                self.assertFalse(result["authorizesRelease"])
                self.assertEqual(before, {str(p.relative_to(f.export)): sha(p) for p in f.export.rglob("*") if p.is_file()})

    def test_backing_v2_refuses_missing_geometry_unbound_artifacts_and_version_confusion(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = self.backing_fixture(Path(tmp))
            audit.validate_record(f.record)
            for key, value in (("trackId", "0" * 16), ("trackId", "not-an-id"),
                ("artifactPath", "master.wav"), ("mediaSha256", "f" * 64),
                ("sourceFrames", 4), ("sourceFrames", True), ("sourceSampleRate", 0),
                ("sourceSampleRate", True), ("sourceChannels", 2)):
                record = copy.deepcopy(f.record); record["backingMedia"][0][key] = value
                with self.subTest(key=key, value=value), self.assertRaises(ValueError): audit.validate_record(record)
            for change in ("missing", "empty", "duplicate-track", "unknown-field", "v1", "wrong-type", "unreferenced-media", "capture-claim"):
                record = copy.deepcopy(f.record)
                if change == "missing": record.pop("backingMedia")
                elif change == "empty": record["backingMedia"] = []
                elif change == "duplicate-track": record["backingMedia"][1] = record["backingMedia"][0]
                elif change == "unknown-field": record["backingMedia"][0]["accepted"] = True
                elif change == "v1": record["schemaVersion"] = 1
                elif change == "wrong-type": record["recordType"] = audit.TYPES["sample"]
                elif change == "capture-claim": record["backingMediaCapture"] = "SIGNED_SOURCE_APPROVED"
                else: record["artifacts"].append(dict(path="media/" + "a" * 64 + ".wav", sha256="a" * 64, frames=3, channels=1))
                with self.subTest(change=change), self.assertRaises(ValueError): audit.validate_record(record)

    def test_backing_v2_replay_refuses_consistent_declared_rate_forgery(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = self.backing_fixture(Path(tmp)); original = copy.deepcopy(f.record["backingMedia"])
            for row in f.record["backingMedia"]: row["sourceSampleRate"] = 48000
            f.update()
            def native(command, timeout):
                result = f.native(command, timeout); result["backingMedia"] = original; return result
            with mock.patch.object(audit.installed, "_run", side_effect=native):
                result = audit.audit_installed_export_record(**f.args)
            self.assertFalse(result["passed"])
            self.assertIn("differs from fresh native rendering", result["errors"][0])

    def test_closed_record_and_non_authorizing_family_bindings(self):
        for recipe in (False, True):
            with tempfile.TemporaryDirectory() as tmp:
                f = Fixture(Path(tmp), recipe)
                audit.validate_record(f.record)
                for change in ({"authorizesRelease": True}, {"schemaVersion": True},
                    {"playback": "PASS"}, {"qualification": "QUALIFIED"}, {"command": "do-something"},
                    {"recordType": "unknown"}, {"masterClipped": 0}, {"masterPeakAbs": float("nan")},
                    {"sampleRate": True}, {"sourceCommit": "bad"}, {"projectLanguages": ["ja", "ja"]}):
                    with self.subTest(recipe=recipe, change=change), self.assertRaises(ValueError):
                        audit.validate_record(f.record | change)
                for path in ("../master.wav", "/master.wav", "a/./master.wav", ".", "a\\master.wav"):
                    record = copy.deepcopy(f.record); record["artifacts"][0]["path"] = path
                    with self.subTest(path=path), self.assertRaises(ValueError):
                        audit.validate_record(record)
                record = copy.deepcopy(f.record); record["artifacts"].append(record["artifacts"][0])
                with self.assertRaises(ValueError): audit.validate_record(record)
                record = copy.deepcopy(f.record); record["project"]["resourceContentHash"] = "f" * 64
                with self.assertRaises(ValueError): audit.validate_record(record)

    def test_mocked_native_replay_uses_caller_inputs_and_cleans_private_outputs(self):
        for recipe in (False, True):
            with tempfile.TemporaryDirectory() as tmp:
                f = Fixture(Path(tmp), recipe)
                original = {p.name: sha(p) for p in f.export.iterdir()}
                with mock.patch.object(audit.installed, "_run", side_effect=f.native):
                    result = audit.audit_installed_export_record(**f.args)
                self.assertTrue(result["passed"], result)
                self.assertFalse(result["authorizesRelease"])
                self.assertEqual("NOT_AUTHENTICATED", result["historicalExecution"])
                self.assertEqual(["executionDateUnixMs", "transactionId"], result["ignoredReceiptFields"])
                self.assertTrue(all(not p.parent.exists() for p in f.outputs))
                self.assertEqual(original, {p.name: sha(p) for p in f.export.iterdir()})

    def test_receipt_changes_beyond_two_volatile_fields_refuse_despite_updated_hashes(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(Path(tmp))
            changed = f.receipt | {"renderQuality": "Preview"}
            f.receipt_path.write_text(json.dumps(changed))
            f.record["exportReceiptSha256"] = sha(f.receipt_path); f.update()
            with mock.patch.object(audit.installed, "_run", side_effect=f.native):
                result = audit.audit_installed_export_record(**f.args)
            self.assertFalse(result["passed"])
            self.assertIn("receipt differs from fresh native", result["errors"][0])

    def test_missing_symlinked_and_interrupted_exports_refuse_before_execution(self):
        for case in ("missing", "leaf-link", "parent-link", "root-link", "journal", "budget"):
            with tempfile.TemporaryDirectory() as tmp:
                f = Fixture(Path(tmp))
                if case == "missing": f.master.unlink()
                elif case == "leaf-link":
                    f.master.rename(f.root / "foreign"); f.master.symlink_to(f.root / "foreign")
                elif case == "parent-link":
                    f.master.rename(f.root / "foreign"); (f.export / "nested").symlink_to(f.root, target_is_directory=True)
                    f.record["artifacts"][0]["path"] = "nested/foreign"; f.update()
                elif case == "root-link":
                    f.export.rename(f.root / "foreign-dir"); f.export.symlink_to(f.root / "foreign-dir", target_is_directory=True)
                elif case == "journal":
                    (f.root / ".export-export-journal.json").write_text("pending canary")
                with mock.patch.object(audit.installed, "_run", side_effect=AssertionError("must not execute")), \
                        mock.patch.object(audit, "AGGREGATE_LIMIT", 1 if case == "budget" else audit.AGGREGATE_LIMIT):
                    result = audit.audit_installed_export_record(**f.args)
                self.assertFalse(result["passed"], (case, result))

    def test_external_pins_and_invalid_timeout_refuse_before_execution(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(Path(tmp))
            with mock.patch.object(audit.installed, "_run", side_effect=AssertionError("must not execute")):
                for name in ("record_sha256", "cli_sha256", "public_key_sha256"):
                    self.assertFalse(audit.audit_installed_export_record(**(f.args | {name: "f" * 64}))["passed"])
                for timeout in (0, -1, True, float("nan"), float("inf"), 301):
                    self.assertFalse(audit.audit_installed_export_record(**(f.args | {"timeout_seconds": timeout}))["passed"])

    def test_export_and_key_substitution_after_execution_are_refused(self):
        for change in ("audio", "key"):
            with tempfile.TemporaryDirectory() as tmp:
                f = Fixture(Path(tmp))
                def substitute(command, timeout):
                    result = f.native(command, timeout)
                    (f.master if change == "audio" else f.key).write_bytes(b"substituted")
                    return result
                with mock.patch.object(audit.installed, "_run", side_effect=substitute):
                    result = audit.audit_installed_export_record(**f.args)
                self.assertFalse(result["passed"], result)
                self.assertTrue(all(not p.parent.exists() for p in f.outputs))

    def test_native_refusal_cleans_private_outputs_and_retains_original_set(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(Path(tmp))
            original = {p.name: sha(p) for p in f.export.iterdir()}
            outputs = []
            def refuse(command, timeout):
                outputs.append(Path(command[-1]).parent)
                raise subprocess.TimeoutExpired(command, timeout)
            with mock.patch.object(audit.installed, "_run", side_effect=refuse):
                result = audit.audit_installed_export_record(**f.args)
            self.assertFalse(result["passed"])
            self.assertTrue(all(not p.exists() for p in outputs))
            self.assertEqual(original, {p.name: sha(p) for p in f.export.iterdir()})


if __name__ == "__main__":
    unittest.main()
