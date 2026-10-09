"""Closed-record and process boundaries; real native positives live in the CLI suite."""
from __future__ import annotations

import copy
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

from tools.external_beta import installed_candidate_record as audit
from tools.external_beta.full_product_report import validate_full_product_report


def fixture(version=2):
    record = {key: "a" * 64 for key in audit.DIGESTS}
    record.update(schemaVersion=version, recordType=audit.LEGACY_RECORD_TYPE if version == 1 else audit.RECORD_TYPE, result="InstalledCandidateVerified",
        evidenceScope="ENGINEERING_ONLY", resourceKind="sample-procedural", payloadFamily="sample",
        resourceId="fixture.bank", resourceVersion="1.0.0", signerKeyId="fixture.signer", installedFiles=4,
        qualification="NOT_QUALIFIED", humanAcceptance="NOT_RUN", authorizesRelease=False, releaseEligible=False)
    if version == 2:
        record.update(externalDependencies=[], dependencyEvidence="SIGNED_DECLARATION", runtimeAvailability="NOT_CHECKED")
    return record


class InstalledCandidateRecordTests(unittest.TestCase):
    def test_closed_shape_never_accepts_human_or_release_claims(self):
        record = fixture()
        audit.validate_record(record)
        mutations = [{**record, "reviews": []}, {**record, "reviewerRegistry": {}},
            {**record, "qualification": "QUALIFIED"}, {**record, "humanAcceptance": "PASS"},
            {**record, "authorizesRelease": True}, {**record, "releaseEligible": True},
            {**record, "schemaVersion": True}, {**record, "installedFiles": True},
            {**record, "resourceKind": []}, {**record, "packageDigest": "not-a-digest"},
            {**record, "resourceKind": "neural-original", "payloadFamily": "model"}]
        for value in mutations:
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    audit.validate_record(value)
        errors = validate_full_product_report(record, verify_references=False)
        self.assertTrue(errors)
        self.assertTrue(any("recordType" in error or "schema" in error for error in errors))

    def test_dependencies_are_family_specific_closed_declarations(self):
        sample = fixture()
        dependency = {"kind": "render-engine", "id": "fixture.engine", "revision": "1"}
        recipe = sample | {"resourceKind": "recipe-original", "payloadFamily": "recipe",
            "externalDependencies": [dependency]}
        audit.validate_record(recipe)
        for record in (sample | {"externalDependencies": [dependency]},
                recipe | {"externalDependencies": []}, recipe | {"externalDependencies": [dependency, dependency]},
                recipe | {"externalDependencies": {}}, recipe | {"runtimeAvailability": "AVAILABLE"},
                recipe | {"dependencyEvidence": "RUNTIME_VERIFIED"}):
            with self.subTest(record=record), self.assertRaises(ValueError):
                audit.validate_record(record)
        for change in ({"kind": "neural-runtime"}, {"id": ""}, {"id": "x" * 129},
                {"revision": 1}, {"revision": "x" * 65}, {"sha256": "a" * 64}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                audit.validate_record(recipe | {"externalDependencies": [dependency | change]})
        legacy = fixture(1)
        audit.validate_record(legacy)
        with self.assertRaises(ValueError):
            audit.validate_record(legacy | {"externalDependencies": []})
        with self.assertRaises(ValueError):
            audit.validate_record(sample | {"schemaVersion": 1})

    def test_replay_keeps_historical_versions_without_upgrading(self):
        # Policy-only fixtures; real current-native output is exercised by CTest.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record, cli, key = (root / name for name in ("record", "cli", "key"))
            cli.write_bytes(b"cli"); key.write_bytes(b"key")
            digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
            for retained_version in (1, 2):
                record.write_text(json.dumps(fixture(retained_version)))
                for emitted_version in (1, 2):
                    with self.subTest(retained=retained_version, emitted=emitted_version), \
                            mock.patch.object(audit, "_run", return_value=fixture(emitted_version)):
                        result = audit.audit_installed_candidate_record(record_path=record,
                            record_sha256=digest(record), package_path=root / "package",
                            installed_directory=root / "installed", public_key_path=key,
                            public_key_sha256=digest(key), cli_path=cli, cli_sha256=digest(cli))
                        self.assertEqual(retained_version == emitted_version, result["passed"])
                        self.assertFalse(result["authorizesRelease"])
                        if retained_version != emitted_version:
                            self.assertIn("requires a pinned CLI emitting", result["errors"][0])

    def test_pins_and_unknown_fields_are_checked_before_native_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = root / "record.json"
            cli = root / "native-cli"
            key = root / "public-key.json"
            cli.write_bytes(b"fixture-not-an-executable")
            key.write_bytes(b"fixture-not-a-trust-key")
            record.write_text(json.dumps(fixture()))
            digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
            args = dict(record_path=record, record_sha256=digest(record), package_path=root / "package",
                installed_directory=root / "installed", public_key_path=key, public_key_sha256=digest(key),
                cli_path=cli, cli_sha256=digest(cli))
            with mock.patch.object(audit, "_run", side_effect=AssertionError("must not execute")):
                for name in ("record_sha256", "public_key_sha256", "cli_sha256"):
                    result = audit.audit_installed_candidate_record(**(args | {name: "f" * 64}))
                    self.assertFalse(result["passed"])
                    self.assertFalse(result["authorizesRelease"])
                    self.assertIn("captured digest", result["errors"][0])
                record.write_text(json.dumps(fixture() | {"reviews": []}))
                self.assertFalse(audit.audit_installed_candidate_record(**(args | {"record_sha256": digest(record)}))["passed"])

    def test_rehashed_forged_record_does_not_match_fresh_native_output(self):
        # This mock checks comparison policy only. The native CLI suite exercises
        # both sample and recipe success through the real executable and key.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            current = fixture()
            claimed = current | {"installedResourceTreeSha256": "b" * 64}
            paths = [root / name for name in ("record", "cli", "key")]
            for path, data in zip(paths, (json.dumps(claimed).encode(), b"cli", b"key")):
                path.write_bytes(data)
            with mock.patch.object(audit, "_run", return_value=current):
                result = audit.audit_installed_candidate_record(record_path=paths[0],
                    record_sha256=hashlib.sha256(paths[0].read_bytes()).hexdigest(), package_path=root / "package",
                    installed_directory=root / "installed", public_key_path=paths[2],
                    public_key_sha256=hashlib.sha256(b"key").hexdigest(), cli_path=paths[1], cli_sha256=hashlib.sha256(b"cli").hexdigest())
            self.assertFalse(result["passed"])
            self.assertIn("fresh native verification", result["errors"][0])

    @unittest.skipUnless(os.name == "posix", "native replay is POSIX-only")
    def test_owned_scratch_is_removed_after_success_and_forced_timeout(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            sibling = parent / "seam-installed-replay-foreign"
            sibling.mkdir()
            unrelated = sibling / "foreign.txt"
            unrelated.write_text("preserve")
            notice = parent / "scratch-path.txt"
            child = ("import os,pathlib,sys,time,json; "
                "p=pathlib.Path(os.environ['TMPDIR']); "
                "(p/'partial-package').write_bytes(b'partial'); "
                "pathlib.Path(sys.argv[1]).write_text(str(p)); ")
            with mock.patch.object(tempfile, "tempdir", str(parent)):
                result = audit._run([sys.executable, "-c", child + "print(json.dumps({'ok':True}))", str(notice)], 5)
                self.assertEqual({"ok": True}, result)
                self.assertEqual(parent.resolve(), Path(notice.read_text()).parent.resolve())
                self.assertNotEqual(sibling.resolve(), Path(notice.read_text()).resolve())
                self.assertFalse(Path(notice.read_text()).exists())
                notice.unlink()
                with self.assertRaisesRegex(ValueError, "timed out"):
                    audit._run([sys.executable, "-c", child + "time.sleep(10)", str(notice)], 1)
                self.assertTrue(notice.exists(), "child must create scratch before timeout")
                self.assertEqual(parent.resolve(), Path(notice.read_text()).parent.resolve())
                self.assertNotEqual(sibling.resolve(), Path(notice.read_text()).resolve())
                self.assertFalse(Path(notice.read_text()).exists())
                self.assertEqual("preserve", unrelated.read_text())

    @unittest.skipUnless(os.name == "posix", "native replay is POSIX-only")
    def test_native_runner_has_output_timeout_and_environment_bounds(self):
        with self.assertRaisesRegex(ValueError, "output limit"):
            audit._run([sys.executable, "-c", "print('x' * 100000)"], 5)
        with self.assertRaisesRegex(ValueError, "timed out"):
            audit._run([sys.executable, "-c", "import time; time.sleep(10)"], 0.03)
        with mock.patch.dict(os.environ, {"SEAM_UNTRUSTED_ENVIRONMENT": "must-not-inherit"}):
            result = audit._run([sys.executable, "-c", "import os,json; print(json.dumps({'clean':'SEAM_UNTRUSTED_ENVIRONMENT' not in os.environ}))"], 5)
        self.assertEqual({"clean": True}, result)


if __name__ == "__main__":
    unittest.main()
