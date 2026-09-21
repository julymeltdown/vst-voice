"""Frozen paired/multi-arm acoustic evaluation for the flatness-objective
experiments.

Compares two or more acoustic checkpoints (e.g. control weight-0 vs treatment,
or the four cells of the flatness/level component ablation) by exporting each
to ONNX, drawing eight conditioned mel samples per development replay source
inside ONE session (seeded RandomNormal ops advance per Run), rendering every
retained mel through the fixed vocoder under the zero-v1 feed, and scoring the
frozen primary statistic, target-relative error term, guardrails and the
12-item held-out acoustic-then-vocoder reconstruction panel.

Diagnostic only: no promotion, qualification, or release eligibility.
"""
import argparse
import hashlib
import io
import json
import math
import statistics
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.io import wavfile

from .__main__ import load_config, publish_new
from .acoustics import wav_log_mel_targets
from .audio_source import decode_pcm_source
from .native_input_replay import prepare_inputs
from .paired_vocoder_evaluation import run_graph
from .phone_periodicity import measure as phone_measure
from .pitch_comparison import compare_wavs
from .reconstruct_source_vocoder import checked_waveform
from .spectral_flatness_diagnostic import spectral_flatness, per_phone_flatness
from .vocoder_reconstruction import compute_stft_spectral_distance

SILENT_LEVEL_FLOOR = -11.5
UNVOICED = {"h", "f", "k", "s", "sh", "t", "ch", "ts"}
VOICED = {"a", "i", "u", "e", "o", "N", "m", "n", "r", "w", "j"}
REST = {"pau", "SP", "sil"}
DENSE_LAGS = list(range(32, 833, 16))
DRAWS = 8
STEPS = 10

# Frozen replay-input identities (bound, not recomputed silently).
REPLAY_SHA256 = {
    "procedural-song-00003": "9b28a864c1afdcafc1698a3bb3893d32b267a6a2c2fa0d2c7bc153e2267fe7a6",
    "procedural-song-00005": "34caa429d58b16b20cf0693aa7fe598c2d0d42215b263f40197c5c891bdafa7d",
    "procedural-song-00024": "8d3ac7b481d553a1d007ebb12f5f00c1f8641c73356ada05c33507aa074ee5af",
    "procedural-song-00402": "efb320582627c1334e99d2ca2b12ac7b9088ee907acf1733115840bf48c387f9",
    "procedural-song-00420": "97a828dfaf0b57dee01bb3743ababb6998602947d57a17dc11cfbe88ea517fbb",
}


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _log_flatness_np(z):
    """Per-frame log spectral flatness of a [T,M] log-mel array."""
    z = np.asarray(z, np.float64)
    m = z.shape[1]
    shifted = z - z.max(axis=1, keepdims=True)
    lse = np.log(np.exp(shifted).sum(axis=1)) + z.max(axis=1)
    return z.mean(axis=1) - (lse - math.log(m))


def _level_np(z):
    """Per-frame log-mean amplitude (logsumexp - log M) of a [T,M] log-mel."""
    z = np.asarray(z, np.float64)
    m = z.shape[1]
    shifted = z - z.max(axis=1, keepdims=True)
    lse = np.log(np.exp(shifted).sum(axis=1)) + z.max(axis=1)
    return lse - math.log(m)


def _lag_corr(segment, lag):
    seg = np.asarray(segment, np.float64)
    seg = seg - seg.mean()
    if len(seg) < lag * 2:
        return None
    left, right = seg[:-lag], seg[lag:]
    den = float(np.linalg.norm(left) * np.linalg.norm(right))
    if den == 0:
        return None
    return float(np.clip(left @ right / den, -1.0, 1.0))


def _one_acoustic_session(graph_bytes):
    import onnxruntime as ort
    ort.disable_telemetry_events()
    options = ort.SessionOptions()
    options.intra_op_num_threads = options.inter_op_num_threads = 1
    return ort.InferenceSession(graph_bytes, options, providers=["CPUExecutionProvider"])


