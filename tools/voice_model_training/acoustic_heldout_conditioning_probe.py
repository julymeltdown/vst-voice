"""Frozen-weight held-out probe under the admitted nonzero-breathiness controls.

This is a SEPARATE, explicitly labeled inference panel that tests the
conditioning-mismatch hypothesis: the r1 held-out panel ran on
song-NNN/conditioning.json whose breathiness is identically zero, while the
admitted training conditioning (conditioning-breathiness-r1) carries nonzero
per-frame controls. This probe reruns the same two frozen acoustic graphs on
the same 12 held-out songs but feeds the admitted breathiness shards, and
reports whether the acoustic output distribution changes. It does NOT modify
the frozen run verdict and is not a retraining.

Diagnostic only: no promotion, qualification, or release eligibility.
"""
import argparse
import hashlib
import io
import json
import math
import statistics
import tempfile
from pathlib import Path

import numpy as np
from scipy.io import wavfile

from .acoustics import wav_log_mel_targets
from .audio_source import decode_pcm_source
from .paired_vocoder_evaluation import run_graph
from .pitch_comparison import compare_wavs
from .vocoder_reconstruction import compute_stft_spectral_distance
from .acoustic_flatness_pair_eval import (_one_acoustic_session, _log_flatness_np,
    _level_np, _phone_spans, _eligible_frames, _panel_for_class, _sha,
    UNVOICED, VOICED, REST, SILENT_LEVEL_FLOOR, STEPS)


def _song_number(source_id):
    return int(source_id.rsplit("-", 1)[1])


def evaluate(arm_name, graph_bytes, root, cond_dir, song_dir, out_dir):
    base = Path(root) / song_dir
    payload = (base / "source.wav").read_bytes()
    src_sha = _sha(payload)
    labels = json.loads((base / "label-config.json").read_bytes())
    label = labels["labels"][0]["label"]
    vocab = labels["vocabulary"]
    phones = [dict(symbol=p["symbol"], startFrame=p["startFrame"], endFrame=p["endFrame"])
              for p in label["phonemes"]]
    _, audio = decode_pcm_source(payload, expected_sha256=src_sha, sample_rate=48000)
    _, ref_mel = wav_log_mel_targets(payload, expected_sha256=src_sha, sample_rate=48000)
    source_id = label.get("sourceId", song_dir)
    cond = json.loads((Path(cond_dir) / f"phrase-{_song_number(source_id):06d}.json").read_bytes())
    breath = np.asarray([[fr["breathiness"] for fr in cond["frames"]]], np.float32)
    tokens = [vocab.index(p["symbol"]) + 1 for p in label["phonemes"]]
    durations = [(p["endFrame"] + 255) // 256 - (p["startFrame"] + 255) // 256
                 for p in label["phonemes"]]
    f0 = np.asarray([label["f0Hz"]], np.float32)
    inputs = dict(tokens=np.asarray([tokens], np.int64),
                  durations=np.asarray([durations], np.int64), f0=f0,
                  steps=np.asarray(STEPS, np.int64), breathiness=breath)
    session = _one_acoustic_session(graph_bytes)
    mel = _run(session, inputs)
    del session
    frames = mel.shape[1]
    cand = mel[:, :frames].astype(np.float32)
    noise = np.zeros((1, frames * 64), np.float32)
    wave = run_graph(_VOCODER_GRAPH, cand, f0[:, :frames], frames=frames, noise=noise)
    wave = wave[:len(audio)]
    wav_dir = Path(out_dir) / "heldout-nonzero" / arm_name
    wav_dir.mkdir(parents=True, exist_ok=True)
    buf = io.BytesIO(); wavfile.write(buf, 48000, wave.astype(np.float32))
    (wav_dir / f"{song_dir}.wav").write_bytes(buf.getvalue())
    dist = compute_stft_spectral_distance(wave, audio)
    with tempfile.TemporaryDirectory(prefix="seam-ho-") as td:
        rp, cp = Path(td) / "ref.wav", Path(td) / "cand.wav"
        rp.write_bytes(payload); wavfile.write(cp, 48000, wave.astype(np.float32))
        pc = compare_wavs(rp, cp, executable=_PITCH_EXEC)
    comp = pc["comparison"]
    # mel-level unvoiced flatness + level on the nonsilent inventory
    ref = np.asarray(ref_mel[:frames], np.float64)
    level_ref = _level_np(ref); ref_logflat = _log_flatness_np(ref)
    cand64 = np.asarray(mel[0, :frames], np.float64)
    cand_logflat = _log_flatness_np(cand64)
    spans = _phone_spans(phones, frames)
    eligible = _eligible_frames(level_ref, spans, frames)
    uv_inv = _panel_for_class(UNVOICED, phones, eligible)
    uv_err, uv_flat = [], []
    for pi in uv_inv:
        p = phones[pi]
        l, r = (p["startFrame"] + 255) // 256, min(p["endFrame"] // 256, frames)
        elig = [f for f in range(l, r) if eligible[f]]
        if elig:
            uv_err.append(float(np.mean(np.abs(cand_logflat[elig] - ref_logflat[elig]))))
            mag = np.maximum(np.exp(cand64[elig]), 1e-10)
            uv_flat.append(float(np.mean(
                np.exp(np.mean(np.log(mag), axis=1)) / np.mean(mag, axis=1))))
    uv_ratios = []
    for p in phones:
        s, e = p["startFrame"], min(p["endFrame"], len(audio))
        if p["symbol"] in UNVOICED and e > s:
            rr = float(np.sqrt(np.mean(audio[s:e] ** 2)))
            cr = float(np.sqrt(np.mean(wave[s:e] ** 2)))
            if rr:
                uv_ratios.append(cr / rr)
    return dict(sourceId=source_id, sourceSha256=src_sha,
                breathinessMax=float(breath.max()),
                spectralDistance=float(dist),
                meanAbsolutePitchCents=comp["meanAbsoluteCents"],
                measurablePitchPairs=comp["measurableVoicedPairs"],
                unvoicedRmsRatioMean=(float(np.mean(uv_ratios)) if uv_ratios else None),
                unvoicedLogFlatnessError=(statistics.fmean(uv_err) if uv_err else None),
                unvoicedFlatness=(statistics.fmean(uv_flat) if uv_flat else None))


