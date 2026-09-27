"""Checks the benchmark's timing boundary and the machine-readable cold-case evidence."""

from pathlib import Path
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

if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
