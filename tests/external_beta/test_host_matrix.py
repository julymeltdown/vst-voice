from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.external_beta.host_collector import (
    AUTOMATED_CHECKS,
    build_host_record,
    installed_tree_digest,
    observe_host,
)
from tools.external_beta.host_evidence import HOST_CHECK_NAMES, validate_host_matrix, validate_host_record

ROOT = Path(__file__).resolve().parents[2]
MATRIX = json.loads((ROOT / "docs/product/external-beta-host-matrix.json").read_text(encoding="utf-8"))


def _record(root: Path, target_id: str = "HOST-001") -> dict:
    evidence = []
    for name in ("session", "bounce", "scan"):
        path = root / "evidence" / f"{name}.log"
        path.parent.mkdir(parents=True, exist_ok=True)
        content = f"{name}-evidence".encode("utf-8")
        path.write_bytes(content)
        evidence.append({"kind": name, "path": path.relative_to(root).as_posix(), "sha256": hashlib.sha256(content).hexdigest(), "capturedAt": "2026-08-22T12:00:00Z", "reviewer": "A4"})
    artifact = root / "installed" / "ProjectSEAMEditor.clap"
    artifact.parent.mkdir()
    artifact.write_bytes(b"installed-plugin-bytes")
    artifact_sha256 = hashlib.sha256(artifact.read_bytes()).hexdigest()
    return {
        "schemaVersion": 1,
        "recordType": "external-beta-host-session",
        "status": "PASS",
        "recordId": "host-macos-reaper-clap-candidate-0001",
        "targetId": target_id,
        "platform": "macos",
        "architecture": "arm64",
        "host": "reaper",
        "hostVersion": "7.30",
        "hostBuild": "rev-2026-01",
        "pluginFormat": "CLAP",
        "osBuild": "macOS-26.2",
        "candidateRootId": "candidate-root-001",
        "pluginSha256": artifact_sha256,
        "installedTreeSha256": artifact_sha256,
        "artifactPath": str(artifact),
        "bankIdentity": {"id": "beta.voice.01", "version": "0.1.0", "contentSha256": "3" * 64, "installedProvenanceTreeSha256": "4" * 64},
        "projectIdentity": {"projectSha256": "5" * 64, "mediaSha256": "6" * 64},
        "workloadId": "eb.host.session.v1",
        "workloadSha256": "7" * 64,
        "machineProfileId": "eb.macos.arm64.reference.v1",
        "machineProfileSha256": "8" * 64,
        "clockAuthority": "physical-device-clock",
        "operator": "A4",
        "verifier": "A6",
        "startedAt": "2026-08-22T10:00:00Z",
        "endedAt": "2026-08-22T11:00:00Z",
        "checks": {name: "PASS" for name in HOST_CHECK_NAMES},
        "evidence": evidence,
    }


