"""Local proposed adapter contract for exact production WAV/placement binding.

No native producer currently emits this receipt. The caller must pin Expected
from the render invocation and retain receipt_sha256 outside the packet being
verified. A self-rehashed packet is not provenance. Placement positions describe
producer-reported timing on the final WAV timeline, NOT measured audible onsets.
No Q1 criterion, acoustic threshold, or acceptance gate is changed here.
"""
from dataclasses import dataclass
import json
import re
import struct

from .packet_io import digest_bytes, MAXIMUM_ASSET_BYTES
from .verify_listener_packet_002 import parse_wav

FORMAT = 'com.project-seam.production-render-binding'
SURFACE = 'production-sample-bank'
MAX_RECEIPT_BYTES = 4 * 1024 * 1024
MAX_PLACEMENTS = 16384


@dataclass(frozen=True)
class Expected:
    receipt_sha256: str
    project_sha256: str
    renderer_sha256: str
    bank_sha256: str
    sample_rate: int
    frame_count: int
    # Canonical note ID and written start frame, resolved by the score adapter.
    note_starts: tuple[tuple[str, int], ...]


@dataclass(frozen=True)
class Binding:
    audio_sha256: str
    receipt_sha256: str
    # Positive means a reported placement begins before the written note.
    placement_lead_frames: tuple[tuple[str, int], ...]
    status: str = 'CONSISTENT_BINDING_ONLY'
    acoustic_onsets_verified: bool = False
    production_execution_verified: bool = False
    listening_status: str = 'NOT_REVIEWED'
    release_eligible: bool = False


def _require(condition, message):
    if not condition:
        raise ValueError(message)


def _integer(value, low, high):
    return type(value) is int and low <= value <= high


def _sha(value):
    return isinstance(value, str) and re.fullmatch(r'[0-9a-f]{64}', value) is not None


def _note(value):
    return (isinstance(value, str) and re.fullmatch(r'[0-9a-f]{16}', value)
            and value != '0000000000000000')


def _object(pairs):
    result = {}
    for key, value in pairs:
        _require(key not in result, 'duplicate JSON key')
        result[key] = value
    return result


def _invalid_constant(value):
    raise ValueError('nonfinite JSON constant')


def _wav_shape(raw):
    """Reject ambiguous RIFF before reusing the existing sample decoder."""
    _require(type(raw) is bytes and 12 <= len(raw) <= MAXIMUM_ASSET_BYTES, 'WAV byte bound')
    _require(raw[:4] == b'RIFF' and raw[8:12] == b'WAVE', 'not RIFF/WAVE')
    _require(struct.unpack_from('<I', raw, 4)[0] + 8 == len(raw), 'RIFF size mismatch')
    pos, chunks = 12, {}
    while pos < len(raw):
        _require(pos + 8 <= len(raw), 'truncated chunk header')
        name, size = struct.unpack_from('<4sI', raw, pos)
        start, end = pos + 8, pos + 8 + size
        _require(end + size % 2 <= len(raw), 'truncated chunk or padding')
        if name in (b'fmt ', b'data'):
            _require(name not in chunks, 'duplicate WAV format/data chunk')
            chunks[name] = raw[start:end]
        pos = end + size % 2
    _require(b'fmt ' in chunks and b'data' in chunks, 'missing WAV format/data')
    _require(len(chunks[b'fmt ']) >= 16, 'short WAV format')
    _, _, rate, byte_rate, alignment, _ = struct.unpack_from('<HHIIHH', chunks[b'fmt '])
    _require(byte_rate == rate * alignment, 'WAV byte rate mismatch')
    try:
        mono, decoded_rate = parse_wav(raw)
    except (ValueError, struct.error, IndexError) as error:
        raise ValueError('invalid WAV samples: ' + str(error)) from error
    return len(mono), decoded_rate


