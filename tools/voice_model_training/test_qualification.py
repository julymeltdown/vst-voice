import hashlib
import json
import math
import struct
import tempfile
import unittest
from pathlib import Path

from tools.voice_model_training.qualification import (
    aggregate, build_request, decode_response, evaluate_item, qualify_command,
    validate_configuration)


def vocabulary_file(tokens=("SP", "a", "i")):
    return json.dumps({"formatId": "com.project-seam.neural-vocabulary", "schemaVersion": 1,
                       "tokens": list(tokens)}, separators=(",", ":")).encode()


def item(frames=48000, phones=("a", "i"), **changes):
    record = {"itemId": "held-1", "songId": "song-9", "phones": list(phones),
              "frameCount": frames, "frequencyHz": 210.0, "gain": 0.5, "requestId": 1}
    record.update(changes)
    return record


def configuration(directory, manifest_sha256, items=None):
    return {"formatId": "com.project-seam.candidate-qualification", "schemaVersion": 1,
            "bundle": {"directory": str(directory), "modelId": "fixture", "modelVersion": "1",
                       "manifestSha256": manifest_sha256, "maximumBundleBytes": 1048576},
            "heldOut": items if items is not None else
            [{"itemId": "held-1", "songId": "song-9", "phones": ["a", "i"],
              "frameCount": 48000, "frequencyHz": 210.0, "gain": 0.5}],
            "repetitions": 2}


def sine(frames, frequency_hz, amplitude=0.25, sample_rate=48000):
    """A sung-note stand-in: a steady tone at the requested frequency.

    The earlier fixtures emitted a constant value, which has no pitch at all. That was enough for
    every criterion except pitch-adherence, and it is exactly the gap this criterion exists to close:
    constant audio is finite, non-silent, deterministic and fast.
    """
    return struct.pack("<" + str(frames) + "f",
                       *[amplitude * math.sin(2.0 * math.pi * frequency_hz * index / sample_rate)
                         for index in range(frames)])


def response(request, prepared, value=0.25, frequency_hz=None):
    count = prepared["frameCount"]
    if frequency_hz is None:
        pcm = struct.pack("<" + str(count) + "f", *([value] * count))
    else:
        pcm = sine(count, frequency_hz, value)
    reply = {"kind": "seam-neural-response-v3", "requestId": prepared["requestId"],
             "requestContentHash": hashlib.sha256(request).hexdigest(),
             "frameCount": count, "sampleRate": 48000, "channels": 1,
             "modelContentHash": "a" * 64, "bundleContentHash": "a" * 64,
             "backendId": "seam-neural-worker-fixture"}
    header = json.dumps(reply, sort_keys=True, separators=(",", ":")).encode()
    return struct.pack("<4sHBBIQ", b"SNW1", 1, 2, 0, len(header), len(pcm)) + header + pcm


CONSTANT_AUDIO = -1.0   # loud, steady, and unpitched: the shape a broken candidate emits

def runs_for(prepared, vocabulary, values=(0.25, 0.25), codes=None, frames=None,
             frequency_hz=None):
    request = build_request(prepared, vocabulary, "b" * 64,
                            {"modelId": "fixture", "modelVersion": "1", "manifestSha256": "a" * 64})
    # Default to singing the requested note, so a case that is not about pitch is not silently a pitch
    # failure. A case that tests pitch passes its own frequency.
    if frequency_hz == CONSTANT_AUDIO:
        sung = None
    else:
        sung = prepared["frequencyHz"] if frequency_hz is None else frequency_hz
    runs = []
    for index, value in enumerate(values):
        count = frames[index] if frames is not None else prepared["frameCount"]
        payload = response(request, dict(prepared, frameCount=count), value,
                           frequency_hz=sung)
        runs.append({"returncode": 0 if codes is None else codes[index], "stdout": payload,
                     "stderr": b"", "request": request, "milliseconds": 12.5 + index})
    return runs