def _run_acoustic_draw(session, inputs):
    names = {v.name for v in session.get_inputs()}
    feeds = dict(inputs)
    if "breathiness" in names and "breathiness" not in feeds:
        feeds["breathiness"] = np.zeros_like(feeds["f0"])
    mel = session.run(["mel"], feeds)[0]
    if mel.dtype != np.float32 or mel.ndim != 3 or not np.isfinite(mel).all():
        raise ValueError("Invalid acoustic replay mel")
    return mel


def _load_song(root, directory):
    base = Path(root) / directory
    payload = (base / "source.wav").read_bytes()
    labels = json.loads((base / "label-config.json").read_bytes())
    label = labels["labels"][0]["label"]
    phones = [dict(symbol=p["symbol"], startFrame=p["startFrame"], endFrame=p["endFrame"])
              for p in label["phonemes"]]
    vocab = labels["vocabulary"]
    conditioning = json.loads((base / "conditioning.json").read_bytes())
    return payload, label, phones, vocab, conditioning


def _phone_spans(phones, frames):
    """ceil/floor analysis-frame spans matching per_phone_flatness."""
    spans = []
    for p in phones:
        left, right = (p["startFrame"] + 255) // 256, p["endFrame"] // 256
        spans.append((p["symbol"], left, min(right, frames)))
    return spans


def _eligible_frames(level_ref, spans, frames):
    """Boolean eligible mask: nonsilent reference frames inside phone spans."""
    eligible = np.zeros(frames, bool)
    for _, left, right in spans:
        for f in range(left, min(right, frames)):
            if level_ref[f] > SILENT_LEVEL_FLOOR:
                eligible[f] = True
    return eligible


def _panel_for_class(symbols, phones, eligible):
    """Phones of a class containing >=1 eligible frame (reference nonsilent)."""
    inventory = []
    for i, p in enumerate(phones):
        if p["symbol"] not in symbols:
            continue
        left, right = (p["startFrame"] + 255) // 256, p["endFrame"] // 256
        if any(eligible[f] for f in range(left, min(right, len(eligible)))):
            inventory.append(i)
    return inventory


