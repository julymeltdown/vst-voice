from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from tools.external_beta.soak_supervisor import SupervisionError, commit_final_sample, supervise_soak


class Child:
    def __init__(self, pid: int):
        self.pid = pid
        self.returncode = None
        self.terminated = False
        self.killed = False
        self.waits = []

    def poll(self):
        return self.returncode

    def terminate(self):
        self.terminated = True
        self.returncode = -15

    def kill(self):
        self.killed = True
        self.returncode = -9

    def wait(self, timeout):
        self.waits.append(timeout)
        return self.returncode


class SoakSupervisorTests(unittest.TestCase):
    def setUp(self):
        self.reader, self.writer = os.pipe()
        self.finished_reader, self.finished_writer = os.pipe()
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.final_sample_reader = os.open(os.path.join(directory.name, "final.json"), os.O_CREAT | os.O_EXCL | os.O_RDWR, 0o600)
        self.final_sample_writer = os.dup(self.final_sample_reader)
        self.product = Child(123)
        self.collector = Child(456)
        self.now = 100.0
        self.sequence = 0
        self.duration = 1800
        self.final_committed = False
        self.auto_commit = True
        self.heartbeat = lambda: self.send()

    def tearDown(self):
        for descriptor in (self.reader, self.writer, self.finished_reader, self.finished_writer,
                           self.final_sample_reader, self.final_sample_writer):
            try:
                os.close(descriptor)
            except OSError:
                pass

    def send(self, value=None):
        self.sequence += 1
        if self.auto_commit and self.now - 100 >= self.duration and not self.final_committed:
            commit_final_sample(self.final_sample_writer, self.finished_writer,
                                record_id="fixture-soak", duration_seconds=self.duration,
                                heartbeat_sequence=self.sequence,
                                sample={"elapsedSeconds": self.now - 100, "rssBytes": 1234})
            self.final_committed = True
        os.write(self.writer, f"{self.sequence if value is None else value}\n".encode())

    def sleep(self, seconds):
        self.now += seconds
        self.heartbeat()

    def run_supervisor(self, **overrides):
        options = dict(record_id="fixture-soak", installed_tree_sha256="a" * 64,
                       workload_sha256="b" * 64, duration_seconds=1800,
                       finished_fd=self.finished_reader, final_sample_fd=self.final_sample_reader,
                       clock=lambda: self.now, sleep=self.sleep)
        options.update(overrides)
        self.duration = options["duration_seconds"]
        with mock.patch("tools.external_beta.soak_supervisor.os.getpid", return_value=789):
            return supervise_soak(self.product, self.collector, self.reader, **options)

    def failure(self, message, **overrides):
        with self.assertRaisesRegex(SupervisionError, message) as caught:
            self.run_supervisor(**overrides)
        receipt = caught.exception.receipt
        self.assertEqual(receipt["status"], "FAILED")
        self.assertFalse(receipt["releaseEligible"])
        self.assertIsNotNone(self.product.returncode)
        self.assertIsNotNone(self.collector.returncode)
        self.assertTrue(self.product.waits)
        self.assertTrue(self.collector.waits)
        return receipt

    def test_complete_durations_require_independent_observations_and_cleanup(self):
        self.send()
        receipt = self.run_supervisor()
        self.assertEqual(receipt["status"], "COMPLETE")
        self.assertEqual(receipt["requiredSeconds"], 1800)
        self.assertEqual(receipt["observations"][0]["elapsedSeconds"], 0)
        self.assertEqual(receipt["observations"][-1]["elapsedSeconds"], 1800)
        self.assertEqual(len(receipt["observations"]), 1801)
        self.assertEqual(receipt["observations"][-1]["heartbeatSequence"], 1801)
        self.assertEqual(receipt["installedTreeSha256"], "a" * 64)
        self.assertEqual(receipt["workloadSha256"], "b" * 64)
        self.assertEqual(receipt["evidenceScope"], "engineering")
        self.assertEqual(receipt["clockAuthority"], "supervisor-monotonic")
        self.assertFalse(receipt["releaseEligible"])
        self.assertTrue(self.product.terminated and self.collector.terminated)
        self.assertTrue(self.product.waits and self.collector.waits)

    def test_two_hour_fixture_covers_the_full_declared_endpoint(self):
        self.send()
        receipt = self.run_supervisor(duration_seconds=7200)
        self.assertEqual(receipt["observations"][-1]["elapsedSeconds"], 7200)
        self.assertEqual(len(receipt["observations"]), 7201)

    def test_missing_and_stale_heartbeat_fail_while_children_are_alive(self):
        self.heartbeat = lambda: None
        self.failure("heartbeat unavailable or stale")

    def test_initial_heartbeat_does_not_hide_a_stalled_collector(self):
        self.send()
        self.heartbeat = lambda: None
        receipt = self.failure("heartbeat unavailable or stale")
        self.assertLess(receipt["observations"][-1]["elapsedSeconds"], 1800)

    def test_five_second_heartbeats_do_not_satisfy_the_one_second_contract(self):
        self.send()
        self.heartbeat = lambda: self.send() if (self.now - 100) % 5 == 0 else None
        self.failure("one-second cadence")

    def test_one_interval_startup_lateness_still_requires_full_cadence(self):
        receipt = self.run_supervisor()
        self.assertEqual(receipt["observations"][-1]["heartbeatSequence"], 1800)
        self.assertEqual(receipt["heartbeatIntervalSeconds"], 1)
        self.assertEqual(receipt["heartbeatLatenessSeconds"], 1)

    def test_product_exit_is_failure_even_with_fresh_heartbeat(self):
        self.send()
        def exit_product():
            self.send()
            self.product.returncode = 0
        self.heartbeat = exit_product
        self.failure("product exited")

    def test_collector_exit_is_failure(self):
        self.collector.returncode = 0
        with self.assertRaisesRegex(SupervisionError, "collector exited"):
            self.run_supervisor()
        self.assertTrue(self.product.terminated)
        self.assertTrue(self.collector.waits)

    def test_closed_heartbeat_pipe_is_not_a_successful_end_of_collection(self):
        os.close(self.writer)
        self.writer = -1
        self.failure("heartbeat pipe closed")

    def test_malformed_duplicate_skipped_and_fast_heartbeats_fail(self):
        os.close(self.reader)
        for payload in (b"invalid\n", b"0\n", b"-1\n", b"1.5\n", b"1\n1\n",
                        b"2\n", b"1\n2\n3\n", b"1" * 21):
            with self.subTest(payload=payload):
                for descriptor in (self.finished_reader, self.finished_writer,
                                   self.final_sample_reader, self.final_sample_writer):
                    try:
                        os.close(descriptor)
                    except OSError:
                        pass
                self.finished_reader, self.finished_writer = os.pipe()
                directory = tempfile.TemporaryDirectory()
                self.addCleanup(directory.cleanup)
                self.final_sample_reader = os.open(os.path.join(directory.name, "final.json"),
                                                   os.O_CREAT | os.O_EXCL | os.O_RDWR, 0o600)
                self.final_sample_writer = os.dup(self.final_sample_reader)
                reader, writer = os.pipe()
                self.reader = reader
                self.product, self.collector = Child(123), Child(456)
                os.write(writer, payload)
                try:
                    self.failure("heartbeat")
                finally:
                    os.close(writer)

    def test_clock_reversal_and_supervisor_observation_gap_fail(self):
        self.send()
        self.heartbeat = lambda: setattr(self, "now", self.now - 2)
        self.failure("clock must advance")

    def test_clock_jump_cannot_turn_a_short_run_into_completion(self):
        self.send()
        self.heartbeat = lambda: setattr(self, "now", self.now + 1800)
        self.failure("supervisor observation gap")

    def test_delayed_first_observation_cannot_replace_the_full_observed_span(self):
        self.send()
        readings = iter((100.0, 1900.0))
        self.failure("initial observation", clock=lambda: next(readings))

    def test_fast_polling_cannot_accumulate_unbounded_observations(self):
        self.send()
        self.failure("declared cadence", sleep=lambda seconds: setattr(self, "now", self.now + .001))

    def test_invalid_profiles_budgets_identities_and_pids_do_not_touch_children(self):
        for field, values in {
            "duration_seconds": (True, 5, 1799, 7200.0, float("nan")),
            "poll_interval_seconds": (0, 0.01, 2, True, float("inf")),
            "max_gap_seconds": (0, 6, True, float("nan")),
            "record_id": ("", None),
            "installed_tree_sha256": ("bad", None),
            "workload_sha256": ("bad", None),
        }.items():
            for value in values:
                with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                    self.run_supervisor(**{field: value})
        for pid in (0, -1, True, 456, 789):
            self.product.pid = pid
            with self.subTest(pid=pid), self.assertRaises(ValueError):
                self.run_supervisor()
        self.assertFalse(self.product.terminated or self.collector.terminated)
        self.assertEqual(self.product.waits + self.collector.waits, [])

    def test_observation_failure_keeps_reason_and_cleans_up(self):
        with mock.patch("tools.external_beta.soak_supervisor.os.read", side_effect=PermissionError("denied")):
            self.failure("denied")

    def test_unresponsive_child_is_killed_and_reaped_after_bounded_wait(self):
        original_wait = self.product.wait
        def wait(timeout):
            if not self.product.killed:
                raise subprocess.TimeoutExpired("fixture", timeout)
            return original_wait(timeout)
        self.product.wait = wait
        self.heartbeat = lambda: None
        self.failure("heartbeat unavailable or stale")
        self.assertTrue(self.product.killed)
        self.assertTrue(all(timeout <= 2 for timeout in self.product.waits))

    def test_cleanup_failure_prevents_a_complete_receipt(self):
        self.send()
        self.product.wait = mock.Mock(side_effect=OSError("cannot reap"))
        with self.assertRaisesRegex(SupervisionError, "cleanup failed") as caught:
            self.run_supervisor()
        self.assertTrue(caught.exception.receipt["cleanupErrors"])
        self.assertEqual(caught.exception.receipt["status"], "FAILED")

    def test_cleanup_permission_denial_does_not_attempt_another_signal_route(self):
        self.product.terminate = mock.Mock(side_effect=PermissionError("denied"))
        self.product.kill = mock.Mock()
        self.product.wait = mock.Mock()
        self.heartbeat = lambda: None
        with self.assertRaisesRegex(SupervisionError, "cleanup failed") as caught:
            self.run_supervisor()
        self.product.kill.assert_not_called()
        self.product.wait.assert_not_called()
        self.assertIn("permission denied", caught.exception.receipt["cleanupErrors"][0])
        self.assertTrue(self.collector.terminated)

    def test_missing_heartbeat_stops_and_reaps_real_controlled_children(self):
        command = [sys.executable, "-c", "import time; time.sleep(30)"]
        children = []
        try:
            for _ in range(2):
                children.append(subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
            with self.assertRaisesRegex(SupervisionError, "heartbeat unavailable or stale") as caught:
                supervise_soak(*children, self.reader, record_id="controlled-timeout",
                               installed_tree_sha256="a" * 64, workload_sha256="b" * 64,
                               duration_seconds=1800, poll_interval_seconds=.1, max_gap_seconds=1,
                               finished_fd=self.finished_reader, final_sample_fd=self.final_sample_reader)
            self.assertTrue(all(child.poll() is not None for child in children))
            self.assertLess(caught.exception.receipt["observations"][-1]["elapsedSeconds"], 2)
            self.assertFalse(caught.exception.receipt["releaseEligible"])
        finally:
            for child in children:
                if child.poll() is None:
                    child.kill()
                child.wait(timeout=2)


class SoakSupervisorInterruptTests(unittest.TestCase):
    def cleanup_interrupt(self, stage):
        reader, writer = os.pipe()
        finished_reader, finished_writer = os.pipe()
        final_sample, sample_path = tempfile.mkstemp()
        product, collector = Child(123), Child(456)
        interruption = KeyboardInterrupt(stage)
        now = [100.0]
        def sleep(seconds):
            now[0] += seconds
        if stage == "terminate":
            terminate = product.terminate
            def interrupted_terminate():
                terminate()
                raise interruption
            product.terminate = interrupted_terminate
        elif stage in ("wait", "observation"):
            cleanup_interruption = KeyboardInterrupt("cleanup wait") if stage == "observation" else interruption
            product.wait = mock.Mock(side_effect=[cleanup_interruption, -9])
            if stage == "observation":
                def sleep(seconds):
                    raise interruption
        else:
            product.wait = mock.Mock(side_effect=[subprocess.TimeoutExpired("fixture", 2),
                                                interruption if stage == "reap" else -9])
            if stage == "kill":
                kill = product.kill
                def interrupted_kill():
                    kill()
                    raise interruption
                product.kill = interrupted_kill
        try:
            with self.assertRaises(KeyboardInterrupt) as caught:
                supervise_soak(product, collector, reader, record_id="interrupt-fixture",
                               installed_tree_sha256="a" * 64, workload_sha256="b" * 64,
                               duration_seconds=1800, clock=lambda: now[0], sleep=sleep,
                               finished_fd=finished_reader, final_sample_fd=final_sample)
            self.assertIs(caught.exception, interruption)
            self.assertTrue(collector.terminated)
            self.assertTrue(collector.waits)
            with self.assertRaises(OSError):
                os.fstat(reader)
            for descriptor in (finished_reader, final_sample):
                with self.assertRaises(OSError):
                    os.fstat(descriptor)
            self.assertEqual(caught.exception.receipt["status"], "FAILED")
            self.assertFalse(caught.exception.receipt["releaseEligible"])
            expected_failure = "observation" if stage == "observation" else "heartbeat unavailable or stale"
            self.assertIn(expected_failure, caught.exception.receipt["failure"])
            if stage in ("wait", "kill", "reap", "observation"):
                self.assertEqual(product.wait.call_count, 2)
        finally:
            for descriptor in (reader, writer, finished_reader, finished_writer, final_sample):
                try:
                    os.close(descriptor)
                except OSError:
                    pass
            os.unlink(sample_path)

    def test_terminate_interrupt_still_cleans_second_child_and_reader(self):
        self.cleanup_interrupt("terminate")

    def test_wait_interrupt_still_cleans_second_child_and_reader(self):
        self.cleanup_interrupt("wait")

    def test_kill_interrupt_still_attempts_reap_and_cleans_second_child_and_reader(self):
        self.cleanup_interrupt("kill")

    def test_reap_interrupt_still_cleans_second_child_and_reader(self):
        self.cleanup_interrupt("reap")

    def test_observation_interrupt_is_not_replaced_by_a_later_cleanup_interrupt(self):
        self.cleanup_interrupt("observation")


if __name__ == "__main__":
    unittest.main()
