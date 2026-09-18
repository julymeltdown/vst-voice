"""Real audio transcription for diagnostic triage; never singer qualification.

The optional backend loads a local CTranslate2 Whisper model. No model download,
reference-lyric prompt, confidence fabrication, or human gate acceptance occurs.
"""
from __future__ import annotations

import hashlib
import importlib.metadata
import io
import math
from pathlib import Path
import platform
import random
import struct
import unicodedata
import wave

DEFAULT_ASR_MODEL = "Systran/faster-whisper-base@ebe41f70d5b6dfa9166e2c581c45c9c0cfc57b66"
DEFAULT_DECODING_SETTINGS = {
    "language": "ja", "beam_size": 5, "temperature": 0.0,
    "condition_on_previous_text": False, "vad_filter": False,
    "initial_prompt": None, "prefix": None, "hotwords": None,
    "no_speech_threshold": 0.6, "log_prob_threshold": -1.0,
    "compression_ratio_threshold": 2.4,
}
PINNED_NEGATIVE_CONTROLS = ("silence-v1", "white-noise-seed-9127-v1", "impulse-v1")
MAX_AUDIO_BYTES = 128 * 1024 * 1024
MAX_ITEMS = 512


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _read(path: Path, limit: int) -> bytes:
    with path.open("rb") as source:
        raw = source.read(limit + 1)
    if len(raw) > limit:
        raise ValueError(f"ASR input exceeds size limit: {path}")
    return raw


