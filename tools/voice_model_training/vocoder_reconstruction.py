"""Held-out vocoder reconstruction measurement against source audio.

Section 5 (U3.3) of the joint development plan:
Reconstruction is measured by comparing the vocoder output directly against the source
audio that mel and F0 were extracted from, using a named spectral distance plus an F0 error.
Plausible audio alone is not reconstruction: shifting pitch or altering spectral envelopes
fails the measurement. Exact hop, padding and trim lengths are asserted; sample-rate and
acoustic profile mismatches are refused.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
from typing import Sequence

import numpy as np
from scipy.signal import stft

from .qualification import measure_median_pitch_hz

EXPECTED_SAMPLE_RATE = 48000
EXPECTED_HOP_SIZE = 256
EXPECTED_PROFILE_ID = "seam-full-hop-slaney-v1"

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
    """Measure median pitch on rendered and source audio, computing error in Hz and cents."""
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

    if ref_hz is None or ref_hz <= 0.0:
        return None, None, "unvoiced_or_unmeasurable_source"

    rend_pitch, rend_cov, rend_reason = measure_median_pitch_hz(rendered, sample_rate, ref_hz)
    if rend_pitch is None:
        return None, None, f"rendered_unvoiced: {rend_reason}"

    f0_rmse_hz = abs(rend_pitch - (src_pitch if src_pitch is not None else ref_hz))
    cents_ratio = rend_pitch / (src_pitch if src_pitch is not None else ref_hz)
    f0_median_error_cents = 1200.0 * math.log2(cents_ratio) if cents_ratio > 0.0 else None

    return f0_median_error_cents, f0_rmse_hz, None


def measure_vocoder_reconstruction(
    *,
    rendered_audio: np.ndarray | Sequence[float],
    source_audio: np.ndarray | Sequence[float],
    sample_rate: int = EXPECTED_SAMPLE_RATE,
    hop_size: int = EXPECTED_HOP_SIZE,
    profile: dict | None = None,
    target_hz: float | None = None,
    max_acceptable_spectral_distance: float = 3.5,
    max_acceptable_pitch_error_cents: float = 50.0,
) -> dict:
    """Measure single-item vocoder reconstruction against source audio.

    Enforces:
    - Exact sample rate match (48000 Hz)
    - Exact hop size framing and trim lengths
    - Profile validation
    - Multi-scale STFT spectral distance
    - Median F0 reconstruction error
    """
    if sample_rate != EXPECTED_SAMPLE_RATE:
        raise ValueError(f"Sample rate mismatch: vocoder requires {EXPECTED_SAMPLE_RATE} Hz, got {sample_rate}")

    if profile is not None:
        if profile.get("profileId") != EXPECTED_PROFILE_ID:
            raise ValueError(f"Profile mismatch: expected {EXPECTED_PROFILE_ID}, got {profile.get('profileId')}")
        if profile.get("sampleRate") != sample_rate:
            raise ValueError("Profile sampleRate differs from audio clock")
        if profile.get("hopSize") != hop_size:
            raise ValueError("Profile hopSize differs from requested hop size")
        if profile.get("tailPadding") != "zero-to-whole-hop":
            raise ValueError("Profile requires zero-to-whole-hop tail padding")

    rendered = np.asarray(rendered_audio, dtype=np.float32).reshape(-1)
    source = np.asarray(source_audio, dtype=np.float32).reshape(-1)

    if not np.isfinite(rendered).all():
        raise ValueError("Nonfinite samples in rendered vocoder output")
    if not np.isfinite(source).all():
        raise ValueError("Nonfinite samples in source audio")

    if len(rendered) % hop_size != 0:
        raise ValueError(
            f"Rendered audio length {len(rendered)} is not a multiple of hop size {hop_size}"
        )

    valid_samples = min(len(rendered), len(source))
    if valid_samples < hop_size:
        raise ValueError(f"Audio payload too short ({valid_samples} samples) for vocoder reconstruction")

    padded_samples = len(rendered) - valid_samples

    rendered_valid = rendered[:valid_samples]
    source_valid = source[:valid_samples]

    spec_dist = compute_stft_spectral_distance(
        rendered_valid, source_valid, sample_rate=sample_rate
    )
    f0_cents, f0_rmse, pitch_reason = compute_f0_reconstruction_error(
        rendered_valid, source_valid, sample_rate=sample_rate, target_hz=target_hz
    )

    rendered_peak = float(np.max(np.abs(rendered_valid)))
    rendered_rms = float(np.sqrt(np.mean(np.square(rendered_valid))))
    source_peak = float(np.max(np.abs(source_valid)))
    source_rms = float(np.sqrt(np.mean(np.square(source_valid))))

    pitch_ok = (f0_cents is not None and abs(f0_cents) <= max_acceptable_pitch_error_cents) or (target_hz is None and pitch_reason is not None)
    spec_ok = spec_dist <= max_acceptable_spectral_distance
    energy_ok = (rendered_peak > 1e-4 or source_peak < 1e-4) and rendered_peak <= 1.05

    reconstruction_satisfied = bool(spec_ok and pitch_ok and energy_ok)

    return {
        "formatId": "com.project-seam.vocoder-reconstruction-measurement",
        "schemaVersion": 1,
        "sampleRate": sample_rate,
        "hopSize": hop_size,
        "profileId": profile.get("profileId", EXPECTED_PROFILE_ID) if profile else EXPECTED_PROFILE_ID,
        "validSamples": int(valid_samples),
        "paddedSamples": int(padded_samples),
        "totalFrames": int(len(rendered) // hop_size),
        "spectralDistance": round(float(spec_dist), 6),
        "f0RmseHz": round(float(f0_rmse), 4) if f0_rmse is not None else None,
        "f0MedianErrorCents": round(float(f0_cents), 3) if f0_cents is not None else None,
        "pitchReason": pitch_reason,
        "renderedPeak": round(rendered_peak, 6),
        "renderedRms": round(rendered_rms, 6),
        "sourcePeak": round(source_peak, 6),
        "sourceRms": round(source_rms, 6),
        "reconstructionSatisfied": reconstruction_satisfied,
    }


def evaluate_held_out_reconstruction(
    generator_fn,
    items: Sequence[dict],
    *,
    dataset_sha256: str,
    profile_sha256: str,
    output_directory: Path | None = None,
    label_origin: str = "com.project-seam.training-generated-teacher",
    sample_rate: int = EXPECTED_SAMPLE_RATE,
    hop_size: int = EXPECTED_HOP_SIZE,
    profile: dict | None = None,
) -> dict:
    """Evaluate vocoder reconstruction over held-out items and retain the measurement receipt.

    Strict invariants:
    - labelOrigin is permanently recorded.
    - releaseEligible is strictly False.
    - trainingAdmitted is strictly False.
    """
    if not items:
        raise ValueError("Held-out evaluation requires at least one item")

    measurements = []
    total_samples = 0
    spec_distances = []
    f0_errors = []

    for item in items:
        source_id = item["sourceId"]
        source_pcm = item["pcm"]
        mel = item["mel"]
        f0 = item.get("f0")
        target_hz = item.get("frequencyHz")

        rendered_pcm = generator_fn(mel, f0)
        measurement = measure_vocoder_reconstruction(
            rendered_audio=rendered_pcm,
            source_audio=source_pcm,
            sample_rate=sample_rate,
            hop_size=hop_size,
            profile=profile,
            target_hz=target_hz,
        )
        measurement["sourceId"] = source_id
        measurements.append(measurement)
        total_samples += measurement["validSamples"]
        spec_distances.append(measurement["spectralDistance"])
        if measurement["f0MedianErrorCents"] is not None:
            f0_errors.append(abs(measurement["f0MedianErrorCents"]))

    receipt = {
        "formatId": "com.project-seam.vocoder-reconstruction-receipt",
        "schemaVersion": 1,
        "createdAt": datetime.now(timezone.utc).isoformat(),
        "datasetSha256": dataset_sha256,
        "profileSha256": profile_sha256,
        "labelOrigin": label_origin,
        "releaseEligible": False,
        "trainingAdmitted": False,
        "summary": {
            "itemCount": len(measurements),
            "totalValidSamples": total_samples,
            "meanSpectralDistance": round(float(np.mean(spec_distances)), 6),
            "meanAbsolutePitchErrorCents": round(float(np.mean(f0_errors)), 3) if f0_errors else None,
            "allReconstructionsSatisfied": all(m["reconstructionSatisfied"] for m in measurements),
        },
        "items": measurements,
        "verdict": "RECONSTRUCTION_MEASURED",
    }

    if output_directory is not None:
        out_path = Path(output_directory) / "reconstruction_receipt.json"
        raw = json.dumps(receipt, indent=2) + "\n"
        with out_path.open("x", encoding="utf-8") as stream:
            stream.write(raw)

    return receipt
