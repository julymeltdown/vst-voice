from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tests.production.public_release_contract_fixtures import acceptance_contract
from tests.production.public_release_fixtures import candidate


ROOT = Path(__file__).resolve().parents[2]


class PublicReleaseCliTests(unittest.TestCase):
    def test_direct_gate_cli_cannot_assert_archive_verification(self) -> None:
        contract = acceptance_contract()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            candidate_path = root / "candidate.json"
            contract_path = root / "contract.json"
            candidate_path.write_text(json.dumps(candidate(contract)), encoding="utf-8")
            contract_path.write_text(json.dumps(contract), encoding="utf-8")

            completed = subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "tools.public_release.release_gate",
                    "--candidate",
                    str(candidate_path),
                    "--acceptance-contract",
                    str(contract_path),
                    "--state",
                    "PUBLIC_ACTIVE",
                    "--archive-verified",
                ],
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )

        self.assertNotEqual(0, completed.returncode, completed.stdout)
        self.assertNotIn('"passed":true', completed.stdout)


if __name__ == "__main__":
    unittest.main()
