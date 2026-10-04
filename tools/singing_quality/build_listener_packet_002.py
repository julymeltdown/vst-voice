"""Build a listening packet for the three questions measurement cannot answer.

After a long measurement campaign, three questions remain open and every one of them is a judgement about
what the audio should sound like, not about what it does sound like:

  1. LEAD TIMING. Every note begins 0.67 to 16.67 ms before its written start. Is that a legato lead that
     sounds intentional, or a timing fault that sounds like notes arriving early?
  2. HARMONIC BALANCE. At some pitches the recipe's second harmonic exceeds the fundamental. Does that
     read as a voice, or as a thin reedy tone that lacks body?
  3. THE MISSING NOTE. One note in one song is silent. Is that audible, and does it read as a dropped
     note or as a phrase break?

Each question needs audio that isolates it, and a paired A/B so the reviewer hears the difference rather
than judging in the abstract. Every file is produced through the production export path and is named for the
question it answers.
"""
import json
import pathlib
import shutil
import subprocess
import tempfile

import numpy as np

ROOT = pathlib.Path("/Users/lhs/Downloads/project-seam-usable-alpha-u3-master")
EXTRACTOR = ROOT / "build/release/seam_voicebank_cli"
RERENDER = pathlib.Path("/tmp/seam_score/rerender")
BASE = pathlib.Path("/Users/lhs/seam-corpus-large-2026-09-19/phrase-00022/baseline")
RECIPE = BASE / "recipes/7688afab747661d59ec1423a9a101114d61956bd062c464c45a74f8fb26f893e.json"
OUT = pathlib.Path(tempfile.mkdtemp(prefix="seam-listening-"))

OUT.mkdir(parents=True, exist_ok=True)
SURFACES = ["あ", "い", "う", "え", "お", "か", "き", "く"]


def build_project(midis, name, gap_ticks=0):
    source = json.loads((BASE / "project.seam").read_text())
    region = source["vocalTracks"][0]["regions"][0]
    lyric_ids = [l["id"] for l in region["lyrics"][:len(midis)]]
    note_ids = [n["id"] for n in region["notes"][:len(midis)]]
    region["lyrics"] = [{"id": lyric_ids[i], "language": "ja", "surface": SURFACES[i % len(SURFACES)]}
                         for i in range(len(midis))]
    notes = []
    tick = 0
    for i, midi in enumerate(midis):
        duration = 1920
        notes.append({
            "articulation": "normal", "durationTick": duration, "id": note_ids[i],
            "lyricId": lyric_ids[i], "midiKey": midi, "phoneticHint": None, "startTick": tick,
            "vibrato": {"depthCents": 50, "enabled": False, "fadeInFraction": 0.1,
                       "fadeOutFraction": 0.1, "periodMilliseconds": 180, "phaseTurns": 0,
                       "startFraction": 0.65}})
        tick += duration + gap_ticks
    region["notes"] = notes
    region["durationTick"] = tick
    dest = OUT / ("_src" + name)
    (dest / "recipes").mkdir(parents=True, exist_ok=True)
    (dest / "project.seam").write_text(json.dumps(source, ensure_ascii=False))
    shutil.copy(RECIPE, dest / "recipes" / RECIPE.name)
    return dest


