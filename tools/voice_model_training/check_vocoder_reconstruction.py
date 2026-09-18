"""Bounded CPU forward check on captured real audio with an untrained upstream vocoder.

This checks the tensor/audio adapter, not training or reconstruction quality. It
loads no weights, creates no approval, and records the source crop and all output
bytes. Run only with the trusted pinned SingingVocoders checkout and an explicitly
selected source; this command does not establish source-use permission.
"""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import wave

from .acoustics import wav_log_mel_targets
from .audio_source import decode_pcm_source
from .check_vocoder_model import TRAINING_REVISION, trusted_checkout
from .native_features import extract_pitch
from .vocoder_reconstruction import evaluate_held_out_reconstruction


def _digest(payload):
    return hashlib.sha256(payload).hexdigest()


def _write_json(path, value):
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write("\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "trusted-checkout", "pitch-extractor", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--source-sha256", required=True)
    parser.add_argument("--offset-samples", type=int, default=48000)
    parser.add_argument("--sample-count", type=int, default=48037)
    args = parser.parse_args()
    try:
        if not 512 <= args.sample_count <= 96000 or args.offset_samples < 0:
            raise ValueError("Select a 512..96000-sample crop at a nonnegative offset")
        if args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir():
            raise ValueError("Check output must be a new directory with an existing parent")
        if args.source.is_symlink() or not 1 <= args.source.stat().st_size <= 64 * 1024 * 1024:
            raise ValueError("Source must be a bounded regular WAV file")
        with args.source.open("rb") as stream:
            source = stream.read(64 * 1024 * 1024 + 1)
        if len(source) > 64 * 1024 * 1024 or _digest(source) != args.source_sha256:
            raise ValueError("Source differs from the captured digest")
        # Preserve the source's integer PCM bytes for this diagnostic crop. No
        # normalization, resampling or format conversion is hidden in the check.
        with wave.open(io.BytesIO(source), "rb") as reader:
            if reader.getnchannels() != 1 or reader.getframerate() != 48000 or reader.getsampwidth() not in (2, 3, 4):
                raise ValueError("Check source must be 48 kHz mono integer PCM")
            if args.offset_samples + args.sample_count > reader.getnframes():
                raise ValueError("Crop exceeds source length")
            width = reader.getsampwidth()
            reader.setpos(args.offset_samples)
            crop_pcm = reader.readframes(args.sample_count)
        buffer = io.BytesIO()
        with wave.open(buffer, "wb") as writer:
            writer.setnchannels(1)
            writer.setsampwidth(width)
            writer.setframerate(48000)
            writer.writeframes(crop_pcm)
        crop = buffer.getvalue()
        # RIFF chunks are word-aligned. Python's wave writer omits this trailing
        # pad for odd-length PCM24 data; the native decoder correctly requires it.
        if len(crop_pcm) % 2:
            crop = crop[:4] + struct.pack("<I", len(crop) - 7) + crop[8:] + b"\0"
        crop_digest = _digest(crop)
        record, mel = wav_log_mel_targets(crop, expected_sha256=crop_digest, sample_rate=48000)
        _, pcm = decode_pcm_source(crop, expected_sha256=crop_digest, sample_rate=48000)
        checkout = trusted_checkout(args.trusted_checkout, TRAINING_REVISION)
        args.output.mkdir(mode=0o700)
        crop_path = args.output / "source-crop.wav"
        with crop_path.open("xb") as stream:
            stream.write(crop)
        pitch = extract_pitch(args.pitch_extractor, crop_path)
        if (pitch["sourceSha256"] != crop_digest or pitch["sampleRate"] != 48000
                or pitch["frameCount"] != args.sample_count or pitch["hopSize"] != 256
                or len(pitch["pitchFrames"]) != len(mel)
                or any(frame["sourceFrame"] != index * 256 for index, frame in enumerate(pitch["pitchFrames"]))):
            raise ValueError("Pitch features do not match the exact crop clock and identity")
        _write_json(args.output / "pitch.json", pitch)
        _write_json(args.output / "target.json", record)
        with (args.output / "mel.f32le").open("xb") as stream:
            stream.write(mel.astype("<f4").tobytes())
        import torch
        torch.set_num_threads(1)
        torch.manual_seed(928)
        spec = importlib.util.spec_from_file_location("seam_reconstruction_upstream",
                                                     checkout / "models/nsf_HiFigan/models.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        configuration = dict(sampling_rate=48000, num_mels=80, hop_size=256, n_fft=1024,
            win_size=1024, fmin=20, fmax=24000, mini_nsf=True, noise_sigma=0.,
            upsample_rates=[8, 8, 2, 2], upsample_kernel_sizes=[16, 16, 4, 4],
            upsample_initial_channel=32, resblock_kernel_sizes=[3],
            resblock_dilation_sizes=[[1, 3, 5]], resblock="1", pc_aug=False)
        model = module.Generator(module.AttrDict(configuration)).train()
        item = dict(sourceId="real-source-crop", pcm=pcm, mel=torch.from_numpy(mel.T.copy()).unsqueeze(0),
            f0=torch.tensor([[frame["f0Hz"] for frame in pitch["pitchFrames"]]], dtype=torch.float32),
            validSamples=args.sample_count, sourceSha256=crop_digest, audioSha256=record["audioSha256"],
            targetSha256=record["targetSha256"], frameOffset=0, phraseAnalysisFrames=len(mel))
        rng = torch.get_rng_state().clone()
        receipt = evaluate_held_out_reconstruction(model, [item], dataset_sha256=crop_digest,
            profile_sha256=record["profileSha256"], profile=record["profile"], output_directory=args.output,
            label_origin="native-pitch-measurement-no-phonetic-labels")
        if not model.training or not torch.equal(rng, torch.get_rng_state()):
            raise AssertionError("Evaluation changed the caller's model mode or CPU RNG")
        report = dict(formatId="com.project-seam.untrained-vocoder-reconstruction-check", schemaVersion=1,
            tensorAudioCheckPassed=True, upstreamRevision=TRAINING_REVISION, configuration=configuration,
            initializationSeed=928, weightsTrained=False, trainingPerformed=False, heldOutStudy=False,
            sourcePath=str(args.source.resolve()), sourceSha256=args.source_sha256,
            sourceBytes=len(source), cropOffsetSamples=args.offset_samples, cropSamples=args.sample_count,
            cropPcmBytes=len(crop_pcm), cropPcmSha256=_digest(crop_pcm), cropSha256=crop_digest,
            sourceUse="evaluation-only; caller-supplied permission, not verified by this check",
            reconstructionReceiptSha256=_digest((args.output / "reconstruction_receipt.json").read_bytes()),
            reconstruction=receipt["summary"], torchVersion=torch.__version__,
            sourcePermissionsAdmitted=False, labelsAdmitted=False, trainingAdmitted=False,
            singerQualified=False, releaseEligible=False)
        _write_json(args.output / "check.json", report)
        print(json.dumps(report, sort_keys=True))
        return 0
    except (OSError, ValueError, RuntimeError, ImportError, wave.Error) as error:
        print(str(error)[:512], file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
