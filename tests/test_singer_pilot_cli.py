"""Exercise real pilot exports; this does not score musical intelligibility."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="seam-pilot-test-") as directory:
        root = Path(directory)
        reports = []
        for name in ("first", "repeat"):
            subprocess.run([str(binary), str(root / name)], check=True, capture_output=True, timeout=60)
            report = json.loads((root / name / "pilot.json").read_text())
            assert report["releaseEligible"] is False
            assert report["status"] == "UNQUALIFIED_LISTENING_PILOT"
            assert len(report["runs"]) == 6
            assert len({row["recipeHash"] for row in report["runs"]}) == 3
            for row in report["runs"]:
                assert 0 < row["rms"] <= row["peak"] < 1
                assert Path(row["wav"]).is_file()
            reports.append(report)
        assert [row["sha256"] for row in reports[0]["runs"]] == [row["sha256"] for row in reports[1]["runs"]]
        before = (root / "first" / "pilot.json").read_bytes()
        rejected = subprocess.run([str(binary), str(root / "first")], capture_output=True, timeout=10)
        assert rejected.returncode != 0
        assert (root / "first" / "pilot.json").read_bytes() == before
    print("Pilot repeatability, finite/nonzero PCM, variant identity and no-overwrite checks passed; quality unassessed.")


if __name__ == "__main__":
    main()
