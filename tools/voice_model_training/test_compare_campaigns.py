import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.voice_model_training.compare_campaigns import compare, load_campaign


def digest(payload):
    return hashlib.sha256(payload).hexdigest()


def campaign():
    identity = dict(sourceId="one", sourceSha256="a" * 64, projectSha256="b" * 64)
    return dict(campaignSha256="c" * 64,
        selection=dict(selectionSha256="d" * 64, corpusSha256="e" * 64,
                       items=[identity], silencePhone="pau", binarySha256={"binary": "f" * 64}),
        rows=[dict(identity, execution="PASSED", metrics=dict(spectralDistance=1., meanAbsoluteCents=30.,
            measurableVoicedPairs=100, withinToleranceFrames=90, unmeasurableFrames=5, voicingMismatchFrames=2))])


class CampaignComparisonTests(unittest.TestCase):
    def test_fraction_gain_does_not_hide_coverage_regression(self):
        before, after = campaign(), campaign()
        after["rows"][0]["metrics"].update(measurableVoicedPairs=90, withinToleranceFrames=85,
                                         unmeasurableFrames=15, meanAbsoluteCents=40.)
        report = compare(before, after)
        self.assertGreater(report["candidateSummary"]["withinToleranceFraction"],
                           report["baselineSummary"]["withinToleranceFraction"])
        self.assertEqual(report["status"], "HAS_REGRESSIONS")
        self.assertIn("measurableVoicedPairs", report["rows"][0]["regressions"])
        self.assertIn("meanAbsoluteCents", report["rows"][0]["regressions"])
        self.assertFalse(report["singerQualified"])

    def test_missing_failed_and_unmeasurable_songs_do_not_disappear(self):
        before, after = campaign(), campaign()
        after["rows"][0].update(execution="FAILED", failure={"error": "render failed"})
        del after["rows"][0]["metrics"]
        report = compare(before, after)
        self.assertEqual(report["status"], "INCOMPLETE")
        self.assertIsNone(report["candidateSummary"])
        self.assertEqual(len(report["rows"]), 1)
        after = campaign()
        after["rows"][0]["metrics"].update(meanAbsoluteCents=None, measurableVoicedPairs=0)
        self.assertIsNone(compare(before, after)["candidateSummary"])
        after["rows"] = []
        with self.assertRaises(ValueError):
            compare(before, after)

    def test_selection_source_project_and_executable_changes_refused(self):
        for key in ("selectionSha256", "corpusSha256", "items", "silencePhone", "binarySha256"):
            after = campaign()
            after["selection"][key] = "different"
            with self.subTest(key=key), self.assertRaises(ValueError):
                compare(campaign(), after)
        after = campaign()
        after["rows"][0]["projectSha256"] = "f" * 64
        with self.assertRaises(ValueError):
            compare(campaign(), after)

    def test_no_regressions_is_not_qualification(self):
        after = campaign()
        after["rows"][0]["metrics"]["meanAbsoluteCents"] = 20.
        result = compare(campaign(), after)
        self.assertEqual(result["status"], "NO_REGRESSIONS_ON_REPORTED_METRICS")
        self.assertFalse(result["combinedModelHoldoutVerified"])
        self.assertFalse(result["releaseEligible"])

    def test_worker_digest_states_are_explicit(self):
        before, after = campaign(), campaign()
        before["inferenceWorkerSha256"] = "0" * 64
        after["inferenceWorkerSha256"] = "0" * 64
        report = compare(before, after)
        self.assertEqual(report["inferenceWorkerComparison"], "IDENTICAL")
        self.assertEqual(report["inferenceWorkerSha256"],
                         dict(baseline="0" * 64, candidate="0" * 64))
        after["inferenceWorkerSha256"] = "1" * 64
        with self.assertRaisesRegex(ValueError, "worker"):
            compare(before, after)
        del after["inferenceWorkerSha256"]
        report = compare(before, after)
        self.assertEqual(report["inferenceWorkerComparison"], "UNKNOWN")
        del before["inferenceWorkerSha256"]
        report = compare(before, after)
        self.assertEqual(report["inferenceWorkerComparison"], "UNKNOWN")


class CampaignCaptureTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.binary = self.root / "binary"
        self.binary.write_bytes(b"native extractor")
        self.directory = self.root / "song-000"
        self.directory.mkdir()
        (self.directory / "export").mkdir()
        (self.directory / "source.wav").write_bytes(b"source")
        (self.directory / "input.seam").write_bytes(b"project")
        identity = dict(sourceId="one", sourceSha256=digest(b"source"), projectSha256=digest(b"project"))
        self.metrics = campaign()["rows"][0]["metrics"]
        self.measured = dict(formatId="com.project-seam.application-audio-comparison", schemaVersion=1,
            referenceSha256=identity["sourceSha256"], candidateSha256=digest(b"master"),
            reference={}, candidate={}, channelPolicy="arithmetic-mean-no-gain-compensation",
            alignment="exact-source-frame-no-shift-no-warp", pitchInputEncoding="ieee-float32-le",
            spectralDistance=1., singerQualified=False, releaseEligible=False,
            pitch=dict(extractor=dict(sha256=digest(b"native extractor")),
                       comparison=dict(status="MISMATCH", **{k: v for k, v in self.metrics.items() if k != "spectralDistance"})))
        self.stored = copy.deepcopy(self.measured)
        self.selection = dict(items=[identity], selectionSha256="a" * 64, corpusSha256="b" * 64,
                              binarySha256={str(self.binary): digest(b"native extractor")})
        self.item = dict(identity, directory="song-000", execution="PASSED", pitchStatus="MISMATCH")

    def write(self):
        payload = json.dumps(self.stored).encode()
        (self.directory / "comparison.json").write_bytes(payload)
        self.item["comparisonSha256"] = digest(payload)
        payload = json.dumps(self.selection).encode()
        (self.root / "selection.json").write_bytes(payload)
        self.report = dict(formatId="com.project-seam.validation-campaign", schemaVersion=1,
            singerQualified=False, releaseEligible=False, selectionReceiptSha256=digest(payload),
            selectionSha256=self.selection["selectionSha256"], corpusSha256=self.selection["corpusSha256"],
            items=[self.item], selectedCount=1, executionPassed=self.item["execution"] == "PASSED")
        payload = json.dumps(self.report).encode()
        (self.root / "campaign.json").write_bytes(payload)
        return digest(payload)

    def load(self):
        sha = self.write()
        with patch("tools.voice_model_training.compare_campaigns.measure", return_value=self.measured) as measured:
            result = load_campaign(self.root / "campaign.json", sha, self.binary)
        return result, measured.call_count

    def test_remeasures_successful_audio_and_checks_metrics(self):
        result, calls = self.load()
        self.assertEqual(calls, 1)
        self.assertEqual(result["rows"][0]["metrics"], self.metrics)
        self.stored["pitch"]["comparison"]["meanAbsoluteCents"] = 0.
        with self.assertRaisesRegex(ValueError, "fresh measurement"):
            self.load()

    def test_changed_bytes_paths_and_selection_refused(self):
        sha = self.write()
        (self.directory / "source.wav").write_bytes(b"modified")
        with self.assertRaisesRegex(ValueError, "source/project"):
            load_campaign(self.root / "campaign.json", sha, self.binary)
        (self.directory / "source.wav").write_bytes(b"source")
        self.item["directory"] = "../escape"
        with self.assertRaises(ValueError):
            self.load()
        self.item["directory"] = "song-000"
        self.selection["items"] = []
        with self.assertRaises(ValueError):
            self.load()

    def test_malformed_worker_digest_refused(self):
        result, _ = self.load()
        self.assertIsNone(result["inferenceWorkerSha256"])
        self.report["inferenceWorkerSha256"] = "not-a-digest"
        payload = json.dumps(self.report).encode()
        (self.root / "campaign.json").write_bytes(payload)
        with self.assertRaisesRegex(ValueError, "worker digest"):
            load_campaign(self.root / "campaign.json", digest(payload), self.binary)
        self.report["inferenceWorkerSha256"] = "9" * 64
        payload = json.dumps(self.report).encode()
        (self.root / "campaign.json").write_bytes(payload)
        with patch("tools.voice_model_training.compare_campaigns.measure", return_value=self.measured):
            result = load_campaign(self.root / "campaign.json", digest(payload), self.binary)
        self.assertEqual(result["inferenceWorkerSha256"], "9" * 64)

    def test_failed_execution_retained_without_measurement(self):
        self.item.update(execution="FAILED", error="failed render", errorType="RuntimeError")
        result, calls = self.load()
        self.assertEqual(calls, 0)
        self.assertEqual(result["rows"][0]["failure"]["error"], "failed render")

    def test_extractor_drift_refused(self):
        sha = self.write()
        self.binary.write_bytes(b"different executable")
        with self.assertRaisesRegex(ValueError, "extractor differs"):
            load_campaign(self.root / "campaign.json", sha, self.binary)


if __name__ == "__main__":
    unittest.main()
