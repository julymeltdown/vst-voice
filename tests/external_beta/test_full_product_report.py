from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from tests.external_beta.full_product_report_fixture import complete_report
from tests.external_beta.release_gate_fixtures import candidate
from tools.external_beta import full_product_report as full_product_report_module
from tools.external_beta import release_gate
from tools.external_beta.full_product_report import (
    FullProductReportError,
    REFERENCE_READ_CHUNK_BYTES,
    _read_regular_reference,
    _safe_reference_path,
    validate_full_product_report_reference,
    validate_full_product_report,
)


ROOT = Path(__file__).resolve().parents[2]
ACCEPTANCE = release_gate.load_candidate(ROOT / "docs/product/external-beta-acceptance.json")
FULL_CONTRACT = release_gate.load_candidate(ROOT / "docs/product/full-product-beta-contract.json")


class FullProductReportReaderTests(unittest.TestCase):
    def _reference(self, path: Path) -> dict[str, str]:
        return {"locator": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}

    def test_report_is_rehashed_before_schema_or_semantic_checks(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.json"
            path.write_bytes(b"{}")
            reference = self._reference(path)
            errors = validate_full_product_report_reference(
                reference,
                candidate=candidate(),
                acceptance_contract=ACCEPTANCE,
                full_product_contract=FULL_CONTRACT,
            )
            self.assertTrue(any(error.startswith("schema ") for error in errors), errors)
            self.assertFalse(any("content digest" in error for error in errors), errors)

            path.write_bytes(b'{"status":"PASS"}')
            errors = validate_full_product_report_reference(
                reference,
                candidate=candidate(),
                acceptance_contract=ACCEPTANCE,
                full_product_contract=FULL_CONTRACT,
            )
            self.assertTrue(any("content digest" in error for error in errors), errors)

    def test_duplicate_report_keys_are_rejected_before_schema_validation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.json"
            path.write_bytes(b'{"status":"PASS","status":"PASS"}')
            errors = validate_full_product_report_reference(
                self._reference(path),
                candidate=candidate(),
                acceptance_contract=ACCEPTANCE,
                full_product_contract=FULL_CONTRACT,
            )
            self.assertTrue(any("duplicate object key" in error for error in errors), errors)

    def test_symlinked_report_is_not_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "target.json"
            target.write_text(json.dumps({"status": "PASS"}), encoding="utf-8")
            report = root / "report.json"
            try:
                report.symlink_to(target)
            except OSError as error:
                self.skipTest(f"symlink creation unavailable: {error}")
            errors = validate_full_product_report_reference(
                {"locator": str(report), "sha256": hashlib.sha256(target.read_bytes()).hexdigest()},
                candidate=candidate(),
                acceptance_contract=ACCEPTANCE,
                full_product_contract=FULL_CONTRACT,
            )
            self.assertTrue(any("regular file" in error for error in errors), errors)

    def test_relative_raw_reference_cannot_escape_report_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(FullProductReportError):
                _safe_reference_path("../outside", root, "rawEvidence")

    def test_small_reference_reads_do_not_allocate_the_policy_maximum(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "evidence.json"
            payload = b'{"status":"PASS"}'
            path.write_bytes(payload)
            requests: list[int] = []
            real_fdopen = full_product_report_module.os.fdopen

            class RecordingReader:
                def __init__(self, stream):
                    self.stream = stream

                def fileno(self):
                    return self.stream.fileno()

                def read(self, size=-1):
                    requests.append(size)
                    return self.stream.read(size)

                def __enter__(self):
                    return self

                def __exit__(self, exception_type, exception, traceback):
                    self.stream.close()

            with mock.patch.object(
                full_product_report_module.os,
                "fdopen",
                side_effect=lambda descriptor, mode: RecordingReader(real_fdopen(descriptor, mode)),
            ):
                contents = _read_regular_reference(
                    self._reference(path), base=root, label="rawEvidence"
                )
            self.assertEqual(payload, contents)
            self.assertEqual(len(payload) + 1, requests[0])
            self.assertLessEqual(max(requests), REFERENCE_READ_CHUNK_BYTES)

    def test_cli_exposes_blocked_semantic_result(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = root / "report.json"
            candidate_path = root / "candidate.json"
            report.write_text("{}", encoding="utf-8")
            candidate_path.write_text(json.dumps(candidate()), encoding="utf-8")
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts/verify_full_product_report.py"),
                    "--report",
                    str(report),
                    "--candidate",
                    str(candidate_path),
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(3, result.returncode, result.stdout + result.stderr)
            self.assertEqual("BLOCKED", json.loads(result.stdout)["status"])


class FullProductSemanticTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.report, self.contract = complete_report(self.root)

    def errors(self):
        return validate_full_product_report(self.report, full_product_contract=self.contract,
            report_path=self.root / "report.json")

    def test_complete_83_case_synthetic_report_has_a_real_success_path(self):
        before = (ROOT / "docs/product/full-product-beta-contract.json").read_bytes()
        self.assertEqual(83, len(self.report["cases"]))
        self.assertEqual(175, sum(len(value["cells"]) for value in self.report["empiricalResults"].values()))
        self.assertEqual((), self.errors())
        self.assertEqual(before, (ROOT / "docs/product/full-product-beta-contract.json").read_bytes())
        canonical_errors = validate_full_product_report(self.report, full_product_contract=FULL_CONTRACT,
            report_path=self.root / "report.json")
        self.assertTrue(any("not frozen" in error for error in canonical_errors), canonical_errors)

    def test_corrupted_raw_audio_is_rejected_by_the_same_full_success_fixture(self):
        (self.root / "synthetic-tone.wav").write_bytes(b"changed")
        self.assertTrue(any("content digest" in error for error in self.errors()))

    def test_numeric_failure_cannot_be_hidden_by_rehashing_its_raw_record(self):
        measurement = self.report["cases"][0]["observations"][0]["measurements"][0]
        self.assertEqual("pitch-median", measurement["criterionId"])
        measurement["value"] = 3000
        reference = measurement["rawEvidence"]
        path = self.root / reference["locator"]
        path.write_text(json.dumps({key: value for key, value in measurement.items() if key != "rawEvidence"}))
        reference["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        self.assertTrue(any("fails the frozen maximum" in error for error in self.errors()))

    def test_protocol_and_measurement_method_are_bound_to_frozen_definitions(self):
        observation = self.report["cases"][0]["observations"][0]
        observation["checkResults"][0]["protocolSha256"] = "e" * 64
        observation["measurements"][0]["methodSha256"] = "f" * 64
        errors = self.errors()
        self.assertTrue(any("protocol digest" in error for error in errors))
        self.assertTrue(any("method digest" in error for error in errors))

    def test_review_claim_cannot_differ_from_retained_reviewer_record(self):
        self.report["cases"][0]["observations"][0]["reviews"][0]["reviewerId"] = "substituted-reviewer"
        self.assertTrue(any("typed claim differs" in error for error in self.errors()))

    def test_raw_boolean_cannot_impersonate_numeric_measurement_one(self):
        measurement = self.report["cases"][0]["observations"][0]["measurements"][2]
        self.assertEqual(1, measurement["value"])
        raw = {key: value for key, value in measurement.items() if key != "rawEvidence"}
        raw["value"] = True
        path = self.root / measurement["rawEvidence"]["locator"]
        path.write_text(json.dumps(raw))
        measurement["rawEvidence"]["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        self.assertTrue(any("typed claim differs" in error for error in self.errors()))

    def test_empirical_grid_compares_measurements_to_frozen_thresholds(self):
        self.report["empiricalResults"]["acoustic-boundaries"]["cells"][0]["value"] = 100
        self.assertTrue(any("fails frozen threshold" in error for error in self.errors()))

    def test_restored_archive_root_is_used_instead_of_the_repository(self):
        path = self.root / "report.json"
        path.write_text(json.dumps(self.report))
        reference = {"locator": "report.json", "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        candidate_value = {"candidateRoot": {"id": self.report["candidateRootId"], "sha256": self.report["candidateRootSha256"]},
            "acceptanceContractSha256": self.report["acceptanceContractSha256"]}
        acceptance = {"fullProductContract": {"locator": "synthetic-contract", "sha256": self.report["fullProductContractSha256"]}}
        errors = validate_full_product_report_reference(reference, candidate=candidate_value,
            acceptance_contract=acceptance, full_product_contract=self.contract, evidence_root=self.root)
        self.assertEqual((), errors)
        reference["locator"] = str(ROOT / "docs/product/full-product-beta-contract.json")
        errors = validate_full_product_report_reference(reference, candidate=candidate_value,
            acceptance_contract=acceptance, full_product_contract=self.contract, evidence_root=self.root)
        self.assertTrue(any("escapes the restored archive root" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
