"""Held-out vocoder reconstruction measurement against source audio.

Section 5 (U3.3) of the joint development plan:
Reconstruction is measured by comparing the vocoder output directly against the source
audio that mel and F0 were extracted from, using a named spectral distance plus an F0 error.
Plausible audio alone is not reconstruction: shifting pitch or altering spectral envelopes
fails the measurement. Exact hop, padding and trim lengths are asserted; sample-rate and
acoustic profile mismatches are refused.
"""
from __future__ import annotations

from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import struct
from typing import Iterable, Sequence

import numpy as np
from scipy.signal import stft

from .qualification import measure_median_pitch_hz
from .pitch_comparison import compare_pitch_tracks

EXPECTED_SAMPLE_RATE = 48000
EXPECTED_HOP_SIZE = 256
EXPECTED_PROFILE_ID = "seam-full-hop-slaney-v1"
MAXIMUM_SAMPLES = 4096 * EXPECTED_HOP_SIZE

STFT_RESOLUTIONS = (
    {"n_fft": 512, "hop_size": 128, "win_length": 512},
    {"n_fft": 1024, "hop_size": 256, "win_length": 1024},
    {"n_fft": 2048, "hop_size": 512, "win_length": 2048},
)


def compute_stft_spectral_distance(
    rendered: np.ndarray,
    source: np.ndarray,
    *,
    sample_rate: int = EXPECTED_SAMPLE_RATE,
    resolutions: Sequence[dict] = STFT_RESOLUTIONS,
    eps: float = 1e-7,
    log_eps: float = 1e-5,
) -> float:
    """Compute multi-resolution STFT spectral convergence and log-magnitude distance.

    Returns 0.0 for identical signals, and strictly positive distance for differing spectra.
    """
    if rendered.ndim != 1 or source.ndim != 1:
        raise ValueError("Spectral distance requires 1D mono audio arrays")
    if len(rendered) != len(source):
        raise ValueError(f"Length mismatch: rendered={len(rendered)} vs source={len(source)}")
    if len(rendered) < 512:
        raise ValueError(f"Audio array too short for spectral evaluation ({len(rendered)} samples)")

    distances = []
    for res in resolutions:
        n_fft = res["n_fft"]
        hop = res["hop_size"]
        win = res["win_length"]
        if len(rendered) < win:
            continue

        _, _, z_rendered = stft(
            rendered,
            nperseg=win,
            noverlap=win - hop,
            nfft=n_fft,
            boundary=None,
            padded=False,
        )
        _, _, z_source = stft(
            source,
            nperseg=win,
            noverlap=win - hop,
            nfft=n_fft,
            boundary=None,
            padded=False,
        )

        mag_rendered = np.abs(z_rendered)
        mag_source = np.abs(z_source)

        fro_diff = float(np.linalg.norm(mag_source - mag_rendered, ord="fro"))
        fro_source = float(np.linalg.norm(mag_source, ord="fro"))
        sc = fro_diff / (fro_source + eps)

        log_diff = np.abs(np.log(mag_source + log_eps) - np.log(mag_rendered + log_eps))
        lm = float(np.mean(log_diff))

        distances.append(sc + lm)

    if not distances:
        raise ValueError("Audio length insufficient for any STFT resolution")

    return float(np.mean(distances))


