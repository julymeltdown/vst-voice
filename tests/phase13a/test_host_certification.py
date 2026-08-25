from __future__ import annotations

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))
import host_certification  # noqa: E402
from distribution_manifest import tree_sha256  # noqa: E402


class HostCertificationTests(unittest.TestCase):
    def artifact(self, installed_root: Path) -> Path:
        artifact = installed_root / "ProjectSEAMEditor.vst3"
        binary = artifact / "Contents" / "x86_64-win" / "ProjectSEAMEditor.vst3"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b"installed-vst3")
        (artifact / "moduleinfo.json").write_text("{}\n", encoding="utf-8")
        return artifact

    def candidate(self, artifact_sha256: str) -> host_certification.JsonObject:
        return {
            "schemaVersion": 1,
            "releaseIdentity": {"buildId": "candidate-build-001"},
            "artifacts": [{"format": "VST3", "sha256": artifact_sha256}],
        }

    def record(self, artifact: Path) -> host_certification.JsonObject:
        return {
            "targetId": "reaper",
            "runtimeResult": "PASS",
            "candidateBuildId": "candidate-build-001",
            "osVersion": "Windows 11 24H2",
            "hostVersion": "REAPER 7.30",
            "pluginFormat": "VST3",
            "artifactPath": str(artifact),
            "toolIdentity": {
                "name": "VST3PluginTestHost",
                "version": "3.8.1",
                "sha256": "c" * 64,
            },
            "executedAt": "2026-08-18T12:00:00Z",
            "executor": "qa-engineer",
            "checks": {name: "PASS" for name in host_certification.CHECK_NAMES},
            "evidence": ["logs/reaper.txt", "screens/reaper.png", "audio/reaper.wav"],
        }

    def evidence(self, root: Path, record: host_certification.JsonObject) -> None:
        paths = record["evidence"]
        assert isinstance(paths, list)
        for relative in paths:
            assert isinstance(relative, str)
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"evidence-" + relative.encode())

    def validate(
        self,
        record: host_certification.JsonObject,
        evidence_root: Path,
        candidate: host_certification.JsonObject,
        installed_root: Path,
    ) -> list[str]:
        return host_certification.validate_record(
            record, evidence_root, candidate, installed_root
        )

    def test_pass_hashes_installed_bytes_and_binds_the_candidate_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            installed_root = root / "installed"
            artifact = self.artifact(installed_root)
            digest = tree_sha256(artifact)
            record = self.record(artifact)
            self.evidence(root, record)

            self.assertEqual(
                [], self.validate(record, root, self.candidate(digest), installed_root)
            )
            self.assertEqual(digest, record["pluginSha256"])
            self.assertEqual(digest, record["artifactTreeSha256"])
            hashes = record["evidenceSha256"]
            self.assertIsInstance(hashes, dict)
            assert isinstance(hashes, dict)
            for relative, actual in hashes.items():
                self.assertEqual(
                    hashlib.sha256((root / relative).read_bytes()).hexdigest(), actual
                )

    def test_nonexistent_installed_artifact_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            installed_root = root / "installed"
            record = self.record(installed_root / "ProjectSEAMEditor.vst3")
            self.evidence(root, record)
            errors = self.validate(record, root, self.candidate("b" * 64), installed_root)
            self.assertTrue(any("does not exist" in error for error in errors))

    def test_artifact_symlink_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            installed_root = root / "installed"
            real = self.artifact(root / "elsewhere")
            installed_root.mkdir()
            link = installed_root / "ProjectSEAMEditor.vst3"
            link.symlink_to(real, target_is_directory=True)
            record = self.record(link)
            self.evidence(root, record)
            errors = self.validate(
                record, root, self.candidate(tree_sha256(real)), installed_root
            )
            self.assertTrue(any("symbolic link" in error for error in errors))

    def test_artifact_outside_declared_install_root_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            installed_root = root / "installed"
            installed_root.mkdir()
            artifact = self.artifact(root / "build-output")
            record = self.record(artifact)
            self.evidence(root, record)
            errors = self.validate(
                record, root, self.candidate(tree_sha256(artifact)), installed_root
            )
            self.assertTrue(any("direct child" in error for error in errors))

    def test_candidate_hash_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            installed_root = root / "installed"
            artifact = self.artifact(installed_root)
            record = self.record(artifact)
            self.evidence(root, record)
            errors = self.validate(record, root, self.candidate("b" * 64), installed_root)
            self.assertTrue(any("candidate manifest" in error for error in errors))

    def test_caller_supplied_hash_cannot_override_installed_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            installed_root = root / "installed"
            artifact = self.artifact(installed_root)
            digest = tree_sha256(artifact)
            record = self.record(artifact)
            record["pluginSha256"] = "b" * 64
            record["artifactTreeSha256"] = "b" * 64
            self.evidence(root, record)
            errors = self.validate(record, root, self.candidate(digest), installed_root)
            self.assertTrue(any("caller-supplied" in error for error in errors))

    def test_not_run_record_cannot_contain_fake_pass_checks(self) -> None:
        record = self.record(Path("unused"))
        record["runtimeResult"] = "NOT_RUN"
        record["evidence"] = []
        errors = self.validate(record, Path("."), {}, Path("."))
        self.assertTrue(any("checks must be empty" in error for error in errors))

    def test_record_updates_only_matching_target(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            installed_root = root / "installed"
            artifact = self.artifact(installed_root)
            record = self.record(artifact)
            self.evidence(root, record)
            self.assertEqual(
                [],
                self.validate(
                    record, root, self.candidate(tree_sha256(artifact)), installed_root
                ),
            )
            matrix: host_certification.JsonObject = {
                "schemaVersion": 1,
                "policy": "MANDATORY",
                "targets": [
                    {
                        "id": "reaper",
                        "implementationState": "SOURCE_READY",
                        "runtimeResult": "NOT_RUN",
                        "evidence": [],
                    },
                    {
                        "id": "logic-pro",
                        "implementationState": "SOURCE_READY",
                        "runtimeResult": "NOT_RUN",
                        "evidence": [],
                    },
                ],
            }
            updated = host_certification.apply_record(matrix, record)
            targets = updated["targets"]
            assert isinstance(targets, list)
            first = targets[0]
            second = targets[1]
            assert isinstance(first, dict) and isinstance(second, dict)
            self.assertEqual("PASS", first["runtimeResult"])
            self.assertEqual("TARGET_BUILD_PASS", first["implementationState"])
            self.assertEqual("NOT_RUN", second["runtimeResult"])

    def test_evidence_symlink_is_rejected_even_when_target_is_inside_root(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            installed_root = root / "installed"
            artifact = self.artifact(installed_root)
            record = self.record(artifact)
            self.evidence(root, record)
            link = root / "logs" / "linked.txt"
            link.symlink_to(root / "logs" / "reaper.txt")
            record["evidence"] = ["logs/linked.txt"]
            errors = self.validate(
                record, root, self.candidate(tree_sha256(artifact)), installed_root
            )
            self.assertTrue(any("symbolic link" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
