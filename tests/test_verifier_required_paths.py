"""Every file a source-boundary verifier requires must actually exist.

A refactor that deletes or splits a file leaves any verifier that names it failing
on every run, which is how `scripts/verify_phase11_sources.py` spent a month
reporting a missing `editor_runtime.cpp` that a legitimate split had removed. A
red check that is always red stops carrying information, so this test imports each
verifier's own required-path list and asserts the paths resolve, rather than
running the verifier and hoping a failure is meaningful.
"""
from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

# Verifiers that expose a module-level tuple of repository-relative required paths.
VERIFIERS = (
    "scripts/verify_phase11_sources.py",
)


def load_module(relative: str):
    spec = importlib.util.spec_from_file_location(
        "seam_verify_" + Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class VerifierPathTests(unittest.TestCase):
    def test_every_required_path_exists(self):
        for relative in VERIFIERS:
            with self.subTest(verifier=relative):
                module = load_module(relative)
                required = getattr(module, "REQUIRED", None)
                self.assertIsNotNone(required, f"{relative} declares no REQUIRED paths")
                self.assertTrue(required, f"{relative} declares an empty REQUIRED list")
                missing = [path for path in required if not (ROOT / path).is_file()]
                self.assertEqual(
                    missing, [],
                    f"{relative} requires paths that do not exist: {missing}")

    def test_a_required_path_list_cannot_be_silently_emptied(self):
        # Guards the guard: this test only has force while at least one verifier
        # still declares paths, so an empty catalog is a failure rather than a pass.
        declared = [load_module(relative) for relative in VERIFIERS]
        self.assertTrue(all(getattr(module, "REQUIRED", None) for module in declared))


if __name__ == "__main__":
    unittest.main()

