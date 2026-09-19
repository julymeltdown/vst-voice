from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class MacosInstallerContractTests(unittest.TestCase):
    _HOOK_EPILOGUE = 'script_root='

    def _reporting_hook(self, root: Path) -> Path:
        # Replace the verifier invocation with a dump of the resolved trust
        # inputs so the test observes exactly what the real hook would forward.
        source = (ROOT / "packaging/macos/scripts/preinstall").read_text(
            encoding="utf-8"
        )
        hook = root / "preinstall"
        hook.write_text(
            source[: source.index(self._HOOK_EPILOGUE)]
            + 'for name in "${required[@]}"; do printf "%s=%s\\n" "$name" "${!name}"; done\n'
            + "exit 0\n",
            encoding="utf-8",
        )
        return hook

    @staticmethod
    def _run(hook: Path, environment: dict[str, str]):
        return subprocess.run(
            ["/bin/bash", str(hook)],
            env=environment,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_preinstall_recovers_trust_inputs_from_intent_beside_archive(self) -> None:
        # macOS Installer does not forward the caller's environment to package
        # scripts, so the hook must recover its inputs from the staged candidate
        # directory that PACKAGE_PATH points into.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            hook = self._reporting_hook(root)
            candidate = root / "staging/candidate-abc"
            candidate.mkdir(parents=True)
            digest = "a" * 64
            (candidate / "installer-intent.env").write_text(
                "\n".join(
                    [
                        "SEAM_INSTALLER_HANDOFF=/x/handoff.json",
                        "SEAM_UPDATE_MANIFEST=/x/update-manifest.json",
                        "SEAM_UPDATE_POLICY=/x/update-trust-policy.json",
                        "SEAM_UPDATE_STAGING_ROOT=/x/staging",
                        "SEAM_EXPECTED_CANDIDATE=candidate-abc",
                        f"SEAM_EXPECTED_HANDOFF_SHA256={digest}",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            package = candidate / "ProjectSEAM-0.13.1-unsigned.pkg"
            package.touch()
            clean = {"PATH": "/usr/bin:/bin", "PACKAGE_PATH": str(package)}
            result = self._run(hook, clean)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertIn(f"SEAM_EXPECTED_HANDOFF_SHA256={digest}", result.stdout)
            self.assertIn("SEAM_EXPECTED_CANDIDATE=candidate-abc", result.stdout)
            self.assertIn("SEAM_UPDATE_STAGING_ROOT=/x/staging", result.stdout)

            # An ambient environment still wins and needs no intent file.
            explicit = dict(clean)
            explicit.update(
                {
                    "SEAM_INSTALLER_HANDOFF": "/y/handoff.json",
                    "SEAM_UPDATE_MANIFEST": "/y/update-manifest.json",
                    "SEAM_UPDATE_POLICY": "/y/update-trust-policy.json",
                    "SEAM_UPDATE_STAGING_ROOT": "/y/staging",
                    "SEAM_EXPECTED_CANDIDATE": "candidate-y",
                    "SEAM_EXPECTED_HANDOFF_SHA256": "b" * 64,
                }
            )
            result = self._run(hook, explicit)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertIn("SEAM_EXPECTED_CANDIDATE=candidate-y", result.stdout)

    def test_preinstall_rejects_missing_unexpected_and_symlinked_intent(self) -> None:
        hook = ROOT / "packaging/macos/scripts/preinstall"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            candidate = root / "staging/candidate-abc"
            candidate.mkdir(parents=True)
            package = candidate / "ProjectSEAM-0.13.1-unsigned.pkg"
            package.touch()
            clean = {"PATH": "/usr/bin:/bin", "PACKAGE_PATH": str(package)}

            # No intent and no environment: refuse instead of guessing.
            result = self._run(hook, clean)
            self.assertEqual(70, result.returncode)
            self.assertIn("missing installer trust input", result.stderr)

            # Unknown keys must not be silently accepted.
            (candidate / "installer-intent.env").write_text(
                "SEAM_INSTALLER_HANDOFF=/x/handoff.json\nEVIL=1\n",
                encoding="utf-8",
            )
            result = self._run(hook, clean)
            self.assertEqual(70, result.returncode)
            self.assertIn("unexpected installer intent key", result.stderr)

            # A symlinked intent must not redirect the privileged read.
            target = root / "elsewhere.env"
            target.write_text(
                "SEAM_INSTALLER_HANDOFF=/z/handoff.json\n", encoding="utf-8"
            )
            (candidate / "installer-intent.env").unlink()
            (candidate / "installer-intent.env").symlink_to(target)
            result = self._run(hook, clean)
            self.assertEqual(70, result.returncode)

            # Without PACKAGE_PATH the hook cannot resolve anything at all.
            result = self._run(hook, {"PATH": "/usr/bin:/bin"})
            self.assertEqual(70, result.returncode)

    def test_installer_ownership_is_explicit_and_outer_package_is_signed(self) -> None:
        ownership = json.loads(
            (ROOT / "packaging/macos/installer-ownership.json").read_text(
                encoding="utf-8"
            )
        )
        package = (ROOT / "scripts/package_macos_plugins.sh").read_text(
            encoding="utf-8"
        )
        standalone = (ROOT / "scripts/package_macos_standalone.sh").read_text(
            encoding="utf-8"
        )
        distribution = (ROOT / "packaging/macos/Distribution.xml.in").read_text(
            encoding="utf-8"
        )
        preinstall = (ROOT / "packaging/macos/scripts/preinstall").read_text(
            encoding="utf-8"
        )
        self.assertEqual("macos-arm64", ownership["platform"])
        self.assertEqual(
            {
                "Applications/Project SEAM.app",
                "Library/Audio/Plug-Ins/CLAP/ProjectSEAMEditor.clap",
                "Library/Audio/Plug-Ins/VST3/ProjectSEAMEditor.vst3",
                "Library/Audio/Plug-Ins/Components/ProjectSEAMEditor.component",
                "Library/Application Support/ProjectSEAM/Documentation",
                "Library/Application Support/ProjectSEAM/Trust",
                "Library/Application Support/ProjectSEAM/Ownership",
                "Library/Application Support/ProjectSEAM/Notices",
                "Library/Application Support/ProjectSEAM/Tools/seam_installer_verifier",
                "Library/Application Support/ProjectSEAM/RELEASE_IDENTITY.json",
                "Library/Application Support/ProjectSEAM/release-payload-manifest.json",
                "Library/Application Support/ProjectSEAM/release-dependency-closure.json",
                "Library/Application Support/ProjectSEAM/THIRD_PARTY_NOTICES.md",
                "Library/Application Support/ProjectSEAM/SBOM.spdx.json",
                "Library/Application Support/ProjectSEAM/uninstall_macos_plugins.sh",
            },
            set(ownership["ownedPaths"]),
        )
        self.assertTrue(ownership["preservedUserRoots"])
        self.assertEqual(
            ["Library/Application Support/ProjectSEAM/InstallerReplay"],
            ownership["preservedSystemRoots"],
        )
        self.assertEqual(
            {"com.project-seam.plugins", "com.project-seam.standalone"},
            set(ownership["ownedPackageReceipts"]),
        )
        self.assertEqual(
            "separate-explicit-action", ownership["destructiveDataRemoval"]
        )
        self.assertIn("productbuild --sign", package)
        self.assertIn("Library/Application Support/ProjectSEAM/Documentation", package)
        self.assertIn("Manual/EULA.md", standalone)
        self.assertIn("@PROJECT_SEAM_VERSION@", distribution)
        self.assertIn("com.project-seam.standalone", distribution)
        self.assertIn("release-payload-manifest.json", package)
        self.assertIn("Standalone/Project SEAM.app", package)
        self.assertIn("Tools/seam_installer_verifier", package)
        self.assertIn("Notices/openssl-LICENSE.txt", package)
        self.assertIn("--scripts", package)
        for option in (
            "--handoff",
            "--manifest",
            "--policy",
            "--staging-root",
            "--expected-candidate",
            "--expected-handoff-sha256",
        ):
            self.assertIn(option, preinstall)
        for forbidden in ("--root-key", "--replay-root", "--now"):
            self.assertNotIn(forbidden, preinstall)
        self.assertIn(
            "InstallerReplay",
            (ROOT / "apps/seam-installer-verifier/main.cpp").read_text(
                encoding="utf-8"
            ),
        )
        self.assertLess(
            preinstall.index("seam_installer_verifier"), preinstall.index("PASS")
        )
        uninstall = (ROOT / "scripts/uninstall_macos_plugins.sh").read_text(
            encoding="utf-8"
        )
        self.assertNotIn('"/Library/Application Support/ProjectSEAM"\n', uninstall)
        self.assertIn("rmdir", uninstall)
        oracle = (ROOT / "scripts/test_macos_installer.sh").read_text(encoding="utf-8")
        self.assertIn("ownedPayloadRemoved", oracle)
        self.assertIn("replayStatePreserved", oracle)
        self.assertIn("ownership_values ownedPaths", oracle)
        self.assertIn("ownership_values ownedPackageReceipts", oracle)
        self.assertIn("standaloneLaunch", oracle)
        self.assertIn("open -na", oracle)
        # The installer does not forward package-script output, so replay
        # rejection must be proven by exit status plus a direct verifier call
        # rather than by grepping installer log text that never contains it.
        self.assertNotIn("grep -q 'INSTALLER_HANDOFF=BLOCKED'", oracle)
        self.assertIn("replay-verifier.log", oracle)
        self.assertIn("already consumed", oracle)


if __name__ == "__main__":
    unittest.main()