def evaluate_development(arm_name, graph_bytes, root, replay_path, song_dir, source_id, out_dir):
    payload, label, phones, vocab, _ = _load_song(root, song_dir)
    src_sha = _sha(payload)
    captured = load_config(Path(replay_path), REPLAY_SHA256[source_id])
    inputs, breathiness, gains = prepare_inputs(captured, STEPS)
    if breathiness is not None:
        inputs["breathiness"] = breathiness
    _, reference_mel = wav_log_mel_targets(payload, expected_sha256=src_sha, sample_rate=48000)
    _, audio = decode_pcm_source(payload, expected_sha256=src_sha, sample_rate=48000)
    session = _one_acoustic_session(graph_bytes)
    mel_dir = Path(out_dir) / "mel" / arm_name / source_id
    mel_dir.mkdir(parents=True, exist_ok=True)
    wav_dir = Path(out_dir) / "wav" / arm_name / source_id
    wav_dir.mkdir(parents=True, exist_ok=True)
    draws = []
    for d in range(DRAWS):
        mel = _run_acoustic_draw(session, inputs)
        raw = mel.astype("<f4").tobytes(order="C")
        (mel_dir / f"draw-{d:02d}.f32le").write_bytes(raw)
        draws.append((mel, _sha(raw)))
    del session
    frames = len(reference_mel)
    if draws[0][0].shape[1] != frames:
        raise ValueError("Predicted/reference mel frame geometry differs")
    ref_mel = np.asarray(reference_mel[:frames], np.float64)
    ref_flat = spectral_flatness(np.exp(ref_mel))
    ref_logflat = _log_flatness_np(ref_mel)
    level_ref = _level_np(ref_mel)
    spans = _phone_spans(phones, frames)
    eligible = _eligible_frames(level_ref, spans, frames)
    uv_inventory = _panel_for_class(UNVOICED, phones, eligible)
    v_inventory = _panel_for_class(VOICED, phones, eligible)
    rows = []
    for pi, p in enumerate(phones):
        left, right = (p["startFrame"] + 255) // 256, min(p["endFrame"] // 256, frames)
        elig = [f for f in range(left, right) if eligible[f]]
        if not elig:
            continue
        per_draw_flat, per_draw_err, per_draw_refflat = [], [], []
        for mel, _ in draws:
            cand = np.asarray(mel[0, :frames], np.float64)
            cand_flat = spectral_flatness(np.exp(cand))
            cand_logflat = _log_flatness_np(cand)
            per_draw_flat.append(float(np.mean(cand_flat[elig])))
            per_draw_err.append(float(np.mean(np.abs(
                cand_logflat[elig] - ref_logflat[elig]))))
            per_draw_refflat.append(float(np.mean(ref_flat[elig])))
        row = dict(phone=p["symbol"], startFrame=p["startFrame"], endFrame=p["endFrame"],
                   spanFrames=right - left, eligibleFrames=len(elig),
                   unvoiced=p["symbol"] in UNVOICED, voiced=p["symbol"] in VOICED,
                   rest=p["symbol"] in REST,
                   flatnessMeanAcrossDraws=float(np.mean(per_draw_flat)),
                   flatnessPerDraw=per_draw_flat,
                   targetRelativeErrorAcrossDraws=float(np.mean(per_draw_err)),
                   referenceFlatness=float(np.mean(per_draw_refflat)))
        rows.append(row)
    # Silent-frame count: reference-floor frames inside unvoiced non-rest spans.
    silent = 0
    for p in phones:
        if p["symbol"] in UNVOICED and p["symbol"] not in REST:
            l, r = (p["startFrame"] + 255) // 256, min(p["endFrame"] // 256, frames)
            silent += sum(1 for f in range(l, r) if level_ref[f] <= SILENT_LEVEL_FLOOR)
    # Waveform panel: render every retained mel through the fixed vocoder.
    # Captured dynamics gains are applied so output is native-equivalent.
    phone_wave_rows, dense_lag_rows, pitch_acc = [], [], []
    clip_or_silence = []
    failed_draws = []
    for d, (mel, msha) in enumerate(draws):
        cand = np.asarray(mel[0, :frames], np.float32)[None]
        f0 = inputs["f0"][:, :frames].astype(np.float32)
        noise = np.zeros((1, frames * 64), np.float32)
        try:
            wave = run_graph(_VOCODER_GRAPH, cand, f0, frames=frames, noise=noise)
        except ValueError as error:
            # A peak>=1.0 rejection IS the clip guardrail; other failures are
            # recorded as draw failures, not clipping, and keep the draw out of
            # the matched denominators.
            is_clip = "whole-hop" in str(error) or "normalized" in str(error)
            clip_or_silence.append(dict(draw=d, newClip=is_clip, newSilence=False,
                                        vocoderRejected=is_clip))
            if not is_clip:
                failed_draws.append(dict(draw=d, reason=str(error)[:200]))
            continue
        wave = wave[:len(audio)]
        # Apply captured per-sample dynamics gains (nonunity in pau regions).
        wave = wave * gains[:len(wave)]
        buf = io.BytesIO(); wavfile.write(buf, 48000, wave.astype(np.float32))
        (wav_dir / f"draw-{d:02d}.wav").write_bytes(buf.getvalue())
        pm = phone_measure(audio, wave, phones)
        phone_wave_rows.append(pm["rows"])
        # dense-lag probe on unvoiced phone spans; retain per-lag detail.
        for p in phones:
            s, e = p["startFrame"], min(p["endFrame"], len(audio))
            if p["symbol"] not in UNVOICED or e - s < 2 * max(DENSE_LAGS):
                continue
            per_lag = {}
            for lag in DENSE_LAGS:
                r = _lag_corr(audio[s:e], lag); c = _lag_corr(wave[s:e], lag)
                if r is not None and c is not None:
                    per_lag[str(lag)] = dict(reference=r, candidate=c,
                                             absError=abs(c - r), signedError=c - r)
            if per_lag:
                dense_lag_rows.append(dict(draw=d, sourceId=source_id,
                    symbol=p["symbol"], startFrame=s, endFrame=e, perLag=per_lag))
        # clip / silence guardrail (candidate-side events; control comparison is
        # done at assessment time on matching source/draw/phone).
        peak = float(np.max(np.abs(wave)))
        new_clip = peak >= 1.0
        new_silence = False
        for p in phones:
            if p["symbol"] in VOICED:
                seg = wave[p["startFrame"]:min(p["endFrame"], len(wave))]
                if len(seg) and float(np.sqrt(np.mean(seg ** 2))) < 1e-4:
                    new_silence = True
        clip_or_silence.append(dict(draw=d, newClip=bool(new_clip), newSilence=bool(new_silence),
                                    vocoderRejected=False))
        # pitch on voiced frames
        with tempfile.TemporaryDirectory(prefix="seam-pitch-") as td:
            rp, cp = Path(td) / "ref.wav", Path(td) / "cand.wav"
            rp.write_bytes(payload)
            wavfile.write(cp, 48000, wave.astype(np.float32))
            pc = compare_wavs(rp, cp, executable=_PITCH_EXEC)
            comp = pc["comparison"]
            pitch_acc.append(dict(draw=d, measurable=comp["measurableVoicedPairs"],
                                  meanAbsoluteCents=comp["meanAbsoluteCents"],
                                  status=comp["status"]))
    return dict(sourceId=source_id, sourceSha256=src_sha,
                comparedFrames=frames, phones=rows,
                unvoicedInventory=uv_inventory, voicedInventory=v_inventory,
                excludedSilentFrames=silent,
                phoneWaveRows=phone_wave_rows, denseLag=dense_lag_rows,
                pitch=pitch_acc, clipOrSilence=clip_or_silence,
                failedDraws=failed_draws,
                melDrawSha256=[s for _, s in draws])


