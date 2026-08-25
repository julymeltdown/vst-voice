import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Phase12CEvidenceTests(unittest.TestCase):
    def test_live_summary_verifier_accepts_deterministic_demo_record(self):
        with tempfile.TemporaryDirectory() as directory:
            summary = Path(directory) / "summary.json"
            summary.write_text(
                json.dumps(
                    {
                        "result": "PASS",
                        "finite": True,
                        "voicebankId": "demo",
                        "voicebankVersion": "1.0.0",
                        "voicebankContentHash": "a" * 64,
                        "unitCount": 4,
                        "renderedFrames": 48000,
                        "noteOns": 2,
                        "steals": 1,
                        "transitionFallbacks": 1,
                        "eventOverflows": 0,
                        "energy": 12.0,
                    }
                ),
                encoding="utf-8",
            )
            completed = subprocess.run(
                [sys.executable, "scripts/verify_phase12c_evidence.py", "--summary", str(summary)],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
            self.assertIn("PHASE12C_EVIDENCE=PASS", completed.stdout)


if __name__ == "__main__":
    unittest.main()
