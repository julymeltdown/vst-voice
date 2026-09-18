"""Unit tests for vocoder reconstruction evaluation, invariants, and retained receipts.

Section 5 (U3.3) of the joint development plan:
- Exact hop/padding/trim lengths asserted;
- Sample-rate and profile mismatch refused;
- Named reconstruction measurement over held-out items with retained numbers;
- Label origin recorded, releaseEligible strictly False;
- Checkpoint resumption and cancellation tested.
"""
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import numpy as np
from scipy.io import wavfile

from tools.voice_model_training.vocoder_reconstruction import (
    compute_stft_spectral_distance,
    compute_f0_reconstruction_error,
    measure_vocoder_reconstruction,
    evaluate_held_out_reconstruction,
    EXPECTED_SAMPLE_RATE,
    EXPECTED_HOP_SIZE,
    EXPECTED_PROFILE_ID,
)
from tools.voice_model_training.vocoder_training_run import train_reviewed_vocoder_epoch


def _generate_sine(freq_hz: float, duration_sec: float = 0.5, sample_rate: int = 48000, hop_size: int = 256) -> np.ndarray:
    total_samples = int(math.ceil(duration_sec * sample_rate / hop_size)) * hop_size
    t = np.arange(total_samples, dtype=np.float64) / sample_rate
    return np.sin(2.0 * np.pi * freq_hz * t).astype(np.float32) * 0.5


