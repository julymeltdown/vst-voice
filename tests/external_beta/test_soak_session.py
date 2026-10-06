from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools.external_beta import soak_collector
from tools.external_beta import soak_session as session
from tools.external_beta.product_soak import validate_product_soak


class Clock:
    def __init__(self):
        self.now = 100.0

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class SamplingCadenceTests(unittest.TestCase):
    def latency_does_not_accumulate(self, duration):
        clock = Clock()
        def measure(pid, elapsed, *args):
            clock.sleep(.2)  # nonzero measurement work
            clock.sleep(.3)  # controlled persistence work in the sampling path
            return {"elapsedSeconds": elapsed, "rssBytes": 1234}
        with mock.patch.object(soak_collector, "sample_once", side_effect=measure):
            samples = soak_collector.collect_soak_samples(
                123, duration, 1, clock=clock, sleep=clock.sleep, cpu_clock=lambda pid: 0,
            )
        self.assertEqual(len(samples), duration + 1)
        self.assertEqual([sample["elapsedSeconds"] for sample in samples], list(range(duration + 1)))

    def test_measurement_and_fsync_latency_does_not_drift_thirty_minute_targets(self):
        self.latency_does_not_accumulate(1800)

    def test_measurement_and_fsync_latency_does_not_drift_two_hour_targets(self):
        self.latency_does_not_accumulate(7200)

    def test_callback_latency_is_part_of_absolute_budget_and_preserves_actual_time(self):
        clock, retained = Clock(), []
        def measure(pid, elapsed, *args):
            clock.sleep(.2)
            return {"elapsedSeconds": elapsed, "rssBytes": 1234}
        def persist(sample):
            clock.sleep(.3)
            retained.append((clock.now, dict(sample)))
            sample["rssBytes"] = 9999  # callback cannot rewrite the returned measurements
        with mock.patch.object(soak_collector, "sample_once", side_effect=measure):
            samples = soak_collector.collect_soak_samples(123, 3, 1, cpu_clock=lambda pid: 0,
                clock=clock, sleep=clock.sleep, on_sample=persist, start_monotonic=100,
                maximum_lateness_seconds=1)
        self.assertEqual([sample["elapsedSeconds"] for sample in samples], [0, 1, 2, 3])
        self.assertTrue(all(sample["rssBytes"] == 1234 for sample in samples))
        self.assertEqual([time - 100 for time, sample in retained], [.5, 1.5, 2.5, 3.5])

    def test_measurement_overrun_emits_no_callback_or_catch_up_sample(self):
        clock, callback = Clock(), mock.Mock()
        def measure(*args):
            clock.sleep(1.1)
            return {"elapsedSeconds": 0}
        with mock.patch.object(soak_collector, "sample_once", side_effect=measure) as measured:
            with self.assertRaisesRegex(ValueError, "measurement exceeded"):
                soak_collector.collect_soak_samples(123, 3, 1, cpu_clock=lambda pid: 0,
                    clock=clock, sleep=clock.sleep, on_sample=callback)
        self.assertEqual(measured.call_count, 1)
        callback.assert_not_called()

    def test_missed_wake_fails_without_burst_or_reanchoring(self):
        clock = Clock()
        def delayed_sleep(seconds):
            clock.sleep(seconds + 1.1)
        with mock.patch.object(soak_collector, "sample_once", return_value={"elapsedSeconds": 0}) as measured:
            with self.assertRaisesRegex(ValueError, "missed its absolute target"):
                soak_collector.collect_soak_samples(123, 3, 1, cpu_clock=lambda pid: 0,
                    clock=clock, sleep=delayed_sleep)
        self.assertEqual(measured.call_count, 1)

    def test_callback_failure_aborts_without_returning_success(self):
        clock, failure = Clock(), OSError("fsync failed")
        with mock.patch.object(soak_collector, "sample_once", return_value={"elapsedSeconds": 0}) as measured:
            with self.assertRaises(OSError) as caught:
                soak_collector.collect_soak_samples(123, 3, 1, cpu_clock=lambda pid: 0,
                    clock=clock, sleep=clock.sleep, on_sample=mock.Mock(side_effect=failure))
        self.assertIs(caught.exception, failure)
        self.assertEqual(measured.call_count, 1)

    def test_small_delayed_wake_retains_actual_time_without_rebasing_targets(self):
        clock = Clock()
        def delayed_sleep(seconds):
            clock.sleep(seconds + .125)
        with mock.patch.object(soak_collector, "sample_once", side_effect=lambda *args: {}) as measured:
            samples = soak_collector.collect_soak_samples(123, 3, 1, cpu_clock=lambda pid: 0,
                clock=clock, sleep=delayed_sleep)
        self.assertEqual([sample["elapsedSeconds"] for sample in samples], [0, 1.125, 2.125, 3.125])
        self.assertEqual(measured.call_count, 4)


