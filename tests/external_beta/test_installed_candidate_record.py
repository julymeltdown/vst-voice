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


def fixture():
    record = {key: "a" * 64 for key in audit.DIGESTS}
    record.update(schemaVersion=1, recordType=audit.RECORD_TYPE, result="InstalledCandidateVerified",
        evidenceScope="ENGINEERING_ONLY", resourceKind="sample-procedural", payloadFamily="sample",
        resourceId="fixture.bank", resourceVersion="1.0.0", signerKeyId="fixture.signer", installedFiles=4,
        qualification="NOT_QUALIFIED", humanAcceptance="NOT_RUN", authorizesRelease=False, releaseEligible=False)
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
