"""Byte-bound PCM source inspection, separate from permission admission."""
import hashlib
import io
import json
import wave


def decode_pcm_source(payload: bytes, *, expected_sha256: str, sample_rate: int):
    """Return inspected identity and normalized float64 mono PCM, without transforms."""
    import numpy as np
    source = inspect_pcm_source(payload, expected_sha256=expected_sha256, sample_rate=sample_rate)
    if source["frameCount"] > 16000000:
        raise ValueError("Decoded source exceeds sample budget")
    with wave.open(io.BytesIO(payload), "rb") as reader:
        pcm = reader.readframes(source["frameCount"])
    width = source["sampleWidthBytes"]
    if width == 3:
        octets = np.frombuffer(pcm, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        values = octets[:, 0] | (octets[:, 1] << 8) | (octets[:, 2] << 16)
        values = (values ^ 0x800000) - 0x800000
    else:
        values = np.frombuffer(pcm, dtype="<i2" if width == 2 else "<i4")
    return source, values.astype(np.float64) / (1 << (8 * width - 1))


def inspect_pcm_source(payload: bytes, *, expected_sha256: str, sample_rate: int) -> dict:
    """Inspect mono integer PCM WAV without resampling or rewriting its source.

    Supports 16/24/32-bit integer PCM. Floating-point/compressed WAV is not
    silently converted. A later preparation stage must record any conversion.
    """
    if not isinstance(payload, bytes) or not 1 <= len(payload) <= 64 * 1024 * 1024:
        raise ValueError("Source WAV must contain at most 64 MiB")
    if type(sample_rate) is not int or not 8000 <= sample_rate <= 192000:
        raise ValueError("Requested sample rate is invalid")
    source_hash = hashlib.sha256(payload).hexdigest()
    if source_hash != expected_sha256:
        raise ValueError("Source WAV differs from its captured digest")
    try:
        with wave.open(io.BytesIO(payload), "rb") as reader:
            channels, width, rate, frames = reader.getnchannels(), reader.getsampwidth(), reader.getframerate(), reader.getnframes()
            if channels != 1 or width not in (2, 3, 4) or reader.getcomptype() != "NONE":
                raise ValueError("Source must be mono 16/24/32-bit integer PCM")
            if rate != sample_rate or not 1 <= frames <= rate * 600:
                raise ValueError("Source rate or ten-minute duration bound differs")
            expected_bytes = frames * channels * width
            if expected_bytes > 64 * 1024 * 1024:
                raise ValueError("Declared PCM exceeds source byte budget")
            pcm = reader.readframes(frames)
            if len(pcm) != expected_bytes or reader.readframes(1):
                raise ValueError("PCM payload differs from declared frame count")
    except (wave.Error, EOFError) as error:
        raise ValueError("Unsupported or malformed source WAV") from error
    geometry = dict(sampleRate=rate, channels=channels, sampleWidthBytes=width, frameCount=frames)
    # Include geometry so identical bytes at different clocks cannot collide.
    audio_hash = hashlib.sha256(json.dumps(geometry, sort_keys=True, separators=(",", ":")).encode() + b"\0" + pcm).hexdigest()
    return dict(formatId="com.project-seam.training-pcm-inspection", schemaVersion=1,
                sourceSha256=source_hash, audioSha256=audio_hash, sourceBytes=len(payload),
                **geometry, sourceRightsAdmitted=False, releaseEligible=False)
