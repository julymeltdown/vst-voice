from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.external_beta import standalone_collector
from tools.external_beta.standalone_collector import (
    build_standalone_record,
    observe_audio_devices,
    select_output_device,
)
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
                "path": path.relative_to(root).as_posix(),
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


def _reference_device_rates() -> dict:
    """Device sample rates read straight from the OS, independent of the collector."""
    try:
        completed = subprocess.run(
            ["system_profiler", "SPAudioDataType", "-json"],
            capture_output=True, text=True, timeout=120, check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return {}
    if completed.returncode != 0:
        return {}
    try:
        entries = json.loads(completed.stdout).get("SPAudioDataType", [])
    except json.JSONDecodeError:
        return {}
    rates = {}
    for entry in entries:
        for item in entry.get("_items", []):
            name = item.get("_name")
            raw = item.get("coreaudio_device_srate")
            if isinstance(name, str) and isinstance(raw, (int, float)) and not isinstance(raw, bool):
                rates[name] = int(raw)
    return rates


class StandaloneCollectorTests(unittest.TestCase):
    """The device block must be measured, and an unmeasurable device must not become PASS.

    A record could previously claim a 48 kHz physical device on a machine whose output runs at
    44.1 kHz, because every field in the device block was typed by the record's author.
    """

    def test_the_device_block_is_read_from_the_running_system(self) -> None:
        observed = observe_audio_devices()
        devices = observed['devices']
        self.assertIsInstance(devices, list)
        if observed['reason']:
            # A host that cannot be queried must say so rather than invent a device.
            self.assertEqual(devices, [])
            return
        for device in devices:
            self.assertIsInstance(device['deviceId'], str)
            self.assertTrue(device['deviceId'])
            if device['sampleRate'] is not None:
                self.assertIn(device['sampleRate'], {44100, 48000, 96000, 192000})

    def test_a_selected_device_has_a_rate_the_validator_accepts(self) -> None:
        device = select_output_device(observe_audio_devices())
        if device is None:
            self.skipTest('no physical output device with a sample rate on this host')
        self.assertIn(device['sampleRate'], {44100, 48000, 96000})

    def test_no_observable_device_yields_not_run_not_pass(self) -> None:
        record = self._record_for(observed={'devices': [], 'reason': 'query-failed'})
        self.assertEqual(record['status'], 'NOT_RUN')
        self.assertNotIn('device', record)
        self.assertEqual(record['deviceObservation']['reason'], 'query-failed')
        # A journey on unidentified hardware cannot claim a physical device authority.
        self.assertNotIn('authority', record.get('device', {}))

    def test_a_measured_device_records_its_real_rate(self) -> None:
        device = {'deviceId': 'Test Output', 'deviceName': 'Test Output',
                 'manufacturer': 'Test', 'transport': 'builtin',
                 'sampleRate': 96000, 'defaultOutput': True}
        record = self._record_for(observed={'devices': [device], 'reason': ''})
        self.assertEqual(record['status'], 'PASS')
        self.assertEqual(record['device']['sampleRate'], 96000)
        self.assertEqual(record['device']['authority'], 'physical')
        self.assertEqual(record['device']['deviceId'], 'Test Output')

    def test_the_sample_rate_comes_from_the_device_query_not_a_constant(self) -> None:
        # Every device on this host reports 48000, so asserting 'the rate is 48000' cannot tell
        # a measured value from a hardcoded one. The rates are compared against what the operating
        # system reports for the same devices, read independently here.
        observed = observe_audio_devices()
        if observed['reason']:
            self.skipTest('audio device query unavailable on this host')
        expected = _reference_device_rates()
        if not expected:
            self.skipTest('no reference device rates available')
        compared = 0
        for device in observed['devices']:
            reference = expected.get(device['deviceId'])
            if reference is None:
                continue
            self.assertEqual(device['sampleRate'], reference)
            compared += 1
        self.assertGreater(compared, 0)

    def _record_for(self, observed: dict) -> dict:
        with tempfile.TemporaryDirectory() as temporary:
            template = _record(Path(temporary))
            original = standalone_collector.observe_audio_devices
            standalone_collector.observe_audio_devices = lambda: observed
            try:
                return build_standalone_record(
                    record_id=template['recordId'],
                    operator=template['operator'],
                    app_identity=template['appIdentity'],
                    bank_identity=template['bankIdentity'],
                    project_identity=template['projectIdentity'],
                    workload_id=template['workloadId'],
                    workload_sha256=template['workloadSha256'],
                    machine_profile_id=template['machineProfileId'],
                    machine_profile_sha256=template['machineProfileSha256'],
                    rows=template['rows'],
                    block_size=template['device']['blockSize'],
                    started_at=template['startedAt'],
                    ended_at=template['endedAt'],
                )
            finally:
                standalone_collector.observe_audio_devices = original

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