class VocoderReconstructionTests(unittest.TestCase):
    def setUp(self):
        self.profile = {
            "profileId": EXPECTED_PROFILE_ID,
            "sampleRate": EXPECTED_SAMPLE_RATE,
            "hopSize": EXPECTED_HOP_SIZE,
            "tailPadding": "zero-to-whole-hop",
        }

    @unittest.skipUnless(importlib.util.find_spec("torch"), "Optional Torch environment required")
    def test_torch_inference_restores_modes_rng_and_retains_real_output(self):
        import torch

        class Generator(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.scale = torch.nn.Parameter(torch.tensor(.1))
                self.child = torch.nn.Dropout(.5)

            def forward(self, mel, f0):
                self.observed = (self.training, self.child.training, torch.is_grad_enabled())
                return self.scale * torch.randn(1, 1, mel.shape[2] * 256)

        model = Generator().train()
        model.child.eval()
        model.scale.grad = torch.tensor(7.)
        rng = torch.get_rng_state().clone()
        item = dict(sourceId="held", pcm=torch.zeros(1, 1, 1024),
                    mel=torch.zeros(1, 80, 4), f0=torch.zeros(1, 4), validSamples=1000)
        with tempfile.TemporaryDirectory() as root:
            receipt = evaluate_held_out_reconstruction(model, [item], dataset_sha256="d" * 64,
                profile_sha256="b" * 64, output_directory=Path(root), profile=self.profile)
            self.assertEqual(model.observed, (False, False, False))
            self.assertTrue(model.training)
            self.assertFalse(model.child.training)
            self.assertTrue(torch.equal(rng, torch.get_rng_state()))
            self.assertEqual(float(model.scale.grad), 7.)
            self.assertEqual(receipt["items"][0]["validSamples"], 1000)
            self.assertEqual(receipt["items"][0]["paddedSamples"], 24)
            result = receipt["items"][0]
            path = Path(root) / result["audioPath"]
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), result["outputAudioSha256"])
            rate, rendered = wavfile.read(path)
            self.assertEqual((rate, rendered.dtype, rendered.shape), (48000, np.dtype("float32"), (1000,)))
            self.assertEqual(hashlib.sha256((Path(root) / result["measurementPath"]).read_bytes()).hexdigest(),
                             result["measurementSha256"])
            with self.assertRaisesRegex(ValueError, "already exists"):
                evaluate_held_out_reconstruction(model, [item], dataset_sha256="d" * 64,
                    profile_sha256="b" * 64, output_directory=Path(root), profile=self.profile)

    @unittest.skipUnless(importlib.util.find_spec("torch"), "Optional Torch environment required")
    def test_torch_failure_restores_state_and_rejects_bad_output_geometry(self):
        import torch

        class Generator(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.child = torch.nn.Dropout(.5)
                self.failure = True

            def forward(self, mel, f0):
                torch.rand(1)
                if self.failure:
                    raise RuntimeError("deliberate forward failure")
                return torch.zeros(1, 1, 768)

        model = Generator().train()
        model.child.eval()
        rng = torch.get_rng_state().clone()
        item = dict(sourceId="held", pcm=torch.zeros(1024), mel=torch.zeros(1, 80, 4), f0=torch.zeros(1, 4))
        for failure, error in ((True, RuntimeError), (False, ValueError)):
            model.failure = failure
            with self.subTest(failure=failure), self.assertRaises(error):
                evaluate_held_out_reconstruction(model, [item], dataset_sha256="a" * 64, profile_sha256="b" * 64)
            self.assertTrue(model.training)
            self.assertFalse(model.child.training)
            self.assertTrue(torch.equal(rng, torch.get_rng_state()))
        for bad in (torch.zeros(1, 80, 4, device="meta"), torch.zeros(1, 80, 4, dtype=torch.float64)):
            with self.assertRaisesRegex(ValueError, "CPU float32"):
                evaluate_held_out_reconstruction(model, [dict(item, mel=bad)],
                    dataset_sha256="a" * 64, profile_sha256="b" * 64)

    def test_source_padding_and_channels_are_not_silently_discarded(self):
        for value in (np.zeros((2, 1024)), np.zeros((1, 2, 512))):
            with self.assertRaisesRegex(ValueError, "mono"):
                measure_vocoder_reconstruction(rendered_audio=value, source_audio=np.zeros(1024))
        source = np.zeros(1024)
        source[1000] = .1
        with self.assertRaisesRegex(ValueError, "padding must be zero"):
            measure_vocoder_reconstruction(rendered_audio=np.zeros(1024), source_audio=source, valid_samples=1000)
        rendered = np.zeros(1024)
        rendered[-1] = np.nan
        with self.assertRaisesRegex(ValueError, "Nonfinite"):
            measure_vocoder_reconstruction(rendered_audio=rendered, source_audio=np.zeros(1000))

    def test_missing_or_extra_whole_hop_is_rejected(self):
        source = _generate_sine(220., duration_sec=.1)
        for rendered in (source[:-256], np.pad(source, (0, 256))):
            with self.subTest(samples=len(rendered)), self.assertRaisesRegex(ValueError, "length"):
                measure_vocoder_reconstruction(rendered_audio=rendered, source_audio=source)

    def test_unmeasurable_pitch_is_unresolved_not_satisfied(self):
        result = measure_vocoder_reconstruction(rendered_audio=np.zeros(1024),
                                                source_audio=np.zeros(1024))
        self.assertIsNone(result["f0MedianErrorCents"])
        self.assertFalse(result["reconstructionSatisfied"])
        self.assertEqual(result["pitchStatus"], "UNRESOLVED")
        result = measure_vocoder_reconstruction(rendered_audio=_generate_sine(220., duration_sec=.1),
            source_audio=np.zeros(4864), target_hz=220.)
        self.assertEqual(result["pitchStatus"], "UNRESOLVED")
        self.assertFalse(result["reconstructionSatisfied"])

    def test_identical_audio_has_zero_spectral_distance_and_zero_pitch_error(self):
        audio = _generate_sine(220.0, duration_sec=0.5)
        result = measure_vocoder_reconstruction(
            rendered_audio=audio,
            source_audio=audio,
            sample_rate=EXPECTED_SAMPLE_RATE,
            hop_size=EXPECTED_HOP_SIZE,
            profile=self.profile,
            target_hz=220.0,
        )
        self.assertEqual(result["formatId"], "com.project-seam.vocoder-reconstruction-measurement")
        self.assertAlmostEqual(result["spectralDistance"], 0.0, places=4)
        self.assertIsNotNone(result["f0MedianErrorCents"])
        self.assertAlmostEqual(result["f0MedianErrorCents"], 0.0, delta=1.0)
        self.assertEqual(result["validSamples"], len(audio))
        self.assertEqual(result["paddedSamples"], 0)
        self.assertTrue(result["reconstructionSatisfied"])

    def test_pitch_shifted_audio_fails_f0_error(self):
        source = _generate_sine(220.0, duration_sec=0.5)
        # Shift pitch by a semitone (approx 233.08 Hz, ~100 cents)
        rendered = _generate_sine(220.0 * (2.0 ** (1.0 / 12.0)), duration_sec=0.5)
        result = measure_vocoder_reconstruction(
            rendered_audio=rendered,
            source_audio=source,
            sample_rate=EXPECTED_SAMPLE_RATE,
            hop_size=EXPECTED_HOP_SIZE,
            profile=self.profile,
            target_hz=220.0,
            max_acceptable_pitch_error_cents=50.0,
        )
        self.assertIsNotNone(result["f0MedianErrorCents"])
        self.assertGreater(abs(result["f0MedianErrorCents"]), 80.0)
        self.assertFalse(result["reconstructionSatisfied"])

    def test_exact_hop_framing_and_trim_lengths_asserted(self):
        valid_len = 10 * EXPECTED_HOP_SIZE
        source = np.ones(valid_len, dtype=np.float32) * 0.1

        # Non-multiple of hop size is rejected
        bad_rendered = np.ones(valid_len + 7, dtype=np.float32) * 0.1
        with self.assertRaisesRegex(ValueError, "not a multiple of hop size"):
            measure_vocoder_reconstruction(
                rendered_audio=bad_rendered,
                source_audio=source,
                sample_rate=EXPECTED_SAMPLE_RATE,
                hop_size=EXPECTED_HOP_SIZE,
                profile=self.profile,
            )

        # Audio too short (< 1 hop) is rejected
        short_source = np.ones(128, dtype=np.float32)
        rendered_one_hop = np.ones(EXPECTED_HOP_SIZE, dtype=np.float32)
        with self.assertRaisesRegex(ValueError, "Audio payload too short"):
            measure_vocoder_reconstruction(
                rendered_audio=rendered_one_hop,
                source_audio=short_source,
                sample_rate=EXPECTED_SAMPLE_RATE,
                hop_size=EXPECTED_HOP_SIZE,
                profile=self.profile,
            )

        # Only the final partial hop is padding, never an extra whole hop.
        partial_source = source[:-37]
        padded_rendered = np.zeros(valid_len, dtype=np.float32)
        padded_rendered[:len(partial_source)] = partial_source
        res = measure_vocoder_reconstruction(
            rendered_audio=padded_rendered,
            source_audio=partial_source,
            sample_rate=EXPECTED_SAMPLE_RATE,
            hop_size=EXPECTED_HOP_SIZE,
            profile=self.profile,
        )
        self.assertEqual(res["validSamples"], valid_len - 37)
        self.assertEqual(res["paddedSamples"], 37)
        self.assertEqual(res["totalFrames"], 10)

    def test_sample_rate_and_profile_mismatch_refused(self):
        audio = _generate_sine(220.0, duration_sec=0.5)
        # 44100 Hz refused
        with self.assertRaisesRegex(ValueError, "Sample rate mismatch"):
            measure_vocoder_reconstruction(
                rendered_audio=audio,
                source_audio=audio,
                sample_rate=44100,
                hop_size=EXPECTED_HOP_SIZE,
                profile=self.profile,
            )

        # Wrong profileId refused
        bad_profile = dict(self.profile, profileId="openvpi-hifigan-44k")
        with self.assertRaisesRegex(ValueError, "Profile mismatch"):
            measure_vocoder_reconstruction(
                rendered_audio=audio,
                source_audio=audio,
                sample_rate=EXPECTED_SAMPLE_RATE,
                hop_size=EXPECTED_HOP_SIZE,
                profile=bad_profile,
            )

        # Wrong tail padding refused
        bad_pad_profile = dict(self.profile, tailPadding="reflect")
        with self.assertRaisesRegex(ValueError, "zero-to-whole-hop"):
            measure_vocoder_reconstruction(
                rendered_audio=audio,
                source_audio=audio,
                sample_rate=EXPECTED_SAMPLE_RATE,
                hop_size=EXPECTED_HOP_SIZE,
                profile=bad_pad_profile,
            )

    def test_nonfinite_audio_refused(self):
        audio = _generate_sine(220.0, duration_sec=0.5)
        audio[10] = float("nan")
        with self.assertRaisesRegex(ValueError, "Nonfinite samples"):
            measure_vocoder_reconstruction(
                rendered_audio=audio,
                source_audio=_generate_sine(220.0, duration_sec=0.5),
                sample_rate=EXPECTED_SAMPLE_RATE,
                hop_size=EXPECTED_HOP_SIZE,
                profile=self.profile,
            )

    def test_evaluate_held_out_reconstruction_and_retained_receipt(self):
        audio_a = _generate_sine(220.0, duration_sec=0.5)
        audio_b = _generate_sine(440.0, duration_sec=0.5)
        held_out = [
            {"sourceId": "held_01", "pcm": audio_a, "mel": None, "f0": None, "frequencyHz": 220.0},
            {"sourceId": "held_02", "pcm": audio_b, "mel": None, "f0": None, "frequencyHz": 440.0},
        ]
        # Generator mock that perfectly reconstructs
        mock_gen = lambda mel, f0: audio_a if f0 is None and mel is None else audio_a
        def generator_fn(mel, f0):
            # Return identical pcm matching each item by length or test identity
            return audio_a

        with tempfile.TemporaryDirectory() as temp_dir:
            receipt = evaluate_held_out_reconstruction(
                generator_fn=lambda mel, f0: audio_a,
                items=[held_out[0]],
                dataset_sha256="d" * 64,
                profile_sha256="p" * 64,
                output_directory=Path(temp_dir),
                label_origin="com.project-seam.training-generated-teacher",
                sample_rate=EXPECTED_SAMPLE_RATE,
                hop_size=EXPECTED_HOP_SIZE,
                profile=self.profile,
            )
            self.assertEqual(receipt["formatId"], "com.project-seam.vocoder-reconstruction-receipt")
            self.assertEqual(receipt["labelOrigin"], "com.project-seam.training-generated-teacher")
            self.assertFalse(receipt["releaseEligible"])
            self.assertFalse(receipt["trainingAdmitted"])
            self.assertEqual(receipt["verdict"], "RECONSTRUCTION_MEASURED")
            self.assertEqual(receipt["summary"]["itemCount"], 1)
            self.assertAlmostEqual(receipt["summary"]["meanSpectralDistance"], 0.0, places=4)

            # Receipt is written to disk
            receipt_path = Path(temp_dir) / "reconstruction_receipt.json"
            self.assertTrue(receipt_path.exists())
            disk_receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
            self.assertEqual(disk_receipt["datasetSha256"], "d" * 64)
            self.assertFalse(disk_receipt["releaseEligible"])

    def test_epoch_training_records_label_origin_and_held_out_reconstruction(self):
        snapshot = dict(
            schemaVersion=3, preparationIssues=[], sourcePermissionsAdmitted=True,
            labelsAdmitted=True, expiresAt=200, datasetSha256="a" * 64,
            sources=[dict(sourceId="s", frameCount=2000),
                     dict(sourceId="held", frameCount=2000, sourceSha256="c" * 64, audioSha256="e" * 64)],
            conditioning=[dict(sourceId="s", frameCount=8), dict(sourceId="held", frameCount=8)],
            bindings=dict(split=dict(groups=[dict(partition="train", sourceIds=["s"]),
                                             dict(partition="test", sourceIds=["held"])]))
        )
        inputs = dict.fromkeys((
            "permission_config", "permission_hash", "label_config", "label_hash", "root",
            "rights_review", "rights_policy", "rights_anchor", "label_review", "label_policy",
            "label_anchor", "seed", "held_out_songs"
        ))
        batch = dict(sourceId="s", partition="train", datasetSha256="a" * 64, profileSha256="b" * 64,
                     frameOffset=0, mel=SimpleNamespace(shape=(1, 80, 8)), f0=None, pcm=None, hopSize=256, validSamples=2000)
        audio = _generate_sine(220.0, duration_sec=2000 / 48000)
        padded = audio.copy()
        padded[2000:] = 0
        held = dict(batch, sourceId="held", partition="test", pcm=padded,
                    sourceSha256="c" * 64, audioSha256="e" * 64, targetSha256="f" * 64,
                    phraseAnalysisFrames=8)
        prefix = "tools.voice_model_training.vocoder_training_run."
        with tempfile.TemporaryDirectory() as root, patch(prefix + "time.time", return_value=100), \
                patch(prefix + "assemble_dataset", return_value=snapshot), \
                patch(prefix + "iter_vocoder_batches", side_effect=lambda *a, **k:
                      iter([batch] if k["partition"] == "train" else [held])), \
                patch(prefix + "vocoder_gan_step", return_value=dict(generatorLoss=1.5, discriminatorLoss=2.5)), \
                patch(prefix + "publish_vocoder_checkpoint") as publish:
            options = dict(
                dataset_inputs=inputs, conditioning_directory=Path(root),
                targets={"held": (dict(profile=self.profile), None)}, pcm_sources={},
                expected_profile_sha256="b" * 64, output=Path(root) / "out", run_metadata={},
                reconstruction_loss=lambda a, b: None, objective_id="fixture", maximum_updates=1,
                held_out_items=["held"], reconstruction_directory=Path(root),
                label_origin="renderer-intent-not-acoustic-truth",
            )
            def finish(*args, **kwargs):
                kwargs["before_publish"]()
                return kwargs["epoch"]
            publish.side_effect = finish
            mock_gen = lambda m, f: audio
            result = train_reviewed_vocoder_epoch(mock_gen, [], None, None, **options)
            self.assertEqual(result["labelOrigin"], "renderer-intent-not-acoustic-truth")
            self.assertFalse(result["releaseEligible"])
            self.assertIn("reconstructionSummary", result)
            self.assertEqual(result["reconstructionSummary"]["itemCount"], 1)
            self.assertEqual(result["reconstruction"]["items"][0]["sourceSha256"], "c" * 64)

            # Cooperative cancellation test
            with self.assertRaisesRegex(RuntimeError, "cancelled"):
                train_reviewed_vocoder_epoch(mock_gen, [], None, None, **options, cancelled=lambda: True)


if __name__ == "__main__":
    unittest.main()
