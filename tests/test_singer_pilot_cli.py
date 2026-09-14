"""Exercise real pilot exports; this does not score musical intelligibility."""
import json
import hashlib
import math
from pathlib import Path
import subprocess
import struct
import sys
import tempfile


_BAND_EDGES = ((100, 200), (200, 350), (350, 500), (500, 700), (700, 900), (900, 1200),
               (1200, 1600), (1600, 2200), (2200, 2800), (2800, 3400))


def _float_mono(raw):
    """The sample rate and mono samples of a float32 WAV the pilot wrote."""
    offset = 12
    chunks = {}
    while offset + 8 <= len(raw):
        size = struct.unpack_from("<I", raw, offset + 4)[0]
        chunks[raw[offset:offset + 4]] = raw[offset + 8:offset + 8 + size]
        offset += 8 + size + size % 2
    encoding, channels, rate = struct.unpack_from("<HHI", chunks[b"fmt "])
    assert encoding == 3 and channels == 1
    return rate, struct.unpack("<" + "f" * (len(chunks[b"data"]) // 4), chunks[b"data"])


def _band_profile(samples, rate):
    """A Hann-windowed band profile, normalized so that loudness does not affect the shape."""
    energy = []
    width = len(samples)
    for low, high in _BAND_EDGES:
        total = 0.0
        for hz in range(low, high + 1, 50):
            real = imaginary = 0.0
            for index, value in enumerate(samples):
                window = 0.5 - 0.5 * math.cos(2.0 * math.pi * index / (width - 1))
                angle = 2.0 * math.pi * hz * index / rate
                real += value * window * math.cos(angle)
                imaginary += value * window * math.sin(angle)
            total += (real * real + imaginary * imaginary) / (width * width)
        energy.append(total)
    scale = sum(energy) or 1.0
    return [value / scale for value in energy]


def _band_distance(left, right):
    return sum(abs(first - second) for first, second in zip(left, right))


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
        custom = root / "custom"
        subprocess.run([str(binary), str(custom), "phrase", "ま:60", "た:64", "ー:67", "ん:65", "あ:60"],
                       check=True, capture_output=True, timeout=60)
        report = json.loads((custom / "pilot.json").read_text())
        assert report["releaseEligible"] is False
        for row in report["runs"]:
            audio = Path(row["wav"])
            if audio.parent.name == "candidates":
                metadata = json.loads(audio.with_suffix(".json").read_text())
                assert [m["phone"] for m in metadata["markers"]] == ["m", "a", "t", "a", "a", "N", "a"]
        for index, tokens in enumerate((["あ:23"], ["あ:97"], ["あ:60x"], [":60"], ["あ"], [], ["あ:60"] * 65,
                                        ["あ:60:0"], ["あ:60:-1"], ["あ:60:3841"], ["あ:60:"], ["あ:60:20x"],
                                        ["あ:60:20:30"], ["あ:60:3840"] * 17)):
            target = root / f"invalid-phrase-{index}"
            result = subprocess.run([str(binary), str(target), "phrase", *tokens], capture_output=True, timeout=10)
            assert result.returncode != 0
            assert not target.exists()
        # The maximal recipe admits the voiced affricate now, so じ renders as one gesture whose
        # candidate says it is a voiced affricate rather than an unvoiced pair. A recipe that does
        # not declare it still refuses the symbol by name, which the articulation-context suite
        # checks where a recipe without the binding can be supplied.
        voiced = root / "voiced-affricate"
        subprocess.run([str(binary), str(voiced), "phrase", "じ:60", "じゃ:62"], check=True,
                       capture_output=True, timeout=60)
        voicedReport = json.loads((voiced / "pilot.json").read_text())
        assert voicedReport["releaseEligible"] is False
        voicedMarkers = 0
        for row in voicedReport["runs"]:
            audio = Path(row["wav"])
            if audio.parent.name != "candidates":
                continue
            metadata = json.loads(audio.with_suffix(".json").read_text())
            assert metadata["schemaVersion"] == 10
            assert metadata["voicedAffricateRevision"] == 1
            kinds = [m["kind"] for m in metadata["markers"]]
            assert kinds == ["voiced-affricate", "oral-vowel", "voiced-affricate", "oral-vowel"], kinds
            assert all(m["palatalized"] is False for m in metadata["markers"])
            voicedMarkers += 1
        assert voicedMarkers == 3
        rhythmic = root / "rhythmic"
        subprocess.run([str(binary), str(rhythmic), "phrase", "ば:60:960", "ー:64:240", "ん:65:720", "あ:60"],
                       check=True, capture_output=True, timeout=60)
        report = json.loads((rhythmic / "pilot.json").read_text())
        for variant in ("baseline", "higher-formants", "breathier"):
            pitch_report = json.loads((rhythmic / (variant + "-pitch.json")).read_text())
            assert [(n["windowStartTick"], n["windowEndTick"]) for n in pitch_report["notes"]] == [
                (240, 720), (1020, 1140), (1380, 1740), (2040, 2280)]
            assert [(n["windowStartFrame"], n["windowEndFrame"]) for n in pitch_report["notes"]] == [
                (6000, 18000), (25500, 28500), (34500, 43500), (51000, 57000)]
        for row in report["runs"]:
            audio = Path(row["wav"])
            if audio.parent.name == "candidates":
                metadata = json.loads(audio.with_suffix(".json").read_text())
                assert metadata["schemaVersion"] == 6
                assert metadata["frameCount"] == 60000
                assert [(m["phone"], m["startFrame"], m["endFrame"]) for m in metadata["markers"]] == [
                    ("b", 0, 2880), ("a", 2880, 24000), ("a", 24000, 30000), ("N", 30000, 48000), ("a", 48000, 60000)]
        stop_hashes = []
        for name in ("stops", "stops-repeat"):
            subprocess.run([str(binary), str(root / name), "stops"], check=True, capture_output=True, timeout=60)
            report = json.loads((root / name / "pilot.json").read_text())
            stop_hashes.append([row["sha256"] for row in report["runs"]])
            for row in report["runs"]:
                audio = Path(row["wav"])
                if audio.parent.name != "candidates":
                    continue
                metadata = json.loads(audio.with_suffix(".json").read_text())
                assert metadata["schemaVersion"] == 6 and metadata["voicedPlosiveRevision"] == 1
                assert metadata["approval"] == "unapproved"
                markers = metadata["markers"]
                assert [m["phone"] for m in markers] == list("pabatadakaga")
                assert [m["kind"] for m in markers[::2]] == ["plosive", "voiced-plosive"] * 3
                raw = audio.read_bytes()
                assert hashlib.sha256(raw).hexdigest() == row["sha256"]
                offset = 12
                chunks = {}
                while offset + 8 <= len(raw):
                    size = struct.unpack_from("<I", raw, offset + 4)[0]
                    chunks[raw[offset:offset + 4]] = raw[offset + 8:offset + 8 + size]
                    offset += 8 + size + size % 2
                encoding, channels, rate = struct.unpack_from("<HHI", chunks[b"fmt "])
                assert encoding == 3 and channels == 1
                samples = struct.unpack("<" + "f" * (len(chunks[b"data"]) // 4), chunks[b"data"])
                for marker in markers[::2]:
                    start, end = marker["startFrame"], marker["endFrame"] - rate // 100
                    closure = samples[start:end]
                    assert len(closure) > 0
                    if marker["kind"] == "plosive":
                        assert all(value == 0 for value in closure)
                    else:
                        assert max(abs(value) for value in closure) > 1e-5
        assert stop_hashes[0] == stop_hashes[1]

        # An affricate is a released closure that continues into a frication tail inside one
        # gesture, so the closure is silent, the release is not, and the tail that follows it is
        # not either. た and さ are rendered beside them as the stop and fricative they are made
        # from, which is what makes the comparison listenable.
        affricate_hashes = []
        for name in ("affricates", "affricates-repeat"):
            subprocess.run([str(binary), str(root / name), "affricates"], check=True,
                           capture_output=True, timeout=60)
            report = json.loads((root / name / "pilot.json").read_text())
            affricate_hashes.append([row["sha256"] for row in report["runs"]])
            for row in report["runs"]:
                audio = Path(row["wav"])
                if audio.parent.name != "candidates":
                    continue
                metadata = json.loads(audio.with_suffix(".json").read_text())
                assert metadata["schemaVersion"] == 7
                assert metadata["affricateRevision"] == 1
                assert metadata["plosiveRevision"] == 1
                assert metadata["approval"] == "unapproved"
                markers = metadata["markers"]
                assert [m["phone"] for m in markers] == ["ts", "u", "ch", "i", "t", "a", "s", "a"]
                assert [m["kind"] for m in markers] == [
                    "affricate", "oral-vowel", "affricate", "oral-vowel",
                    "plosive", "oral-vowel", "frication", "oral-vowel"]
                raw = audio.read_bytes()
                assert hashlib.sha256(raw).hexdigest() == row["sha256"]
                offset = 12
                chunks = {}
                while offset + 8 <= len(raw):
                    size = struct.unpack_from("<I", raw, offset + 4)[0]
                    chunks[raw[offset:offset + 4]] = raw[offset + 8:offset + 8 + size]
                    offset += 8 + size + size % 2
                encoding, channels, rate = struct.unpack_from("<HHI", chunks[b"fmt "])
                assert encoding == 3 and channels == 1
                samples = struct.unpack("<" + "f" * (len(chunks[b"data"]) // 4), chunks[b"data"])
                for marker, burst_milliseconds in zip(
                        [m for m in markers if m["kind"] == "affricate"], (10, 12)):
                    start = marker["startFrame"]
                    span = marker["endFrame"] - start
                    burst = round(burst_milliseconds * rate / 1000)
                    minimum_tail = round(0.020 * rate / 1000)
                    after_burst = span - burst
                    tail = min(after_burst - 1, max(minimum_tail, after_burst // 2))
                    closure = after_burst - tail
                    assert closure >= 1 and tail >= minimum_tail
                    assert all(value == 0 for value in samples[start:start + closure])
                    assert any(value != 0 for value in samples[start + closure:start + closure + burst])
                    assert any(value != 0 for value in samples[start + closure + burst:start + span])
        assert affricate_hashes[0] == affricate_hashes[1]

        # A liquid or glide is a voiced gesture whose defining motion is the formant transition
        # its recipe declares into the vowel that follows. Each approximant span stays voiced
        # throughout -- unlike the plosive closure above -- and ends measurably closer to its
        # vowel's steady pose than it started. ら, わ and や are rendered beside a bare あ, which
        # is the comparison point; none of this is a listening or quality claim.
        glide_hashes = []
        for name in ("glides", "glides-repeat"):
            subprocess.run([str(binary), str(root / name), "glides"], check=True,
                           capture_output=True, timeout=60)
            report = json.loads((root / name / "pilot.json").read_text())
            glide_hashes.append([row["sha256"] for row in report["runs"]])
            for row in report["runs"]:
                audio = Path(row["wav"])
                if audio.parent.name != "candidates":
                    continue
                metadata = json.loads(audio.with_suffix(".json").read_text())
                assert metadata["schemaVersion"] == 8
                assert metadata["approximantRevision"] == 1
                assert metadata["articulationPlanRevision"] == 13
                assert metadata["approval"] == "unapproved"
                markers = metadata["markers"]
                assert [m["phone"] for m in markers] == ["r", "a", "w", "a", "y", "a", "a"]
                assert [m["kind"] for m in markers[:6]] == ["approximant", "oral-vowel"] * 3
                assert [m["kind"] for m in markers[6:]] == ["oral-vowel"]
                raw = audio.read_bytes()
                assert hashlib.sha256(raw).hexdigest() == row["sha256"]
                rate, samples = _float_mono(raw)
                window = rate // 200
                for index, marker in enumerate(markers):
                    if marker["kind"] != "approximant":
                        continue
                    start, end = marker["startFrame"], marker["endFrame"]
                    # No silent segment anywhere in the glide: a closure would have one.
                    for offset in range(start, end, window):
                        assert max(abs(value) for value in samples[offset:min(end, offset + window)]) > 1e-6
                    quarter = (end - start) // 4
                    head = _band_profile(samples[start:start + quarter], rate)
                    tail = _band_profile(samples[end - quarter:end], rate)
                    # The gesture is a movement rather than a static pose. That it moves toward
                    # the vowel is asserted where the declared duration can be compared against a
                    # control, in seam_articulation_context_tests; this fixture is fixed.
                    assert _band_distance(head, tail) > 0.05
        assert glide_hashes[0] == glide_hashes[1]

        # A declared event phone is a span, not a recorded articulation: a closure is exactly
        # silent for the span its role resolves and a breath is unvoiced noise from its own
        # source. The fixture renders the moraic obstruent as a vowel's coda, a breath, a closure,
        # a glottal occlusion before its vowel, a pause and a bare vowel as the control.
        event_hashes = []
        for name in ("events", "events-repeat"):
            subprocess.run([str(binary), str(root / name), "events"], check=True,
                           capture_output=True, timeout=60)
            report = json.loads((root / name / "pilot.json").read_text())
            event_hashes.append([row["sha256"] for row in report["runs"]])
            for row in report["runs"]:
                audio = Path(row["wav"])
                if audio.parent.name != "candidates":
                    continue
                metadata = json.loads(audio.with_suffix(".json").read_text())
                assert metadata["schemaVersion"] == 11
                assert metadata["closureRevision"] == 1
                assert metadata["breathRevision"] == 1
                assert metadata["articulationPlanRevision"] == 13
                assert metadata["approval"] == "unapproved"
                markers = metadata["markers"]
                assert [(m["phone"], m["kind"]) for m in markers] == [
                    ("a", "oral-vowel"), ("R", "closure"), ("br", "breath"), ("cl", "closure"),
                    ("glottal", "closure"), ("a", "oral-vowel"), ("pau", "closure"), ("a", "oral-vowel")]
                raw = audio.read_bytes()
                assert hashlib.sha256(raw).hexdigest() == row["sha256"]
                rate, samples = _float_mono(raw)
                for marker in markers:
                    start, end = marker["startFrame"], marker["endFrame"]
                    span = samples[start:end]
                    assert span, marker
                    if marker["kind"] == "closure":
                        # A closure is silence, exactly: no residual excitation, no noise.
                        assert all(value == 0 for value in span), marker
                    else:
                        assert any(value != 0 for value in span), marker
                # The vowel-to-coda unit keeps its vowel: the moraic obstruent only owns the tail.
                assert any(value != 0 for value in samples[0:markers[1]["startFrame"]])
        assert event_hashes[0] == event_hashes[1]
    print("Pilot repeatability, finite/nonzero PCM, variant identity and no-overwrite checks passed; quality unassessed.")


if __name__ == "__main__":
    main()
