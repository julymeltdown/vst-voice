import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Phase12CLiveContractTests(unittest.TestCase):
    def test_canonical_live_contract_is_fail_closed(self):
        completed = subprocess.run(
            [sys.executable, "scripts/verify_phase12c_live_contracts.py", "--root", str(ROOT)],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
        self.assertIn("PHASE12C_LIVE_CONTRACT=PASS", completed.stdout)


if __name__ == "__main__":
    unittest.main()
