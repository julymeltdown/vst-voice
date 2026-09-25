import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

from tools.neural_runtime.check_exported_worker_replay import (
    FRAME, parse_response, validate_exported_wavs,
    validate_render_binding,
)


class ExportedWorkerReplayTests(unittest.TestCase):
    def response(self):
        request_hash = hashlib.sha256(b"request").hexdigest()
        digest = "a" * 64
        metadata = dict(kind="seam-neural-response-v3", frameCount=2,
                        sampleRate=48000, channels=1, modelContentHash=digest,
                        bundleContentHash=digest, requestContentHash=request_hash,
                        requestId=731,
                        backendId="seam-neural-worker-acoustic-vocoder-1/steps-10")
        header = json.dumps(metadata, sort_keys=True, separators=(",", ":")).encode()
        audio = struct.pack("<2f", .125, -.25)
        frame = FRAME.pack(b"SNW1", 1, 2, 0, len(header), len(audio)) + header + audio
        return frame, request_hash, digest

    def test_parses_identity_bound_finite_pcm(self):
        frame, request_hash, digest = self.response()
        metadata, pcm_bytes, pcm = parse_response(frame, expected_frames=2,
            expected_digest=digest, expected_request_hash=request_hash,
            expected_request_id=731,
            expected_backend_id="seam-neural-worker-acoustic-vocoder-1/steps-10")
        self.assertEqual(metadata["kind"], "seam-neural-response-v3")
        self.assertEqual(pcm_bytes, struct.pack("<2f", .125, -.25))
        self.assertEqual(pcm, (.125, -.25))

    def test_rejects_wrong_request_binding_and_invalid_pcm(self):
        frame, _request_hash, digest = self.response()
        with self.assertRaisesRegex(ValueError, "identity"):
            parse_response(frame, expected_frames=2, expected_digest=digest,
                           expected_request_hash="b" * 64, expected_request_id=731,
                           expected_backend_id="seam-neural-worker-acoustic-vocoder-1/steps-10")
        frame, request_hash, digest = self.response()
        for changes in ({"requestId": 732}, {"backendId": "other/steps-10"},
                        {"requestId": True}, {"backendId": None}):
            metadata = dict(kind="seam-neural-response-v3", frameCount=2,
                            sampleRate=48000, channels=1, modelContentHash=digest,
                            bundleContentHash=digest, requestContentHash=request_hash,
                            requestId=731,
                            backendId="seam-neural-worker-acoustic-vocoder-1/steps-10")
            metadata.update(changes)
            header = json.dumps(metadata, sort_keys=True, separators=(",", ":")).encode()
            audio = struct.pack("<2f", .125, -.25)
            wrong_identity = FRAME.pack(b"SNW1", 1, 2, 0, len(header), len(audio)) + header + audio
            with self.assertRaisesRegex(ValueError, "identity"):
                parse_response(wrong_identity, expected_frames=2,
                    expected_digest=digest, expected_request_hash=request_hash,
                    expected_request_id=731,
                    expected_backend_id="seam-neural-worker-acoustic-vocoder-1/steps-10")
        metadata = dict(kind="seam-neural-response-v3", frameCount=2,
                        sampleRate=48000, channels=1, modelContentHash=digest,
                        bundleContentHash=digest, requestContentHash="c" * 64,
                        requestId=731,
                        backendId="seam-neural-worker-acoustic-vocoder-1/steps-10")
        header = json.dumps(metadata, sort_keys=True, separators=(",", ":")).encode()
        audio = struct.pack("<2f", float("nan"), 0.0)
        invalid = FRAME.pack(b"SNW1", 1, 2, 0, len(header), len(audio)) + header + audio
        with self.assertRaisesRegex(ValueError, "invalid PCM"):
            parse_response(invalid, expected_frames=2, expected_digest=digest,
                           expected_request_hash="c" * 64, expected_request_id=731,
                           expected_backend_id="seam-neural-worker-acoustic-vocoder-1/steps-10")

    def test_render_inputs_bind_worker_bundle_project_and_steps(self):
        inputs = dict(workerSha256="a" * 64, manifestSha256="b" * 64,
                      projectSha256="c" * 64, steps=10)
        validate_render_binding(inputs, expected_worker_hash="a" * 64,
            expected_manifest_hash="b" * 64, expected_project_hash="c" * 64,
            expected_steps=10)
        for key, value in (("workerSha256", "d" * 64), ("manifestSha256", "d" * 64),
                           ("projectSha256", "d" * 64), ("steps", 20)):
            altered = dict(inputs, **{key: value})
            with self.assertRaisesRegex(ValueError, "not bound"):
                validate_render_binding(altered, expected_worker_hash="a" * 64,
                    expected_manifest_hash="b" * 64, expected_project_hash="c" * 64,
                    expected_steps=10)

    @staticmethod
    def wav_bytes(*, encoding=1, channels=1, sample_rate=48000, bits=24, samples=b"\x01\0\0"):
        sample_bytes = bits // 8
        align = channels * sample_bytes
        data = samples
        fmt = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(data), b"WAVE",
                          b"fmt ", 16, encoding, channels, sample_rate,
                          sample_rate * align, align, bits, b"data", len(data))
        return fmt + data

    def test_validates_actual_exported_pcm_wav_content(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            (output / "master.wav").write_bytes(self.wav_bytes(samples=b"\x01\0\0"))
            float_samples = struct.pack("<2f", 0.25, -0.125)
            (output / "candidate.wav").write_bytes(self.wav_bytes(
                encoding=3, bits=32, samples=float_samples))
            report = validate_exported_wavs(output)
            self.assertEqual(report, {"fileCount": 2, "totalFrames": 3,
                                      "nonSilentFileCount": 2})

    def test_rejects_silent_wrong_clock_and_truncated_export(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            path = output / "master.wav"
            path.write_bytes(self.wav_bytes(samples=b"\0\0\0"))
            with self.assertRaisesRegex(ValueError, "only silent"):
                validate_exported_wavs(output)
            path.write_bytes(self.wav_bytes(sample_rate=44100))
            with self.assertRaisesRegex(ValueError, "invalid format"):
                validate_exported_wavs(output)
            path.write_bytes(self.wav_bytes(samples=b"\x01\0\0")[:-1])
            with self.assertRaisesRegex(ValueError, "invalid format"):
                validate_exported_wavs(output)

    def test_rejects_nonfinite_or_out_of_range_float_wav(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            path = output / "candidate.wav"
            for value in (float("nan"), 1.25):
                path.write_bytes(self.wav_bytes(encoding=3, bits=32,
                    samples=struct.pack("<f", value)))
                with self.assertRaisesRegex(ValueError, "invalid float PCM"):
                    validate_exported_wavs(output)


if __name__ == "__main__":
    unittest.main()