def identity(duration=1800):
    return {"durationSeconds": duration, "installedTreeSha256": "a" * 64,
            "workloadSha256": "b" * 64, "machineProfileId": "controlled-machine",
            "machineProfileSha256": "c" * 64, "sourceCommit": "d" * 40}


class Child:
    def __init__(self, pid, descriptors=()):
        self.pid, self.descriptors = pid, list(descriptors)
        self.returncode, self.waits, self.killed = None, [], False
        self.after_stop = lambda: None

    def poll(self):
        return self.returncode

    def terminate(self):
        self.returncode = -15
        for fd in self.descriptors:
            os.close(fd)
        self.descriptors = []
        self.after_stop()

    def kill(self):
        self.killed = True
        self.terminate()

    def wait(self, timeout):
        self.waits.append(timeout)
        return self.returncode


class ControlledLauncher:
    """Replay independently planned sampler events; never launch an app or query RSS."""
    def __init__(self, duration=1800, final_cost=.2):
        self.duration, self.final_cost = duration, final_cost
        self.clock, self.worker_clock = Clock(), Clock()
        self.children, self.events, self.calls = [], [], []
        self.fail_worker = None
        self.ready_change = lambda frame: frame
        self.started = False

    def launch(self, command, **options):
        self.calls.append((command, options))
        if not self.children:
            child = Child(123)
            self.children.append(child)
            return child
        if self.fail_worker:
            raise self.fail_worker
        self.directory, self.digest = Path(command[4]), command[5]
        self.manifest = json.loads((self.directory / "session.json").read_bytes())
        self.fds = tuple(os.dup(fd) for fd in options["pass_fds"])
        child = Child(456, self.fds)
        self.children.append(child)
        binding = {"type": "READY", "sessionId": self.manifest["sessionId"],
                   "manifestSha256": self.digest, "productPid": 123}
        ready = self.ready_change(binding)
        if ready is not None:
            session._send(self.fds[4], ready)
        planner = Clock()
        def measurement(pid, elapsed, *args):
            planner.sleep(.2)
            return {"elapsedSeconds": elapsed, "rssBytes": 1234}
        def persistence(sample):
            planner.sleep(.3)
            if sample["elapsedSeconds"] >= self.duration:
                planner.sleep(self.final_cost)
            self.events.append((planner.now, dict(sample)))
        with mock.patch.object(soak_collector, "sample_once", side_effect=measurement):
            soak_collector.collect_soak_samples(123, self.duration, 1, on_sample=persistence,
                clock=planner, sleep=planner.sleep, cpu_clock=lambda pid: 0)
        self.writer = session._SampleWriter(self.directory, self.manifest, self.digest, 123, 456,
            *self.fds[:4], epoch=100, clock=self.worker_clock)
        return child

    def sleep(self, seconds):
        self.clock.sleep(seconds)
        if len(self.children) < 2:
            return
        if not self.started:
            os.set_blocking(self.fds[5], False)
            try:
                data = os.read(self.fds[5], 1024)
            except BlockingIOError:
                return
            if not data:
                return
            go = json.loads(data)
            if go["sessionId"] != self.manifest["sessionId"] or go["manifestSha256"] != self.digest:
                raise ValueError("controlled GO mismatch")
            self.asserted_epoch = go["startMonotonic"]
            self.started = True
        while self.events and self.events[0][0] <= self.clock.now + 1e-9:
            ready_at, sample = self.events.pop(0)
            self.worker_clock.now = ready_at
            self.writer(sample)


class SessionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name) / "packet"
        self.launcher = ControlledLauncher()

    def run_session(self, launcher=None, **overrides):
        launcher = launcher or self.launcher
        options = {"_launch": launcher.launch, "_clock": launcher.clock, "_sleep": launcher.sleep}
        options.update(overrides)
        # Full spans use simulated time and instrumented fsync, not physical persistence proof.
        with mock.patch.object(session.os, "fsync"), mock.patch.object(session.os, "getpid", return_value=789):
            return session.run_engineering_soak_session(["controlled-product"], identity(launcher.duration),
                                                        self.output, **options)

    def assert_cleaned(self, launcher=None):
        for child in (launcher or self.launcher).children:
            self.assertIsNotNone(child.returncode)
            self.assertTrue(child.waits)
            self.assertEqual(child.descriptors, [])

    def successful_packet(self, duration):
        launcher = ControlledLauncher(duration)
        read = session._read_regular
        def after_cleanup(path, limit):
            if path.name in ("samples.jsonl", "final-sample.json", "supervision.json"):
                self.assert_cleaned(launcher)
            return read(path, limit)
        with mock.patch.object(session, "_read_regular", side_effect=after_cleanup):
            result = self.run_session(launcher)
        self.assert_cleaned(launcher)
        self.assertEqual(result["status"], "COMPLETE")
        self.assertFalse(result["releaseEligible"])
        self.assertEqual(launcher.asserted_epoch, 100)
        receipt = json.loads((self.output / "supervision.json").read_bytes())
        self.assertEqual(receipt["pollIntervalSeconds"], .1)
        received = receipt["finishedAcknowledgement"]["receivedElapsedSeconds"]
        self.assertGreater(received, duration)
        self.assertLess(received, duration + 1)
        rows = [json.loads(line) for line in (self.output / "samples.jsonl").read_bytes().splitlines()]
        self.assertEqual(len(rows), duration + 1)
        self.assertEqual(rows[-1]["sample"], receipt["finishedAcknowledgement"]["finalSample"]["sample"])
        index_bytes = (self.output / "packet-index.json").read_bytes()
        self.assertEqual(hashlib.sha256(index_bytes).hexdigest(), result["packetIndexSha256"])
        for name, ref in json.loads(index_bytes)["files"].items():
            data = (self.output / name).read_bytes()
            self.assertEqual(len(data), ref["bytes"])
            self.assertEqual(hashlib.sha256(data).hexdigest(), ref["sha256"])
        self.assertFalse(validate_product_soak(json.loads((self.output / "session.json").read_bytes()), self.output).passed)

    def test_owned_thirty_minute_packet_and_final_latency_with_point_one_poll(self):
        self.successful_packet(1800)

    def test_owned_two_hour_packet_and_final_latency_with_point_one_poll(self):
        self.successful_packet(7200)

    def test_worker_launch_failure_reaps_already_owned_product_and_retains_partial_packet(self):
        failure = OSError("worker launch failed")
        self.launcher.fail_worker = failure
        with self.assertRaises(OSError) as caught:
            self.run_session()
        self.assertIs(caught.exception, failure)
        self.assert_cleaned()
        self.assertEqual(caught.exception.session_result["status"], "FAILED")
        self.assertTrue((self.output / "session.json").is_file())
        self.assertFalse((self.output / "supervision.json").exists())

    def test_wrong_ready_session_fails_before_ownership_transfer(self):
        self.launcher.ready_change = lambda value: {**value, "sessionId": "wrong-session"}
        with self.assertRaisesRegex(ValueError, "READY session binding"):
            self.run_session()
        self.assert_cleaned()
        self.assertFalse(self.launcher.started)

    def test_missing_ready_times_out_and_cleans_both_children(self):
        self.launcher.ready_change = lambda value: None
        with self.assertRaisesRegex(ValueError, "deadline expired"):
            self.run_session()
        self.assert_cleaned()
        self.assertLessEqual(self.launcher.clock.now, 105.01)

    def test_existing_packet_is_unchanged_and_launch_is_not_attempted(self):
        self.output.mkdir()
        existing = self.output / "user.txt"
        existing.write_bytes(b"keep these bytes")
        with self.assertRaises(FileExistsError):
            self.run_session()
        self.assertEqual(existing.read_bytes(), b"keep these bytes")
        self.assertEqual(self.launcher.calls, [])

    def test_caller_cannot_supply_physical_authority(self):
        launch = mock.Mock()
        with self.assertRaisesRegex(ValueError, "authority fields"):
            session.run_engineering_soak_session(["controlled"], {**identity(), "releaseEligible": True},
                                                 self.output, _launch=launch)
        launch.assert_not_called()
        self.assertFalse(self.output.exists())

    def test_observation_interrupt_is_retained_after_cleanup_and_raw_receipt_persistence(self):
        interruption = KeyboardInterrupt("controlled cancellation")
        with self.assertRaises(KeyboardInterrupt) as caught:
            self.run_session(_sleep=mock.Mock(side_effect=interruption))
        self.assertIs(caught.exception, interruption)
        self.assert_cleaned()
        self.assertEqual(caught.exception.session_result["status"], "FAILED")
        receipt = json.loads((self.output / "supervision.json").read_bytes())
        self.assertEqual(receipt, interruption.receipt)
        self.assertEqual(receipt["status"], "FAILED")

    def test_raw_sample_substitution_fails_without_rewriting_complete_supervisor_receipt(self):
        launch = self.launcher.launch
        def changed_launch(command, **options):
            child = launch(command, **options)
            if child.pid == 456:
                def substitute():
                    path = self.output / "samples.jsonl"
                    rows = [json.loads(line) for line in path.read_bytes().splitlines()]
                    rows[-1]["sample"]["rssBytes"] = 9999
                    path.write_bytes(b"".join((json.dumps(row) + "\n").encode() for row in rows))
                child.after_stop = substitute
            return child
        with self.assertRaisesRegex(ValueError, "final sample differs") as caught:
            self.run_session(_launch=changed_launch)
        self.assert_cleaned()
        self.assertEqual(caught.exception.session_result["status"], "FAILED")
        self.assertEqual(json.loads((self.output / "supervision.json").read_bytes())["status"], "COMPLETE")

    def test_hash_permission_denial_is_not_retried_or_sealed(self):
        read, denied = session._read_regular, []
        def deny_raw(path, limit):
            if path.name == "samples.jsonl":
                self.assert_cleaned()
                denied.append(path)
                raise PermissionError("raw hash denied")
            return read(path, limit)
        with mock.patch.object(session, "_read_regular", side_effect=deny_raw):
            with self.assertRaises(PermissionError) as caught:
                self.run_session()
        self.assertEqual(len(denied), 1)
        self.assertEqual(caught.exception.session_result["status"], "FAILED")
        self.assertFalse((self.output / "packet-index.json").exists())

    def test_wrong_raw_manifest_is_rejected_after_cleanup(self):
        launch = self.launcher.launch
        def changed_launch(command, **options):
            child = launch(command, **options)
            if child.pid == 456:
                def substitute():
                    path = self.output / "samples.jsonl"
                    rows = path.read_bytes().splitlines(keepends=True)
                    row = json.loads(rows[1])
                    row["manifestSha256"] = "f" * 64
                    rows[1] = session._encode(row, session.MAX_ROW)
                    path.write_bytes(b"".join(rows))
                child.after_stop = substitute
            return child
        with self.assertRaisesRegex(ValueError, "raw sample session or sequence"):
            self.run_session(_launch=changed_launch)
        self.assert_cleaned()
        self.assertEqual(json.loads((self.output / "supervision.json").read_bytes())["status"], "COMPLETE")
        self.assertEqual(json.loads((self.output / "packet-index.json").read_bytes())["status"], "FAILED")

    def test_index_fsync_failure_leaves_no_committed_index(self):
        write_new = session._write_new
        def failed_index(path, value, limit=session.MAX_METADATA):
            if path.name == "packet-index.pending.json":
                self.assert_cleaned()
                path.write_bytes(session._encode(value, limit))
                raise OSError("index fsync failed")
            return write_new(path, value, limit)
        with mock.patch.object(session, "_write_new", side_effect=failed_index):
            with self.assertRaisesRegex(OSError, "index fsync failed") as caught:
                self.run_session()
        self.assertEqual(caught.exception.session_result["status"], "FAILED")
        self.assertTrue((self.output / "packet-index.pending.json").exists())
        self.assertFalse((self.output / "packet-index.json").exists())

    def test_denied_startup_cleanup_does_not_try_kill_or_wait_for_that_child(self):
        self.launcher.fail_worker = OSError("worker launch failed")
        launch = self.launcher.launch
        def denied_launch(command, **options):
            child = launch(command, **options)
            child.terminate = mock.Mock(side_effect=PermissionError("terminate denied"))
            child.kill = mock.Mock()
            return child
        with self.assertRaisesRegex(OSError, "worker launch failed") as caught:
            self.run_session(_launch=denied_launch)
        child = self.launcher.children[0]
        child.terminate.assert_called_once()
        child.kill.assert_not_called()
        self.assertEqual(child.waits, [])
        self.assertTrue(caught.exception.session_result["cleanupErrors"])
        self.assertFalse((self.output / "packet-index.json").exists())

    def test_startup_cleanup_interrupt_is_preserved_and_other_child_is_reaped(self):
        self.launcher.ready_change = lambda value: {**value, "sessionId": "wrong-session"}
        launch, interruption = self.launcher.launch, KeyboardInterrupt("cleanup cancelled")
        def interrupted_launch(command, **options):
            child = launch(command, **options)
            if child.pid == 123:
                terminate = child.terminate
                def interrupted_terminate():
                    terminate()
                    raise interruption
                child.terminate = interrupted_terminate
            return child
        with self.assertRaises(KeyboardInterrupt) as caught:
            self.run_session(_launch=interrupted_launch)
        self.assertIs(caught.exception, interruption)
        self.assert_cleaned()
        self.assertTrue(caught.exception.session_result["cleanupErrors"])
        self.assertFalse((self.output / "packet-index.json").exists())


class WorkerStartupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory, self.clock = Path(self.temp.name), Clock()
        self.manifest = {**identity(), "schemaVersion": 1, "sessionId": "controlled-session",
                         "recordType": "external-beta-engineering-soak-session",
                         "evidenceScope": "engineering", "releaseEligible": False}
        data = session._write_new(self.directory / "session.json", self.manifest)
        self.digest = hashlib.sha256(data).hexdigest()
        self.fds = []
        for name in ("samples.jsonl", "final-sample.json"):
            self.fds.append(os.open(self.directory / name, os.O_CREAT | os.O_EXCL | os.O_RDWR, 0o600))
        self.hb_read, hb_write = os.pipe()
        self.fin_read, fin_write = os.pipe()
        self.ready_read, ready_write = os.pipe()
        self.go_read, self.go_write = os.pipe()
        self.worker_fds = (*self.fds, hb_write, fin_write, ready_write, self.go_read)
        self.fds.extend((hb_write, fin_write, ready_write, self.go_read,
                         self.hb_read, self.fin_read, self.ready_read, self.go_write))

    def tearDown(self):
        for fd in self.fds:
            os.close(fd)

    def go(self, **changes):
        session._send(self.go_write, {"type": "GO", "sessionId": self.manifest["sessionId"],
            "manifestSha256": self.digest, "productPid": 123, "startMonotonic": 100, **changes})

    def run_worker(self, collect):
        session._run_worker(self.directory, self.digest, 123, *self.worker_fds,
                            clock=self.clock, sleep=self.clock.sleep, collect=collect)

    def no_progress(self):
        for fd in (self.hb_read, self.fin_read):
            os.set_blocking(fd, False)
            with self.assertRaises(BlockingIOError):
                os.read(fd, 1024)
        self.assertFalse((self.directory / "worker-result.json").exists())

    def test_wrong_go_binding_prevents_measurement(self):
        self.go(sessionId="replayed-session")
        collect = mock.Mock()
        with self.assertRaisesRegex(ValueError, "GO session binding"):
            self.run_worker(collect)
        collect.assert_not_called()
        self.no_progress()

    def test_missing_go_is_bounded_and_prevents_measurement(self):
        collect = mock.Mock()
        with self.assertRaisesRegex(ValueError, "deadline expired"):
            self.run_worker(collect)
        collect.assert_not_called()
        self.assertLessEqual(self.clock.now, 105.01)
        self.no_progress()

    def test_changed_manifest_prevents_ready_and_measurement(self):
        (self.directory / "session.json").write_bytes(b"{}\n")
        collect = mock.Mock()
        with self.assertRaisesRegex(ValueError, "manifest bytes"):
            self.run_worker(collect)
        collect.assert_not_called()
        os.set_blocking(self.ready_read, False)
        with self.assertRaises(BlockingIOError):
            os.read(self.ready_read, 1024)
        self.no_progress()

    def test_unavailable_measurement_is_preserved_without_fallback_or_heartbeat(self):
        self.go()
        failure = soak_collector.ProcessMeasurementError(123, "controlled denial")
        collect = mock.Mock(side_effect=failure)
        with self.assertRaises(soak_collector.ProcessMeasurementError) as caught:
            self.run_worker(collect)
        self.assertIs(caught.exception, failure)
        collect.assert_called_once()
        self.assertIn("controlled denial", json.loads((self.directory / "worker-error.json").read_bytes())["error"])
        self.no_progress()


class WriterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory, self.clock = Path(self.temp.name), Clock()
        self.fds = []
        for name in ("samples.jsonl", "final-sample.json"):
            self.fds.append(os.open(self.directory / name, os.O_CREAT | os.O_EXCL | os.O_RDWR, 0o600))
        self.hb_read, hb_write = os.pipe()
        self.fin_read, fin_write = os.pipe()
        self.fds.extend((hb_write, fin_write, self.hb_read, self.fin_read))
        self.manifest = {**identity(), "sessionId": "controlled-session"}
        self.writer = session._SampleWriter(self.directory, self.manifest, "e" * 64, 123, 456,
                                           *self.fds[:4], epoch=100, clock=self.clock)

    def tearDown(self):
        for fd in self.fds:
            os.close(fd)

    def no_frame(self, fd):
        os.set_blocking(fd, False)
        with self.assertRaises(BlockingIOError):
            os.read(fd, 1024)

    def test_actual_raw_fsync_completes_before_heartbeat_and_handles_partial_writes(self):
        write, fsync = os.write, os.fsync
        def partial(fd, data):
            return write(fd, data[:7] if fd == self.fds[0] else data)
        def durable(fd):
            self.no_frame(self.hb_read)
            self.assertGreater(os.fstat(fd).st_size, 0)
            return fsync(fd)
        with mock.patch.object(session.os, "write", side_effect=partial), \
             mock.patch.object(session.os, "fsync", side_effect=durable):
            self.writer({"elapsedSeconds": 0, "rssBytes": 1234})
        self.assertEqual(os.read(self.hb_read, 1024), b"1\n")
        self.assertEqual(json.loads((self.directory / "samples.jsonl").read_bytes())["sequence"], 1)

    def test_failed_fsync_leaves_partial_bytes_and_emits_no_progress(self):
        failure = OSError("controlled fsync failure")
        with mock.patch.object(session.os, "fsync", side_effect=failure):
            with self.assertRaises(OSError) as caught:
                self.writer({"elapsedSeconds": 0})
        self.assertIs(caught.exception, failure)
        self.assertGreater((self.directory / "samples.jsonl").stat().st_size, 0)
        self.no_frame(self.hb_read)
        self.no_frame(self.fin_read)

    def test_raw_fsync_overrun_emits_no_heartbeat_or_catch_up(self):
        with mock.patch.object(session.os, "fsync", side_effect=lambda fd: self.clock.sleep(1.1)):
            with self.assertRaisesRegex(ValueError, "absolute deadline"):
                self.writer({"elapsedSeconds": 0})
        self.assertEqual(self.writer.sequence, 0)
        self.no_frame(self.hb_read)
        self.no_frame(self.fin_read)

    def test_failed_final_fsync_emits_no_finished_or_final_heartbeat(self):
        self.writer.sequence = 1800
        self.clock.now = 1900.2
        def durable(fd):
            if fd == self.fds[1]:
                raise OSError("final fsync failed")
        with mock.patch.object(session.os, "fsync", side_effect=durable):
            with self.assertRaisesRegex(OSError, "final fsync failed"):
                self.writer({"elapsedSeconds": 1800.2})
        self.assertGreater((self.directory / "final-sample.json").stat().st_size, 0)
        self.no_frame(self.hb_read)
        self.no_frame(self.fin_read)

    def test_late_final_fsync_cannot_justify_final_heartbeat(self):
        self.writer.sequence = 1800
        self.clock.now = 1900.2
        def durable(fd):
            if fd == self.fds[1]:
                self.clock.sleep(.9)
        with mock.patch.object(session.os, "fsync", side_effect=durable):
            with self.assertRaisesRegex(ValueError, "final sample commit exceeded"):
                self.writer({"elapsedSeconds": 1800.2})
        self.assertFalse(self.writer.complete)
        self.no_frame(self.hb_read)
        self.assertEqual(json.loads(os.read(self.fin_read, 1024))["type"], "FINISHED")


if __name__ == "__main__":
    unittest.main()
