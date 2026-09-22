"""Inspect the frozen 42-input LF panel; descriptive evidence, never acceptance.

Reads retained artifacts only. Does not render, filter, normalize, choose clips,
change the original scores, or overwrite an existing diagnostics file.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import stat

import numpy as np

from tools.singing_quality.prepare_world_reference import load_lock
from tools.singing_quality.source_frequency_diagnostic import diagnose
from tools.voice_model_training.audio_source import decode_pcm_source

PROTOCOL_SHA256 = "348f0cc113908ae4690cb17cc2dba1ea90eafc05bb73542711cb13ceeee7e649"
PROTOCOL_PATH = Path(__file__).resolve().parents[2] / "docs/implementation/U16_LF_CONTROL_PANEL_2026-09-22.md"
OUTPUT_IDS = {"seam-experiment", "world-reconstruction", "world-forced-unvoiced"}
ARRAY_FILES = {
    "timeAxis": "time-axis.f64le", "analysisF0": "analysis-f0.f64le",
    "spectralEnvelope": "envelope.f64le", "analysisAperiodicity": "aperiodicity.f64le",
    "unvoicedSynthesisF0": "unvoiced-f0.f64le",
    "unvoicedSynthesisAperiodicity": "unvoiced-aperiodicity.f64le",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_bounded(path: Path, limit: int, expected_hash: str | None = None) -> bytes:
    # Local retained-packet check, not a claim of concurrent filesystem immutability.
    require(stat.S_ISREG(path.lstat().st_mode), f"Nonregular artifact: {path}")
    with path.open("rb") as stream:
        payload = stream.read(limit + 1)
    require(len(payload) <= limit, f"Artifact exceeds limit: {path}")
    if expected_hash is not None:
        require(hashlib.sha256(payload).hexdigest() == expected_hash, f"Digest mismatch: {path}")
    return payload


def expected_controls() -> dict:
    result = {}
    for rate in (44100, 48000):
        for midi in (48, 60, 72):
            prefix = f"lf-{rate}-{midi}"
            clean = f"{prefix}-none"
            result[clean] = (rate, midi, 0, 0, clean)
            for frequency in (10, 30):
                for amplitude in (5, 20, 60):
                    result[f"{prefix}-{frequency}hz-a{amplitude:02}"] = (
                        rate, midi, frequency, amplitude / 100, clean)
    return result


def validate_cases(cases: list) -> dict:
    expected = expected_controls()
    require(len(cases) == 42, "Expected all 42 cases")
    by_id = {item["id"]: item for item in cases}
    require(len(by_id) == 42 and by_id.keys() == expected.keys(), "Duplicate or unexpected case IDs")
    for identity, (rate, midi, frequency, amplitude, clean) in expected.items():
        item, control = by_id[identity], by_id[identity]["control"]
        require(item["sampleRate"] == rate and item["sourceFrames"] == rate // 2,
                f"Source geometry differs: {identity}")
        require((control["midi"], control["lfHz"], control["lfPeakAmplitude"], control["cleanCaseId"])
                == (midi, frequency, amplitude, clean), f"Control differs: {identity}")
        require(control["voicedGainFloat32"] == float(np.float32(0.2))
                and control["lfPhaseRadians"] == 0, f"Gain/phase differs: {identity}")
        hz = 440 * math.exp2((midi - 69) / 12)
        require(all(math.isclose(item[key], hz, rel_tol=1e-14, abs_tol=0)
                    for key in ("analysisConstantHz", "seamCarrierHz", "oracleHz")),
                f"Underlying voiced F0 differs: {identity}")
        require(len(item["outputs"]) == 3 and {x["id"] for x in item["outputs"]} == OUTPUT_IDS,
                f"Incomplete/duplicate outputs: {identity}")
    return by_id


def source_pcm(directory, name, digest, rate):
    payload = read_bounded(directory / name, 256 * 1024, digest)
    metadata, samples = decode_pcm_source(payload, expected_sha256=digest, sample_rate=rate)
    require(metadata.get("sampleEncoding") == "ieee-float32-le" and samples.size == rate // 2,
            f"Expected 500 ms Float32 component: {name}")
    return payload, samples


def verify_components(source, voiced, lf, rate, frequency, amplitude):
    require(np.max(np.abs(source)) <= float(np.float32(0.69)), "Source exceeds frozen headroom")
    recomposed = (voiced + lf).astype("<f4")
    require(recomposed.tobytes() == source.astype("<f4").tobytes(), "Float32 recomposition differs")
    expected = (amplitude * np.sin(2 * np.pi * frequency * np.arange(rate // 2) / rate)).astype("<f4")
    # NumPy and C++ libm can round differently; only this independent sine check
    # has a tolerance. Retained components/recomposition above are bit-exact.
    require(np.allclose(lf, expected, rtol=0, atol=1e-7), "LF component differs from frozen sine")


def verify_analysis(directory, item):
    require(item["retainedAnalysisFile"] == f"{item['id']}/analysis.json", "Unexpected analysis path")
    admission = json.loads(read_bounded(directory / "analysis.json", 2 * 1024 * 1024,
                                        item["retainedAnalysisSha256"]))
    require(admission["parameters"] == item["parameters"], "Analysis parameter identities differ")
    rows, bins = item["analysisRows"], item["bins"]
    require(rows == 101 and bins == 1025 and item["fftSize"] == 2048
            and admission["rows"] == rows and admission["bins"] == bins, "Analysis geometry differs")
    require(item["parameters"].keys() == ARRAY_FILES.keys(), "Analysis arrays incomplete")
    arrays = {}
    for key, name in ARRAY_FILES.items():
        record = item["parameters"][key]
        require(record["file"] == name, "Unexpected array path")
        payload = read_bounded(directory / name, 4 * 1024 * 1024, record["sha256"])
        values = np.frombuffer(payload, dtype="<f8")
        size = rows if key in ("timeAxis", "analysisF0", "unvoicedSynthesisF0") else rows * bins
        require(values.size == record["elements"] == size, "Array shape differs")
        arrays[key] = values
    require(np.array_equal(arrays["timeAxis"], np.arange(rows) * 5.0 / 1000), "Time axis differs")
    require(np.all(arrays["analysisF0"] == item["analysisConstantHz"]), "Analysis F0 differs")
    require(np.isfinite(arrays["spectralEnvelope"]).all() and np.all(arrays["spectralEnvelope"] > 0),
            "Invalid spectral envelope")
    require(np.all(arrays["unvoicedSynthesisF0"] == 0)
            and np.all(arrays["unvoicedSynthesisAperiodicity"] == 1), "UV overrides differ")
    ap = arrays["analysisAperiodicity"]
    masks = {"nan": np.isnan(ap), "positiveInfinity": np.isposinf(ap), "negativeInfinity": np.isneginf(ap),
             "outOfRangeFinite": np.isfinite(ap) & ((ap < 0) | (ap > 1))}
    for key, mask in masks.items():
        require(admission[key + "IndicesRowMajor"] == np.flatnonzero(mask).tolist(), "AP index mask differs")
        count_key = "aperiodicityOutOfRangeFiniteCells" if key == "outOfRangeFinite" else key + "Cells"
        require(admission[count_key] == int(mask.sum()), "AP mask count differs")
    finite = int(np.isfinite(ap).sum())
    require(admission["aperiodicityFiniteCells"] == finite
            and admission["aperiodicityNonfiniteCells"] == ap.size - finite, "AP finite count differs")
    rejected = any(mask.any() for mask in masks.values())
    require(admission["synthesisAdmission"] == ("REJECTED_INVALID_ANALYSIS" if rejected else "ADMITTED")
            and item["executionStatus"] == ("ERROR" if rejected else "PASS"), "Admission differs")
    for output in item["outputs"]:
        status = "NOT_RUN" if rejected and output["id"] != "seam-experiment" else "PASS"
        require(output["executionStatus"] == status, "Output admission differs")
        if status == "PASS":
            require(output["outputFile"] == output["id"] + ".wav", "Unexpected output path")
            read_bounded(directory / output["outputFile"], 256 * 1024, output["outputSha256"])
    return arrays["spectralEnvelope"], rejected


def paired_metrics(current, clean):
    envelope, base = current["envelope"], clean["envelope"]
    require(envelope.shape == base.shape, "Paired envelopes differ in shape")
    result = dict(envelopeRelativeL1=float(np.abs(envelope - base).sum() / base.sum()),
                  envelopeSumRatio=float(envelope.sum() / base.sum()),
                  envelopeScope="All 101 analysis frames and 1025 bins; unweighted array sums", outputs=[])
    for identity in sorted(OUTPUT_IDS):
        case = current["outputs"][identity]
        reference = clean["outputs"][identity]
        if case["executionStatus"] != "PASS" or reference["executionStatus"] != "PASS":
            result["outputs"].append(dict(id=identity, status="UNAVAILABLE_PAIR",
                caseExecution=case["executionStatus"], cleanExecution=reference["executionStatus"]))
            continue
        case_rms = case["rmsRatio"] * current["sourceRms"]
        clean_rms = reference["rmsRatio"] * clean["sourceRms"]
        result["outputs"].append(dict(id=identity, status="AVAILABLE",
            outputRmsRatioToClean=case_rms / clean_rms,
            derivation="Retained C++ output/source RMS ratio times measured combined-source RMS, fixed 100-400 ms"))
    return result


def inspect_packet(path: Path) -> dict:
    require(stat.S_ISDIR(path.parent.lstat().st_mode), "Packet directory is not a real directory")
    payload = read_bounded(path, 2 * 1024 * 1024)
    packet = json.loads(payload)
    require(packet["formatId"] == "com.project-seam.world-lf-controls" and packet["schemaVersion"] == 1,
            "Unexpected panel format")
    require(packet["protocolSha256"] == PROTOCOL_SHA256, "Protocol identity differs")
    read_bounded(PROTOCOL_PATH, 64 * 1024, PROTOCOL_SHA256)
    lock, lock_hash = load_lock()
    require(packet["sourceLockSha256"] == lock_hash and packet["worldRevision"] == lock["revision"],
            "WORLD lock identity differs")
    require(packet["variant"] in ("upstream", "openutau-d4c-guard"), "Unexpected WORLD arm")
    require(packet["productionEnabled"] is False and packet["releaseEligible"] is False
            and packet["listeningStatus"] == "NOT_REVIEWED", "Unexpected promotion claim")
    require(packet["evidencePacketStatus"] == "COMPLETE" and packet["unexpectedErrors"] == 0
            and packet["artifactErrors"] == 0, "Incomplete execution packet")
    cases = validate_cases(packet["cases"])
    require(len({x["sourceWavSha256"] for x in cases.values()}) == 42, "Combined sources are not unique")
    require(len({x["control"]["voicedComponent"]["sha256"] for x in cases.values()}) == 6,
            "Expected six unique voiced bases")
    measured, rejections = {}, 0
    for identity, item in cases.items():
        directory, control = path.parent / identity, item["control"]
        require(stat.S_ISDIR(directory.lstat().st_mode), "Case is not a real directory")
        rate = item["sampleRate"]
        source_bytes, source = source_pcm(directory, "source.wav", item["sourceWavSha256"], rate)
        components = []
        for key, name in (("voicedComponent", "voiced.wav"), ("lfComponent", "lf.wav")):
            record = control[key]
            require(record["file"] == name and record["frames"] == rate // 2, "Component metadata differs")
            components.append(source_pcm(directory, name, record["sha256"], rate)[1])
        verify_components(source, *components, rate, control["lfHz"], control["lfPeakAmplitude"])
        clean = cases[control["cleanCaseId"]]
        require(control["voicedComponent"]["sha256"] == clean["sourceWavSha256"], "Paired voiced bytes differ")
        envelope, rejected = verify_analysis(directory, item)
        rejections += int(rejected)
        diagnostic = diagnose(source_bytes, item["sourceWavSha256"], rate)
        measured[identity] = dict(envelope=envelope, diagnostic=diagnostic,
            sourceRms=diagnostic["fixed100To400ms"]["rms"], outputs={x["id"]: x for x in item["outputs"]})
    require(rejections == packet["analysisRejections"] and packet["executionStatus"] ==
            ("ERROR" if rejections else "PASS"), "Aggregate admission differs")
    result = {key: packet[key] for key in ("variant", "configuration", "executableSha256",
              "protocolSha256", "sourceLockSha256", "sourceManifestSha256", "worldRevision")}
    result.update(formatId="com.project-seam.lf-control-diagnostics", schemaVersion=1,
        comparisonSha256=hashlib.sha256(payload).hexdigest(),
        diagnosticSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        sourceDecoderSha256=hashlib.sha256((Path(__file__).resolve().parents[1] /
                                           "voice_model_training/audio_source.py").read_bytes()).hexdigest(),
        analysisRejections=rejections, productionEnabled=False, releaseEligible=False,
        listeningStatus="NOT_REVIEWED", numpyVersion=np.__version__,
        interpretation="Fixed-construction sensitivity, not quality ranking, LF decomposition, or population error", cases=[])
    for identity, value in measured.items():
        clean_id = cases[identity]["control"]["cleanCaseId"]
        result["cases"].append(dict(id=identity, cleanCaseId=clean_id,
            sourceFrequency=value["diagnostic"], paired=paired_metrics(value, measured[clean_id])))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("comparison", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        rendered = json.dumps(inspect_packet(args.comparison), indent=2, allow_nan=False) + "\n"
        with args.output.open("x", encoding="utf-8") as stream:
            stream.write(rendered)
    except (OSError, ValueError, KeyError, TypeError, ZeroDivisionError) as error:
        parser.exit(2, f"LF_CONTROL_DIAGNOSTICS=ERROR: {error}\n")


if __name__ == "__main__":
    main()
