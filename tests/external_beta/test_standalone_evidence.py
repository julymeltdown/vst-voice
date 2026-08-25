from __future__ import annotations

import copy
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tools.external_beta.standalone_evidence import UA_ROW_IDS, validate_matrix, validate_standalone_record

ROOT = Path(__file__).resolve().parents[2]
MATRIX = json.loads((ROOT / "docs/product/external-beta-standalone-matrix.json").read_text(encoding="utf-8"))


def _record(root: Path) -> dict:
    rows = []
    for row_id in UA_ROW_IDS:
        path = root / "artifacts" / f"{row_id}.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        content = json.dumps({"row": row_id}, sort_keys=True).encode("utf-8")
        path.write_bytes(content)
        rows.append({
            "id": row_id,
            "status": "PASS",
            "evidence": [{
                "kind": "journey-artifact",
                "path": str(path.relative_to(root)),
                "sha256": hashlib.sha256(content).hexdigest(),
                "capturedAt": "2026-08-21T12:00:00Z",
                "reviewer": "ua-reviewer",
            }],
        })
    return {
        "schemaVersion": 1,
        "recordType": "engineering-standalone-journey",
        "status": "PASS",
        "engineeringQualification": True,
        "recordId": "standalone-macos-arm64-20260821-001",
        "platform": "macos",
        "architecture": "arm64",
        "osBuild": "macOS-15.6",
        "appIdentity": {"version": "0.13.1", "buildId": "beta-build-001", "sourceCommit": "a" * 40, "installedTreeSha256": "b" * 64},
        "bankIdentity": {"id": "beta.voice.01", "version": "0.1.0", "contentSha256": "c" * 64, "installedProvenanceTreeSha256": "d" * 64},
        "projectIdentity": {"projectSha256": "e" * 64, "mediaSha256": "f" * 64},
        "workloadId": "eb.standalone.ua.v1",
        "workloadSha256": "1" * 64,
        "machineProfileId": "eb.macos.arm64.reference.v1",
        "machineProfileSha256": "2" * 64,
        "device": {"deviceId": "coreaudio-physical-01", "sampleRate": 48000, "blockSize": 128, "channels": 2, "authority": "physical"},
        "clockAuthority": "physical-device-clock",
        "comparisonPolicy": {"crossPlatformByteIdentity": False, "crossPlatformTolerance": "duration/channels/finiteness/alignment/listening tolerances"},
        "operator": "operator-01",
        "startedAt": "2026-08-21T10:00:00Z",
        "endedAt": "2026-08-21T11:00:00Z",
        "rows": rows,
    }


class StandaloneEvidenceTests(unittest.TestCase):
    def test_matrix_has_both_target_os_and_twenty_rows(self) -> None:
        result = validate_matrix(MATRIX)
        self.assertTrue(result.passed, result.errors)
        self.assertEqual(20, len(MATRIX["rows"]))

    def test_complete_physical_engineering_journey_passes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = validate_standalone_record(_record(root), MATRIX, root)
            self.assertTrue(result.passed, result.errors)

    def test_missing_row_and_missing_artifact_are_blocking(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            record["rows"] = record["rows"][1:]
            record["rows"][0]["evidence"][0]["path"] = "missing.bin"
            result = validate_standalone_record(record, MATRIX, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("UA-001" in error for error in result.errors))
            self.assertTrue(any("does not exist" in error for error in result.errors))

    def test_threaded_clock_and_official_fixture_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            record["clockAuthority"] = "threaded-test-clock"
            record["bankIdentity"]["id"] = "official.voice.01"
            result = validate_standalone_record(record, MATRIX, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("physical-device" in error for error in result.errors))
            self.assertTrue(any("Official Voicebank" in error for error in result.errors))

    def test_windows_record_must_use_x64_and_physical_device(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            record["platform"] = "windows"
            record["architecture"] = "arm64"
            record["device"]["authority"] = "simulated"
            result = validate_standalone_record(record, MATRIX, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("platform/architecture" in error for error in result.errors))
            self.assertTrue(any("authority must be physical" in error for error in result.errors))

    def test_hash_tampering_is_detected_without_mutating_record(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = _record(root)
            original = copy.deepcopy(record)
            first_path = root / record["rows"][0]["evidence"][0]["path"]
            first_path.write_text("tampered", encoding="utf-8")
            result = validate_standalone_record(record, MATRIX, root)
            self.assertFalse(result.passed)
            self.assertTrue(any("does not match" in error for error in result.errors))
            self.assertEqual(original, record)


if __name__ == "__main__":
    unittest.main()
