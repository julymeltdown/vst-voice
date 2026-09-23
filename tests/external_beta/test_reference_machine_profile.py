"""Coverage for the reference-machine profile collector.

The full-product evaluation profile requires a typed machine profile per declared
platform, and four empirical criteria carry it as a binding. Until this collector
existed, the only producer of that shape was a test fixture, so those criteria
were unreachable rather than unmeasured. These cases check the collector against
the contract's own validator rather than against a restatement of it.
"""
from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from scripts.collect_reference_machine_profile import (  # noqa: E402
    PLATFORM_CELLS,
    canonical_json,
    collect,
    sha256_json,
)
from tools.external_beta.full_product_contract_empirical import _machine_errors  # noqa: E402
from tools.external_beta.release_gate import sha256_json as gate_sha256_json  # noqa: E402

COLLECTOR = ROOT / "scripts" / "collect_reference_machine_profile.py"


class ReferenceMachineProfileTests(unittest.TestCase):
    def _host_platform(self) -> str:
        """The one cell this host can honestly claim, for tests that need a real run."""
        import platform

        machine = platform.machine().lower()
        for cell, expected in PLATFORM_CELLS.items():
            if expected["machine"] == machine:
                return cell
        self.skipTest(f"host machine {machine!r} matches no declared platform cell")
        raise AssertionError("unreachable")

    def test_collected_profile_satisfies_the_contract_validator(self) -> None:
        cell = self._host_platform()
        payload = collect(cell, "c++", "test-operator")
        # The validator wants the nested profile as a content-bound reference, so
        # bind the digest the collector computed and confirm it is accepted.
        value = dict(payload["profile"])
        value["profile"] = {"locator": "/tmp/profile.json", "sha256": payload["profileSha256"]}
        self.assertEqual(_machine_errors(value, cell), [])

    def test_digest_convention_matches_the_release_gate(self) -> None:
        # Two canonicalisers in one repository is how a digest silently stops
        # verifying, so the collector's must agree with the gate's.
        payload = collect(self._host_platform(), "c++", "test-operator")
        self.assertEqual(payload["profileSha256"], gate_sha256_json(payload["profile"]))
        self.assertEqual(sha256_json(payload["profile"]), gate_sha256_json(payload["profile"]))

    def test_a_changed_field_changes_the_digest(self) -> None:
        payload = collect(self._host_platform(), "c++", "test-operator")
        body = copy.deepcopy(payload["profile"])
        for field, replacement in (
            ("cpuModel", "other-cpu"),
            ("logicalCpuCount", body["logicalCpuCount"] + 1),
            ("ramBytes", body["ramBytes"] + 1),
            ("osVersion", "0"),
            ("toolchainId", "other-toolchain"),
        ):
            with self.subTest(field=field):
                tampered = copy.deepcopy(body)
                tampered[field] = replacement
                self.assertNotEqual(sha256_json(tampered), payload["profileSha256"])

    def test_unresolved_identity_fields_are_refused_not_placed_holder(self) -> None:
        # A profile with a fabricated toolchain is worse than no profile, because
        # it looks complete. An unusable compiler must be an error.
        with self.assertRaises(ValueError):
            collect(self._host_platform(), "definitely-not-a-compiler", "test-operator")
        with self.assertRaises(ValueError):
            collect(self._host_platform(), "c++", "   ")

    def test_platform_cell_is_never_inferred_and_must_match_the_host(self) -> None:
        # The contract's cells are macos-arm64 and windows-x86_64. Claiming the
        # wrong one must fail rather than silently binding evidence to a cell the
        # host cannot represent.
        host = self._host_platform()
        other = next(cell for cell in PLATFORM_CELLS if cell != host)
        with self.assertRaises(ValueError):
            collect(other, "c++", "test-operator")
        with self.assertRaises(ValueError):
            collect("linux-x86_64", "c++", "test-operator")

    def test_collector_is_not_evidence_and_does_not_claim_to_be(self) -> None:
        payload = collect(self._host_platform(), "c++", "test-operator")
        self.assertFalse(payload["singerQualified"])
        self.assertFalse(payload["releaseEligible"])
        self.assertFalse(payload["trainingAdmitted"])
        # A machine profile is hardware identity. It must say so.
        self.assertTrue(payload["declared"])
        self.assertFalse(payload["measured"])

    def test_cli_writes_a_new_file_and_refuses_an_occupied_destination(self) -> None:
        cell = self._host_platform()
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "machine.json"
            first = subprocess.run(
                [sys.executable, str(COLLECTOR), "--platform", cell, "--compiler", "c++",
                 "--actor", "test-operator", "--output", str(destination)],
                capture_output=True, text=True, timeout=180, check=False)
            self.assertEqual(first.returncode, 0, first.stderr)
            written = json.loads(destination.read_text())
            self.assertEqual(written["platform"], cell)
            inside = dict(written["profile"])
            inside["profile"] = {"locator": str(destination), "sha256": written["profileSha256"]}
            self.assertEqual(_machine_errors(inside, cell), [])

            # A second run must not replace a profile silently: two runs on
            # different machines would otherwise be indistinguishable afterwards.
            second = subprocess.run(
                [sys.executable, str(COLLECTOR), "--platform", cell, "--compiler", "c++",
                 "--actor", "test-operator", "--output", str(destination)],
                capture_output=True, text=True, timeout=180, check=False)
            self.assertEqual(second.returncode, 3)
            self.assertEqual(json.loads(destination.read_text()), written)

    def test_toolchain_banner_is_single_line_and_bounded(self) -> None:
        payload = collect(self._host_platform(), "c++", "test-operator")
        toolchain = payload["profile"]["toolchainId"]
        self.assertTrue(toolchain)
        self.assertNotIn("\n", toolchain)
        self.assertLessEqual(len(toolchain), 64 * 1024)

    def test_canonical_form_is_stable_across_key_order(self) -> None:
        # Key order must not change the digest, or a re-serialised profile would
        # stop verifying against the reference that pointed at it.
        one = {"b": 1, "a": 2}
        two = {"a": 2, "b": 1}
        self.assertEqual(canonical_json(one), canonical_json(two))
        self.assertEqual(sha256_json(one), sha256_json(two))


if __name__ == "__main__":
    unittest.main()