class EvaluationTests(unittest.TestCase):
    def test_identical_runs_pass_every_automatic_criterion(self):
        prepared = item()
        vocabulary = {"SP": 0, "a": 1, "i": 2}
        record = evaluate_item(prepared, runs_for(prepared, vocabulary), vocabulary, 1000)
        self.assertEqual(record["status"], "PASS")
        self.assertEqual(record["milliseconds"], [12.5, 13.5])
        self.assertEqual(len(record["audioSha256"]), 64)
        self.assertEqual(record["backendId"], "seam-neural-worker-fixture")

    def test_repeated_requests_may_not_differ(self):
        prepared = item()
        vocabulary = {"SP": 0, "a": 1, "i": 2}
        record = evaluate_item(prepared, runs_for(prepared, vocabulary, values=(0.25, 0.5)),
                               vocabulary, None)
        self.assertEqual(record["status"], "FAIL")
        self.assertIn("different audio", record["detail"])

    def test_silence_nonfinite_and_wrong_length_are_rejected(self):
        prepared = item()
        vocabulary = {"SP": 0, "a": 1, "i": 2}
        silent = evaluate_item(prepared, runs_for(prepared, vocabulary, values=(0.0, 0.0)), vocabulary, None)
        self.assertEqual(silent["status"], "FAIL")
        self.assertIn("silence", silent["detail"])
        nonfinite = evaluate_item(prepared, runs_for(prepared, vocabulary, values=(float("nan"), float("nan"))),
                                  vocabulary, None)
        self.assertEqual(nonfinite["status"], "FAIL")
        short = evaluate_item(prepared, runs_for(prepared, vocabulary, frames=(32, 32)), vocabulary, None)
        self.assertEqual(short["status"], "FAIL")
        self.assertIn("frame count", short["detail"])

    def test_worker_failure_and_budget_overrun_are_reported(self):
        prepared = item()
        vocabulary = {"SP": 0, "a": 1, "i": 2}
        failed = evaluate_item(prepared, runs_for(prepared, vocabulary, codes=(8, 8)), vocabulary, None)
        self.assertEqual(failed["status"], "FAIL")
        self.assertIn("exit status 8", failed["detail"])
        over = evaluate_item(prepared, runs_for(prepared, vocabulary), vocabulary, 5)
        self.assertEqual(over["status"], "FAIL")
        self.assertIn("budget", over["detail"])

    def test_audio_that_does_not_sing_the_requested_note_is_reported(self):
        """The criterion the other automatic ones could not substitute for.

        Every fixture this command had emitted a constant value, which is finite, non-silent,
        deterministic and fast, and which contains no pitch at all. It passed admission, coverage,
        determinism, finite audio and the runtime budget. A candidate can also be a whole octave or a
        fifth away and pass all of those. This case pins the comparison that closes the gap.
        """
        prepared = item()
        vocabulary = {"SP": 0, "a": 1, "i": 2}
        # Singing the requested note passes.
        in_tune = evaluate_item(prepared, runs_for(prepared, vocabulary), vocabulary, 1000)
        self.assertEqual(in_tune["status"], "PASS")
        self.assertAlmostEqual(in_tune["measuredPitchHz"], 210.0, delta=2.0)
        self.assertLess(abs(in_tune["pitchErrorCents"]), 5.0)
        # A fifth sharp fails, and the detail says what was measured rather than only that it failed.
        wrong = evaluate_item(prepared,
                              runs_for(prepared, vocabulary, frequency_hz=315.0),
                              vocabulary, 1000)
        self.assertEqual(wrong["status"], "FAIL")
        self.assertEqual(wrong["criteria"]["pitch-adherence"], "FAIL")
        self.assertIn("sang", wrong["detail"])
        self.assertIn("against the requested 210.0 Hz", wrong["detail"])
        self.assertIn("210.0", wrong["detail"])
        # An octave error is named as one, because that is the failure a listener would describe and
        # the one a reader needs to recognize without doing the arithmetic.
        octave = evaluate_item(prepared,
                               runs_for(prepared, vocabulary, frequency_hz=105.0),
                               vocabulary, 1000)
        self.assertEqual(octave["criteria"]["pitch-adherence"], "FAIL")
        self.assertIn("octave error", octave["detail"])
        # Pitch slightly out is not a failure. The criterion is a gross-error detector, and a
        # candidate must not be failed for a tuning nuance a listener has not judged.
        near = evaluate_item(prepared,
                             runs_for(prepared, vocabulary, frequency_hz=210.0 * 2.0 ** (30.0 / 1200.0)),
                             vocabulary, 1000)
        self.assertEqual(near["status"], "PASS")

    def test_audio_with_no_measurable_pitch_fails_adherence_but_a_short_item_does_not(self):
        prepared = item()
        vocabulary = {"SP": 0, "a": 1, "i": 2}
        # Constant audio is measured and found to contain no pitch. That is a finding about the
        # candidate, so it is a failure.
        flat = evaluate_item(prepared, runs_for(prepared, vocabulary, values=(0.25, 0.25), frequency_hz=CONSTANT_AUDIO),
                             vocabulary, 1000)
        self.assertEqual(flat["criteria"]["pitch-adherence"], "FAIL")
        self.assertIn("no voiced pitch", flat["detail"])
        # An item too short to contain the note it names cannot support a pitch claim. That is a
        # limitation of the item, so it is UNRESOLVED rather than a candidate failure -- blaming the
        # model for an unusable held-out item is the mistake this distinction prevents.
        tiny = item(frames=64)
        short = evaluate_item(tiny, runs_for(tiny, vocabulary), vocabulary, 1000)
        self.assertEqual(short["criteria"]["pitch-adherence"], "UNRESOLVED")
        self.assertIn("too short", short["detail"])

    def test_a_failed_worker_is_not_reported_as_a_pitch_problem(self):
        """A more fundamental failure must be reported instead of an unmeasurable pitch."""
        prepared = item()
        vocabulary = {"SP": 0, "a": 1, "i": 2}
        failed = evaluate_item(prepared, runs_for(prepared, vocabulary, codes=(8, 8)), vocabulary, None)
        self.assertEqual(failed["detail"].startswith("worker exit status 8"), True)
        self.assertEqual(failed["criteria"]["pitch-adherence"], "PASS")

        prepared = item(phones=("a", "u"))
        record = evaluate_item(prepared, [], {"SP": 0, "a": 1, "i": 2}, None)
        self.assertEqual(record["status"], "FAIL")
        self.assertIn("absent from the admitted vocabulary", record["detail"])
        self.assertIn("u", record["detail"])

    def test_aggregate_never_qualifies_and_always_withholds_approval(self):
        passed = [dict(item(), status="PASS",
                       criteria={criterion: "PASS" for criterion in
                                 ("bundle-admission", "vocabulary-coverage", "determinism",
                                  "finite-audio", "runtime-budget")})]
        dossier = aggregate(passed, {"directory": "/b"}, {"path": "/w"}, 2, None)
        self.assertEqual(dossier["verdict"], "UNRESOLVED")
        self.assertEqual(dossier["failedCriteria"], [])
        self.assertEqual(dossier["musicalJudgement"], "UNRESOLVED")
        self.assertFalse(dossier["releaseEligible"])
        self.assertIsNone(dossier["approval"])
        statuses = {entry["id"]: entry["status"] for entry in dossier["criteria"]}
        self.assertEqual(statuses["runtime-budget"], "UNRESOLVED")
        self.assertEqual(statuses["intelligibility"], "UNRESOLVED")
        self.assertEqual(statuses["musicality"], "UNRESOLVED")

    def test_one_failed_item_fails_only_its_criterion(self):
        good = dict(item(), status="PASS",
                    criteria={"bundle-admission": "PASS", "vocabulary-coverage": "PASS",
                              "determinism": "PASS", "finite-audio": "PASS", "runtime-budget": "PASS"})
        bad = dict(item(itemId="held-2"), status="FAIL",
                   criteria={"bundle-admission": "PASS", "vocabulary-coverage": "PASS",
                             "determinism": "FAIL", "finite-audio": "PASS", "runtime-budget": "PASS"})
        dossier = aggregate([good, bad], {}, {}, 2, 100)
        self.assertEqual(dossier["verdict"], "FAILED")
        self.assertEqual(dossier["failedCriteria"], ["determinism"])
        statuses = {entry["id"]: entry["status"] for entry in dossier["criteria"]}
        self.assertEqual(statuses["determinism"], "FAIL")
        self.assertEqual(statuses["finite-audio"], "PASS")
        self.assertEqual(statuses["musicality"], "UNRESOLVED")
        self.assertFalse(dossier["releaseEligible"])


