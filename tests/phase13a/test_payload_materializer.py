from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from tools.phase13a.payload_materializer import (
    first_artifact,
    materialize_vst3,
    materialize_release_inputs,
)
from tools.phase13a.payload_surfaces import PayloadPlatform


class PayloadMaterializerTests(unittest.TestCase):
    def test_artifact_selection_uses_the_requested_configuration(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            debug = root / "Debug/ProjectSEAMEditor.clap"
            release = root / "Release/ProjectSEAMEditor.clap"
            debug.parent.mkdir(parents=True)
            release.parent.mkdir(parents=True)
            debug.write_bytes(b"debug")
            release.write_bytes(b"release")

            self.assertEqual(
                release,
                first_artifact(root, "ProjectSEAMEditor.clap", "Release"),
            )

    def test_artifact_selection_fails_when_requested_configuration_is_absent(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            debug = root / "Debug/ProjectSEAMEditor.clap"
            debug.parent.mkdir(parents=True)
            debug.write_bytes(b"debug")

            with self.assertRaisesRegex(RuntimeError, "configuration Release"):
                first_artifact(root, "ProjectSEAMEditor.clap", "Release")

    def test_release_input_materialization_removes_stale_trust_siblings(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            output = root / "output"
            for relative in (
                "packaging/trust/release-trust-roots.json",
                "packaging/trust/update-root-public-key.json",
                "packaging/macos/installer-ownership.json",
            ):
                path = source / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("{}", encoding="utf-8")
            stale = output / "Trust/retired-private-key.json"
            stale.parent.mkdir(parents=True)
            stale.write_text("secret", encoding="utf-8")

            materialize_release_inputs(source, output, PayloadPlatform.MACOS_ARM64)

            self.assertEqual(
                {"release-trust-roots.json", "update-root-public-key.json"},
                {path.name for path in (output / "Trust").iterdir()},
            )

    def package(self, root: Path, name: str) -> Path:
        """Minimal Windows folder package the VST3 shape rule accepts."""
        package = root / name
        (package / "Contents" / "Resources").mkdir(parents=True)
        (package / "Contents" / "Resources" / "moduleinfo.json").write_text(
            "{}", encoding="utf-8"
        )
        binary = package / "Contents" / "x86_64-win" / f"{name}.vst3"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b"MZ")
        return package

    def test_vst3_materialization_retires_the_generated_root_copy(self) -> None:
        # The wrapper project's artifact directory is the payload root, so the
        # build leaves a generated copy of the package at the root beside the
        # declared VST3/ surface. validate_payload_shape rejects it as an
        # undeclared top-level entry, which is exactly what failed the Windows
        # job of phase13a-plugin-formats after the dependency gate was cleared.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            wrapper_build = root / "wrapper"
            output = root / "payload"
            output.mkdir(parents=True)
            generated = self.package(output, "ProjectSEAMEditor.vst3")
            self.assertTrue(generated.is_dir())

            destination = materialize_vst3(
                wrapper_build,
                output,
                "a" * 64,
                "Windows",
                "1.0.0",
                {"version": "1.0.0"},
                "Release",
            )

            self.assertEqual(output / "VST3" / "ProjectSEAMEditor.vst3", destination)
            self.assertTrue(destination.is_dir())
            self.assertFalse(generated.exists())
            self.assertEqual({"VST3"}, {item.name for item in output.iterdir()})


if __name__ == "__main__":
    unittest.main()
