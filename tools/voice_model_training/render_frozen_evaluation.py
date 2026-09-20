"""Render a frozen pilot cohort without assigning any source to training."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from .__main__ import load_config, publish_new
from .phrase_fingerprint import fingerprint, project_events, token_events
from .prepare_captured_teacher import capture_inputs


def validate_plan(plan, history):
    if (plan.get("formatId") != "com.project-seam.frozen-phrase-evaluation-plan"
            or type(plan.get("schemaVersion")) is not int or plan["schemaVersion"] != 1
            or plan.get("purpose") != "same-voice-unseen-phrase-engineering-evaluation"
            or plan.get("ppq") != 960 or plan.get("tempoBpm") != 120
            or plan.get("candidateRendered") is not False
            or plan.get("releaseEligible") is not False):
        raise ValueError("Expected a frozen, unrendered pilot evaluation plan")
    items, rows = plan.get("items"), history.get("items")
    if (not isinstance(items, list) or not 1 <= len(items) <= 256
            or not isinstance(rows, list) or not 1 <= len(rows) <= 10000
            or plan.get("historicalSourceCount") != len(rows)):
        raise ValueError("Invalid bounded cohort or historical index")
    keys = tuple(fingerprint(token_events(["あ:60:480"])))
    seen = {key: {row[key] for row in rows} for key in keys}
    ids = {row["sourceId"] for row in rows}
    for item in items:
        source = item.get("sourceId")
        if (not isinstance(source, str) or not re.fullmatch(r"procedural-song-[0-9]{5}", source)
                or source in ids):
            raise ValueError("Invalid or reused evaluation source identity")
        actual = fingerprint(token_events(item["tokens"]), ppq=plan["ppq"])
        if actual != item.get("fingerprints"):
            raise ValueError("Frozen tokens differ from their fingerprints")
        for key, value in actual.items():
            if value in seen[key]:
                raise ValueError(f"Frozen phrase collision: {source} {key}")
            seen[key].add(value)
        ids.add(source)
    return items


def render(*, plan, plan_sha256, history, pilot, output):
    plan = load_config(Path(plan), plan_sha256)
    history = load_config(Path(history), plan["historicalIndexSha256"])
    items = validate_plan(plan, history)
    pilot = Path(pilot).resolve(strict=True)
    if not pilot.is_file() or not os.access(pilot, os.X_OK):
        raise ValueError("Expected executable pilot")
    output = Path(output)
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError("Evaluation output must be new with an existing parent")
    pilot_hash = hashlib.sha256(pilot.read_bytes()).hexdigest()
    output.mkdir(mode=0o700)
    publish_new(output / "frozen-plan.json", plan)
    results = []
    for item in items:
        root = output / item["sourceId"]
        row = dict(sourceId=item["sourceId"], state="FAILED")
        try:
            completed = subprocess.run([str(pilot), str(root), "phrase", *item["tokens"]],
                capture_output=True, text=True, timeout=600)
            if completed.returncode:
                raise ValueError(f"Pilot exit {completed.returncode}: {completed.stderr[-500:]}")
            take = root / "baseline"
            receipt_hash = hashlib.sha256((take / "receipt.json").read_bytes()).hexdigest()
            candidates = sorted((take / "candidates").glob("*.json"))
            if len(candidates) != 1:
                raise ValueError("Expected exactly one captured candidate")
            relative = "candidates/" + candidates[0].name
            _, _, _, _, provenance = capture_inputs(take, receipt_hash, relative)
            project = load_config(take / "project.seam", provenance["projectSha256"])
            actual = fingerprint(project_events(project), ppq=project["ppq"])
            if actual != item["fingerprints"]:
                raise ValueError("Rendered project differs from frozen phrase")
            if provenance["recipeSha256"] != plan["voiceRecipeSha256"]:
                raise ValueError("Rendered voice differs from frozen voice recipe")
            row.update(state="CAPTURED", exportRoot=str(take.resolve()),
                       fingerprints=actual, **provenance)
        except (ValueError, OSError, KeyError, TypeError, subprocess.SubprocessError) as error:
            row["error"] = str(error)[:1000]
        results.append(row)
        publish_new(output / (item["sourceId"] + ".json"), row)
    unchanged = hashlib.sha256(pilot.read_bytes()).hexdigest() == pilot_hash
    report = dict(formatId="com.project-seam.frozen-evaluation-capture", schemaVersion=1,
        planSha256=plan_sha256, pilotSha256=pilot_hash, pilotUnchanged=unchanged,
        state="CAPTURED" if unchanged and all(r["state"] == "CAPTURED" for r in results) else "INCOMPLETE",
        items=results, trainingAdmitted=False, candidateRendered=False,
        newSourceAudioOverlapChecked=False, combinedModelHoldoutVerified=False,
        singerQualified=False, releaseEligible=False)
    publish_new(output / "capture.json", report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("plan", "plan-sha256", "history", "pilot", "output"):
        parser.add_argument("--" + name, required=True)
    report = render(**vars(parser.parse_args()))
    print(json.dumps(dict(state=report["state"], phrases=len(report["items"]))))
    return 0 if report["state"] == "CAPTURED" else 2


if __name__ == "__main__":
    raise SystemExit(main())
