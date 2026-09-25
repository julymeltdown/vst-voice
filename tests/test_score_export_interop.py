"""External-interoperability coverage for the bank tool's score export.

The point of this test is to make one specific claim reproducible: a score SEAM
itself wrote is readable by a tool that is not SEAM. The evidence it records came
from OpenUtau's own Ustx.Load path (commit 8c0dc40) reading a production
`export-score` result, and from DryWetMidi 7.2.0 -- the library OpenUtau uses for
MIDI -- reading the SMF half of the same exchange.

OpenUtau and .NET are optional local dependencies, not build dependencies of
this repository. What this test locks down without those optional tools is that
the exact bytes SEAM produces for a pinned project must keep satisfying the
properties the external reader relied on. If SEAM's writer changes in a way that
would break a real OpenUtau import, the fixture comparison fails here, and the
recorded oracle expectation below says which fields are at stake.

Regenerate the recorded expectations with:

    seam_voicebank_cli export-score tests/singing_quality/corpus/original-melody.seam OUT.ustx
    seam_voicebank_cli export-score tests/singing_quality/corpus/original-melody.seam OUT.mid

then re-read both with the oracle in tools/openutau_oracle/ and update the
constants below to match. Updating them without re-running the oracle defeats the
purpose of the test.
"""
import hashlib
import json
import re
import struct
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "tests/singing_quality/corpus/original-melody.seam"

# The USTX export is lossy by construction (SEAM singer identity, bus routing and
# smoothstep pitch cannot be represented in USTX 0.9), so the reported loss count is
# part of the contract: a conversion that suddenly reports zero losses is lying.
EXPECTED_USTX_LOSSES = 2
# The writer emits an empty comment so its own output does not fabricate an
# unsupported-comment loss on reimport; this exact byte revision passed the
# pinned OpenUtau Ustx.Load oracle.
EXPECTED_USTX_CONTENT_HASH = "097b21716c75c1e03d46119c6a0216a60617b6e3fa048745a97adfff7f416f9c"
# SMF carries no SEAM performance metadata, and this project has none to carry, so a
# loss-free SMF export is the correct outcome here -- not a suppressed report.
EXPECTED_SMF_LOSSES = 0
EXPECTED_SMF_CONTENT_HASH = "774231a09739d0f27ad39a4984a380bf0b5429ae5ac128c4259f2eb7a4afb79f"
# PPQ conversion: the project is authored at 960 PPQ, USTX is defined at 480 PPQ.
EXPECTED_PPQ = 960
EXPECTED_USTX_PPQ = 480


def run(executable, *arguments, expect=0):
    result = subprocess.run([executable, *arguments], capture_output=True, text=True, timeout=120)
    assert result.returncode == expect, (arguments, result.returncode, result.stdout, result.stderr)
    return result


def fields(output):
    return dict(line.split("=", 1) for line in output.strip().splitlines() if "=" in line)


def project_notes():
    """The authored notes, as the project file itself states them."""
    document = json.loads(PROJECT.read_text())
    track = document["vocalTracks"][0]
    region = track["regions"][0]
    surfaces = {item["id"]: item["surface"] for item in region["lyrics"]}
    return [
        (note["startTick"], note["durationTick"], note["midiKey"], surfaces[note["lyricId"]])
        for note in region["notes"]
    ]


def read_smf_lyrics(path):
    """Minimal SMF reader: walk the single track and collect lyric meta events.

    Written by hand rather than with a library so the test depends only on the SMF
    specification, which is what makes it meaningful evidence about the file.
    """
    data = path.read_bytes()
    assert data[:4] == b"MThd", data[:4]
    header_length = struct.unpack(">I", data[4:8])[0]
    _format, tracks, division = struct.unpack(">HHH", data[8:8 + 6])
    assert tracks == 1, tracks
    assert division == EXPECTED_PPQ, division
    offset = 8 + header_length
    assert data[offset:offset + 4] == b"MTrk", data[offset:offset + 4]
    track_length = struct.unpack(">I", data[offset + 4:offset + 8])[0]
    body = data[offset + 8:offset + 8 + track_length]
    assert len(body) == track_length, (len(body), track_length)

    def read_vlq(payload, position):
        value = 0
        while True:
            byte = payload[position]
            position += 1
            value = (value << 7) | (byte & 0x7F)
            if not byte & 0x80:
                return value, position

    lyrics, notes, tick, position, running = [], [], 0, 0, None
    while position < len(body):
        delta, position = read_vlq(body, position)
        tick += delta
        status = body[position]
        if status & 0x80:
            running = status
            position += 1
        else:
            status = running
        if status == 0xFF:
            kind = body[position]
            position += 1
            length, position = read_vlq(body, position)
            payload = body[position:position + length]
            position += length
            if kind == 0x05:
                lyrics.append((tick, payload.decode("utf-8")))
            elif kind == 0x2F:
                break
        elif status & 0xF0 == 0x90:
            notes.append((tick, body[position], body[position + 1], body[position + 2]))
            position += 2
        elif status & 0xF0 in (0x80, 0xA0, 0xB0, 0xE0):
            position += 2
        elif status & 0xF0 in (0xC0, 0xD0):
            position += 1
        else:
            raise AssertionError(f"unexpected status byte {status:#04x} at {position}")
    return lyrics, notes