def compute_f0_reconstruction_error(
    rendered: np.ndarray,
    source: np.ndarray,
    *,
    sample_rate: int = EXPECTED_SAMPLE_RATE,
    target_hz: float | None = None,
) -> tuple[float | None, float | None, str | None]:
    """Legacy diagnostic only: difference of phrase medians, not framewise RMSE.

    This statistic cannot establish melody, timing or reconstruction correctness.
    target_hz only guides this legacy estimator, never the native comparison.
    """
    ref_hz = target_hz
    src_pitch, src_cov, src_reason = None, 0.0, None
    if ref_hz is None:
        for candidate_hz in (220.0, 440.0, 165.0, 330.0):
            p, c, r = measure_median_pitch_hz(source, sample_rate, candidate_hz)
            if p is not None and c > src_cov:
                src_pitch, src_cov, src_reason = p, c, r
        if src_pitch is not None:
            ref_hz = src_pitch
    else:
        src_pitch, src_cov, src_reason = measure_median_pitch_hz(source, sample_rate, ref_hz)

    if src_pitch is None or ref_hz is None or ref_hz <= 0.0:
        return None, None, "unvoiced_or_unmeasurable_source"

    rend_pitch, rend_cov, rend_reason = measure_median_pitch_hz(rendered, sample_rate, ref_hz)
    if rend_pitch is None:
        return None, None, f"rendered_unvoiced: {rend_reason}"

    f0_rmse_hz = abs(rend_pitch - src_pitch)
    cents_ratio = rend_pitch / src_pitch
    f0_median_error_cents = 1200.0 * math.log2(cents_ratio) if cents_ratio > 0.0 else None

    return f0_median_error_cents, f0_rmse_hz, None


def _mono_audio(value, name):
    """Capture bounded mono CPU audio without flattening multiple channels."""
    try:
        import torch
    except ImportError:
        torch = None
    if torch is not None and isinstance(value, torch.Tensor):
        if value.device.type != "cpu" or value.dtype != torch.float32 or value.layout != torch.strided:
            raise ValueError(f"{name} must be a dense CPU float32 tensor")
        value = value.detach().numpy()
    value = np.asarray(value)
    if (value.dtype.kind not in "fi" or value.ndim not in (1, 2, 3)
            or any(size != 1 for size in value.shape[:-1])
            or not 1 <= value.shape[-1] <= MAXIMUM_SAMPLES):
        raise ValueError(f"{name} must be bounded mono audio [S], [1,S], or [1,1,S]")
    result = value.reshape(-1).astype(np.float32, copy=True)
    if not np.isfinite(result).all():
        raise ValueError(f"Nonfinite samples in {name}")
    return result


@contextmanager
def _evaluation_mode(generator, seed):
    """Preserve CPU RNG, gradients and mixed module modes, including on failure.

    Arbitrary custom forward methods that mutate parameters/buffers are not rolled
    back. Standard running statistics stay unchanged because inference uses eval.
    """
    try:
        import torch
    except ImportError:
        yield None
        return
    module = isinstance(generator, torch.nn.Module)
    if module and any(value.device.type != "cpu" for value in
                      (*generator.parameters(), *generator.buffers())):
        raise ValueError("Vocoder evaluation supports CPU models only")
    modes = [(child, child.training) for child in generator.modules()] if module else []
    try:
        with torch.random.fork_rng(devices=[]), torch.no_grad():
            torch.random.default_generator.manual_seed(seed)
            if module:
                generator.eval()
            yield torch if module else None
    finally:
        for child, training in modes:
            child.training = training


def _conditioning_tensor(value, name, torch):
    if isinstance(value, np.ndarray):
        if value.dtype != np.float32:
            raise ValueError(f"Vocoder {name} must be float32")
        value = torch.from_numpy(value.copy())
    if (not isinstance(value, torch.Tensor) or value.device.type != "cpu"
            or value.dtype != torch.float32 or value.layout != torch.strided
            or not torch.isfinite(value).all()):
        raise ValueError(f"Vocoder {name} must be finite dense CPU float32")
    return value.detach()


def _float_wav(audio, sample_rate):
    pcm = audio.astype("<f4", copy=False).tobytes()
    return struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(pcm), b"WAVE", b"fmt ",
                       16, 3, 1, sample_rate, sample_rate * 4, 4, 32, b"data", len(pcm)) + pcm