def validate_wave(payload: bytes) -> None:
    """Bound uncompressed WAV geometry before allocating decoded audio."""
    if payload[:4] != b"RIFF" or payload[8:12] != b"WAVE" or len(payload) < 44:
        raise ValueError("ASR triage expects RIFF/WAVE audio")
    if struct.unpack_from("<I", payload, 4)[0] + 8 != len(payload):
        raise ValueError("ASR WAV length disagrees with RIFF header")
    chunks, position = {}, 12
    while position + 8 <= len(payload):
        name, size = struct.unpack_from("<4sI", payload, position)
        position += 8
        if position + size > len(payload):
            raise ValueError("Truncated ASR WAV chunk")
        if name in (b"fmt ", b"data"):
            if name in chunks:
                raise ValueError("Duplicate ASR WAV chunk")
            chunks[name] = (position, size)
        position += size + size % 2
    if b"fmt " not in chunks or b"data" not in chunks or chunks[b"fmt "][1] < 16:
        raise ValueError("ASR WAV needs format and sample data")
    encoding, channels, rate, _, alignment, bits = struct.unpack_from("<HHIIHH", payload, chunks[b"fmt "][0])
    if ((encoding, bits) not in ((1, 16), (1, 24), (1, 32), (3, 32))
            or not 1 <= channels <= 8 or not 8000 <= rate <= 192000
            or alignment != channels * (bits // 8)):
        raise ValueError("ASR expects bounded PCM16/24/32 or Float32 WAV")
    size = chunks[b"data"][1]
    if not size or size % alignment or size // alignment > rate * 120:
        raise ValueError("ASR WAV must have complete frames and at most 120 seconds")


def control_wav(name: str) -> bytes:
    """Exactly two seconds of seeded PCM16, including a real single-sample glitch."""
    rng = random.Random(9127)
    if name == "silence-v1":
        samples = [0] * 32000
    elif name == "white-noise-seed-9127-v1":
        samples = [rng.randrange(-4096, 4097) for _ in range(32000)]
    elif name == "impulse-v1":
        samples = [0] * 32000
        samples[16000] = 30000
    else:
        raise ValueError("Unknown ASR negative control")
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as output:
        output.setparams((1, 2, 16000, len(samples), "NONE", "not compressed"))
        output.writeframes(struct.pack("<32000h", *samples))
    return buffer.getvalue()


def normalize_text(text: str) -> str:
    # Orthographic comparison only: no invented kanji readings or semantic match.
    normalized = unicodedata.normalize("NFKC", text).casefold()
    return "".join(chr(ord(c) - 0x60) if "ァ" <= c <= "ヶ" else c
                   for c in normalized if unicodedata.category(c)[0] in "LNM")


def character_error_rate(expected: str, actual: str) -> float:
    expected, actual = normalize_text(expected), normalize_text(actual)
    if not expected:
        raise ValueError("ASR reference text has no comparable characters")
    previous = list(range(len(actual) + 1))
    for i, left in enumerate(expected, 1):
        current = [i]
        for j, right in enumerate(actual, 1):
            current.append(min(current[-1] + 1, previous[j] + 1,
                               previous[j - 1] + (left != right)))
        previous = current
    return previous[-1] / len(expected)


class FasterWhisperBackend:
    def __init__(self, model_directory: Path, language: str = "ja"):
        if language not in ("ja", "en", "ko"):
            raise ValueError("ASR language must be ja, en, or ko")
        if not model_directory.is_dir():
            raise ValueError("--asr-model must name a local faster-whisper model directory")
        try:
            from faster_whisper import WhisperModel
        except ImportError as error:
            raise ValueError("Install tools/singing_quality/requirements-asr.txt in an isolated environment") from error
        files = {}
        for name in ("model.bin", "config.json", "tokenizer.json", "vocabulary.txt"):
            path = model_directory / name
            h = hashlib.sha256()
            with path.open("rb") as source:
                for chunk in iter(lambda: source.read(1024 * 1024), b""):
                    h.update(chunk)
            files[name] = h.hexdigest()
        self.settings = dict(DEFAULT_DECODING_SETTINGS, language=language)
        self.identity = {
            "backend": "faster-whisper", "files": files,
            "versions": {name: importlib.metadata.version(name) for name in
                         ("faster-whisper", "ctranslate2", "av", "numpy", "tokenizers")},
            "python": platform.python_version(), "platform": platform.platform(),
            "device": "cpu", "computeType": "int8", "cpuThreads": 4,
        }
        self.model = WhisperModel(str(model_directory.resolve()), device="cpu",
                                  compute_type="int8", cpu_threads=4, num_workers=1,
                                  local_files_only=True)

    def transcribe(self, payload: bytes) -> dict:
        import numpy as np
        from faster_whisper.audio import decode_audio
        validate_wave(payload)
        audio = decode_audio(io.BytesIO(payload), sampling_rate=16000)
        if not 0 < len(audio) <= 16000 * 120 or not np.isfinite(audio).all():
            raise ValueError("ASR audio must be finite and at most 120 seconds")
        segments, _ = self.model.transcribe(audio, **self.settings)
        retained = [{"start": s.start, "end": s.end, "text": s.text,
                     "avgLogprob": s.avg_logprob, "noSpeechProbability": s.no_speech_prob}
                    for s in segments]  # Consuming the generator actually runs inference.
        for row in retained:
            if any(not math.isfinite(row[k]) for k in
                   ("start", "end", "avgLogprob", "noSpeechProbability")):
                raise ValueError("Nonfinite ASR result")
        return {"text": "".join(s["text"] for s in retained).strip(), "segments": retained,
                "decoded16kMono": {"frames": len(audio), "peak": float(np.abs(audio).max()),
                                   "rms": float(np.sqrt(np.mean(audio.astype(np.float64) ** 2)))}}


def screen_packet(manifest: dict, audio_root: Path, backend, expected_text: dict | None = None,
                  progress=None) -> dict:
    """Hash the actual inputs and run every item/control through the same recognizer.

    A backend is injectable for unit tests; only FasterWhisperBackend is used by CLI.
    Test doubles prove orchestration, never recognition quality.
    """
    root = audio_root.resolve(strict=True)
    entries = []
    if "items" in manifest:
        entries = [(it.get("scoreIdentity"), it) for it in manifest["items"]]
    else:
        entries = [(case.get("id"), it) for case in manifest.get("cases", [])
                   for it in case.get("outputs", [])]
    if not 0 < len(entries) <= MAX_ITEMS:
        raise ValueError("ASR packet must contain 1..512 audio items")
    expected_text = {} if expected_text is None else expected_text
    if not isinstance(expected_text, dict) or any(
            not isinstance(k, str) or not isinstance(v, str) or not 0 < len(v) <= 4096
            or not normalize_text(v) for k, v in expected_text.items()):
        raise ValueError("ASR expected text must map case IDs to nonempty text (at most 4096 characters)")
    if set(expected_text) - {case_id for case_id, _ in entries}:
        raise ValueError("ASR expected text contains an unknown case ID")
    # Validate the complete manifest before invoking an expensive recognizer.
    checked, seen = [], set()
    for case_id, item in entries:
        relative = item.get("path")
        if not isinstance(relative, str) or not relative or Path(relative).is_absolute():
            raise ValueError("ASR packet paths must be relative")
        path = (root / relative).resolve(strict=True)
        if not path.is_relative_to(root) or path in seen:
            raise ValueError("ASR packet path escapes the root or is duplicated")
        seen.add(path)
        digest = item.get("wavSha256") or item.get("sha256")
        if not isinstance(digest, str) or len(digest) != 64:
            raise ValueError("ASR packet audio must have a SHA-256 digest")
        payload = _read(path, MAX_AUDIO_BYTES)
        if sha256(payload) != digest:
            raise ValueError(f"ASR packet hash mismatch: {relative}")
        validate_wave(payload)
        checked.append((case_id, item, path, digest))
    controls = []
    for name in PINNED_NEGATIVE_CONTROLS:
        payload = control_wav(name)
        result = backend.transcribe(payload)
        detected = bool(normalize_text(result["text"]))
        controls.append({"id": name, "sha256": sha256(payload), "transcription": result,
                         "detected": detected, "controlHeld": not detected,
                         "status": "BREACH" if detected else "OBSERVED_NO_TEXT"})
    items = []
    for case_id, item, path, digest in checked:
        payload = _read(path, MAX_AUDIO_BYTES)
        if sha256(payload) != digest:
            raise ValueError(f"ASR audio changed during screening: {item['path']}")
        result = backend.transcribe(payload)
        if len(result["text"]) > 8192:
            raise ValueError("ASR transcript exceeds screening limit")
        expected = expected_text.get(case_id)
        cer = character_error_rate(expected, result["text"]) if expected is not None else None
        reasons = []
        if not normalize_text(result["text"]):
            reasons.append("no_text_recognized")
        if cer is not None and cer > 0:
            reasons.append("reference_text_mismatch")
        row = {"path": item["path"], "wavSha256": digest, "scoreIdentity": case_id,
               "recipeIdentity": item.get("recipeIdentity") or item.get("variant"),
               "transcription": result, "expectedText": expected, "characterErrorRate": cer,
               "textComparison": "MEASURED_ORTHOGRAPHIC_ONLY" if expected else "NOT_AVAILABLE",
               "flagged": bool(reasons), "reasons": reasons}
        items.append(row)
        if progress:
            progress(f"ASR_ITEM={len(items)}/{len(checked)} path={item['path']} text={result['text']!r}")
    breach = any(not row["controlHeld"] for row in controls)
    return {"formatId": "com.project-seam.listening-asr-triage", "schemaVersion": 2,
            "runnerSha256": sha256(Path(__file__).read_bytes()),
            "packetSourceCommit": manifest.get("sourceCommit"),
            "label": "triage", "verdict": "triage_control_breach" if breach else "triage",
            "perceptualStatus": "UNREVIEWED", "releaseEligible": False,
            "calibration": "NEGATIVE_CONTROLS_ONLY_NO_SINGING_SENSITIVITY_CLAIM",
            "model": backend.identity, "decodingSettings": backend.settings,
            "negativeControls": controls, "items": items,
            "summary": {"totalItems": len(items), "flaggedItems": sum(i["flagged"] for i in items),
                        "textComparedItems": sum(i["expectedText"] is not None for i in items),
                        "controlsBreached": breach}}
