from __future__ import annotations

import json
import copy
import hashlib
import math
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tests.external_beta.test_product_soak import _record, _sample
from tests.external_beta.release_gate_fixtures import candidate
from tools.external_beta.product_soak import validate_product_soak
from tools.external_beta.release_gate import evaluate_ready
from tools.external_beta import soak_session_validation as validation
from tools.external_beta import product_soak, full_product_report, release_gate
from tools.external_beta.soak_collector import summarise


def encoded(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode()


def digest(data):
    return hashlib.sha256(data).hexdigest()


class Packet:
    """Independent persisted fixture; no launch, sampler, supervisor or physical claim."""
    def __init__(self, root, duration=1800, name="packet", session_id="controlled-session", poll=1, extra=None):
        self.root, self.directory = root, root / name
        self.directory.mkdir()
        self.record = _record(root, duration)
        self.record["recordId"] = session_id
        self.samples = [_sample(target + .25) for target in range(duration + 1)]
        self.record["samples"], self.record["summary"] = self.samples, summarise(self.samples)
        self.manifest = {"schemaVersion": 1, "recordType": "external-beta-engineering-soak-session",
            "sessionId": session_id, "durationSeconds": duration, "evidenceScope": "engineering", "releaseEligible": False,
            "installedTreeSha256": self.record["appIdentity"]["installedTreeSha256"],
            "workloadSha256": self.record["workloadSha256"], "workloadId": self.record["workloadId"],
            "machineProfileId": self.record["machineProfileId"], "machineProfileSha256": self.record["machineProfileSha256"],
            "platform": "macos", "architecture": "arm64", "host": "standalone", "surface": "standalone",
            **{key: copy.deepcopy(self.record[key]) for key in ("appIdentity", "bankIdentity", "projectIdentity")}, **(extra or {})}
        manifest_bytes = encoded(self.manifest)
        self.manifest_digest = digest(manifest_bytes)
        snapshot = {"schemaVersion": 1, "recordType": "external-beta-final-soak-sample", "recordId": session_id,
            "durationSeconds": duration, "heartbeatSequence": duration + 1, "sample": self.samples[-1]}
        snapshot_bytes = encoded(snapshot)
        observations, elapsed, previous_sequence, progress = [], 0.0, 0, 0.0
        while True:
            sequence = int(elapsed) if poll == 1 else min(duration, max(0, math.floor(elapsed - .5) + 1))
            finished = elapsed >= duration + (1 if poll == 1 else .7)
            if finished:
                sequence = duration + 1
            if sequence > previous_sequence:
                progress = elapsed
            observations.append({"elapsedSeconds": elapsed, "heartbeatSequence": sequence, "heartbeatAgeSeconds": elapsed - progress})
            if finished:
                break
            previous_sequence = sequence
            elapsed = min(elapsed + poll, duration if elapsed < duration else duration + 1)
        self.receipt = {"schemaVersion": 2, "recordType": "external-beta-soak-supervision", "status": "COMPLETE",
            "recordId": session_id, "evidenceScope": "engineering", "releaseEligible": False, "requiredSeconds": duration,
            "installedTreeSha256": self.manifest["installedTreeSha256"], "workloadSha256": self.manifest["workloadSha256"],
            "productPid": 123, "collectorPid": 456, "supervisorPid": 789, "clockAuthority": "supervisor-monotonic",
            "pollIntervalSeconds": poll, "maxGapSeconds": 5, "heartbeatIntervalSeconds": 1, "heartbeatLatenessSeconds": 1,
            "endpointAckSeconds": 1, "endpointProtocol": "durable-final-sample-v1", "cleanupErrors": [], "observations": observations,
            "finishedAcknowledgement": {"receivedElapsedSeconds": elapsed, "finalSample": snapshot,
                "frame": {"type": "FINISHED", "recordId": session_id, "durationSeconds": duration,
                    "heartbeatSequence": duration + 1, "sampleBytes": len(snapshot_bytes), "sampleSha256": digest(snapshot_bytes)}}}
        contents = {"session.json": manifest_bytes, "final-sample.json": snapshot_bytes,
            "supervision.json": encoded(self.receipt), "worker-result.json": encoded({"status": "FINISHED", "sessionId": session_id,
                "manifestSha256": self.manifest_digest, "sampleCount": duration + 1, "productPid": 123, "collectorPid": 456}),
            "samples.jsonl": b"".join(encoded({"sessionId": session_id, "manifestSha256": self.manifest_digest,
                "productPid": 123, "sequence": target + 1, "targetElapsedSeconds": target, "sample": sample}) for target, sample in enumerate(self.samples))}
        self.index = {"status": "COMPLETE", "evidenceScope": "engineering", "releaseEligible": False,
            "sessionId": session_id, "manifestSha256": self.manifest_digest, "durationSeconds": duration, "cleanupErrors": [],
            "files": {key: {"sha256": digest(data), "bytes": len(data)} for key, data in contents.items()}}
        for key, data in contents.items():
            (self.directory / key).write_bytes(data)
        self.refresh_index()
        self.record["evidence"].append({"kind": "soak-session-index", "path": self.reference["locator"],
            "sha256": self.reference["sha256"], "capturedAt": "2026-10-06T00:00:00Z", "reviewer": "controlled-reviewer"})

    def refresh_index(self):
        data = encoded(self.index)
        (self.directory / "packet-index.json").write_bytes(data)
        self.reference = {"locator": self.directory.name + "/packet-index.json", "sha256": digest(data)}

    def change(self, name, mutate):
        path = self.directory / name
        value = json.loads(path.read_bytes())
        mutate(value)
        data = encoded(value)
        path.write_bytes(data)
        self.index["files"][name] = {"sha256": digest(data), "bytes": len(data)}
        self.refresh_index()

    def change_rows(self, mutate):
        path = self.directory / "samples.jsonl"
        rows = [json.loads(line) for line in path.read_bytes().splitlines()]
        mutate(rows)
        data = b"".join(encoded(row) for row in rows)
        path.write_bytes(data)
        self.index["files"]["samples.jsonl"] = {"sha256": digest(data), "bytes": len(data)}
        self.refresh_index()

    def typed_reference(self, name="record.json"):
        self.record["evidence"][-1].update(path=self.reference["locator"], sha256=self.reference["sha256"])
        data = encoded(self.record)
        (self.root / name).write_bytes(data)
        return {"locator": name, "sha256": digest(data)}


class PacketSemanticTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.packet = Packet(self.root)

    def result(self, **kwargs):
        return validation.validate_soak_session_reference(self.packet.reference, evidence_root=self.root, **kwargs)

    def invalid(self, text):
        result = self.result()
        self.assertFalse(result.valid)
        self.assertIn(text, " ".join(result.errors))

    def test_engineering_packet_is_valid_but_never_release_eligible(self):
        result = self.result(expected_samples=self.packet.samples)
        self.assertTrue(result.valid, result.errors)
        self.assertFalse(result.release_eligible)
        self.assertEqual("engineering", result.evidence_scope)
        product = validate_product_soak(self.packet.record, self.root)
        self.assertEqual((validation.ENGINEERING_ONLY,), product.errors)
        self.assertFalse(product.passed)

    def test_two_hour_packet_and_point_one_endpoint_latency_are_accepted(self):
        for duration, poll, name in ((7200, 1, "two-hour"), (1800, .1, "latency")):
            packet = Packet(self.root, duration, name=name, poll=poll)
            result = validation.validate_soak_session_reference(packet.reference, evidence_root=self.root)
            self.assertTrue(result.valid, result.errors)

    def test_rehashed_sequence_gap_is_semantically_rejected(self):
        self.packet.change_rows(lambda rows: rows[2].update(sequence=99))
        self.invalid("raw row session or sequence")

    def test_rehashed_early_endpoint_is_rejected(self):
        self.packet.change_rows(lambda rows: rows[-1]["sample"].update(elapsedSeconds=1799.9))
        self.invalid("raw sample absolute timing")

    def test_rehashed_short_observed_duration_is_rejected(self):
        self.packet.change("supervision.json", lambda value: value.update(observations=value["observations"][:-2]))
        self.invalid("supervision lacks final duration")

    def test_rehashed_early_poll_is_rejected(self):
        self.packet.change("supervision.json", lambda value: value["observations"][2].update(elapsedSeconds=1.5))
        self.invalid("polled early")

    def test_rehashed_false_heartbeat_age_is_rejected(self):
        self.packet.change("supervision.json", lambda value: value["observations"][2].update(heartbeatAgeSeconds=.2))
        self.invalid("heartbeat age differs")

    def test_rehashed_loosened_budget_is_rejected(self):
        self.packet.change("supervision.json", lambda value: value.update(endpointAckSeconds=2))
        self.invalid("timing budgets differ")

    def test_rehashed_wrong_finished_hash_is_rejected(self):
        self.packet.change("supervision.json", lambda value: value["finishedAcknowledgement"]["frame"].update(sampleSha256="0" * 64))
        self.invalid("FINISHED final hash")

    def test_rehashed_unclean_cleanup_is_rejected(self):
        self.packet.change("supervision.json", lambda value: value.update(cleanupErrors=["controlled failure"]))
        self.invalid("supervisor cleanup is unclean")

    def test_rehashed_complete_receipt_cannot_retain_failure_or_unknown_fields(self):
        for field in ("failure", "unknownProducerField"):
            with self.subTest(field=field):
                self.packet.change("supervision.json", lambda value: value.update({field: "contradictory retained state"}))
                self.invalid("receipt COMPLETE fields differ")
                self.packet.change("supervision.json", lambda value: value.pop(field))

    def test_omitted_worker_error_still_prevents_completion(self):
        (self.packet.directory / "worker-error.json").write_bytes(b"{}\n")
        self.invalid("retained worker/session failure")

    def test_rehashed_authority_flip_is_not_a_physical_path(self):
        self.packet.index["releaseEligible"] = True
        self.packet.refresh_index()
        self.invalid("engineering authority differs")

    def test_pending_index_cannot_replace_committed_index(self):
        (self.packet.directory / "packet-index.pending.json").write_bytes(encoded(self.packet.index))
        self.packet.reference["locator"] = "packet/packet-index.pending.json"
        self.invalid("committed packet-index.json")

    def test_missing_artifact_is_rejected(self):
        (self.packet.directory / "final-sample.json").unlink()
        self.invalid("FileNotFoundError")

    def test_duplicate_key_and_nonfinite_json_are_rejected_after_rehash(self):
        for data, expected in ((b'{"status":"COMPLETE","status":"COMPLETE"}', "duplicate JSON key"),
                               (b'{"value":NaN}', "nonfinite JSON number")):
            (self.packet.directory / "packet-index.json").write_bytes(data)
            self.packet.reference["sha256"] = digest(data)
            self.invalid(expected)

    def test_boolean_sequence_and_pid_are_not_numeric_aliases(self):
        self.packet.change_rows(lambda rows: rows[0].update(sequence=True))
        self.invalid("raw row session or sequence")
        self.packet.change("supervision.json", lambda value: value.update(productPid=True))
        self.invalid("process identities differ")

    def test_expected_identity_and_sample_substitution_are_rejected(self):
        result = self.result(expected_bindings={"sourceCommit": "f" * 40})
        self.assertIn("session binding sourceCommit", " ".join(result.errors))
        samples = copy.deepcopy(self.packet.samples)
        samples[3]["rssBytes"] += 1
        result = self.result(expected_samples=samples)
        self.assertIn("product sample series differs", " ".join(result.errors))

    def test_symlink_directory_and_fifo_are_refused_without_following(self):
        (self.root / "alias").symlink_to(self.packet.directory, target_is_directory=True)
        self.packet.reference["locator"] = "alias/packet-index.json"
        self.invalid("reference cannot be read")
        self.packet.reference["locator"] = "packet/packet-index.json"
        path = self.packet.directory / "final-sample.json"
        path.unlink()
        os.mkfifo(path)
        self.invalid("not a regular file")

    def test_denied_index_is_read_once_even_with_legacy_alias_and_second_consumer(self):
        opened, denied = validation.os.open, []
        def deny(path, *args, **kwargs):
            if path == "packet-index.json":
                denied.append(path)
                raise PermissionError("controlled read denial")
            return opened(path, *args, **kwargs)
        context = validation.SoakReplayContext()
        self.packet.record["evidence"].append({**self.packet.record["evidence"][-1], "kind": "metrics-alias"})
        read_bytes = Path.read_bytes
        def no_legacy(path):
            if path.name == "packet-index.json":
                raise AssertionError("legacy reader must not retry a guarded reference")
            return read_bytes(path)
        with mock.patch.object(validation.os, "open", side_effect=deny), mock.patch.object(Path, "read_bytes", no_legacy):
            for _ in range(2):
                result = validate_product_soak(self.packet.record, self.root, soak_replay_context=context)
                self.assertIn("PermissionError", " ".join(result.errors))
        self.assertEqual(["packet-index.json"], denied)

    def test_shared_context_rejects_conflicting_session_bytes_and_duplicate_coverage(self):
        context = validation.SoakReplayContext()
        first = self.result(replay_context=context)
        same = self.result(replay_context=context)
        self.assertTrue(first.valid and same.valid)
        second = Packet(self.root, name="other", extra={"sourceCommit": "e" * 40})
        result = validation.validate_soak_session_reference(second.reference, evidence_root=self.root, replay_context=context)
        self.assertIn("conflicting or replayed session claim", " ".join(result.errors))
        self.assertTrue(context.consume("R17", "one-cell", first.session_id))
        self.assertFalse(context.consume("R17", "one-cell", first.session_id))
        self.assertEqual(1, len(context.coverage))
        with self.assertRaisesRegex(ValueError, "conflicting soak coverage"):
            context.consume("R17", "one-cell", "another-session")
        with self.assertRaisesRegex(ValueError, "replayed session"):
            context.consume("R17", "other-cell", first.session_id)

    def test_new_index_size_guard_runs_before_legacy_evidence_reader(self):
        (self.packet.directory / "packet-index.json").write_bytes(b" " * (validation.INDEX_LIMIT + 1))
        with mock.patch.object(product_soak, "_evidence", wraps=product_soak._evidence) as legacy:
            result = validate_product_soak(self.packet.record, self.root)
        self.assertIn("exceeds its byte limit", " ".join(result.errors))
        self.assertEqual(3, legacy.call_count)
        self.assertTrue(all(call.args[1]["kind"] != "soak-session-index" for call in legacy.call_args_list))

    def test_unsafe_locator_and_deep_json_are_rejected(self):
        for locator in ("../packet/packet-index.json", "/packet-index.json", "packet/./packet-index.json", "packet\\packet-index.json"):
            result = validation.validate_soak_session_reference({**self.packet.reference, "locator": locator}, evidence_root=self.root)
            self.assertIn("safe relative locator", " ".join(result.errors))
        data = b"[" * 66 + b"0" + b"]" * 66
        (self.packet.directory / "packet-index.json").write_bytes(data)
        self.packet.reference["sha256"] = digest(data)
        self.invalid("JSON depth exceeds 64")

    def test_rehashed_boolean_metric_is_rejected(self):
        self.packet.change_rows(lambda rows: rows[3]["sample"].update(rssBytes=True))
        self.invalid("raw sample metric is invalid")

    def test_poll_tolerance_matches_the_producer(self):
        self.packet.change("supervision.json", lambda value: value["observations"][1].update(elapsedSeconds=1 - .5e-6))
        self.assertTrue(self.result().valid)
        self.packet.change("supervision.json", lambda value: value["observations"][1].update(elapsedSeconds=1 - 2e-6))
        self.invalid("polled early")

    def test_whitespace_session_and_indexed_byte_count_are_rejected(self):
        packet = Packet(self.root, name="blank", session_id="  ")
        result = validation.validate_soak_session_reference(packet.reference, evidence_root=self.root)
        self.assertIn("manifest session or duration", " ".join(result.errors))
        self.packet.index["files"]["samples.jsonl"]["bytes"] += 1
        self.packet.refresh_index()
        self.invalid("indexed artifact byte length differs")

    def test_outer_build_version_and_workload_cannot_relabel_typed_record(self):
        for bindings, expected in (({"buildId": "another-build"}, "appIdentity.buildId"),
                                   ({"version": "9.9.9"}, "appIdentity.version"),
                                   ({"workloadId": "another-workload"}, "binding workloadId")):
            result = validate_product_soak(self.packet.record, self.root, expected_session_bindings=bindings)
            self.assertIn(expected, " ".join(result.errors))

    def test_cached_alias_preserves_exact_reference_shape(self):
        context = validation.SoakReplayContext()
        self.assertTrue(self.result(replay_context=context).valid)
        with self.assertRaisesRegex(ValueError, "exactly locator and sha256"):
            validation.reuse_reference({**self.packet.reference, "extra": True}, base=self.root,
                maximum_bytes=validation.INDEX_LIMIT, replay_context=context)

    def test_primed_session_cache_cannot_accept_extra_reference_fields(self):
        context = validation.SoakReplayContext()
        self.assertTrue(self.result(replay_context=context).valid)
        sessions = mock.Mock(wraps=context.sessions)
        context.sessions = sessions
        with mock.patch.object(validation.os, "open", side_effect=AssertionError("malformed reference must not open files")):
            result = validation.validate_soak_session_reference({**self.packet.reference, "extra": True},
                evidence_root=self.root, replay_context=context)
        self.assertFalse(result.valid)
        self.assertIn("exactly locator and sha256", " ".join(result.errors))
        sessions.get.assert_not_called()

    def test_primed_alias_cache_cannot_normalize_an_unsafe_original_locator(self):
        context = validation.SoakReplayContext()
        self.assertTrue(self.result(replay_context=context).valid)
        for locator in ("packet/../packet/packet-index.json", "packet/./packet-index.json",
                        "packet//packet-index.json", str(self.packet.directory / "packet-index.json"),
                        "packet\\packet-index.json"):
            # Even an invalid announced locator remains classified as soak data.
            announced = validation.validate_soak_session_reference({**self.packet.reference, "locator": locator},
                evidence_root=self.root, replay_context=context)
            self.assertIn("safe relative locator", " ".join(announced.errors))
            with self.subTest(locator=locator), \
                 mock.patch.object(validation, "read_reference", side_effect=AssertionError("invalid alias must not use cached bytes")):
                with self.assertRaisesRegex(ValueError, "safe relative locator"):
                    validation.reuse_reference({**self.packet.reference, "locator": locator}, base=self.root,
                        maximum_bytes=validation.INDEX_LIMIT, replay_context=context)

    def test_failure_probe_denial_is_terminal_for_product_and_report_aliases(self):
        opened, read_bytes = validation.os.open, Path.read_bytes
        for name in ("worker-error.json", "session-error.json"):
            with self.subTest(name=name):
                context, denied = validation.SoakReplayContext(), []
                packet = Packet(self.root, name="probe-" + name[:-5], session_id="probe-" + name)
                (packet.directory / name).write_bytes(b"{}\n")
                alias = {"locator": packet.directory.name + "/" + name, "sha256": digest(b"{}\n")}
                record = copy.deepcopy(packet.record)
                record["evidence"].append({"kind": "ordinary-alias", "path": alias["locator"],
                    "sha256": alias["sha256"], "capturedAt": "2026-10-06T00:00:00Z", "reviewer": "controlled"})
                def deny(path, *args, **kwargs):
                    if path == name:
                        denied.append(path)
                        raise PermissionError("controlled failure-probe denial: " + name)
                    return opened(path, *args, **kwargs)
                def no_legacy(path):
                    if path.name == name:
                        raise AssertionError("denied failure probe must not use legacy read_bytes")
                    return read_bytes(path)
                with mock.patch.object(validation.os, "open", side_effect=deny), \
                     mock.patch.object(Path, "read_bytes", no_legacy), \
                     mock.patch.object(product_soak, "_evidence", wraps=product_soak._evidence) as legacy:
                    for _ in range(2):
                        result = validate_product_soak(record, self.root, soak_replay_context=context)
                        self.assertIn("controlled failure-probe denial", " ".join(result.errors))
                    errors = full_product_report._reference_errors(alias, base=self.root, label="failure alias",
                        verify=True, soak_context=context)
                    self.assertIn("PermissionError", " ".join(errors))
                    errors = full_product_report.validate_full_product_report_reference(alias, candidate={},
                        acceptance_contract={}, evidence_root=self.root, soak_replay_context=context)
                    self.assertIn("PermissionError", " ".join(errors))
                self.assertEqual([name], denied)
                self.assertTrue(all(call.args[1].get("kind") != "ordinary-alias" for call in legacy.call_args_list))

    def test_duplicate_session_references_still_guard_oversized_index_alias(self):
        contents = b" " * (validation.INDEX_LIMIT + 1)
        (self.packet.directory / "packet-index.json").write_bytes(contents)
        item = self.packet.record["evidence"][-1]
        item["sha256"] = digest(contents)
        self.packet.record["evidence"].extend([dict(item), {**item, "kind": "ordinary-alias"}])
        read_bytes = Path.read_bytes
        with mock.patch.object(Path, "read_bytes", autospec=True, side_effect=read_bytes) as legacy_reads, \
             mock.patch.object(product_soak, "_evidence", wraps=product_soak._evidence) as legacy:
            result = validate_product_soak(self.packet.record, self.root)
        self.assertIn(validation.SESSION_REQUIRED, result.errors)
        self.assertIn("exceeds its byte limit", " ".join(result.errors))
        self.assertTrue(all(call.args[0].name != "packet-index.json" for call in legacy_reads.call_args_list))
        self.assertTrue(all(call.args[1].get("kind") != "ordinary-alias" for call in legacy.call_args_list))

    def test_invalid_announced_hash_cannot_send_oversized_index_alias_to_legacy_reader(self):
        contents = b" " * (validation.INDEX_LIMIT + 1)
        (self.packet.directory / "packet-index.json").write_bytes(contents)
        read_bytes, opened = Path.read_bytes, validation.os.open
        for invalid_hash in (None, "malformed"):
            with self.subTest(sha256=invalid_hash):
                record, context = copy.deepcopy(self.packet.record), validation.SoakReplayContext()
                item = record["evidence"][-1]
                if invalid_hash is None:
                    item.pop("sha256")
                else:
                    item["sha256"] = invalid_hash
                record["evidence"].append({**item, "kind": "ordinary-alias", "sha256": digest(contents)})
                with mock.patch.object(Path, "read_bytes", autospec=True, side_effect=read_bytes) as legacy_reads, \
                     mock.patch.object(validation.os, "open", wraps=opened) as opens, \
                     mock.patch.object(product_soak, "_evidence", wraps=product_soak._evidence) as legacy:
                    for _ in range(2):
                        result = validate_product_soak(record, self.root, soak_replay_context=context)
                        self.assertIn("reference SHA-256 is invalid", " ".join(result.errors))
                        self.assertIn("exceeds its byte limit", " ".join(result.errors))
                self.assertEqual(1, sum(call.args[0] == "packet-index.json" for call in opens.call_args_list))
                self.assertTrue(all(call.args[0].name != "packet-index.json" for call in legacy_reads.call_args_list))
                self.assertTrue(all(call.args[1].get("kind") != "ordinary-alias" for call in legacy.call_args_list))

    def test_invalid_indexed_metadata_still_guards_the_announced_artifact(self):
        contents = b" " * (validation.METADATA_LIMIT + 1)
        (self.packet.directory / "session.json").write_bytes(contents)
        self.packet.index["files"]["session.json"] = {"sha256": digest(contents), "bytes": "invalid"}
        self.packet.refresh_index()
        self.packet.record["evidence"][-1]["sha256"] = self.packet.reference["sha256"]
        self.packet.record["evidence"].append({"kind": "ordinary-alias", "path": "packet/session.json",
            "sha256": digest(contents), "capturedAt": "2026-10-06T00:00:00Z", "reviewer": "controlled"})
        read_bytes = Path.read_bytes
        with mock.patch.object(Path, "read_bytes", autospec=True, side_effect=read_bytes) as legacy_reads:
            result = validate_product_soak(self.packet.record, self.root)
        self.assertIn("indexed artifact metadata differs", " ".join(result.errors))
        self.assertIn("exceeds its byte limit", " ".join(result.errors))
        self.assertTrue(all(call.args[0].name != "session.json" for call in legacy_reads.call_args_list))

    def test_public_report_reference_preserves_denial_across_trusted_root_spellings(self):
        parent_alias = self.root / "trusted-parent-alias"
        parent_alias.symlink_to(self.root.parent, target_is_directory=True)
        evidence_root, context = parent_alias / self.root.name, validation.SoakReplayContext()
        reference, opened, denied = self.packet.typed_reference(), validation.os.open, []
        def deny(path, *args, **kwargs):
            if path == reference["locator"]:
                denied.append(path)
                raise PermissionError("controlled public-reference denial")
            return opened(path, *args, **kwargs)
        with mock.patch.object(validation.os, "open", side_effect=deny), \
             mock.patch.object(full_product_report, "_safe_reference_path", side_effect=AssertionError("guarded target must precede legacy resolution")):
            result = product_soak.validate_product_soak_reference(reference, evidence_root, soak_replay_context=context)
            self.assertIn("PermissionError", " ".join(result.errors))
            absolute = {**reference, "locator": str((self.root / reference["locator"]).resolve())}
            errors = full_product_report.validate_full_product_report_reference(absolute, candidate={},
                acceptance_contract={}, evidence_root=self.root.resolve(), soak_replay_context=context)
            self.assertIn("safe relative locator", " ".join(errors))
            errors = full_product_report.validate_full_product_report_reference(reference, candidate={},
                acceptance_contract={}, evidence_root=self.root.resolve(), soak_replay_context=context)
            self.assertIn("PermissionError: controlled public-reference denial", " ".join(errors))
        self.assertEqual([reference["locator"]], denied)


class ConsumerOrderingTests(unittest.TestCase):
    def test_selected_typed_eb005_packet_has_only_engineering_semantic_error(self):
        with tempfile.TemporaryDirectory() as directory:
            root, value = Path(directory), candidate()
            selected = next(item for item in value["evidence"] if item["requirementId"] == "EB-005-standalone-soak")
            value["requirements"]["EB-005-standalone-soak"]["evidenceRecordIds"] = [selected["recordId"]]
            value["releaseIdentity"]["buildId"] = "build-001"
            template = _record(root)
            selected.update(installedTreeSha256=template["appIdentity"]["installedTreeSha256"],
                **{key: template[key] for key in ("workloadId", "workloadSha256", "machineProfileId", "machineProfileSha256")})
            extra = {key: selected[key] for key in ("sourceCommit", "stageNodeId", "parentEdgeId")}
            extra.update(candidateRootId=value["candidateRoot"]["id"], candidateRootSha256=value["candidateRoot"]["sha256"],
                acceptanceContractSha256=value["acceptanceContractSha256"], buildId="build-001", version="0.13.1",
                signedDeliverableSha256=selected["finalDeliverableSha256"])
            packet = Packet(root, extra=extra)
            selected["rawArchive"] = packet.typed_reference()
            result = evaluate_ready(value, archive_verified=True, evidence_root=root)
            prefix = f"{selected['recordId']}: product soak reference: "
            self.assertEqual([prefix + validation.ENGINEERING_ONLY], [error for error in result.errors if error.startswith(prefix)])
            self.assertIn("EB-005-standalone-soak", result.blocked_ids)
            self.assertFalse(result.passed)

    def test_typed_r17_duration_pair_has_only_engineering_authority_errors(self):
        with tempfile.TemporaryDirectory() as directory:
            root, context = Path(directory), validation.SoakReplayContext()
            first = Packet(root)
            second = Packet(root, 7200, name="two-hour", session_id="controlled-two-hour")
            observation = {key: first.manifest[key] for key in ("platform", "host", "installedTreeSha256",
                "workloadId", "workloadSha256", "machineProfileId", "machineProfileSha256")}
            observation.update(sourceCommit="a" * 40, buildId="build-001", signedDeliverableSha256="b" * 64,
                resourceIds=["voice"], bindings={"workload": {"locator": "workload.json", "sha256": "c" * 64}})
            shared = {"candidateRootId": "controlled-root", "candidateRootSha256": "d" * 64,
                "acceptanceContractSha256": "e" * 64, "fullProductContractSha256": "f" * 64,
                "resourceMatrixSha256": "1" * 64}
            for packet in (first, second):
                def bind(value):
                    value.update(shared)
                    value.update({key: value_ for key, value_ in observation.items() if key != "artifacts"})
                packet.change("session.json", bind)
                # Rewrite every manifest hash binding independently after the declared identity change.
                packet.manifest_digest = packet.index["files"]["session.json"]["sha256"]
                packet.index["manifestSha256"] = packet.manifest_digest
                packet.change_rows(lambda rows: [row.update(manifestSha256=packet.manifest_digest) for row in rows])
                packet.change("worker-result.json", lambda value: value.update(manifestSha256=packet.manifest_digest))
                packet.refresh_index()
            observation["artifacts"] = [{"kind": "soak-log", **first.typed_reference()},
                {"kind": "soak-log", **second.typed_reference("two-hour-record.json")}]
            errors = full_product_report._soak_observation_errors(observation, label="R17", soak_root=root,
                soak_context=context, soak_bindings=shared, soak_reference_base=root)
            self.assertEqual([f"R17: {validation.ENGINEERING_ONLY}"] * 2, errors)
            self.assertEqual(2, len(context.coverage))
            again = full_product_report._soak_observation_errors(observation, label="R17", soak_root=root,
                soak_context=context, soak_bindings=shared, soak_reference_base=root)
            self.assertEqual(errors, again)
            self.assertEqual(2, len(context.coverage))

    def test_denied_typed_reference_is_not_reopened_as_the_report_itself(self):
        with tempfile.TemporaryDirectory() as directory:
            root, context = Path(directory), validation.SoakReplayContext()
            reference = {"locator": "denied.json", "sha256": "a" * 64}
            opened, denied = validation.os.open, []
            def deny(path, *args, **kwargs):
                if path == "denied.json":
                    denied.append(path)
                    raise PermissionError("controlled report alias denial")
                return opened(path, *args, **kwargs)
            with mock.patch.object(validation.os, "open", side_effect=deny), \
                 mock.patch.object(full_product_report, "_safe_reference_path", side_effect=AssertionError("denied alias must precede legacy resolution")):
                result = product_soak.validate_product_soak_reference(reference, root, soak_replay_context=context)
                self.assertIn("PermissionError", " ".join(result.errors))
                errors = full_product_report.validate_full_product_report_reference(reference, candidate={},
                    acceptance_contract={}, evidence_root=root, soak_replay_context=context)
            self.assertIn("PermissionError", " ".join(errors))
            self.assertEqual(["denied.json"], denied)

    def test_explicit_root_is_required_even_when_reference_verification_is_disabled(self):
        report = {"status": "PASS", "cases": [{"id": "R17.soak-hosts", "observations": [{"artifacts": [{"kind": "soak-log", "locator": "outside.json", "sha256": "a" * 64}]}]}]}
        with mock.patch.object(full_product_report, "_schema_errors", return_value=[]), \
             mock.patch.object(full_product_report, "_observation_errors", side_effect=lambda observation, **options: options["soak_errors"]), \
             mock.patch.object(validation.os, "open", side_effect=AssertionError("no root means no soak file open")):
            errors = full_product_report.validate_full_product_report(report, verify_references=False)
        self.assertIn("explicit evidence root is required", " ".join(errors))

    def test_eb005_and_r17_share_one_context_and_denied_typed_alias_is_not_reopened(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data = encoded({"recordType": "external-beta-product-soak"})
            reference = {"locator": "denied.json", "sha256": digest(data)}
            report = {"status": "PASS", "rawArchive": reference, "cases": [{"id": "R17.soak-hosts", "observations": [{"artifacts": [{"kind": "soak-log", **reference}]}]}]}
            report_data = encoded(report)
            (root / "report.json").write_bytes(report_data)
            value = candidate()
            for item in value["evidence"]:
                if item["requirementId"] == "EB-005-standalone-soak":
                    item["rawArchive"] = dict(reference)
            value["evidence"].append({"requirementId": "EB-009-full-product", "recordId": "full-product", "fullProductReport": {"locator": "report.json", "sha256": digest(report_data)}})
            opened, denied, contexts = validation.os.open, [], []
            def deny(path, *args, **kwargs):
                if path == "denied.json":
                    denied.append(path)
                    raise PermissionError("controlled typed denial")
                return opened(path, *args, **kwargs)
            product_reader = product_soak.validate_product_soak_reference
            def observe(reference, evidence_root, **options):
                contexts.append(options["soak_replay_context"])
                return product_reader(reference, evidence_root, **options)
            with mock.patch.object(validation.os, "open", side_effect=deny), \
                 mock.patch.object(release_gate, "validate_product_soak_reference", side_effect=observe), \
                 mock.patch.object(full_product_report, "validate_product_soak_reference", side_effect=observe), \
                 mock.patch.object(full_product_report, "_schema_errors", return_value=[]), \
                 mock.patch.object(full_product_report, "_observation_errors", side_effect=lambda observation, **options: options["soak_errors"]):
                result = evaluate_ready(value, archive_verified=True, evidence_root=root)
            self.assertGreaterEqual(len(contexts), 3)
            self.assertTrue(all(context is contexts[0] for context in contexts))
            self.assertEqual(["denied.json"], denied)
            self.assertIn("PermissionError", " ".join(result.errors))
            self.assertIn("EB-005-standalone-soak", result.blocked_ids)
            self.assertIn("EB-009-full-product", result.blocked_ids)


class MissingAdmissionTests(unittest.TestCase):
    def test_product_record_requires_independent_session_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = validate_product_soak(_record(root), root)
            self.assertIn("SOAK_SESSION_REQUIRED", " ".join(result.errors))
            self.assertFalse(result.passed)

    def test_selected_eb005_reference_is_semantically_consumed(self):
        with tempfile.TemporaryDirectory() as directory:
            result = evaluate_ready(candidate(), archive_verified=True, evidence_root=Path(directory))
            self.assertIn("EB-005-standalone-soak", result.blocked_ids)
            self.assertTrue(any("product soak reference" in error for error in result.errors), result.errors)


if __name__ == "__main__":
    unittest.main()