def evaluate_heldout(arm_name, graph_bytes, root, song_dir, out_dir):
    payload, label, phones, vocab, conditioning = _load_song(root, song_dir)
    src_sha = _sha(payload)
    _, audio = decode_pcm_source(payload, expected_sha256=src_sha, sample_rate=48000)
    n = len(label["f0Hz"])
    tokens = [vocab.index(p["symbol"]) + 1 for p in label["phonemes"]]
    durations = [(p["endFrame"] + 255) // 256 - (p["startFrame"] + 255) // 256
                 for p in label["phonemes"]]
    f0 = np.asarray([label["f0Hz"]], np.float32)
    breath = np.asarray([[fr["breathiness"] for fr in conditioning["frames"]]], np.float32)
    breath_nonzero = bool(conditioning.get("hasBreathiness")) and float(breath.max()) > 0
    inputs = dict(tokens=np.asarray([tokens], np.int64),
                  durations=np.asarray([durations], np.int64), f0=f0,
                  steps=np.asarray(STEPS, np.int64), breathiness=breath)
    session = _one_acoustic_session(graph_bytes)
    mel = _run_acoustic_draw(session, inputs)
    del session
    frames = mel.shape[1]
    cand = mel[:, :frames].astype(np.float32)
    noise = np.zeros((1, frames * 64), np.float32)
    wave = run_graph(_VOCODER_GRAPH, cand, f0[:, :frames], frames=frames, noise=noise)
    wave = wave[:len(audio)]
    wav_dir = Path(out_dir) / "heldout" / arm_name
    wav_dir.mkdir(parents=True, exist_ok=True)
    buf = io.BytesIO(); wavfile.write(buf, 48000, wave.astype(np.float32))
    (wav_dir / f"{song_dir}.wav").write_bytes(buf.getvalue())
    dist = compute_stft_spectral_distance(wave, audio)
    with tempfile.TemporaryDirectory(prefix="seam-ho-") as td:
        rp, cp = Path(td) / "ref.wav", Path(td) / "cand.wav"
        rp.write_bytes(payload); wavfile.write(cp, 48000, wave.astype(np.float32))
        pc = compare_wavs(rp, cp, executable=_PITCH_EXEC)
    comp = pc["comparison"]
    # Unvoiced phone-window RMS ratio guardrail on this panel.
    uv_ratios = []
    for p in phones:
        s, e = p["startFrame"], min(p["endFrame"], len(audio))
        if p["symbol"] in UNVOICED and e > s:
            ref_rms = float(np.sqrt(np.mean(audio[s:e] ** 2)))
            cand_rms = float(np.sqrt(np.mean(wave[s:e] ** 2)))
            if ref_rms:
                uv_ratios.append(cand_rms / ref_rms)
    return dict(sourceId=label.get("sourceId", song_dir), sourceSha256=src_sha,
                breathinessConditioning=("nonzero" if breath_nonzero else "zero"),
                spectralDistance=float(dist),
                meanAbsolutePitchCents=comp["meanAbsoluteCents"],
                measurablePitchPairs=comp["measurableVoicedPairs"],
                pitchStatus=comp["status"],
                unvoicedRmsRatioMean=(float(np.mean(uv_ratios)) if uv_ratios else None),
                reconstructionSatisfied=comp["comparisonSatisfied"])