def parse_ustx(path):
    """Read back the USTX 0.9 fields the external reader checked.

    Parsed with a small targeted reader so the assertion is about the file's own
    text, not about a YAML library's interpretation of it.
    """
    text = path.read_text()
    version = re.search(r'^ustx_version:\s*"?([\d.]+)"?', text, re.M)
    assert version and version.group(1) == "0.9", version
    notes = re.findall(
        r"position:\s*(-?\d+)\s*\n\s*duration:\s*(-?\d+)\s*\n\s*tone:\s*(\d+)\s*\n\s*lyric:\s*(.+)",
        text,
    )
    tempos = re.findall(r"\{position:\s*(\d+),\s*bpm:\s*([\d.]+)\}", text)
    return version.group(1), notes, tempos


def main():
    executable = sys.argv[1]
    assert PROJECT.is_file(), PROJECT
    authored = project_notes()
    assert authored, "the pinned corpus project must contain notes"

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        ustx, smf = root / "out.ustx", root / "out.mid"

        ustx_result = run(executable, "export-score", str(PROJECT), str(ustx))
        ustx_fields = fields(ustx_result.stdout)
        assert ustx_fields["destination"].endswith("out.ustx"), ustx_fields
        assert ustx_fields["contentHash"] == EXPECTED_USTX_CONTENT_HASH, ustx_fields
        assert int(ustx_fields["issues"]) == EXPECTED_USTX_LOSSES, ustx_fields
        # Every conversion loss must be named at its source path; a bare count is not a report.
        reported = [line for line in ustx_result.stderr.splitlines() if line.startswith("loss: ")]
        assert len(reported) == EXPECTED_USTX_LOSSES, ustx_result.stderr
        assert all(": " in line for line in reported), reported

        smf_result = run(executable, "export-score", str(PROJECT), str(smf))
        smf_fields = fields(smf_result.stdout)
        assert smf_fields["destination"].endswith("out.mid"), smf_fields
        assert smf_fields["contentHash"] == EXPECTED_SMF_CONTENT_HASH, smf_fields
        assert int(smf_fields["issues"]) == EXPECTED_SMF_LOSSES, smf_fields

        # --- USTX: the properties OpenUtau's load path confirmed ---
        version, ustx_notes, tempos = parse_ustx(ustx)
        assert version == "0.9", version
        assert tempos, "USTX must carry the tempo map"
        assert len(ustx_notes) == len(authored), (len(ustx_notes), len(authored))
        for (position, duration, tone, lyric), (start, length, key, surface) in zip(ustx_notes, authored):
            # 960 PPQ project -> 480 PPQ USTX. This project sits on the grid exactly, so a
            # scaling error would show up as a wrong position rather than a rounding report.
            scale = EXPECTED_USTX_PPQ / EXPECTED_PPQ
            assert int(position) == round(start * scale), (position, start)
            assert int(duration) == round(length * scale), (duration, length)
            assert int(tone) == key, (tone, key)
            # Multi-byte lyrics must survive as UTF-8; an ASCII-only writer would have
            # replaced these with '?' and this is exactly what the oracle caught once.
            assert surface in lyric, (lyric, surface)
            assert "?" not in lyric, lyric

        # --- SMF: the properties DryWetMidi (OpenUtau's MIDI reader) confirmed ---
        lyrics, notes = read_smf_lyrics(smf)
        assert len(notes) == len(authored), (len(notes), len(authored))
        assert len(lyrics) == len(authored), (lyrics, len(authored))
        for (tick, text), (start, _length, _key, surface) in zip(lyrics, authored):
            # SMF keeps the project's own 960 PPQ, so no scaling should appear here.
            assert tick == start, (tick, start)
            assert text == surface, (text, surface)
        for (tick, key, velocity, _off), (start, _length, expected_key, _surface) in zip(notes, authored):
            assert tick == start, (tick, start)
            assert key == expected_key, (key, expected_key)
            assert velocity > 0, velocity

        # --- Inbound half: a foreign file must become a SEAM project, then survive being sent back ---
        # The MIDI round trip is the sharpest test available without a DAW. Importing the export of a
        # pinned project and re-exporting it must reproduce the exact bytes, because nothing in this
        # exchange is approximated; a regression that normalised timing or dropped a lyric would change
        # the hash rather than merely shifting a rounding.
        foreign = root / "foreign.mid"
        imported_project = root / "imported.seam"
        reexported = root / "reexported.mid"
        run(executable, "export-score", str(PROJECT), str(foreign))
        imported = run(executable, "import-score", str(foreign), str(imported_project), "Interop Round Trip")
        imported_fields = fields(imported.stdout)
        assert int(imported_fields["issues"]) == 0, imported.stderr
        assert imported_project.is_file(), imported_project
        document = json.loads(imported_project.read_text())
        region = document["vocalTracks"][0]["regions"][0]
        assert len(region["notes"]) == len(authored), len(region["notes"])
        assert len(region["lyrics"]) == len(authored), len(region["lyrics"])
        surfaces = sorted(item["surface"] for item in region["lyrics"])
        assert surfaces == sorted(surface for *_rest, surface in authored), surfaces

        run(executable, "export-score", str(imported_project), str(reexported))
        assert reexported.read_bytes() == foreign.read_bytes(), "MIDI round trip changed the file"

        # USTX is deliberately lossy, so byte equality is not the claim there. What must hold is that
        # every approximation is named and that the notes the external reader confirmed still survive.
        foreign_ustx = root / "foreign.ustx"
        ustx_project = root / "imported-ustx.seam"
        run(executable, "export-score", str(PROJECT), str(foreign_ustx))
        ustx_import = run(executable, "import-score", str(foreign_ustx), str(ustx_project), "Ustx Round Trip")
        for line in ustx_import.stderr.splitlines():
            assert line.startswith(("loss: ", "warning: ")), line
        ustx_document = json.loads(ustx_project.read_text())
        ustx_region = ustx_document["vocalTracks"][0]["regions"][0]
        assert len(ustx_region["notes"]) == len(authored), len(ustx_region["notes"])

        # --- Fail-closed behaviour: bad input must not produce a file ---
        # An import must not be able to replace an existing project. Codec::save writes atomically but
        # replaces the destination and moves the old file aside, so a mistyped path would silently
        # overwrite real work with unfamiliar data. The command uses create-new semantics instead.
        occupied = root / "occupied.seam"
        occupied.write_text("ORIGINAL USER CONTENT\n")
        collision = run(executable, "import-score", str(foreign), str(occupied), "Collision", expect=6)
        assert occupied.read_text() == "ORIGINAL USER CONTENT\n", occupied.read_text()
        assert not (root / "occupied.seam.bak").exists(), "the previous file must not be moved aside"
        assert "exist" in collision.stderr.lower(), collision.stderr
        bad_extension = run(executable, "import-score", str(PROJECT), str(root / "never.seam"), expect=3)
        assert "ustx" in bad_extension.stderr, bad_extension.stderr
        assert not (root / "never.seam").exists()
        run(executable, "import-score", str(root / "absent.ustx"), str(root / "none.seam"), expect=4)
        assert not (root / "none.seam").exists()
        refused = run(executable, "export-score", str(PROJECT), str(root / "out.wav"), expect=3)

        assert "ustx" in refused.stderr, refused.stderr
        assert not (root / "out.wav").exists()
        missing = run(executable, "export-score", str(root / "absent.seam"), str(root / "never.ustx"), expect=2)
        assert "absent.seam" in missing.stderr or "error" in missing.stderr, missing.stderr
        assert not (root / "never.ustx").exists()


if __name__ == "__main__":
    main()
