"""Checks the benchmark's timing boundary and the machine-readable cold-case evidence."""

import json
import os
from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]


class FrameBenchmarkContractTests(unittest.TestCase):
    def test_prepare_frame_occurs_once_and_inside_the_paint_clock(self):
        source = (ROOT / "benchmarks/phase5_benchmark.cpp").read_text()
        frame = source.split("double frame(DesignCase& record, bool playing) {")[1].split("\n  }\n};")[0]
        self.assertEqual(frame.count("shell.prepareFrame("), 1)
        self.assertLess(frame.index("const auto paintStart"), frame.index("shell.prepareFrame("))
        self.assertLess(frame.index("shell.prepareFrame("), frame.index("const auto paintEnd"))
        self.assertLess(frame.index("const auto stateEnd"), frame.index("const auto paintStart"))

    def test_cold_case_output_names_the_work_measured_and_keeps_exit_meaningful(self):
        binary = Path(sys.argv[-1])
        if not binary.is_file():
            self.skipTest("benchmark executable not supplied")
        env = dict(os.environ, SEAM_BENCHMARK_DESIGN_ONLY="1", SEAM_BENCHMARK_SAMPLES="1")
        result = subprocess.run([str(binary.resolve())], cwd=ROOT, env=env, capture_output=True,
                                text=True, timeout=120, check=False)
        report = json.loads(result.stdout)["designShell"]
        self.assertEqual(result.returncode == 0, report["pass"])
        for mode in ("emo", "scene"):
            cases = {case["case"]: case for case in report["cases"] if case["mode"] == mode}
            cold = cases["cold-full-frame"]
            retained = cases["retained-background-invalidation"]
            self.assertEqual(cold["caseMeaning"], "true first-paint-equivalent frame")
            self.assertEqual(retained["caseMeaning"],
                             "retained-background upper-layers-only invalidation")
            self.assertIn("L0 background painter", cold["invalidation"])
            self.assertIn("L0 snapshot retained", retained["invalidation"])
            for case in (cold, retained):
                self.assertEqual(case["budgetP95Ms"], 14)
                self.assertGreaterEqual(case["p95Ms"], 0)
                self.assertEqual(case["pass"], case["p95Ms"] <= case["budgetP95Ms"])

if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
