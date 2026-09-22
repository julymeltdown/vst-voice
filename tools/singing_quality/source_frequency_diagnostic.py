"""Additive source-energy diagnostic, never a singing or renderer acceptance oracle.

No filtering, demeaning, gain matching, F0 estimation, or automatic clip selection.
The rectangular DFT shares use Parseval weighting; Hann shares are separately
labeled windowed measurements. A dominant spectral bin is not a physical F0.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np

from tools.voice_model_training.audio_source import decode_pcm_source


def measure_window(samples, rate: int) -> dict:
    samples = np.asarray(samples, dtype=np.float64)
    if (rate not in (44100, 48000) or samples.ndim != 1
            or not 2 <= samples.size <= rate or not np.isfinite(samples).all()
            or np.max(np.abs(samples)) > 1):
        raise ValueError("Expected bounded, finite, normalized mono source samples")
    square = float(np.mean(samples * samples))
    result = dict(frames=int(samples.size), mean=float(samples.mean()),
                  rms=float(np.sqrt(square)), acRms=float(np.std(samples)),
                  peak=float(np.max(np.abs(samples))),
                  dcMeanSquareShare=float(samples.mean() ** 2 / square) if square else None,
                  spectra=[])
    for name, window in (("rectangular", np.ones(samples.size)),
                         ("hann", np.hanning(samples.size))):
        windowed = samples * window
        power = np.abs(np.fft.rfft(windowed)) ** 2
        # One-sided real DFT: DC and even-length Nyquist are not doubled.
        weights = np.full(power.size, 2.0)
        weights[0] = 1.0
        if samples.size % 2 == 0:
            weights[-1] = 1.0
        power *= weights
        total = float(power.sum())
        time_energy = float(np.dot(windowed, windowed))
        frequency_energy = total / samples.size
        if not np.isclose(frequency_energy, time_energy, rtol=1e-12, atol=1e-15):
            raise ValueError("Parseval energy accounting failed")
        frequencies = np.fft.rfftfreq(samples.size, 1.0 / rate)
        result["spectra"].append(dict(
            window=name, windowedTimeEnergy=time_energy,
            parsevalFrequencyEnergy=frequency_energy,
            dominantBinHz=float(frequencies[np.argmax(power)]) if total else None,
            powerSharesBelowHz={str(cutoff): float(power[frequencies < cutoff].sum() / total)
                                if total else None for cutoff in (40, 71, 100, 400)}))
    return result


def diagnose(payload: bytes, expected_sha256: str, rate: int, *, start: int = 0,
             frames: int | None = None) -> dict:
    metadata, samples = decode_pcm_source(payload, expected_sha256=expected_sha256, sample_rate=rate)
    if frames is None:
        frames = samples.size - start
    if (type(start) is not int or type(frames) is not int or start < 0
            or rate not in (44100, 48000) or not rate * 4 // 10 <= frames <= rate
            or start + frames > samples.size):
        raise ValueError("Expected explicit in-range 400-1000 ms source slice")
    selected = samples[start:start + frames]
    return dict(formatId="com.project-seam.source-frequency-diagnostic", schemaVersion=1,
                source=metadata, startFrame=start, selectedFrames=frames,
                wholeSlice=measure_window(selected, rate),
                fixed100To400ms=measure_window(selected[rate // 10:rate * 4 // 10], rate),
                interpretation="Descriptive source energy only; dominant bin is not F0; no source suitability or perceptual verdict",
                transformedSource=False, originalAssessmentsSuperseded=False,
                productionEnabled=False, releaseEligible=False, listeningStatus="NOT_REVIEWED",
                numpyVersion=np.__version__,
                diagnosticSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--sample-rate", type=int, choices=(44100, 48000), required=True)
    parser.add_argument("--start-frame", type=int, default=0)
    parser.add_argument("--frames", type=int)
    parser.add_argument("--output", type=Path, help="New JSON file; existing files are never overwritten")
    args = parser.parse_args()
    try:
        with args.source.open("rb") as stream:
            payload = stream.read(64 * 1024 * 1024 + 1)
        result = diagnose(payload, args.sha256, args.sample_rate,
                          start=args.start_frame, frames=args.frames)
        rendered = json.dumps(result, indent=2, allow_nan=False) + "\n"
        if args.output:
            with args.output.open("x", encoding="utf-8") as stream:
                stream.write(rendered)
        else:
            print(rendered, end="")
    except (OSError, ValueError) as error:
        parser.exit(2, f"SOURCE_FREQUENCY_DIAGNOSTIC=ERROR: {error}\n")


if __name__ == "__main__":
    main()
