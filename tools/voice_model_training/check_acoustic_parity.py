"""Optional Torch/librosa comparison; not trained-model or musical qualification.

Reference conventions inspected in openvpi/DiffSinger nvSTFT.py at
336cf01b57f2ad44c6b37a79cf33993043291759. Apply SEAM's explicit whole-hop zero
tail padding before that frontend's reflection/STFT/mel/log operations.
"""
import json


def main():
    import librosa
    import numpy as np
    import torch
    from tools.voice_model_training.acoustics import log_mel_targets

    torch.set_num_threads(1)
    results = []
    for rate, fft, hop in ((22050, 1024, 256), (48000, 1024, 256), (48000, 2048, 512)):
        size = fft * 3 + 17
        signals = {"silence": np.zeros(size),
                   "tone": .2 * np.sin(2 * np.pi * 440 * np.arange(size) / rate),
                   "noise": np.random.default_rng(17).uniform(-.25, .25, size),
                   "impulse": np.concatenate(([.5], np.zeros(size - 1)))}
        for name, signal in signals.items():
            actual = log_mel_targets(signal, sample_rate=rate, fft_size=fft, hop_size=hop)
            padded = np.pad(signal, (0, (-size) % hop))
            basis = librosa.filters.mel(sr=rate, n_fft=fft, n_mels=80, fmin=20,
                                        fmax=rate / 2, htk=False, norm="slaney", dtype=np.float64)
            for dtype, label in ((torch.float64, "float64"), (torch.float32, "float32")):
                audio = torch.tensor(padded, dtype=dtype)[None, :]
                audio = torch.nn.functional.pad(audio[:, None, :],
                    ((fft - hop) // 2, (fft - hop + 1) // 2), mode="reflect")[:, 0, :]
                spectrum = torch.stft(audio, n_fft=fft, hop_length=hop, win_length=fft,
                    window=torch.hann_window(fft, dtype=dtype), center=False,
                    normalized=False, onesided=True, return_complex=True).abs()
                reference = torch.log(torch.clamp(torch.tensor(basis, dtype=dtype) @ spectrum, min=1e-5))
                reference = reference[0].T.numpy()
                if reference.shape != actual.shape:
                    raise ValueError("Acoustic parity frame geometry differs")
                difference = np.abs(reference - actual)
                row = dict(sampleRate=rate, fftSize=fft, hopSize=hop, signal=name, referencePrecision=label,
                           maximumAbsoluteError=float(difference.max()), meanAbsoluteError=float(difference.mean()))
                results.append(row)
                # Float64 isolates algorithm conventions; float32 additionally measures
                # production frontend precision, especially at the logarithmic floor.
                row["withinTolerance"] = bool(difference.max() <= (2e-6 if label == "float64" else 0.01)
                                               and difference.mean() <= (1e-6 if label == "float64" else 0.0001))
    passed = all(row["withinTolerance"] for row in results)
    print(json.dumps(dict(numpy=np.__version__, torch=torch.__version__, librosa=librosa.__version__,
                         cases=results, passed=passed, modelCompatibilityProven=False), indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