def verify_production_binding(wav_bytes: bytes, receipt_bytes: bytes, expected: Expected) -> Binding:
    """Validate already-read bytes; never execute a renderer or trust packet paths."""
    _require(isinstance(expected, Expected), 'expected invocation required')
    for value in (expected.receipt_sha256, expected.project_sha256,
                  expected.renderer_sha256, expected.bank_sha256):
        _require(_sha(value), 'invalid expected SHA-256')
    _require(_integer(expected.sample_rate, 8000, 384000), 'invalid expected sample rate')
    _require(_integer(expected.frame_count, 1, MAXIMUM_ASSET_BYTES), 'invalid expected frame count')
    _require(type(expected.note_starts) is tuple and 0 < len(expected.note_starts) <= MAX_PLACEMENTS,
             'expected notes required')
    starts = {}
    for entry in expected.note_starts:
        _require(type(entry) is tuple and len(entry) == 2, 'invalid expected note entry')
        note, frame = entry
        _require(_note(note) and note not in starts, 'duplicate/invalid expected note')
        _require(_integer(frame, 0, expected.frame_count - 1), 'invalid written note frame')
        starts[note] = frame
    _require(type(receipt_bytes) is bytes and 0 < len(receipt_bytes) <= MAX_RECEIPT_BYTES,
             'receipt byte bound')
    receipt_hash = digest_bytes(receipt_bytes)
    _require(receipt_hash == expected.receipt_sha256, 'receipt identity mismatch')
    try:
        receipt = json.loads(receipt_bytes, object_pairs_hook=_object, parse_constant=_invalid_constant)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise ValueError('invalid receipt JSON: ' + str(error)) from error
    _require(type(receipt) is dict, 'receipt must be an object')
    fields = {'formatId', 'schemaVersion', 'audioSurface', 'timeline', 'projectSha256',
              'rendererSha256', 'bankSha256', 'audioSha256', 'sampleRate', 'frameCount', 'placements'}
    _require(set(receipt) == fields, 'unexpected receipt fields (dry metadata is not a receipt)')
    _require(receipt['formatId'] == FORMAT and type(receipt['schemaVersion']) is int
             and receipt['schemaVersion'] == 1, 'unsupported receipt format')
    _require(receipt['audioSurface'] == SURFACE and receipt['timeline'] == 'final-wav-frame-zero',
             'production surface/timeline required')
    for field, value in (('projectSha256', expected.project_sha256),
                         ('rendererSha256', expected.renderer_sha256), ('bankSha256', expected.bank_sha256)):
        _require(receipt[field] == value, field + ' mismatch')
    _require(type(wav_bytes) is bytes, 'WAV bytes required')
    _require(len(wav_bytes) <= MAXIMUM_ASSET_BYTES, 'WAV byte bound')
    audio_hash = digest_bytes(wav_bytes)
    _require(_sha(receipt['audioSha256']) and receipt['audioSha256'] == audio_hash, 'WAV identity mismatch')
    frames, rate = _wav_shape(wav_bytes)
    _require(type(receipt['frameCount']) is int and frames == receipt['frameCount'] == expected.frame_count,
             'WAV frame count mismatch')
    _require(type(receipt['sampleRate']) is int and rate == receipt['sampleRate'] == expected.sample_rate,
             'WAV sample rate mismatch')
    placements = receipt['placements']
    _require(type(placements) is list and 0 < len(placements) <= MAX_PLACEMENTS, 'placements required/bounded')
    identities, first = set(), {}
    for placement in placements:
        _require(type(placement) is dict and set(placement) ==
                 {'placementId', 'noteId', 'unitId', 'startFrame', 'endFrame', 'vowelOnsetFrame'},
                 'invalid placement fields')
        pid, note, unit = (placement[k] for k in ('placementId', 'noteId', 'unitId'))
        _require(isinstance(pid, str) and re.fullmatch(r'[A-Za-z0-9_.:-]{1,128}', pid)
                 and pid not in identities, 'duplicate/invalid placement identity')
        _require(isinstance(unit, str) and re.fullmatch(r'[A-Za-z0-9_.:-]{1,128}', unit), 'invalid unit identity')
        _require(_note(note) and note in starts, 'unknown placement note')
        start, end, onset = (placement[k] for k in ('startFrame', 'endFrame', 'vowelOnsetFrame'))
        _require(_integer(start, 0, frames - 1) and _integer(end, 1, frames) and start < end,
                 'placement outside final WAV')
        _require(_integer(onset, start, end - 1), 'vowel onset outside placement')
        identities.add(pid)
        first[note] = min(first.get(note, start), start)
    _require(set(first) == set(starts), 'missing placement note')
    return Binding(audio_hash, receipt_hash, tuple((note, frame - first[note]) for note, frame in starts.items()))