class HostCollectorTests(unittest.TestCase):
    """The collector must report what a host validator actually did, and nothing more.

    The failure this guards against is a host record claiming twenty-three PASSes for checks
    nobody performed, which is exactly what a hand-written record does.
    """

    def test_a_missing_validator_never_produces_pass_checks(self) -> None:
        result = {'status': 'NOT_RUN', 'reason': 'tool-missing'}
        record = self._record_for(result)
        self.assertEqual(record['status'], 'FAIL')
        for name in AUTOMATED_CHECKS:
            self.assertEqual(record['checks'][name], 'FAIL')
        manual = [name for name in HOST_CHECK_NAMES if name not in AUTOMATED_CHECKS]
        self.assertTrue(manual)
        for name in manual:
            self.assertEqual(record['checks'][name], 'NOT_RUN')
        self.assertNotIn('PASS', set(record['checks'].values()))

    def test_a_failed_validator_marks_only_the_automated_checks_failed(self) -> None:
        result = {'status': 'FAIL', 'failureClass': 'auval-nonzero'}
        record = self._record_for(result)
        self.assertEqual(record['status'], 'FAIL')
        self.assertEqual(record['checks']['instantiate'], 'FAIL')
        self.assertEqual(record['hostValidator']['failureClass'], 'auval-nonzero')

    def test_every_canonical_check_is_present_exactly_once(self) -> None:
        record = self._record_for({'status': 'PASS'})
        self.assertEqual(set(record['checks']), set(HOST_CHECK_NAMES))
        self.assertEqual(len(record['checks']), len(HOST_CHECK_NAMES))
        automated = [n for n in HOST_CHECK_NAMES if n in AUTOMATED_CHECKS]
        for name in automated:
            self.assertEqual(record['checks'][name], 'PASS')
        manual = [n for n in HOST_CHECK_NAMES if n not in AUTOMATED_CHECKS]
        for name in manual:
            self.assertEqual(record['checks'][name], 'NOT_RUN')

    def test_collector_reports_the_running_machine(self) -> None:
        observed = observe_host()
        self.assertIn(observed['platform'], {'macos', 'windows', 'linux'})
        self.assertIn(observed['architecture'], {'arm64', 'x86_64'})
        self.assertTrue(observed['osBuild'])

    def _record_for(self, validator_result: dict) -> dict:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            template = _record(root)
            artifact = root / 'installed' / 'ProjectSEAMEditor.clap'
            target = next(row for row in MATRIX['targets'] if row['id'] == 'HOST-001')
            return build_host_record(
                record_id=template['recordId'],
                target=target,
                candidate_root_id=template['candidateRootId'],
                artifact_path=str(artifact),
                operator=template['operator'],
                verifier=template['verifier'],
                workload_sha256=template['workloadSha256'],
                machine_profile_sha256=template['machineProfileSha256'],
                bank_identity=template['bankIdentity'],
                project_identity=template['projectIdentity'],
                validator_result=validator_result,
                started_at=template['startedAt'],
                ended_at=template['endedAt'],
                artifact_digest=installed_tree_digest(artifact),
            )

class HostMatrixTests(unittest.TestCase):
    def test_matrix_declares_the_nine_required_host_tuples(self) -> None:
        result = validate_host_matrix(MATRIX)
        self.assertTrue(result.passed, result.errors)
        self.assertEqual(9, len(MATRIX["targets"]))

    def test_complete_installed_host_record_passes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            result = validate_host_record(_record(Path(directory)), MATRIX, Path(directory))
            self.assertTrue(result.passed, result.errors)

    def test_target_tuple_mismatch_blocks_cross_host_inheritance(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            record["targetId"] = "HOST-009"
            result = validate_host_record(record, MATRIX, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("target tuple" in error for error in result.errors))

    def test_missing_check_and_tampered_evidence_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            original = copy.deepcopy(record)
            del record["checks"][next(iter(HOST_CHECK_NAMES))]
            artifact = root / record["evidence"][0]["path"]
            artifact.write_text("tampered", encoding="utf-8")
            result = validate_host_record(record, MATRIX, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("missing checks" in error for error in result.errors))
            self.assertTrue(any("does not match" in error for error in result.errors))
            self.assertEqual(original["recordId"], record["recordId"])

    def test_installed_artifact_bytes_must_match_claimed_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            artifact = root / "installed" / "ProjectSEAMEditor.clap"
            artifact.parent.mkdir(exist_ok=True)
            artifact.write_bytes(b"installed-plugin-bytes")
            record["artifactPath"] = str(artifact)
            record["pluginSha256"] = "0" * 64
            record["installedTreeSha256"] = "0" * 64

            result = validate_host_record(record, MATRIX, root)

            self.assertFalse(result.passed)
            self.assertTrue(any("installed artifact" in error.lower() for error in result.errors))

    def test_cli_expect_blocked_is_observable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record_path = root / "record.json"
            record = _record(root)
            record["status"] = "NOT_RUN"
            record["checks"] = {}
            record["evidence"] = []
            record_path.write_text(json.dumps(record), encoding="utf-8")
            completed = subprocess.run(
                [sys.executable, str(ROOT / "scripts/run_external_beta_host_evidence.py"), "--record", str(record_path), "--evidence-root", str(root), "--expect-blocked"],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(0, completed.returncode, completed.stdout)
            self.assertIn('"passed": false', completed.stdout)


if __name__ == "__main__":
    unittest.main()