def _run(session, inputs):
    names = {v.name for v in session.get_inputs()}
    feeds = dict(inputs)
    if "breathiness" in names and "breathiness" not in feeds:
        feeds["breathiness"] = np.zeros_like(feeds["f0"])
    mel = session.run(["mel"], feeds)[0]
    if mel.dtype != np.float32 or mel.ndim != 3 or not np.isfinite(mel).all():
        raise ValueError("Invalid acoustic mel")
    return mel


_VOCODER_GRAPH = None
_PITCH_EXEC = None


def main():
    global _VOCODER_GRAPH, _PITCH_EXEC
    ap = argparse.ArgumentParser(description=__doc__)
    for n in ("control-export", "treatment-export", "vocoder-export", "source-root",
              "conditioning-dir", "pitch-executable", "output"):
        ap.add_argument("--" + n, type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        raise SystemExit("Output must be new")
    _PITCH_EXEC = args.pitch_executable
    _VOCODER_GRAPH = (args.vocoder_export / "vocoder.onnx").read_bytes()
    arms = {"control": (args.control_export / "acoustic.onnx").read_bytes(),
            "treatment": (args.treatment_export / "acoustic.onnx").read_bytes()}
    heldout = ["song-000", "song-004", "song-007", "song-018", "song-023", "song-038",
               "song-008", "song-011", "song-017", "song-019", "song-021", "song-029"]
    report = dict(formatId="com.project-seam.acoustic-heldout-conditioning-probe",
                  schemaVersion=1, steps=STEPS,
                  label="frozen-weight matched-control inference panel; admitted nonzero breathiness; does NOT change the frozen run verdict",
                  singerQualified=False, releaseEligible=False,
                  vocoderSha256=_sha(_VOCODER_GRAPH), arms={})
    for name, graph in arms.items():
        report["arms"][name] = dict(acousticSha256=_sha(graph), items=[])
        for song in heldout:
            report["arms"][name]["items"].append(
                evaluate(name, graph, args.source_root, args.conditioning_dir, song, args.output))
    out = args.output / "conditioning-probe.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=1))
    print(json.dumps(dict(done=True, output=str(out))))


if __name__ == "__main__":
    raise SystemExit(main())
