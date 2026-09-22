"""Contract/negative checks for LF diagnostics, not perceptual quality tests."""
import copy
import hashlib
import json
import math
from pathlib import Path
import tempfile
import unittest

import numpy as np

from tools.singing_quality.lf_control_diagnostics import (
    ARRAY_FILES, OUTPUT_IDS, expected_controls, paired_metrics, read_bounded,
    validate_cases, verify_analysis, verify_components,
)


def case_records():
    result = []
    for identity, (rate, midi, frequency, amplitude, clean) in expected_controls().items():
        hz = 440 * math.exp2((midi - 69) / 12)
        result.append(dict(id=identity, sampleRate=rate, sourceFrames=rate // 2,
            analysisConstantHz=hz, seamCarrierHz=hz, oracleHz=hz,
            control=dict(midi=midi, lfHz=frequency, lfPeakAmplitude=amplitude,
                         cleanCaseId=clean, voicedGainFloat32=float(np.float32(0.2)), lfPhaseRadians=0),
            outputs=[dict(id=identity) for identity in sorted(OUTPUT_IDS)]))
    return result


class LfControlDiagnosticsTests(unittest.TestCase):
    def test_grid_requires_exact_42_unique_controls_and_fixed_conditioning(self):
        records = case_records()
        self.assertEqual(42, len(validate_cases(records)))
        self.assertEqual(6, sum(x["control"]["lfPeakAmplitude"] == 0 for x in records))
        mutations = [lambda xs: xs.pop(), lambda xs: xs.__setitem__(1, copy.deepcopy(xs[0])),
                     lambda xs: xs[0].update(id="../outside"),
                     lambda xs: xs[0].update(sourceFrames=500),
                     lambda xs: xs[0].update(oracleHz=991.014),
                     lambda xs: xs[0]["control"].update(lfPhaseRadians=1),
                     lambda xs: xs[0]["control"].update(cleanCaseId=xs[7]["id"]),
                     lambda xs: xs[0]["outputs"].pop()]
        for mutate in mutations:
            records = case_records()
            mutate(records)
            with self.assertRaises(ValueError):
                validate_cases(records)

    def test_recomposition_is_exact_but_independent_sine_is_tolerance_labeled(self):
        rate = 44100
        time = np.arange(rate // 2) / rate
        voiced = (0.05 * np.sin(2 * np.pi * 200 * time)).astype("<f4").astype(float)
        lf = (0.2 * np.sin(2 * np.pi * 10 * time)).astype("<f4").astype(float)
        source = (voiced + lf).astype("<f4").astype(float)
        verify_components(source, voiced, lf, rate, 10, 0.2)
        with self.assertRaisesRegex(ValueError, "recomposition"):
            verify_components(source * 0.999, voiced, lf, rate, 10, 0.2)
        with self.assertRaisesRegex(ValueError, "sine"):
            verify_components(source, voiced, lf, rate, 30, 0.2)
        with self.assertRaisesRegex(ValueError, "headroom"):
            verify_components(np.full(rate // 2, 0.7), voiced, lf, rate, 10, 0.2)

    def test_bounded_reads_check_actual_bytes_and_reject_links_before_opening(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "data"
            path.write_bytes(b"evidence")
            self.assertEqual(b"evidence", read_bounded(path, 8, hashlib.sha256(b"evidence").hexdigest()))
            for limit, digest in ((7, None), (8, "0" * 64)):
                with self.assertRaises(ValueError):
                    read_bounded(path, limit, digest)
            link = root / "link"
            link.symlink_to(path)
            with self.assertRaisesRegex(ValueError, "Nonregular"):
                read_bounded(link, 8)
            with self.assertRaisesRegex(ValueError, "Nonregular"):
                read_bounded(root, 8)

    def test_pairs_use_combined_source_rms_and_preserve_unavailable_clean_runs(self):
        clean = dict(envelope=np.array([1., 2.]), sourceRms=0.1,
                     outputs={name: dict(executionStatus="PASS", rmsRatio=1.) for name in OUTPUT_IDS})
        current = dict(envelope=np.array([2., 4.]), sourceRms=0.5,
                       outputs={name: dict(executionStatus="PASS", rmsRatio=0.2) for name in OUTPUT_IDS})
        result = paired_metrics(current, clean)
        self.assertEqual(1., result["envelopeRelativeL1"])
        self.assertEqual(2., result["envelopeSumRatio"])
        self.assertTrue(all(x["outputRmsRatioToClean"] == 1 for x in result["outputs"]))
        clean["outputs"]["world-reconstruction"] = dict(executionStatus="NOT_RUN")
        result = paired_metrics(current, clean)
        unavailable = next(x for x in result["outputs"] if x["id"] == "world-reconstruction")
        self.assertEqual("UNAVAILABLE_PAIR", unavailable["status"])
        self.assertNotIn("outputRmsRatioToClean", unavailable)

    def test_raw_nan_admission_indices_hashes_and_uv_overrides_are_checked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            item = dict(id="case", analysisRows=101, bins=1025, fftSize=2048,
                        analysisConstantHz=200., executionStatus="ERROR", parameters={}, outputs=[])
            for key, name in ARRAY_FILES.items():
                size = 101 if key in ("timeAxis", "analysisF0", "unvoicedSynthesisF0") else 101 * 1025
                values = np.ones(size, dtype="<f8")
                if key == "timeAxis":
                    values = np.arange(101) * 5. / 1000
                elif key == "analysisF0":
                    values.fill(200.)
                elif key == "unvoicedSynthesisF0":
                    values.fill(0.)
                elif key == "analysisAperiodicity":
                    values[3] = float("nan")
                payload = values.tobytes()
                (root / name).write_bytes(payload)
                item["parameters"][key] = dict(file=name, sha256=hashlib.sha256(payload).hexdigest(), elements=size)
            admission = dict(parameters=item["parameters"], rows=101, bins=1025,
                synthesisAdmission="REJECTED_INVALID_ANALYSIS", aperiodicityFiniteCells=101*1025-1,
                aperiodicityNonfiniteCells=1, nanCells=1, positiveInfinityCells=0, negativeInfinityCells=0,
                aperiodicityOutOfRangeFiniteCells=0, nanIndicesRowMajor=[3], positiveInfinityIndicesRowMajor=[],
                negativeInfinityIndicesRowMajor=[], outOfRangeFiniteIndicesRowMajor=[])

            def retain():
                payload = json.dumps(admission).encode()
                (root / "analysis.json").write_bytes(payload)
                item.update(retainedAnalysisFile="case/analysis.json", retainedAnalysisSha256=hashlib.sha256(payload).hexdigest())

            retain()
            envelope, rejected = verify_analysis(root, item)
            self.assertTrue(rejected)
            self.assertEqual(101 * 1025, envelope.size)
            admission["nanIndicesRowMajor"] = [4]
            retain()
            with self.assertRaisesRegex(ValueError, "index mask"):
                verify_analysis(root, item)
            admission["nanIndicesRowMajor"] = [3]
            retain()
            (root / "unvoiced-f0.f64le").write_bytes(np.ones(101, dtype="<f8").tobytes())
            with self.assertRaisesRegex(ValueError, "Digest"):
                verify_analysis(root, item)


if __name__ == "__main__":
    unittest.main()
