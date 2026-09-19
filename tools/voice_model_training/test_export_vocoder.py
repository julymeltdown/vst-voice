import copy
import hashlib
import json
import unittest
import subprocess
import sys

from tools.voice_model_training.export_vocoder import export_identity, TRAINING_REVISION
from tools.voice_model_training.check_vocoder_model import summarize_pitch_conditioning, vocoder_configuration


class VocoderPitchDiagnosticTests(unittest.TestCase):
    def test_probe_rejects_unbounded_lengths_before_loading_upstream(self):
        for value in ("0", "15", "4097", "1.5"):
            result = subprocess.run([sys.executable, "-m",
                "tools.voice_model_training.check_vocoder_model", "/missing-training", "/missing-deployment",
                "--fixture-frames", value], capture_output=True, text=True, timeout=10)
            with self.subTest(value=value):
                self.assertEqual(result.returncode, 2)
                self.assertIn("--fixture-frames", result.stderr)
                self.assertNotIn("Traceback", result.stderr)

    def cases(self):
        return [dict(requestedHz=hz, measuredHz=hz, voicedCoverage=1.,
                     measurementReason=None, finite=True) for hz in (110., 220., 440., 880.)]

    def test_all_requested_notes_must_be_accurate(self):
        cases = self.cases()
        self.assertTrue(summarize_pitch_conditioning(cases)["pitchFollowsRequestedNote"])
        for case in cases:
            case["measuredHz"] *= 2
        report = summarize_pitch_conditioning(cases)
        self.assertTrue(report["pitchChangesWithRequestedNote"])
        self.assertFalse(report["pitchFollowsRequestedNote"])
        self.assertEqual([c["absoluteErrorCents"] for c in report["conditioning"]], [1200.] * 4)

    def test_epoch_six_synthetic_diagnostic_is_not_accurate_following(self):
        cases = self.cases()
        for case, hz in zip(cases, (107.143, 214.293, 461.405, 857.481)):
            case["measuredHz"] = hz
        result = summarize_pitch_conditioning(cases)
        self.assertTrue(result["pitchChangesWithRequestedNote"])
        self.assertFalse(result["pitchFollowsRequestedNote"])

    def test_missing_low_coverage_nonfinite_and_unresolved_cannot_pass(self):
        for change in (dict(measuredHz=None), dict(voicedCoverage=.79), dict(finite=False),
                       dict(measurementReason="unresolved"), dict(measuredHz=187.5)):
            cases = self.cases()
            cases[0].update(change)
            with self.subTest(change=change):
                self.assertFalse(summarize_pitch_conditioning(cases)["pitchFollowsRequestedNote"])
        for change in (dict(measuredHz=float("nan")), dict(measuredHz=0),
                       dict(voicedCoverage=float("nan")), dict(voicedCoverage=2)):
            cases = self.cases()
            cases[0].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                summarize_pitch_conditioning(cases)
        with self.assertRaises(ValueError): summarize_pitch_conditioning(self.cases()[:2])


class VocoderExportIdentityTests(unittest.TestCase):
    def test_identity_and_semantic_mismatches(self):
        config = dict(sampling_rate=48000, num_mels=80, hop_size=256, n_fft=1024,
            win_size=1024, fmin=20, fmax=24000, mini_nsf=True, noise_sigma=0.,
            upsample_rates=[8, 8, 2, 2], upsample_kernel_sizes=[16, 16, 4, 4],
            upsample_initial_channel=32, resblock_kernel_sizes=[3],
            resblock_dilation_sizes=[[1, 3, 5]], resblock="1", pc_aug=False)
        profile = dict(profileId="seam-full-hop-slaney-v1", sampleRate=48000,
            fftSize=1024, windowSize=1024, hopSize=256, bins=80, minimumHz=20, maximumHz=24000,
            tailPadding="zero-to-whole-hop", boundaryPadding="reflect-fft-minus-hop",
            window="periodic-hann", spectrum="unnormalized-magnitude", melNormalization="slaney-area",
            melFrequencyScale="slaney", amplitudeScale="ln-amplitude", floor=1e-5,
            layout="TF", dtype="float32-le")
        digest = hashlib.sha256(json.dumps(profile, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        identity = dict(profileSha256=digest, datasetSha256="a" * 64,
                        objectiveId="nsf-lsgan-logmel-48k80-v1")
        state = dict(metadata=dict(identity, run=dict(trainingRevision=TRAINING_REVISION,
                                                     configuration=config)),
                     epoch=dict(identity, formatId="com.project-seam.vocoder-epoch-result"))
        receipt = dict(formatId="com.project-seam.gan-checkpoint")
        self.assertEqual(export_identity(state, receipt, profile), config)
        large = copy.deepcopy(state)
        large_config = vocoder_configuration("mini-nsf-512-mrf-v1")
        large["metadata"]["run"]["configuration"] = large_config
        self.assertEqual(export_identity(large, receipt, profile), large_config)
        large["metadata"]["run"]["configuration"]["upsample_initial_channel"] = 256
        with self.assertRaises(ValueError): export_identity(large, receipt, profile)
        for field, value in (("profileSha256", "0" * 64), ("datasetSha256", "b" * 64),
                             ("objectiveId", "unsupported")):
            broken = copy.deepcopy(state)
            broken["epoch"][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                export_identity(broken, receipt, profile)
        for field, value in (("amplitudeScale", "log10-amplitude"), ("hopSize", 512)):
            with self.subTest(field=field), self.assertRaises(ValueError):
                export_identity(state, receipt, dict(profile, **{field: value}))
        broken = copy.deepcopy(state)
        broken["metadata"]["run"]["configuration"]["mini_nsf"] = False
        with self.assertRaises(ValueError):
            export_identity(broken, receipt, profile)
        with self.assertRaises(ValueError):
            export_identity(state, dict(formatId="com.project-seam.training-checkpoint"), profile)


if __name__ == "__main__":
    unittest.main()
