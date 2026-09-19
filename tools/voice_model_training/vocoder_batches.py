"""Join captured PCM with source-bound mel/F0 batches; not source authorization.

The epoch service must freshly revalidate rights/labels and reject incomplete
coverage before publishing. Saved dataset snapshots alone are not authority.
"""
import os
from pathlib import Path
import stat

from .audio_source import decode_pcm_source
from .batches import iter_supervised_batches


def segment_frame_ranges(frames, maximum_frames):
    """Balanced ownership avoids a one-hop tail that cannot support reflect STFT."""
    if (type(frames) is not int or not 2 <= frames <= 4096 or
            type(maximum_frames) is not int or not 16 <= maximum_frames <= 4096):
        raise ValueError("Segmented vocoder training needs 2..4096 source hops and a 16..4096 hop budget")
    count = (frames + maximum_frames - 1) // maximum_frames
    width, extra = divmod(frames, count)
    start = 0
    for index in range(count):
        end = start + width + (index < extra)
        yield start, end
        start = end


def iter_vocoder_batches(snapshot, directory, targets, pcm_sources, *, expected_profile_sha256,
                         partition, batch_frames=256, training_segment_frames=None):
    """Yield owned CPU float32 BFT mel, BF F0 and B1S PCM for one source at a time.

pcm_sources maps each captured source ID to its trusted local WAV path. No
resampling, normalization or random replacement is applied. A partial final hop
is zero-padded according to the acoustic profile and explicitly counted.
"""
    import numpy as np
    import torch
    if training_segment_frames is not None and (partition != "train" or batch_frames != 4096):
        raise ValueError("Balanced training segments require whole-source train batches")
    sources = {row["sourceId"]: row for row in snapshot["sources"]}
    if not isinstance(pcm_sources, dict) or set(pcm_sources) != set(sources):
        raise ValueError("PCM inventory must cover exactly the captured dataset")
    active, samples, inspected = None, None, None
    for batch in iter_supervised_batches(snapshot, directory, targets,
            expected_profile_sha256=expected_profile_sha256, partition=partition,
            batch_frames=batch_frames, context_frames=0):
        identity = batch["sourceId"]
        row = sources[identity]
        profile = targets[identity][0]["profile"]
        if (profile.get("tailPadding") != "zero-to-whole-hop"
                or profile.get("amplitudeScale") != "ln-amplitude"):
            raise ValueError("Vocoder requires explicit full-hop log-mel geometry")
        if active != identity:
            path = Path(pcm_sources[identity])
            if path.is_symlink():
                raise ValueError("PCM source cannot be a symlink")
            flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
            with os.fdopen(os.open(path, flags), "rb") as stream:
                info = os.fstat(stream.fileno())
                if not stat.S_ISREG(info.st_mode) or not 1 <= info.st_size <= 64 * 1024 * 1024:
                    raise ValueError("PCM source must be a bounded regular file")
                payload = stream.read(info.st_size + 1)
                after = os.fstat(stream.fileno())
                if len(payload) != info.st_size or (info.st_size, info.st_mtime_ns, info.st_ctime_ns) != (after.st_size, after.st_mtime_ns, after.st_ctime_ns):
                    raise ValueError("PCM source changed during capture")
            inspected, samples = decode_pcm_source(payload, expected_sha256=row["sourceSha256"], sample_rate=row["sampleRate"])
            if any(inspected[key] != row[key] for key in ("audioSha256", "frameCount", "sampleRate")):
                raise ValueError("PCM identity or clock differs from captured dataset")
            active = identity
        hop = batch["hopSize"]
        frames = len(batch["columns"]["f0Hz"])
        length, start = frames * hop, batch["frameOffset"] * hop
        if not 1 <= length <= 1048576:
            raise ValueError("Vocoder PCM batch exceeds one million samples")
        valid = min(length, len(samples) - start)
        if valid <= 0 or valid != sum(batch["columns"]["validSamples"]):
            raise ValueError("PCM ownership differs from conditioning frame coverage")
        audio = np.zeros(length, dtype=np.float32)
        audio[:valid] = samples[start:start + valid]
        f0 = np.asarray(batch["columns"]["f0Hz"], dtype=np.float32)
        if not np.isfinite(f0).all() or np.any(f0 < 0):
            raise ValueError("Invalid vocoder pitch conditioning")
        captured = dict(sourceId=identity, partition=partition, datasetSha256=batch["datasetSha256"],
                   profileSha256=expected_profile_sha256, targetSha256=batch["targetSha256"],
                   sourceSha256=inspected["sourceSha256"], audioSha256=inspected["audioSha256"],
                   frameOffset=batch["frameOffset"], hopSize=hop, validSamples=valid,
                   paddedSamples=length - valid, phraseAnalysisFrames=batch["phraseAnalysisFrames"],
                   mel=torch.from_numpy(batch["melTargets"].T.copy()).unsqueeze(0),
                   f0=torch.from_numpy(f0.copy()).unsqueeze(0),
                   pcm=torch.from_numpy(audio).reshape(1, 1, -1), trainingAdmitted=False)
        if training_segment_frames is None:
            yield captured
        else:
            if start != 0 or frames != batch["phraseAnalysisFrames"]:
                raise ValueError("Training segmentation requires one complete source")
            for begin, end in segment_frame_ranges(frames, training_segment_frames):
                owned = min((end - begin) * hop, valid - begin * hop)
                if owned <= 0:
                    raise ValueError("Training segment has no owned source samples")
                yield dict(captured, frameOffset=begin, validSamples=owned,
                           paddedSamples=(end - begin) * hop - owned,
                           mel=captured["mel"][:, :, begin:end].clone(),
                           f0=captured["f0"][:, begin:end].clone(),
                           pcm=captured["pcm"][:, :, begin * hop:end * hop].clone())
