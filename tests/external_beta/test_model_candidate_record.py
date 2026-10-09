"""Model refusal records never attest graph execution, installation or quality."""
from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools.external_beta import model_candidate_record as model
from tools.external_beta import installed_candidate_record as installed
from tools.external_beta.full_product_report import validate_full_product_report


def fixture():
    return dict(model.FIXED, **{key: "a" * 64 for key in model.DIGESTS},
        resourceId="fixture.model", resourceVersion="0.0.1", packageEntries=4,
        externalDependencies=[{"kind": "neural-runtime", "id": "fixture.runtime", "revision": "1"}])


class ModelCandidateRecordTests(unittest.TestCase):
    def test_closed_shape_refuses_install_runtime_quality_and_release_claims(self):
        record = fixture()
        model.validate_record(record)
        mutations = {"schemaVersion": True, "installationResult": "INSTALLED", "refusalReason": "OTHER",
            "installDirectoryCreated": True, "graphExecution": "PASS", "runtimeAvailability": "AVAILABLE",
            "qualification": "QUALIFIED", "humanAcceptance": "PASS", "authorizesRelease": True,
            "releaseEligible": True, "resourceKind": "sample-real", "payloadFamily": "sample",
            "packageEntries": True, "resourceCandidateSha256": "invalid"}
        for key, value in mutations.items():
            with self.subTest(key=key), self.assertRaises(ValueError):
                model.validate_record(record | {key: value})
        for extra in ("installedResourceTreeSha256", "installReceiptSha256", "reviewerRegistry", "command"):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                model.validate_record(record | {extra: "not permitted"})
        with self.assertRaises(ValueError):
            installed.validate_record(record)
        self.assertTrue(validate_full_product_report(record, verify_references=False))

    def test_dependency_is_exact_declared_kind_id_revision_without_fabricated_digest(self):
        for dependencies in ([], {}, ["runtime"], [{"kind": "render-engine", "id": "x", "revision": "1"}],
                [{"kind": "neural-runtime", "id": "x", "revision": 1}],
                [{"kind": "neural-runtime", "id": "x", "revision": "1", "sha256": "a" * 64}],
                fixture()["externalDependencies"] * 2):
            with self.subTest(dependencies=dependencies), self.assertRaises(ValueError):
                model.validate_record(fixture() | {"externalDependencies": dependencies})

    def test_pinned_forged_dependency_is_rechecked_by_fresh_native_probe(self):
        # Mock tests comparison/routing policy only. Native CLI integration covers
        # real signed opaque packages, actual refusal and replay through this module.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            retained = fixture()
            actual = copy.deepcopy(retained)
            actual["externalDependencies"][0]["revision"] = "2"
            record, cli, key = (root / name for name in ("record", "cli", "key"))
            record.write_text(json.dumps(retained)); cli.write_bytes(b"cli"); key.write_bytes(b"key")
            digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
            args = dict(record_path=record, record_sha256=digest(record), package_path=root / "package",
                public_key_path=key, public_key_sha256=digest(key), cli_path=cli, cli_sha256=digest(cli))
            with mock.patch.object(installed, "_run", return_value=actual) as runner:
                result = model.audit_model_candidate_record(**args)
                self.assertFalse(result["passed"])
                self.assertFalse(result["authorizesRelease"])
                self.assertIn("fresh native verification", result["errors"][0])
                self.assertEqual([str(cli), "probe-model-candidate", str(root / "package"),
                    retained["packageDigest"], retained["resourceCandidateSha256"], str(key)], runner.call_args.args[0])
            with mock.patch.object(installed, "_run", side_effect=AssertionError("must not execute")):
                self.assertFalse(model.audit_model_candidate_record(**(args | {"record_sha256": "f" * 64}))["passed"])


if __name__ == "__main__":
    unittest.main()
