from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.external_beta.install_evidence import INSTALL_ROW_IDS, validate_install_matrix, validate_install_record

ROOT = Path(__file__).resolve().parents[2]
MATRIX = json.loads((ROOT / "docs/product/external-beta-install-matrix.json").read_text(encoding="utf-8"))


def _record(root: Path, platform: str = "macos") -> dict:
    rows = []
    for row_id in INSTALL_ROW_IDS:
        path = root / "evidence" / f"{row_id}.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        content = json.dumps({"row": row_id}, sort_keys=True).encode("utf-8")
        path.write_bytes(content)
        digest = hashlib.sha256(content).hexdigest()
        rows.append({
            "id": row_id,
            "status": "PASS",
            "preInventorySha256": "1" * 64,
            "postInventorySha256": "2" * 64,
            "evidence": [{
                "kind": "installer-log",
                "path": str(path.relative_to(root)),
                "sha256": digest,
                "capturedAt": "2026-08-22T12:00:00Z",
                "reviewer": "A6",
            }],
        })
    artifacts = {}
    for name in ("deliverable.pkg", "installer.pkg", "installed-tree"):
        path = root / name
        path.write_bytes(name.encode("utf-8"))
        artifacts[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    return {
        "schemaVersion": 1,
        "recordType": "external-beta-install-lifecycle",
        "status": "PASS",
        "recordId": "install-macos-arm64-candidate-0001",
        "platform": platform,
        "architecture": "arm64" if platform == "macos" else "x86_64",
        "osBuild": "macOS-26.2",
        "imageId": "clean-snapshot-001",
        "accountAuthority": "clean-verifier-snapshot",
        "candidateRootId": "candidate-root-001",
        "deliverablePath": "deliverable.pkg",
        "deliverableSha256": artifacts["deliverable.pkg"],
        "installerPath": "installer.pkg",
        "installerSha256": artifacts["installer.pkg"],
        "installedPath": "installed-tree",
        "installedTreeSha256": artifacts["installed-tree"],
        "bankIdentity": {"id": "beta.voice.01", "version": "0.1.0", "contentSha256": "6" * 64, "installedProvenanceTreeSha256": "7" * 64},
        "acquisition": {"channel": "governed-envelope", "envelopeManifestSha256": "8" * 64, "networkDisabledAfterAcquisition": True},
        "clockAuthority": "physical-device-clock",
        "operator": "A6",
        "verifier": "A4",
        "startedAt": "2026-08-22T10:00:00Z",
        "endedAt": "2026-08-22T11:00:00Z",
        "inventory": {"preInstallSha256": "9" * 64, "postInstallSha256": "a" * 64, "postUninstallSha256": "b" * 64, "preservedUserDataCanaries": True, "residualOwnedCode": False},
        "rows": rows,
    }


class InstallEvidenceTests(unittest.TestCase):
    def test_matrix_declares_both_target_platforms_and_canonical_rows(self) -> None:
        result = validate_install_matrix(MATRIX)
        self.assertTrue(result.passed, result.errors)
        self.assertEqual(12, len(MATRIX["rows"]))

    def test_complete_clean_snapshot_record_passes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            result = validate_install_record(_record(Path(directory)), MATRIX, Path(directory))
            self.assertTrue(result.passed, result.errors)

    def test_missing_row_or_network_reenable_blocks_record(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            record["rows"] = record["rows"][1:]
            record["acquisition"]["networkDisabledAfterAcquisition"] = False
            result = validate_install_record(record, MATRIX, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("INSTALL-001" in error for error in result.errors))
            self.assertTrue(any("network" in error.lower() for error in result.errors))

    def test_tampered_evidence_and_source_path_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            original = copy.deepcopy(record)
            artifact = root / record["rows"][0]["evidence"][0]["path"]
            artifact.write_text("tampered", encoding="utf-8")
            record["rows"][1]["evidence"][0]["path"] = "build/private.log"
            result = validate_install_record(record, MATRIX, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("does not match" in error for error in result.errors))
            self.assertTrue(any("source/build" in error for error in result.errors))
            self.assertEqual(original["recordId"], record["recordId"])

    def test_artifact_byte_paths_are_required_and_rehashed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            for name in ("deliverable.pkg", "installer.pkg", "installed-tree"):
                (root / name).write_bytes(name.encode("utf-8"))
            record["deliverablePath"] = "deliverable.pkg"
            record["installerPath"] = "installer.pkg"
            record["installedPath"] = "installed-tree"
            record["deliverableSha256"] = "0" * 64
            record["installerSha256"] = "0" * 64
            record["installedTreeSha256"] = "0" * 64

            result = validate_install_record(record, MATRIX, root)

            self.assertFalse(result.passed)
            self.assertTrue(any("deliverablesha" in error.lower() for error in result.errors))

    def test_wrong_platform_and_nonclean_authority_fail(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root, "windows")
            record["architecture"] = "arm64"
            record["accountAuthority"] = "developer-workspace"
            result = validate_install_record(record, MATRIX, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("platform/architecture" in error for error in result.errors))
            self.assertTrue(any("clean-verifier" in error for error in result.errors))

    def test_cli_expect_blocked_is_an_observable_fail_closed_surface(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            record["status"] = "NOT_RUN"
            record_path = root / "record.json"
            record_path.write_text(json.dumps(record), encoding="utf-8")
            completed = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts/run_external_beta_install_evidence.py"),
                    "--record",
                    str(record_path),
                    "--evidence-root",
                    str(root),
                    "--expect-blocked",
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(0, completed.returncode, completed.stdout)
            self.assertIn('"passed": false', completed.stdout)


if __name__ == "__main__":
    unittest.main()
