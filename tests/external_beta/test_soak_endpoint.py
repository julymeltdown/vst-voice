from __future__ import annotations

import hashlib
import json
import os
import unittest
from unittest import mock

from tools.external_beta import soak_supervisor as supervisor
from tests.external_beta import test_soak_supervisor as fixtures


class SoakEndpointTests(unittest.TestCase):
    def setUp(self):
        fixtures.SoakSupervisorTests.setUp(self)
        self.auto_commit = False
    tearDown = fixtures.SoakSupervisorTests.tearDown
    send = fixtures.SoakSupervisorTests.send
    sleep = fixtures.SoakSupervisorTests.sleep
    run_supervisor = fixtures.SoakSupervisorTests.run_supervisor
    failure = fixtures.SoakSupervisorTests.failure

    def commit(self, **overrides):
        options = dict(record_id="fixture-soak", duration_seconds=self.duration,
                       heartbeat_sequence=self.sequence,
                       sample={"elapsedSeconds": self.now - 100, "rssBytes": 1234})
        options.update(overrides)
        supervisor.commit_final_sample(self.final_sample_writer, self.finished_writer, **options)

    def capture_commit(self, **overrides):
        frames = []
        write = os.write
        def capture(fd, data):
            if fd == self.finished_writer:
                frames.append(data)
                return len(data)
            return write(fd, data)
        with mock.patch.object(supervisor.os, "write", side_effect=capture):
            self.commit(**overrides)
        return json.loads(frames[0])

    def frame(self, value):
        os.write(self.finished_writer, (json.dumps(value) + "\n").encode())

    def at_endpoint(self, action, delay=0):
        def heartbeat():
            if self.now - 100 >= self.duration + delay and not self.final_committed:
                self.sequence += 1
                action()
                os.write(self.writer, f"{self.sequence}\n".encode())
                self.final_committed = True
            else:
                self.send()
        self.heartbeat = heartbeat
        self.send()

    def assert_readers_closed(self):
        for descriptor in (self.reader, self.finished_reader, self.final_sample_reader):
            with self.assertRaises(OSError):
                os.fstat(descriptor)

    def refuse_clock_only_completion(self, duration):
        self.send()
        with mock.patch.object(supervisor.os, "getpid", return_value=789):
            with self.assertRaisesRegex(ValueError, "FINISHED"):
                supervisor.supervise_soak(
                    self.product, self.collector, self.reader,
                    record_id="fixture-soak", installed_tree_sha256="a" * 64,
                    workload_sha256="b" * 64, duration_seconds=duration,
                    clock=lambda: self.now, sleep=self.sleep,
                )

    def test_thirty_minute_clock_coverage_cannot_replace_final_commit(self):
        self.refuse_clock_only_completion(1800)

    def test_two_hour_clock_coverage_cannot_replace_final_commit(self):
        self.refuse_clock_only_completion(7200)

    def test_live_heartbeats_without_finished_fail_at_bounded_endpoint(self):
        self.send()
        receipt = self.failure("FINISHED acknowledgement unavailable")
        self.assertEqual(receipt["observations"][-1]["elapsedSeconds"], 1801)
        self.assertIsNone(receipt["finishedAcknowledgement"])
        self.assert_readers_closed()

    def test_finished_without_persisted_final_sample_fails(self):
        self.at_endpoint(lambda: self.frame(dict(type="FINISHED", recordId="fixture-soak",
            durationSeconds=1800, heartbeatSequence=self.sequence,
            sampleBytes=100, sampleSha256="c" * 64)))
        self.failure("not committed")

    def test_wrong_session_finished_fails(self):
        self.at_endpoint(lambda: self.commit(record_id="different-session"))
        self.failure("session or duration")

    def test_wrong_duration_finished_fails(self):
        self.at_endpoint(lambda: self.commit(duration_seconds=7200,
                                            sample={"elapsedSeconds": 7200}))
        self.failure("session or duration")

    def test_truncated_committed_sample_fails(self):
        def truncate():
            frame = self.capture_commit()
            os.ftruncate(self.final_sample_writer, 1)
            self.frame(frame)
        self.at_endpoint(truncate)
        self.failure("not committed")

    def test_wrong_sample_hash_fails(self):
        def substitute():
            frame = self.capture_commit()
            frame["sampleSha256"] = "c" * 64
            self.frame(frame)
        self.at_endpoint(substitute)
        self.failure("bytes changed or do not match")

    def rewrite_snapshot(self, mutate):
        frame = self.capture_commit()
        snapshot = json.loads(os.pread(self.final_sample_writer, 4096, 0))
        mutate(snapshot)
        data = (json.dumps(snapshot) + "\n").encode()
        os.ftruncate(self.final_sample_writer, 0)
        os.pwrite(self.final_sample_writer, data, 0)
        os.fsync(self.final_sample_writer)
        frame["sampleBytes"] = len(data)
        frame["sampleSha256"] = hashlib.sha256(data).hexdigest()
        self.frame(frame)

    def test_hash_valid_final_sample_short_of_duration_fails(self):
        self.at_endpoint(lambda: self.rewrite_snapshot(
            lambda value: value["sample"].update(elapsedSeconds=1799)))
        self.failure("duration coverage")

    def test_hash_valid_sample_from_different_session_fails(self):
        self.at_endpoint(lambda: self.rewrite_snapshot(
            lambda value: value.update(recordId="replayed-session")))
        self.failure("matching session")

    def test_finished_sequence_behind_final_heartbeat_fails(self):
        self.at_endpoint(lambda: self.commit(heartbeat_sequence=self.sequence - 1))
        self.failure("differs from the final heartbeat")

    def test_finished_sequence_without_corresponding_heartbeat_fails(self):
        def heartbeat():
            if self.now - 100 <= self.duration:
                self.send()
            if self.now - 100 == self.duration:
                self.commit(heartbeat_sequence=self.sequence + 1)
        self.heartbeat = heartbeat
        self.send()
        self.failure("FINISHED acknowledgement unavailable or incomplete")

    def test_early_finished_cannot_replace_supervisor_coverage(self):
        def heartbeat():
            self.send()
            self.commit(sample={"elapsedSeconds": 1800})
        self.heartbeat = heartbeat
        self.send()
        self.failure("before the declared endpoint")

    def test_partial_finished_frame_times_out(self):
        self.at_endpoint(lambda: os.write(self.finished_writer,
                                         json.dumps(self.capture_commit()).encode()))
        self.failure("FINISHED acknowledgement unavailable")

    def test_oversized_finished_frame_fails(self):
        self.at_endpoint(lambda: os.write(self.finished_writer, b"x" * 1025))
        self.failure("FINISHED frame exceeds")

    def test_duplicate_finished_frames_fail(self):
        def replay():
            frame = self.capture_commit()
            data = (json.dumps(frame) + "\n").encode()
            os.write(self.finished_writer, data + data)
        self.at_endpoint(replay)
        self.failure("exactly one frame")

    def test_duplicate_json_keys_fail(self):
        self.at_endpoint(lambda: os.write(self.finished_writer,
                                         b'{"type":"FINISHED","type":"FINISHED"}\n'))
        self.failure("duplicate keys")

    def test_one_second_finish_delay_preserves_duration_and_cleanup(self):
        self.at_endpoint(self.commit, delay=1)
        receipt = self.run_supervisor()
        self.assertEqual(receipt["requiredSeconds"], 1800)
        self.assertEqual(receipt["observations"][-1]["elapsedSeconds"], 1801)
        self.assertEqual(receipt["finishedAcknowledgement"]["receivedElapsedSeconds"], 1801)
        self.assertEqual(receipt["finishedAcknowledgement"]["finalSample"]["sample"]["elapsedSeconds"], 1801)
        self.assertFalse(receipt["releaseEligible"])
        self.assertEqual(receipt["status"], "COMPLETE")
        self.assert_readers_closed()

    def test_finished_can_arrive_before_its_durable_sample_heartbeat_is_read(self):
        def heartbeat():
            elapsed = self.now - 100
            if elapsed < 1800:
                self.send()
            elif elapsed == 1800:
                self.sequence += 1
                self.commit()
            elif elapsed == 1801:
                os.write(self.writer, f"{self.sequence}\n".encode())
        self.heartbeat = heartbeat
        self.send()
        receipt = self.run_supervisor()
        self.assertEqual(receipt["finishedAcknowledgement"]["receivedElapsedSeconds"], 1800)
        self.assertEqual(receipt["observations"][-1]["elapsedSeconds"], 1801)
        self.assertEqual(receipt["observations"][-1]["heartbeatSequence"], 1801)
        self.assertEqual(receipt["status"], "COMPLETE")
        self.assertFalse(receipt["releaseEligible"])

    def test_fsync_failure_emits_no_finished_and_preserves_partial_file(self):
        self.now = 1900
        self.sequence = 1801
        with mock.patch.object(supervisor.os, "fsync", side_effect=OSError("flush failed")):
            with self.assertRaisesRegex(OSError, "flush failed"):
                self.commit()
        self.assertGreater(os.fstat(self.final_sample_writer).st_size, 0)
        os.set_blocking(self.finished_reader, False)
        with self.assertRaises(BlockingIOError):
            os.read(self.finished_reader, 1024)

    def test_writer_orders_complete_write_and_fsync_before_finished(self):
        self.now = 1900
        self.sequence = 1801
        events = []
        write, fsync = os.write, os.fsync
        def traced_write(fd, data):
            if fd == self.final_sample_writer:
                events.append("sample")
                return write(fd, data[:7])  # exercise partial writes
            events.append("FINISHED" if fd == self.finished_writer else "heartbeat")
            if fd == self.finished_writer:
                self.assertEqual(events[-2], "fsync")
            return write(fd, data)
        def traced_fsync(fd):
            events.append("fsync")
            return fsync(fd)
        with mock.patch.object(supervisor.os, "write", side_effect=traced_write), \
             mock.patch.object(supervisor.os, "fsync", side_effect=traced_fsync):
            self.commit()
            os.write(self.writer, f"{self.sequence}\n".encode())
        self.assertGreater(events.count("sample"), 1)
        self.assertEqual(events[-3:], ["fsync", "FINISHED", "heartbeat"])

    def test_finished_permission_denial_is_not_retried(self):
        read = os.read
        calls = []
        def denied(fd, count):
            if fd == self.finished_reader:
                calls.append(fd)
                raise PermissionError("FINISHED denied")
            return read(fd, count)
        self.send()
        with mock.patch.object(supervisor.os, "read", side_effect=denied):
            self.failure("FINISHED denied")
        self.assertEqual(calls, [self.finished_reader])
        self.assert_readers_closed()

    def test_reader_close_interrupt_still_closes_remaining_descriptors(self):
        close = os.close
        interruption = KeyboardInterrupt("reader close")
        def interrupted(fd):
            close(fd)
            if fd == self.reader:
                raise interruption
        self.at_endpoint(self.commit)
        with mock.patch.object(supervisor.os, "close", side_effect=interrupted):
            with self.assertRaises(KeyboardInterrupt) as caught:
                self.run_supervisor()
        self.assertIs(caught.exception, interruption)
        self.assertEqual(caught.exception.receipt["status"], "FAILED")
        self.assertTrue(self.product.terminated and self.collector.terminated)
        self.assert_readers_closed()

    def test_half_second_polls_receive_finished_within_same_one_second_budget(self):
        def heartbeat():
            elapsed = self.now - 100
            if elapsed < 1800 and elapsed.is_integer():
                self.send()
            if elapsed == 1800.5:
                self.sequence += 1
                self.commit()
                os.write(self.writer, f"{self.sequence}\n".encode())
        self.heartbeat = heartbeat
        self.send()
        receipt = self.run_supervisor(poll_interval_seconds=.5)
        self.assertEqual(receipt["observations"][-1]["elapsedSeconds"], 1800.5)
        self.assertEqual(receipt["endpointAckSeconds"], 1)
        self.assertEqual(receipt["status"], "COMPLETE")

    def test_final_sample_mutation_while_waiting_for_heartbeat_fails(self):
        def heartbeat():
            self.send()
            elapsed = self.now - 100
            if elapsed == 1800:
                self.commit(heartbeat_sequence=self.sequence + 1)
            elif elapsed == 1801:
                os.pwrite(self.final_sample_writer, b"X", 0)
        self.heartbeat = heartbeat
        self.send()
        self.failure("bytes changed or do not match")


if __name__ == "__main__":
    unittest.main()