_VOCODER_GRAPH = None
_PITCH_EXEC = None


def _aggregate_uv(rows_by_source, key):
    vals = [r[key] for s in rows_by_source for r in s["phones"]
            if r["unvoiced"] and not r["rest"] and r[key] is not None]
    return statistics.fmean(vals) if vals else None


def main():
    global _VOCODER_GRAPH, _PITCH_EXEC
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--arm", action="append", required=True, metavar="NAME=DIR",
                    help="Named acoustic export directory; repeat for each arm (2 or more)")
    for n in ("vocoder-export", "source-root",
              "pitch-executable", "output"):
        ap.add_argument("--" + n, type=Path, required=True)
    ap.add_argument("--replay-dir", type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        raise SystemExit("Output must be new")
    _PITCH_EXEC = args.pitch_executable
    _VOCODER_GRAPH = (args.vocoder_export / "vocoder.onnx").read_bytes()
    arms = {}
    for spec in args.arm:
        name, sep, path = spec.partition("=")
        if not sep or not name or not path:
            raise SystemExit("Each --arm must be NAME=DIR")
        if name in arms:
            raise SystemExit(f"Duplicate arm name {name}")
        arms[name] = (Path(path) / "acoustic.onnx").read_bytes()
    if len(arms) < 2:
        raise SystemExit("At least two --arm entries are required")
    dev = [("replay-e2-00003-r2-inputs.json", "song-003", "procedural-song-00003"),
           ("replay-e2-00005-r2-inputs.json", "song-005", "procedural-song-00005"),
           ("replay-e2-00024-r2-inputs.json", "song-024", "procedural-song-00024"),
           ("replay-e2-00402-r2-inputs.json", "song-402", "procedural-song-00402"),
           ("replay-e2-00420-r2-inputs.json", "song-420", "procedural-song-00420")]
    heldout = ["song-000", "song-004", "song-007", "song-018", "song-023", "song-038",
               "song-008", "song-011", "song-017", "song-019", "song-021", "song-029"]
    report = dict(formatId="com.project-seam.acoustic-flatness-pair-eval", schemaVersion=2,
                  draws=DRAWS, steps=STEPS, singerQualified=False, releaseEligible=False,
                  combinedModelHoldoutVerified=False, listening="NOT_REVIEWED",
                  developmentDrawsPerSource=DRAWS,
                  heldoutDrawsPerItem=1,
                  heldoutPanelLabel="one-draw zero-breathiness (song-NNN/conditioning.json hasBreathiness=false); mel-based UV-level/voiced-flatness guardrails NOT_EVALUATED on this panel",
                  developmentConditioning="zero-breathiness: all five frozen replay captures carry breathiness=0",
                  vocoderSha256=_sha(_VOCODER_GRAPH), arms={}, heldout={})
    for name, graph in arms.items():
        report["arms"][name] = dict(acousticSha256=_sha(graph), development=[], heldoutItems=[])
        for replay, song, source_id in dev:
            report["arms"][name]["development"].append(
                evaluate_development(name, graph, args.source_root,
                                     args.replay_dir / replay, song, source_id, args.output))
        for song in heldout:
            report["arms"][name]["heldoutItems"].append(
                evaluate_heldout(name, graph, args.source_root, song, args.output))
    publish_new(args.output / "pair-eval.json", report)
    print(json.dumps(dict(done=True, output=str(args.output))))


if __name__ == "__main__":
    raise SystemExit(main())
