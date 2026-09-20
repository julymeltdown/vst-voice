"""Identity-bound application campaign comparison; never automatic promotion.

Re-measure captured WAVs with the captured native extractor before comparing.
No subset selection, fitted alignment, failed-item exclusion or quality waiver.
"""
import argparse
import json
import math
from pathlib import Path

from .__main__ import load_config, publish_new
from .compare_application_export import measure
from .pitch_comparison import _capture, _digest


METRICS = ("meanAbsoluteCents", "measurableVoicedPairs", "withinToleranceFrames",
           "unmeasurableFrames", "voicingMismatchFrames")
HIGHER_IS_BETTER = {"measurableVoicedPairs", "withinToleranceFrames"}


def load_campaign(path, digest, executable):
    """Consume an explicitly trusted receipt and revalidate its on-disk closure."""
    path, executable = Path(path), Path(executable)
    if path.parent.is_symlink():
        raise ValueError("Campaign directory cannot be a symlink")
    report = load_config(path, digest)
    if (report.get("formatId") != "com.project-seam.validation-campaign"
            or type(report.get("schemaVersion")) is not int or report["schemaVersion"] != 1
            or report.get("singerQualified") is not False or report.get("releaseEligible") is not False):
        raise ValueError("Expected an unqualified validation campaign")
    selection = load_config(path.parent / "selection.json", report["selectionReceiptSha256"])
    items = report.get("items")
    if (not isinstance(items, list) or not 1 <= len(items) <= 16
            or type(report.get("selectedCount")) is not int or report["selectedCount"] != len(items)
            or not isinstance(selection.get("items"), list) or len(selection["items"]) != len(items)):
        raise ValueError("Campaign must retain the entire bounded selection")
    for key in ("selectionSha256", "corpusSha256"):
        if not _digest(report.get(key)) or report[key] != selection.get(key):
            raise ValueError("Campaign/selection identity differs")
    executable_hash = _capture(executable, 128 * 1024 * 1024)[1]
    binaries = selection.get("binarySha256")
    if (not isinstance(binaries, dict) or not binaries
            or any(not _digest(value) for value in binaries.values())
            or binaries.get(str(executable.resolve())) != executable_hash):
        raise ValueError("Native extractor differs from the campaign executable")
    rows, seen = [], set()
    for index, (item, selected) in enumerate(zip(items, selection["items"])):
        identity = {key: item[key] for key in ("sourceId", "sourceSha256", "projectSha256")}
        source = identity["sourceId"]
        if (not isinstance(source, str) or not 1 <= len(source.encode()) <= 256
                or source in seen or identity != selected
                or not all(_digest(identity[key]) for key in ("sourceSha256", "projectSha256"))
                or item.get("directory") != f"song-{index:03d}"
                or item.get("execution") not in ("PASSED", "FAILED")):
            raise ValueError("Campaign row differs from its complete frozen selection")
        seen.add(source)
        directory = path.parent / item["directory"]
        if directory.is_symlink():
            raise ValueError("Song directory cannot be a symlink")
        if (_capture(directory / "source.wav", 64 * 1024 * 1024)[1] != identity["sourceSha256"]
                or _capture(directory / "input.seam", 4 * 1024 * 1024)[1] != identity["projectSha256"]):
            raise ValueError("Captured source/project changed")
        row = dict(identity, execution=item["execution"])
        if item["execution"] == "FAILED":
            row["failure"] = dict(errorType=item.get("errorType"), error=item.get("error"))
        else:
            comparison = load_config(directory / "comparison.json", item["comparisonSha256"])
            if (directory / "export").is_symlink():
                raise ValueError("Export directory cannot be a symlink")
            actual = measure(directory / "source.wav", directory / "export/master.wav", executable=executable)
            if actual["referenceSha256"] != identity["sourceSha256"]:
                raise ValueError("Measured source differs from the frozen selection")
            for key in ("formatId", "schemaVersion", "referenceSha256", "candidateSha256",
                        "reference", "candidate", "channelPolicy", "alignment", "pitchInputEncoding",
                        "singerQualified", "releaseEligible"):
                if comparison.get(key) != actual[key]:
                    raise ValueError("Application comparison differs from captured audio or policy")
            if (comparison["pitch"]["comparison"] != actual["pitch"]["comparison"]
                    or item.get("pitchStatus") != actual["pitch"]["comparison"]["status"]
                    or comparison["pitch"]["extractor"]["sha256"] != executable_hash
                    or type(comparison.get("spectralDistance")) not in (int, float)
                    or not math.isclose(comparison["spectralDistance"], actual["spectralDistance"],
                                        rel_tol=1e-12, abs_tol=1e-12)):
                raise ValueError("Recorded metrics differ from fresh measurement")
            row["metrics"] = dict(spectralDistance=actual["spectralDistance"],
                **{key: actual["pitch"]["comparison"][key] for key in METRICS})
            row["pitchStatus"] = item["pitchStatus"]
        rows.append(row)
    if report.get("executionPassed") is not all(row["execution"] == "PASSED" for row in rows):
        raise ValueError("Campaign execution summary disagrees with rows")
    if _capture(executable, 128 * 1024 * 1024)[1] != executable_hash:
        raise ValueError("Extractor changed during remeasurement")
    return dict(campaignSha256=digest, selection=selection, rows=rows,
                inferenceWorkerSha256=report.get("inferenceWorkerSha256"))