def render(midis, name, gap_ticks=0):
    dest = build_project(midis, name, gap_ticks)
    out = OUT / ("_out" + name)
    result = subprocess.run([str(RERENDER), str(dest / "project.seam"), str(out)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        print("render failed for %s: %s" % (name, result.stderr[:200]))
        return None
    produced = out / "master.wav"
    final = OUT / (name + ".wav")
    shutil.copy(produced, final)
    return final


def info(path):
    out = subprocess.run([str(EXTRACTOR), "inspect-wav", str(path)],
                         capture_output=True, text=True)
    if out.returncode != 0:
        return {}
    return json.loads(out.stdout)


manifest = {
    "formatId": "com.project-seam.listening-packet",
    "schemaVersion": 1,
    "sourceCommit": subprocess.run(["git", "rev-parse", "HEAD"], cwd=str(ROOT),
                                    capture_output=True, text=True).stdout.strip(),
    "listeningStatus": "NOT_REVIEWED",
    "releaseEligible": False,
    "cases": [],
}


def add_case(identifier, question, verdict_options, what_to_hear, files):
    manifest["cases"].append({
        "id": identifier,
        "question": question,
        "verdictOptions": verdict_options,
        "whatToHear": what_to_hear,
        "files": files,
    })


# ---------------------------------------------------------------- question 1
# Lead timing. A pair of identical runs cannot show a timing offset to the ear on its own, so this gives a
# legato run (small gaps, where a lead reads as phrasing) and a detached run (long gaps, where the same lead
# reads as arriving early). The reviewer judges the same renderer twice and says whether the offset sounds
# intentional in one and faulty in the other.
legato = render([67, 64, 62, 60, 62, 64, 67], "q1-legato-run")
detached = render([67, 64, 62, 60, 62, 64, 67], "q1-detached-run", gap_ticks=960)
add_case(
    "q1-lead-timing",
    "Each note begins between 0.7 and 16.7 ms before its written start. Does that read as intentional "
    "legato phrasing, as notes arriving early, or as not audible at all?",
    {
        "A": "Intentional legato. The lead sounds like phrasing and the audio sounds deliberate.",
        "B": "Timing fault. Notes sound early or rushed, especially in the detached run.",
        "C": "Not audible. The lead cannot be heard as either phrasing or fault.",
    },
    "Play q1-legato-run.wav then q1-detached-run.wav. They are the same seven notes sung twice; the only "
    "difference is the written gap between them. Judge whether the onset lead reads as phrasing or as "
    "arrive-early in each.",
    [p.name for p in (legato, detached) if p])

# ---------------------------------------------------------------- question 2
# Harmonic balance. A second harmonic above the fundamental is a legitimate voice timbre; too much of it
# sounds thin. This gives the pitches measured as fundamental-dominant and second-dominant so the reviewer
# hears the range of what the recipe produces, and judges whether the weak-fundamental pitches sound wrong.
balanced = render([55, 60, 67, 72, 79], "q2-pitch-range")
weak = render([67], "q2-single-weak-fundamental")
add_case(
    "q2-harmonic-balance",
    "At some pitches the recipe's second harmonic is stronger than its fundamental. Does that read as a "
    "voice with brightness, or as a thin reedy tone?",
    {
        "A": "Sounds like a voice. The brightness is timbre, not thinness.",
        "B": "Sounds thin or reedy on the higher notes. The balance is wrong for a sung voice.",
        "C": "Cannot hear a difference between the notes.",
    },
    "Play q2-single-weak-fundamental.wav FIRST: that is MIDI 67 sung alone, measured with its second "
    "harmonic above its fundamental, which is the case in question. Then play q2-pitch-range.wav, five "
    "ascending notes where the same pitch sounds quite different. Say whether the solo note sounds thin or "
    "reedy, and whether the change between the two is a fault or just phrasing.",
    [p.name for p in (weak, balanced) if p])

# ---------------------------------------------------------------- question 3
# The missing note. One note in one song is silent. This plays the phrase with and without it, so the
# reviewer hears whether its absence is an event.
with_missing = pathlib.Path("/tmp/seam_voices2/out/original/master.wav")
if with_missing.exists():
    final = OUT / "q3-phrase-with-missing-note.wav"
    shutil.copy(with_missing, final)
    control = render([79, 67, 71, 64, 72, 62, 64], "q3-phrase-control")
    add_case(
        "q3-missing-note",
        "One note in this phrase renders silent. Is its absence audible, and does it read as a dropped "
        "note or as a phrase break?",
        {
            "A": "Audible as a dropped note. The phrase has a hole where a note should be.",
            "B": "Reads as a phrase break or a breath, not as a fault.",
            "C": "Cannot hear anything missing.",
        },
        "q3-phrase-with-missing-note.wav is the phrase as rendered. q3-phrase-control.wav is the same seven "
        "pitches without the silent note, for comparison. The silent note is the fifth written note, MIDI "
        "74 between MIDI 64 and MIDI 72. Say whether you can hear that something is missing.",
        [p.name for p in (final, control) if p])

# retain and verify
import hashlib

verified = []
for case in manifest["cases"]:
    for name in case["files"]:
        path = OUT / name
        if not path.exists():
            continue
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        details = info(path)
        verified.append({
            "file": name,
            "sha256": digest,
            "bytes": path.stat().st_size,
            "durationSeconds": details.get("durationSeconds"),
            "peak": details.get("peak"),
            "rms": details.get("rms"),
            "clippedSamples": details.get("clippedSamples"),
        })
manifest["artifacts"] = verified

(OUT / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")

print("listening packet built at", OUT)
print()
for case in manifest["cases"]:
    print("%s: %s" % (case["id"], case["files"]))
print()
print("%-38s %9s %10s %9s %9s %8s" % (
    "file", "seconds", "peak", "rms", "clipped", "sha256"))
for a in verified:
    print("%-38s %9.2f %10.4f %9.5f %9s %8s" % (
        a["file"], a["durationSeconds"] or 0.0, a["peak"] or 0.0, a["rms"] or 0.0,
        a["clippedSamples"], a["sha256"][:8]))
print()
print("total audio: %.2f s across %d files" % (
    sum(a["durationSeconds"] or 0.0 for a in verified), len(verified)))
