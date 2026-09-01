from __future__ import annotations

import os
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest import mock

from tools.creator_scope.ustx_study_contracts import (
    BridgeError,
    MAXIMUM_INPUT_BYTES,
)
from tools.creator_scope.ustx_study_io import (
    TextOutput,
    write_new,
    write_pair_new,
)
from tools.creator_scope.ustx_study_load import read_bounded
from tests.production.test_creator_ustx_study_bridge import USTX_FIXTURE, run_bridge


class CreatorUstxStudyBridgeSafetyTests(unittest.TestCase):
    def test_parser_resource_failures_are_translated_without_partial_output(
        self,
    ) -> None:
        huge_number = USTX_FIXTURE.read_text(encoding="utf-8").replace(
            "bpm: 120", "bpm: " + "9" * 4_000, 1
        )
        hostile_inputs = {
            "deep-yaml.ustx": (
                "import-ustx",
                "ustx_version: '0.9'\nvalue: " + "[" * 2_000 + "0" + "]" * 2_000,
            ),
            "oversized-integer.ustx": (
                "import-ustx",
                "ustx_version: '0.9'\nvalue: " + "9" * 5_000 + "\n",
            ),
            "non-finite-conversion.ustx": ("import-ustx", huge_number),
            "escaped-surrogate.seam": (
                "export-ustx",
                '{"formatId":"com.project-seam.project","name":"\\ud800"}\n',
            ),
        }
        with TemporaryDirectory() as directory:
            root = Path(directory)
            for name, (command, source) in hostile_inputs.items():
                with self.subTest(name=name):
                    source_path = root / name
                    target_path = root / f"{name}.output"
                    report_path = root / f"{name}.report.json"
                    source_path.write_text(source, encoding="utf-8")

                    completed = run_bridge(
                        command,
                        str(source_path),
                        str(target_path),
                        "--report",
                        str(report_path),
                    )

                    self.assertEqual(3, completed.returncode, completed.stderr)
                    self.assertIn("STUDY_BRIDGE=FAIL", completed.stderr)
                    self.assertNotIn("Traceback", completed.stderr)
                    self.assertFalse(target_path.exists())
                    self.assertFalse(report_path.exists())

    def test_input_symlink_and_oversized_file_are_rejected_before_output(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            oversized = root / "oversized.ustx"
            oversized.write_bytes(b"x" * (MAXIMUM_INPUT_BYTES + 1))
            symlink = root / "linked.ustx"
            symlink.symlink_to(USTX_FIXTURE)

            for label, source_path in (("oversized", oversized), ("symlink", symlink)):
                with self.subTest(label=label):
                    target_path = root / f"{label}.seam"
                    report_path = root / f"{label}.json"

                    completed = run_bridge(
                        "import-ustx",
                        str(source_path),
                        str(target_path),
                        "--report",
                        str(report_path),
                    )

                    self.assertEqual(3, completed.returncode, completed.stderr)
                    self.assertFalse(target_path.exists())
                    self.assertFalse(report_path.exists())

    def test_read_bounded_keeps_the_file_opened_before_path_replacement(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.ustx"
            replacement = root / "replacement.ustx"
            source.write_bytes(b"opened bytes\n")
            replacement.write_bytes(b"replacement bytes\n")
            real_open = os.open
            path_was_replaced = False

            def open_then_replace(
                path: str | os.PathLike[str],
                flags: int,
                mode: int = 0o777,
                *,
                dir_fd: int | None = None,
            ) -> int:
                nonlocal path_was_replaced
                descriptor = real_open(path, flags, mode, dir_fd=dir_fd)
                if Path(path) == source and not path_was_replaced:
                    replacement.replace(source)
                    path_was_replaced = True
                return descriptor

            with mock.patch(
                "tools.creator_scope.ustx_study_load.os.open",
                side_effect=open_then_replace,
            ):
                payload = read_bounded(source)

            self.assertTrue(path_was_replaced)
            self.assertEqual(b"replacement bytes\n", source.read_bytes())
            self.assertEqual(b"opened bytes\n", payload)

    def test_aliases_and_multiple_documents_are_rejected_before_output(self) -> None:
        hostile_inputs = {
            "alias": "ustx_version: '0.9'\ntracks: &tracks []\ncopy: *tracks\n",
            "multiple-documents": "ustx_version: '0.9'\n---\nname: second\n",
            "duplicate-key": "ustx_version: '0.9'\nname: first\nname: second\n",
        }
        with TemporaryDirectory() as directory:
            root = Path(directory)
            for label, source in hostile_inputs.items():
                with self.subTest(label=label):
                    input_path = root / f"{label}.ustx"
                    output_path = root / f"{label}.seam"
                    report_path = root / f"{label}.json"
                    input_path.write_text(source, encoding="utf-8")

                    completed = run_bridge(
                        "import-ustx",
                        str(input_path),
                        str(output_path),
                        "--report",
                        str(report_path),
                    )

                    self.assertNotEqual(0, completed.returncode)
                    self.assertIn("not supported", completed.stderr)
                    self.assertFalse(output_path.exists())
                    self.assertFalse(report_path.exists())

    def test_existing_outputs_are_not_overwritten(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            output_path = root / "existing.seam"
            report_path = root / "report.json"
            output_path.write_text("preserve me\n", encoding="utf-8")

            completed = run_bridge(
                "import-ustx",
                str(USTX_FIXTURE),
                str(output_path),
                "--report",
                str(report_path),
            )

            self.assertNotEqual(0, completed.returncode)
            self.assertIn("Refusing to overwrite", completed.stderr)
            self.assertEqual("preserve me\n", output_path.read_text(encoding="utf-8"))
            self.assertFalse(report_path.exists())

    def test_destination_created_during_publication_is_not_overwritten(self) -> None:
        with TemporaryDirectory() as directory:
            target = Path(directory) / "raced.seam"
            real_fsync = os.fsync

            def create_raced_destination(descriptor: int) -> None:
                real_fsync(descriptor)
                target.write_text("racer owns this\n", encoding="utf-8")

            with mock.patch(
                "tools.creator_scope.ustx_study_io.os.fsync",
                side_effect=create_raced_destination,
            ):
                with self.assertRaises(BridgeError):
                    write_new(target, "generated output\n")

            self.assertEqual("racer owns this\n", target.read_text(encoding="utf-8"))

    def test_report_setup_failure_leaves_no_partial_target(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "result.seam"
            blocked_parent = root / "not-a-directory"
            blocked_parent.write_text("block directory creation\n", encoding="utf-8")
            report = blocked_parent / "report.json"

            completed = run_bridge(
                "import-ustx",
                str(USTX_FIXTURE),
                str(target),
                "--report",
                str(report),
            )

            self.assertEqual(3, completed.returncode)
            self.assertIn("STUDY_BRIDGE=FAIL", completed.stderr)
            self.assertNotIn("Traceback", completed.stderr)
            self.assertFalse(target.exists())

    def test_second_destination_race_rolls_back_first_owned_output(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "result.seam"
            report = root / "report.json"
            real_link = os.link

            def race_report_destination(
                source: str | os.PathLike[str],
                destination: str | os.PathLike[str],
                *,
                follow_symlinks: bool = True,
            ) -> None:
                if Path(destination) == report:
                    report.write_text("racer report\n", encoding="utf-8")
                    raise FileExistsError("raced report destination")
                real_link(source, destination, follow_symlinks=follow_symlinks)

            with mock.patch(
                "tools.creator_scope.ustx_study_io.os.link",
                side_effect=race_report_destination,
            ):
                with self.assertRaises(BridgeError):
                    write_pair_new(
                        TextOutput(target, "converted project\n"),
                        TextOutput(report, "generated report\n"),
                    )

            self.assertFalse(target.exists())
            self.assertEqual("racer report\n", report.read_text(encoding="utf-8"))


if __name__ == "__main__":
    _ = unittest.main()