class ConfigurationTests(unittest.TestCase):
    def test_unknown_format_schema_and_empty_holdout_are_rejected(self):
        with self.assertRaises(ValueError):
            validate_configuration({"formatId": "other", "schemaVersion": 1})
        with self.assertRaises(ValueError):
            validate_configuration({"formatId": "com.project-seam.candidate-qualification", "schemaVersion": 2})
        with self.assertRaises(ValueError):
            validate_configuration(configuration("/b", "a" * 64, items=[]))

    def test_item_bounds_and_repetition_bounds_are_enforced(self):
        cases = [
            [dict(item(), itemId="x", frameCount=8)],
            [dict(item(), itemId="x", frameCount=48001)],
            [dict(item(), itemId="x", gain=0.0)],
            [dict(item(), itemId="x", gain=1.5)],
            [dict(item(), itemId="x", phones=[])],
            [dict(item(), itemId="x", phones=["a"] * 65)],
        ]
        for held_out in cases:
            with self.assertRaises(ValueError):
                validate_configuration(configuration("/b", "a" * 64, items=held_out))
        duplicated = [item(), item()]
        with self.assertRaises(ValueError):
            validate_configuration(configuration("/b", "a" * 64, items=duplicated))
        for repetitions in (0, 6):
            payload = configuration("/b", "a" * 64)
            payload["repetitions"] = repetitions
            with self.assertRaises(ValueError):
                validate_configuration(payload)

    def test_request_carries_one_token_per_phone_and_exact_frames(self):
        prepared = item(frames=48, phones=("a", "i"))
        vocabulary = {"SP": 0, "a": 1, "i": 2}
        request = build_request(prepared, vocabulary, "c" * 64,
                                {"modelId": "fixture", "modelVersion": "1", "manifestSha256": "a" * 64})
        magic, version, kind, reserved, size, payload_size = struct.unpack("<4sHBBIQ", request[:20])
        self.assertEqual((magic, version, kind, reserved), (b"SNW1", 1, 1, 0))
        header = json.loads(request[20:20 + size])
        self.assertEqual(header["vocabularySize"], 3)
        self.assertEqual(header["vocabularyHash"], "c" * 64)
        self.assertEqual([entry["tokenId"] for entry in header["phonemes"]], [1, 2])
        self.assertEqual(header["phonemes"][0]["startFrame"], 0)
        self.assertEqual(header["phonemes"][-1]["endFrame"], 48)
        self.assertEqual(payload_size, 48 * 2 * 4)
        reply, pcm = decode_response(response(request, prepared), request, prepared)
        self.assertEqual(reply["requestId"], 1)
        self.assertEqual(len(pcm), 48 * 4)