def summarize(rows):
    # Missing or unmeasurable songs cannot disappear into a successful aggregate.
    if any(row["execution"] != "PASSED" or row["metrics"]["meanAbsoluteCents"] is None
           or row["metrics"]["measurableVoicedPairs"] <= 0 for row in rows):
        return None
    result = {key: sum(row["metrics"][key] for row in rows) for key in METRICS[1:]}
    pairs = result["measurableVoicedPairs"]
    result["weightedMeanAbsoluteCents"] = sum(row["metrics"]["meanAbsoluteCents"]
        * row["metrics"]["measurableVoicedPairs"] for row in rows) / pairs
    result["withinToleranceFraction"] = result["withinToleranceFrames"] / pairs
    return result


def compare(baseline, candidate):
    for key in ("selectionSha256", "corpusSha256", "items", "silencePhone", "binarySha256"):
        if baseline["selection"].get(key) != candidate["selection"].get(key):
            raise ValueError("Campaigns use different selection, source, project, silence or executable identities")
    baseline_worker = baseline.get("inferenceWorkerSha256")
    candidate_worker = candidate.get("inferenceWorkerSha256")
    if baseline_worker is not None and candidate_worker is not None and baseline_worker != candidate_worker:
        raise ValueError("Campaigns ran different inference worker binaries")
    if len(baseline["rows"]) != len(candidate["rows"]):
        raise ValueError("Campaign row counts differ")
    rows = []
    for before, after in zip(baseline["rows"], candidate["rows"]):
        if any(before[key] != after[key] for key in ("sourceId", "sourceSha256", "projectSha256")):
            raise ValueError("Paired source/project identities differ")
        row = dict(sourceId=before["sourceId"], baseline=before, candidate=after,
                   comparable=False, regressions=[], improvements=[])
        if (before["execution"] == after["execution"] == "PASSED"
                and before["metrics"]["meanAbsoluteCents"] is not None
                and after["metrics"]["meanAbsoluteCents"] is not None):
            row["comparable"] = True
            row["delta"] = {key: after["metrics"][key] - before["metrics"][key]
                            for key in ("spectralDistance", *METRICS)}
            for key, delta in row["delta"].items():
                if delta:
                    regressed = delta < 0 if key in HIGHER_IS_BETTER else delta > 0
                    row["regressions" if regressed else "improvements"].append(key)
        rows.append(row)
    status = ("INCOMPLETE" if not all(row["comparable"] for row in rows)
              else "HAS_REGRESSIONS" if any(row["regressions"] for row in rows)
              else "NO_REGRESSIONS_ON_REPORTED_METRICS")
    return dict(formatId="com.project-seam.paired-campaign-diagnostic", schemaVersion=1,
        baselineCampaignSha256=baseline["campaignSha256"], candidateCampaignSha256=candidate["campaignSha256"],
        rows=rows, status=status, baselineSummary=summarize(baseline["rows"]),
        candidateSummary=summarize(candidate["rows"]),
        baselineVocoderTrainingAudit=baseline["selection"].get("vocoderTrainingAudit"),
        candidateVocoderTrainingAudit=candidate["selection"].get("vocoderTrainingAudit"),
        combinedModelHoldoutVerified=False, singerQualified=False, releaseEligible=False,
        decisionPolicy="Descriptive per-song changes only; no promotion or qualification")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("baseline", "candidate", "pitch-executable", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    parser.add_argument("--baseline-sha256", required=True)
    parser.add_argument("--candidate-sha256", required=True)
    args = parser.parse_args()
    if args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir():
        parser.error("Output must be new with an existing parent")
    baseline = load_campaign(args.baseline, args.baseline_sha256, args.pitch_executable)
    candidate = load_campaign(args.candidate, args.candidate_sha256, args.pitch_executable)
    report = compare(baseline, candidate)
    publish_new(args.output, report)
    print(json.dumps({key: report[key] for key in ("status", "baselineSummary", "candidateSummary")}))


if __name__ == "__main__":
    main()