def measure_vocoder_reconstruction(
    *,
    rendered_audio: np.ndarray | Sequence[float],
    source_audio: np.ndarray | Sequence[float],
    sample_rate: int = EXPECTED_SAMPLE_RATE,
    hop_size: int = EXPECTED_HOP_SIZE,
    profile: dict | None = None,
    valid_samples: int | None = None,
    target_hz: float | None = None,
    pitch_tracks: dict | None = None,
    max_acceptable_spectral_distance: float = 3.5,
    max_acceptable_pitch_error_cents: float = 50.0,
) -> dict:
    """Measure single-item vocoder reconstruction against source audio.

    Enforces:
    - Exact sample rate match (48000 Hz)
    - Exact hop size framing and trim lengths
    - Profile validation
    - Multi-scale STFT spectral distance
    - Optional time-aligned native F0 reconstruction error

    pitch_tracks must contain source/rendered native feature records extracted
    from the exact canonical float32 mono WAV bytes (_float_wav) for each trimmed
    array. Their WAV hashes are checked here. Without both tracks, legacy median
    numbers remain diagnostic and pitch reconstruction is UNRESOLVED. Caller-
    supplied tracks do not authenticate extractor provenance; use compare_wavs
    for direct native extraction from existing retained WAVs.
    """
    if sample_rate != EXPECTED_SAMPLE_RATE:
        raise ValueError(f"Sample rate mismatch: vocoder requires {EXPECTED_SAMPLE_RATE} Hz, got {sample_rate}")
    if type(hop_size) is not int or hop_size != EXPECTED_HOP_SIZE:
        raise ValueError("Vocoder reconstruction requires hop size 256")
    if target_hz is not None and (type(target_hz) not in (int, float)
                                 or not math.isfinite(target_hz) or not 0 < target_hz <= 20000):
        raise ValueError("Pitch reference must be finite and positive")

    if profile is not None:
        if profile.get("profileId") != EXPECTED_PROFILE_ID:
            raise ValueError(f"Profile mismatch: expected {EXPECTED_PROFILE_ID}, got {profile.get('profileId')}")
        if profile.get("sampleRate") != sample_rate:
            raise ValueError("Profile sampleRate differs from audio clock")
        if profile.get("hopSize") != hop_size:
            raise ValueError("Profile hopSize differs from requested hop size")
        if profile.get("tailPadding") != "zero-to-whole-hop":
            raise ValueError("Profile requires zero-to-whole-hop tail padding")

    rendered = _mono_audio(rendered_audio, "rendered vocoder output")
    source = _mono_audio(source_audio, "source audio")

    if len(rendered) % hop_size != 0:
        raise ValueError(
            f"Rendered audio length {len(rendered)} is not a multiple of hop size {hop_size}"
        )

    valid_samples = len(source) if valid_samples is None else valid_samples
    if type(valid_samples) is not int or not 512 <= valid_samples <= MAXIMUM_SAMPLES:
        raise ValueError(f"Audio payload too short ({valid_samples} samples) for vocoder reconstruction")
    expected_samples = ((valid_samples + hop_size - 1) // hop_size) * hop_size
    if len(rendered) != expected_samples or len(source) not in (valid_samples, expected_samples):
        raise ValueError("Vocoder output/source length differs from the exact full-hop phrase length")
    if np.any(source[valid_samples:] != 0):
        raise ValueError("Source padding must be zero outside valid samples")

    padded_samples = len(rendered) - valid_samples

    rendered_valid = rendered[:valid_samples]
    source_valid = source[:valid_samples]

    spec_dist = compute_stft_spectral_distance(
        rendered_valid, source_valid, sample_rate=sample_rate
    )
    f0_cents, median_difference_hz, legacy_pitch_reason = compute_f0_reconstruction_error(
        rendered_valid, source_valid, sample_rate=sample_rate, target_hz=target_hz
    )
    pitch_comparison = None
    if pitch_tracks is not None:
        if not isinstance(pitch_tracks, dict) or set(pitch_tracks) != {"source", "rendered"}:
            raise ValueError("Framewise pitch requires source/rendered native track pairs")
        pitch_comparison = compare_pitch_tracks(pitch_tracks["source"], pitch_tracks["rendered"],
            reference_sha256=hashlib.sha256(_float_wav(source_valid, sample_rate)).hexdigest(),
            candidate_sha256=hashlib.sha256(_float_wav(rendered_valid, sample_rate)).hexdigest(),
            sample_rate=sample_rate, frame_count=valid_samples, hop_size=hop_size,
            maximum_error_cents=max_acceptable_pitch_error_cents)

    rendered_peak = float(np.max(np.abs(rendered_valid)))
    rendered_rms = float(np.sqrt(np.mean(np.square(rendered_valid))))
    source_peak = float(np.max(np.abs(source_valid)))
    source_rms = float(np.sqrt(np.mean(np.square(source_valid))))

    pitch_ok = pitch_comparison is not None and pitch_comparison["comparisonSatisfied"]
    pitch_status = ("PASS" if pitch_ok else "FAIL" if pitch_comparison is not None
                    and pitch_comparison["status"] == "MISMATCH" else "UNRESOLVED")
    pitch_reason = ("native_framewise_tracks_not_supplied" if pitch_comparison is None
                    else None if pitch_ok else pitch_comparison["status"])
    spec_ok = spec_dist <= max_acceptable_spectral_distance
    energy_ok = (rendered_peak > 1e-4 or source_peak < 1e-4) and float(np.max(np.abs(rendered))) <= 1.05

    reconstruction_satisfied = bool(spec_ok and pitch_ok and energy_ok)

    return {
        "formatId": "com.project-seam.vocoder-reconstruction-measurement",
        "schemaVersion": 2,
        "sampleRate": sample_rate,
        "hopSize": hop_size,
        "profileId": profile.get("profileId", EXPECTED_PROFILE_ID) if profile else EXPECTED_PROFILE_ID,
        "validSamples": int(valid_samples),
        "paddedSamples": int(padded_samples),
        "totalFrames": int(len(rendered) // hop_size),
        "spectralDistance": round(float(spec_dist), 6),
        "f0RmseHz": pitch_comparison["f0RmseHz"] if pitch_comparison is not None else None,
        "f0MedianErrorCents": round(float(f0_cents), 3) if f0_cents is not None else None,
        "f0MedianErrorCentsDiagnosticOnly": True,
        "legacyMedianDifferenceHz": round(float(median_difference_hz), 4) if median_difference_hz is not None else None,
        "legacyPitchReason": legacy_pitch_reason,
        "legacyPitchMetric": "difference-between-whole-phrase-median-F0-estimates-diagnostic-only",
        "pitchReason": pitch_reason,
        "pitchStatus": pitch_status,
        "pitchMetric": "time-aligned-native-F0-absolute-cents",
        "pitchComparison": pitch_comparison,
        "renderedPeak": round(rendered_peak, 6),
        "renderedRms": round(rendered_rms, 6),
        "sourcePeak": round(source_peak, 6),
        "sourceRms": round(source_rms, 6),
        "reconstructionSatisfied": reconstruction_satisfied,
    }


def evaluate_held_out_reconstruction(
    generator_fn,
    items: Iterable[dict],
    *,
    dataset_sha256: str,
    profile_sha256: str,
    output_directory: Path | None = None,
    label_origin: str = "unspecified",
    sample_rate: int = EXPECTED_SAMPLE_RATE,
    hop_size: int = EXPECTED_HOP_SIZE,
    profile: dict | None = None,
    seed: int = 0,
    check_running=None,
) -> dict:
    """Evaluate vocoder reconstruction over held-out items and retain the measurement receipt.

    Strict invariants:
    - labelOrigin is permanently recorded.
    - releaseEligible is strictly False.
    - trainingAdmitted is strictly False.

    This low-level measurement accepts caller-owned items; it does not itself
    authenticate a dataset or establish a held-out study. The reviewed epoch
    service selects and reads admitted validation/test sources before calling it.
    A partial final hop is measured over validSamples and retained WAVs contain
    exactly those samples. Failures may leave item files without a final receipt.
    """
    if type(seed) is not int or not 0 <= seed < 2**63:
        raise ValueError("Vocoder evaluation requires a nonnegative 63-bit seed")
    if not isinstance(label_origin, str) or not label_origin:
        raise ValueError("Vocoder evaluation requires a label origin")
    if output_directory is not None:
        output_directory = Path(output_directory)
        if output_directory.is_symlink() or not output_directory.is_dir():
            raise ValueError("Reconstruction output must be an existing regular directory")
        if (output_directory / "reconstruction_receipt.json").exists():
            raise ValueError("Reconstruction receipt already exists")

    measurements = []
    total_samples = 0
    spec_distances = []
    legacy_f0_errors, f0_error_sum, f0_error_frames = [], 0.0, 0

    seen = set()
    with _evaluation_mode(generator_fn, seed) as torch:
        for item in items:
            if check_running is not None:
                check_running()
            source_id = item["sourceId"]
            if not isinstance(source_id, str) or not source_id or source_id in seen or len(seen) >= 256:
                raise ValueError("Held-out evaluation requires 1..256 distinct source IDs")
            seen.add(source_id)
            if item.get("partition") not in (None, "validation", "test"):
                raise ValueError("Reconstruction input cannot use the training partition")
            if (item.get("datasetSha256", dataset_sha256) != dataset_sha256
                    or item.get("profileSha256", profile_sha256) != profile_sha256):
                raise ValueError("Reconstruction input identity differs from the selected dataset/profile")
            source_pcm = _mono_audio(item["pcm"], "source audio")
            mel, f0 = item["mel"], item.get("f0")
            if torch is not None:
                mel = _conditioning_tensor(mel, "mel", torch)
                f0 = _conditioning_tensor(f0, "f0", torch)
                if (mel.ndim != 3 or mel.shape[:2] != (1, 80) or not 2 <= mel.shape[2] <= 4096
                        or f0.shape != (1, mel.shape[2]) or torch.any(f0 < 0) or torch.any(f0 > 20000)):
                    raise ValueError("Vocoder conditioning requires mel [1,80,T] and f0 [1,T]")
            rendered_pcm = _mono_audio(generator_fn(mel, f0), "rendered vocoder output")
            if torch is not None and len(rendered_pcm) != mel.shape[2] * hop_size:
                raise ValueError("Vocoder output length differs from its conditioning frame count")
            measurement = measure_vocoder_reconstruction(
                rendered_audio=rendered_pcm, source_audio=source_pcm, sample_rate=sample_rate,
                hop_size=hop_size, profile=profile, valid_samples=item.get("validSamples"),
                target_hz=item.get("frequencyHz"), pitch_tracks=item.get("pitchTracks"))
            if check_running is not None:
                check_running()
            valid = measurement["validSamples"]
            measurement.update(sourceId=source_id, partition=item.get("partition"),
                renderedPaddedPcmSha256=hashlib.sha256(rendered_pcm.astype("<f4").tobytes()).hexdigest(),
                sourcePcmSha256=hashlib.sha256(source_pcm[:valid].astype("<f4").tobytes()).hexdigest())
            for key in ("sourceSha256", "audioSha256", "targetSha256", "frameOffset", "phraseAnalysisFrames"):
                if key in item:
                    measurement[key] = item[key]
            if torch is not None:
                for name, value in (("mel", mel), ("f0", f0)):
                    measurement[name + "Sha256"] = hashlib.sha256(value.numpy().astype("<f4").tobytes()).hexdigest()
            if output_directory is not None:
                stem = f"item-{len(measurements) + 1:06d}"
                payload = _float_wav(rendered_pcm[:valid], sample_rate)
                measurement.update(audioPath=stem + ".wav", outputAudioSha256=hashlib.sha256(payload).hexdigest(),
                                   outputAudioBytes=len(payload), outputEncoding="IEEE-float32-mono-WAV")
                with (output_directory / measurement["audioPath"]).open("xb") as stream:
                    stream.write(payload)
                encoded = (json.dumps(measurement, indent=2, allow_nan=False) + "\n").encode()
                with (output_directory / (stem + ".json")).open("xb") as stream:
                    stream.write(encoded)
                measurement.update(measurementPath=stem + ".json", measurementSha256=hashlib.sha256(encoded).hexdigest())
            measurements.append(measurement)
            total_samples += valid
            spec_distances.append(measurement["spectralDistance"])
            if measurement["f0MedianErrorCents"] is not None:
                legacy_f0_errors.append(abs(measurement["f0MedianErrorCents"]))
            comparison = measurement["pitchComparison"]
            if comparison is not None and comparison["measurableVoicedPairs"]:
                f0_error_frames += comparison["measurableVoicedPairs"]
                f0_error_sum += sum(abs(error) for error in comparison["frameErrorsCents"] if error is not None)
    if not measurements:
        raise ValueError("Held-out evaluation requires at least one item")

    receipt = {
        "formatId": "com.project-seam.vocoder-reconstruction-receipt",
        "schemaVersion": 2,
        "createdAt": datetime.now(timezone.utc).isoformat(),
        "datasetSha256": dataset_sha256,
        "profileSha256": profile_sha256,
        "labelOrigin": label_origin,
        "releaseEligible": False,
        "trainingAdmitted": False,
        "evaluationSeed": seed,
        "summary": {
            "itemCount": len(measurements),
            "totalValidSamples": total_samples,
            "meanSpectralDistance": round(float(np.mean(spec_distances)), 6),
            "meanAbsolutePitchErrorCents": round(f0_error_sum / f0_error_frames, 3) if f0_error_frames else None,
            "measurablePitchFrames": f0_error_frames,
            "legacyMeanAbsoluteMedianDifferenceCents": round(float(np.mean(legacy_f0_errors)), 3) if legacy_f0_errors else None,
            "allReconstructionsSatisfied": all(m["reconstructionSatisfied"] for m in measurements),
            "unresolvedPitchItems": sum(m["pitchStatus"] == "UNRESOLVED" for m in measurements),
        },
        "items": measurements,
        "verdict": "RECONSTRUCTION_MEASURED",
    }

    if output_directory is not None:
        out_path = Path(output_directory) / "reconstruction_receipt.json"
        raw = json.dumps(receipt, indent=2, allow_nan=False) + "\n"
        with out_path.open("x", encoding="utf-8") as stream:
            stream.write(raw)

    return receipt

def build_pitch_tracks(executable, source_audio, rendered_audio, *, sample_rate, sample_rate_hz=None,
                       timeout_seconds=30.0):
    """Framewise native pitch for both audio signals, as the comparison requires.

    The acceptance predicate includes a pitch term that can only be evaluated when both
    tracks exist, and until now no producer supplied them, so the term could never be
    satisfied. Both tracks are measured by the same pinned first-party extractor over
    identical geometry, so a comparison between them is a real difference between two
    measurements rather than between a measurement and a transcription.

    Returns the two tracks keyed by ``source`` and ``rendered`` alongside the exact WAV
    digests the extractor bound its frames to. Those digests are not incidental: the
    comparison refuses a track whose binding does not match the bytes it is given, so
    inheriting them from this function is what keeps the caller from having to rebuild
    the same WAV a second time and risk disagreeing with it.

    Both signals must already be the same length, because the extractor's frame grid is
    derived from the sample count and the comparison requires matching grids. Padding a
    shorter signal would invent pitch for samples that were never synthesized, so a
    length mismatch is refused rather than repaired.
    """
    import tempfile
    from .native_features import extract_pitch

    source = _mono_audio(source_audio, "source audio")
    rendered = _mono_audio(rendered_audio, "rendered vocoder output")
    rate = sample_rate if sample_rate_hz is None else sample_rate_hz
    if len(rendered) != len(source):
        raise ValueError("Pitch tracks require equally long source and rendered audio")
    with tempfile.TemporaryDirectory(prefix="seam-pitch-tracks-") as directory:
        directory = Path(directory)
        tracks, digests = {}, {}
        for name, audio in (("source", source), ("rendered", rendered)):
            path = directory / f"{name}.wav"
            payload = _float_wav(audio, rate)
            path.write_bytes(payload)
            # The extractor binds each track to the digest of the WAV file it was given,
            # not to the raw sample bytes, so the comparison must be handed the same
            # file-level digest or it refuses a track that is actually correct.
            digests[name] = hashlib.sha256(payload).hexdigest()
            tracks[name] = extract_pitch(executable, path, timeout_seconds=timeout_seconds)
    return tracks, digests
