"""Byte-bound PCM source inspection, separate from permission admission."""
import hashlib
import json
import math
import struct


def decode_pcm_source(payload: bytes, *, expected_sha256: str, sample_rate: int):
    """Return inspected identity and normalized float64 mono PCM, without transforms."""
    import numpy as np
    if type(sample_rate) is not int or not 8000 <= sample_rate <= 192000:
        raise ValueError("Requested sample rate is invalid")
    source, pcm = read_pcm_source(payload, expected_sha256=expected_sha256, sample_rate=sample_rate)
    if source["frameCount"] > 16000000:
        raise ValueError("Decoded source exceeds sample budget")
    width = source["sampleWidthBytes"]
    if source.get("sampleEncoding") == "ieee-float32-le":
        return source, np.frombuffer(pcm, dtype="<f4").astype(np.float64)
    if width == 3:
        octets = np.frombuffer(pcm, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        values = octets[:, 0] | (octets[:, 1] << 8) | (octets[:, 2] << 16)
        values = (values ^ 0x800000) - 0x800000
    else:
        values = np.frombuffer(pcm, dtype="<i2" if width == 2 else "<i4")
    return source, values.astype(np.float64) / (1 << (8 * width - 1))


def inspect_pcm_source(payload: bytes, *, expected_sha256: str, sample_rate: int) -> dict:
    """Inspect mono PCM16/24/32 or finite normalized float32 without conversion.

    Integer audio identities retain schema 1. Float32 uses schema 2 and an encoding
    discriminator, so its bytes cannot be mistaken for int32 samples in a split.
    """
    if type(sample_rate) is not int or not 8000 <= sample_rate <= 192000:
        raise ValueError("Requested sample rate is invalid")
    return read_pcm_source(payload, expected_sha256=expected_sha256, sample_rate=sample_rate)[0]


def read_pcm_source(payload: bytes, *, expected_sha256: str, sample_rate: int | None = None):
    """Return inspected identity and a read-only view of exact sample bytes.

    Omit sample_rate only to capture the file's clock (e.g. for a teacher digest).
    A supplied clock must match. No source rights or annotation approval is inferred.
    """
    if not isinstance(payload, bytes) or not 1 <= len(payload) <= 64 * 1024 * 1024:
        raise ValueError("Source WAV must contain at most 64 MiB")
    if sample_rate is not None and (type(sample_rate) is not int or not 8000 <= sample_rate <= 192000):
        raise ValueError("Requested sample rate is invalid")
    source_hash = hashlib.sha256(payload).hexdigest()
    if source_hash != expected_sha256:
        raise ValueError("Source WAV differs from its captured digest")
    if (len(payload) < 44 or payload[:4] != b"RIFF" or payload[8:12] != b"WAVE"
            or struct.unpack_from("<I", payload, 4)[0] != len(payload) - 8):
        raise ValueError("Unsupported or malformed source WAV")
    chunks, cursor, chunk_count = {}, 12, 0
    while cursor < len(payload):
        if cursor + 8 > len(payload) or chunk_count >= 1024:
            raise ValueError("Malformed or excessive WAV chunks")
        name, size = struct.unpack_from("<4sI", payload, cursor)
        cursor += 8
        end = cursor + size
        if end > len(payload):
            raise ValueError("Truncated WAV chunk")
        if name in (b"fmt ", b"data", b"fact"):
            if name in chunks:
                raise ValueError("Duplicate source WAV chunk")
            chunks[name] = memoryview(payload)[cursor:end]
        # Python's wave writer omits final odd-byte padding; accept that final
        # form as well as RIFF's padded form, without treating pad bytes as PCM.
        cursor = end if end == len(payload) else end + size % 2
        chunk_count += 1
    if b"fmt " not in chunks or b"data" not in chunks or len(chunks[b"fmt "]) < 16:
        raise ValueError("Source WAV requires format and sample data")
    fmt, pcm = chunks[b"fmt "], chunks[b"data"]
    encoding, channels, rate, byte_rate, alignment, bits = struct.unpack_from("<HHIIHH", fmt)
    if encoding == 0xfffe:
        if (len(fmt) < 40 or struct.unpack_from("<H", fmt, 16)[0] < 22
                or len(fmt) < 18 + struct.unpack_from("<H", fmt, 16)[0]):
            raise ValueError("Truncated extensible WAV format")
        valid_bits, mask = struct.unpack_from("<HI", fmt, 18)
        guid = bytes(fmt[24:40])
        if (valid_bits != bits or mask not in (0, 4)
                or guid[4:] != bytes.fromhex("00001000800000aa00389b71")):
            raise ValueError("Unsupported extensible PCM layout")
        encoding = struct.unpack_from("<I", guid)[0]
    width = bits // 8
    if (channels != 1 or (encoding, bits) not in ((1, 16), (1, 24), (1, 32), (3, 32))
            or alignment != width or byte_rate != rate * alignment):
        raise ValueError("Source must be mono PCM16/24/32 or IEEE float32")
    if (not 8000 <= rate <= 192000 or sample_rate is not None and rate != sample_rate
            or not pcm or len(pcm) % alignment):
        raise ValueError("Source clock or PCM frame geometry differs")
    frames = len(pcm) // alignment
    if frames > rate * 600:
        raise ValueError("Source exceeds ten-minute duration bound")
    if b"fact" in chunks and (len(chunks[b"fact"]) < 4
            or struct.unpack_from("<I", chunks[b"fact"])[0] != frames):
        raise ValueError("WAV fact frame count differs from sample data")
    if encoding == 3 and any(not math.isfinite(value) or abs(value) > 1
                             for (value,) in struct.iter_unpack("<f", pcm)):
        raise ValueError("Float source must be finite and normalized; no clipping is applied")
    geometry = dict(sampleRate=rate, channels=channels, sampleWidthBytes=width, frameCount=frames)
    if encoding == 3:
        geometry["sampleEncoding"] = "ieee-float32-le"
    # Include geometry so identical bytes at different clocks cannot collide.
    audio_hash = hashlib.sha256(json.dumps(geometry, sort_keys=True, separators=(",", ":")).encode() + b"\0")
    audio_hash.update(pcm)
    return dict(formatId="com.project-seam.training-pcm-inspection", schemaVersion=2 if encoding == 3 else 1,
                sourceSha256=source_hash, audioSha256=audio_hash.hexdigest(), sourceBytes=len(payload),
                **geometry, sourceRightsAdmitted=False, releaseEligible=False), pcm
