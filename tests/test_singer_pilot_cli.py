"""Exercise real pilot exports; this does not score musical intelligibility."""
import json
import hashlib
from pathlib import Path
import subprocess
import struct
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
                if Path(row["wav"]).parent.name == "candidates":
                    pitch = json.loads((root / name / (row["variant"] + "-pitch.json")).read_text())
                    assert pitch["audioSha256"] == row["sha256"]
                    assert pitch["status"] == "DIAGNOSTIC_NOT_QUALIFICATION"
                    assert len(pitch["notes"]) == 6
                    for note in pitch["notes"]:
                        assert note["analysisFrames"] > 0
                        assert 0 <= note["within50CentsFrames"] <= note["voicedFrames"] <= note["analysisFrames"]
                    if row["variant"] == "baseline":
                        # Fixed diagnostic fixture only, not full-singer qualification.
                        assert all(n["medianAbsoluteCents"] < 30 for n in pitch["notes"])
                        assert all(n["within50CentsFrames"] == n["analysisFrames"] for n in pitch["notes"])
            reports.append(report)
        assert [row["sha256"] for row in reports[0]["runs"]] == [row["sha256"] for row in reports[1]["runs"]]
        before = (root / "first" / "pilot.json").read_bytes()
        rejected = subprocess.run([str(binary), str(root / "first")], capture_output=True, timeout=10)
        assert rejected.returncode != 0
        assert (root / "first" / "pilot.json").read_bytes() == before
        articulation_hashes = []
        expected_phones = [phone for onset, vowel in zip(
            ["m"] * 5 + ["n"] * 5 + ["p", "t", "k", "s"],
            list("aiueoaiueoaaaa")) for phone in (onset, vowel)]
        for name in ("articulation", "articulation-repeat"):
            subprocess.run([str(binary), str(root / name), "articulation"],
                           check=True, capture_output=True, timeout=60)
            report = json.loads((root / name / "pilot.json").read_text())
            assert report["releaseEligible"] is False
            hashes = []
            for row in report["runs"]:
                audio = Path(row["wav"])
                assert hashlib.sha256(audio.read_bytes()).hexdigest() == row["sha256"]
                hashes.append(row["sha256"])
                if audio.parent.name != "candidates":
                    continue
                metadata = json.loads(audio.with_suffix(".json").read_text())
                assert metadata["approval"] == "unapproved"
                assert metadata["audioSha256"] == row["sha256"]
                markers = metadata["markers"]
                assert [m["phone"] for m in markers] == expected_phones
                assert {m["kind"] for m in markers} == {"nasal", "plosive", "frication", "oral-vowel"}
                assert markers[0]["startFrame"] == 0
                assert markers[-1]["endFrame"] == metadata["frameCount"]
                assert all(m["startFrame"] < m["endFrame"] for m in markers)
                assert all(a["endFrame"] == b["startFrame"] for a, b in zip(markers, markers[1:]))
                pitch = json.loads((root / name / (row["variant"] + "-pitch.json")).read_text())
                assert len(pitch["notes"]) == 14
            articulation_hashes.append(hashes)
        assert articulation_hashes[0] == articulation_hashes[1]
        boundary_hashes = []
        for name in ("boundaries", "boundaries-repeat"):
            subprocess.run([str(binary), str(root / name), "boundaries"],
                           check=True, capture_output=True, timeout=60)
            report = json.loads((root / name / "pilot.json").read_text())
            assert report["releaseEligible"] is False
            boundary_hashes.append([row["sha256"] for row in report["runs"]])
            for row in report["runs"]:
                audio = Path(row["wav"])
                if audio.parent.name != "candidates":
                    continue
                metadata = json.loads(audio.with_suffix(".json").read_text())
                assert [m["phone"] for m in metadata["markers"]] == ["a"] * 8
                raw = audio.read_bytes()
                assert hashlib.sha256(raw).hexdigest() == row["sha256"]
                assert raw[:4] == b"RIFF" and raw[8:12] == b"WAVE"
                chunks = {}
                offset = 12
                while offset + 8 <= len(raw):
                    size = struct.unpack_from("<I", raw, offset + 4)[0]
                    chunks[raw[offset:offset + 4]] = raw[offset + 8:offset + 8 + size]
                    offset += 8 + size + (size % 2)
                encoding, channels, rate = struct.unpack_from("<HHI", chunks[b"fmt "])
                assert encoding == 3 and channels == 1
                samples = struct.unpack("<" + "f" * (len(chunks[b"data"]) // 4), chunks[b"data"])
                for note_index in range(1, 8):
                    boundary = note_index * rate // 4
                    energy = abs(samples[boundary - 1]) + abs(samples[boundary])
                    if note_index <= 4:
                        assert energy == 0, (row["variant"], note_index, energy)
                    else:
                        assert energy > 1e-6, (row["variant"], note_index, energy)
        assert boundary_hashes[0] == boundary_hashes[1]
        nasal_hashes = []
        for name in ("nasals", "nasals-repeat"):
            subprocess.run([str(binary), str(root / name), "nasals"],
                           check=True, capture_output=True, timeout=60)
            report = json.loads((root / name / "pilot.json").read_text())
            assert report["releaseEligible"] is False
            nasal_hashes.append([row["sha256"] for row in report["runs"]])
            for row in report["runs"]:
                audio = Path(row["wav"])
                assert hashlib.sha256(audio.read_bytes()).hexdigest() == row["sha256"]
                if audio.parent.name != "candidates":
                    continue
                metadata = json.loads(audio.with_suffix(".json").read_text())
                markers = metadata["markers"]
                assert [m["phone"] for m in markers] == ["N", "a", "N", "i", "N", "u"]
                assert [m["kind"] for m in markers] == ["nasal", "oral-vowel"] * 3
                pitch = json.loads((root / name / (row["variant"] + "-pitch.json")).read_text())
                assert len(pitch["notes"]) == 6
                assert all(note["voicedFrames"] > 0 for note in pitch["notes"])
                assert markers[0]["startFrame"] == 0
                assert markers[-1]["endFrame"] == metadata["frameCount"]
                assert all(a["endFrame"] == b["startFrame"] for a, b in zip(markers, markers[1:]))
                assert all(m["endFrame"] - m["startFrame"] == metadata["frameCount"] // 6 for m in markers)
                assert metadata["approval"] == "unapproved"
        assert nasal_hashes[0] == nasal_hashes[1]
    print("Pilot repeatability, finite/nonzero PCM, variant identity and no-overwrite checks passed; quality unassessed.")


if __name__ == "__main__":
    main()