class CommandTests(unittest.TestCase):
    def bundle(self, root):
        directory = root / "bundle"
        directory.mkdir()
        manifest = json.dumps({"formatId": "com.project-seam.neural-bundle", "modelId": "fixture"},
                             separators=(",", ":")).encode()
        (directory / "manifest.json").write_bytes(manifest)
        (directory / "vocabulary").write_bytes(vocabulary_file())
        return directory, hashlib.sha256(manifest).hexdigest()

    def write(self, root, bundle, manifest_sha256, items=None):
        payload = json.dumps(configuration(bundle, manifest_sha256, items), separators=(",", ":")).encode()
        path = root / "qualification.json"
        path.write_bytes(payload)
        return path, hashlib.sha256(payload).hexdigest()

    def worker(self, root):
        path = root / "worker"
        path.write_bytes(b"worker fixture")
        return path

    def test_command_publishes_an_honest_dossier_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory, manifest_sha256 = self.bundle(root)
            config, config_sha256 = self.write(root, directory, manifest_sha256)
            worker = self.worker(root)
            vocabulary = {"SP": 0, "a": 1, "i": 2}

            def runner(worker_path, arguments, request, timeout):
                # The stub sings the requested note, because the end-to-end case is about publication,
                # overwrite refusal and an honest verdict rather than about pitch. A stub emitting
                # constant audio would now be a legitimate pitch failure and would test the wrong thing.
                prepared = item()
                return 0, response(request, prepared, frequency_hz=prepared["frequencyHz"]), b""

            output = root / "dossier.json"
            from tools.voice_model_training.qualification import qualify, publish_new
            dossier, exit_code = qualify(config, config_sha256, worker, output, runner=runner)
            self.assertEqual(exit_code, 0)
            self.assertEqual(dossier["verdict"], "UNRESOLVED")
            self.assertEqual(dossier["items"][0]["status"], "PASS")
            self.assertEqual(dossier["worker"]["sha256"], hashlib.sha256(worker.read_bytes()).hexdigest())
            publish_new(output, dossier)
            self.assertTrue(output.is_file())
            with self.assertRaises(ValueError):
                from tools.voice_model_training.qualification import qualify as again
                again(config, config_sha256, worker, output, runner=runner)
            self.assertEqual(len(vocabulary), 3)

    def test_command_reports_a_failed_candidate_with_exit_four(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory, manifest_sha256 = self.bundle(root)
            config, config_sha256 = self.write(root, directory, manifest_sha256)
            worker = self.worker(root)
            calls = {"count": 0}

            def runner(worker_path, arguments, request, timeout):
                calls["count"] += 1
                prepared = item()
                return 0, response(request, prepared, 0.25 if calls["count"] % 2 else 0.5), b""

            output = root / "dossier.json"
            from tools.voice_model_training.qualification import qualify, publish_new
            dossier, exit_code = qualify(config, config_sha256, worker, output, runner=runner)
            self.assertEqual(exit_code, 4)
            self.assertEqual(dossier["verdict"], "FAILED")
            self.assertEqual(dossier["failedCriteria"], ["determinism"])
            publish_new(output, dossier)
            written = json.loads(output.read_text())
            self.assertFalse(written["releaseEligible"])
            self.assertIsNone(written["approval"])
            self.assertEqual(calls["count"], 2)

    def test_command_rejects_a_manifest_that_differs_from_its_capture(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory, manifest_sha256 = self.bundle(root)
            config, config_sha256 = self.write(root, directory, "0" * 64)
            worker = self.worker(root)
            from tools.voice_model_training.qualification import qualify
            with self.assertRaises(ValueError):
                qualify(config, config_sha256, worker, root / "dossier.json",
                        runner=lambda *arguments: (0, b"", b""))


if __name__ == "__main__":
    unittest.main()
