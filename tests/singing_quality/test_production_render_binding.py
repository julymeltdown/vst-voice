"""Synthetic byte/receipt tests only: no native production-success claim."""
from copy import deepcopy
from dataclasses import replace
import json
import struct
import unittest

from tools.singing_quality.packet_io import digest_bytes
from tools.singing_quality.production_render_binding import (
    Expected, FORMAT, SURFACE, verify_production_binding,
)


def wav(rate=48000, frames=128, value=0.1, channels=1):
    samples = struct.pack('<f', value) * frames * channels
    fmt = struct.pack('<HHIIHH', 3, channels, rate, rate * 4 * channels, 4 * channels, 32)
    body = b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(samples)) + samples
    return b'RIFF' + struct.pack('<I', len(body)) + body


def encode(value):
    return json.dumps(value, allow_nan=False).encode()


class ProductionRenderBindingTests(unittest.TestCase):
    def setUp(self):
        self.audio = wav()
        self.receipt = dict(formatId=FORMAT, schemaVersion=1, audioSurface=SURFACE,
                            timeline='final-wav-frame-zero', projectSha256='1'*64,
                            rendererSha256='2'*64, bankSha256='3'*64,
                            audioSha256=digest_bytes(self.audio), sampleRate=48000, frameCount=128,
                            placements=[dict(placementId='p1', noteId='0000000000000001', unitId='u1',
                                             startFrame=0, endFrame=70, vowelOnsetFrame=5),
                                        dict(placementId='p2', noteId='0000000000000002', unitId='u2',
                                             startFrame=60, endFrame=128, vowelOnsetFrame=65)])
        self.expected = Expected(digest_bytes(encode(self.receipt)), '1'*64, '2'*64, '3'*64, 48000, 128,
                                 (('0000000000000001', 0), ('0000000000000002', 64)))

    def check(self, receipt=None, audio=None, rebind=False, expected=None):
        raw = encode(self.receipt if receipt is None else receipt)
        anchor = expected or self.expected
        if rebind:
            anchor = replace(anchor, receipt_sha256=digest_bytes(raw))
        return verify_production_binding(self.audio if audio is None else audio, raw, anchor)

    def test_synthetic_consistency_is_not_production_or_acoustic_acceptance(self):
        result = self.check()
        self.assertEqual(result.placement_lead_frames, (('0000000000000001', 0), ('0000000000000002', 4)))
        self.assertEqual(result.audio_sha256, digest_bytes(self.audio))
        self.assertEqual(result.status, 'CONSISTENT_BINDING_ONLY')
        self.assertFalse(result.acoustic_onsets_verified)
        self.assertFalse(result.production_execution_verified)
        self.assertFalse(result.release_eligible)
        self.assertEqual(result.listening_status, 'NOT_REVIEWED')

    def test_zero_lead_is_not_transformed_into_a_q1_pass(self):
        self.receipt['placements'][1]['startFrame'] = 64
        self.assertEqual(self.check(rebind=True).placement_lead_frames[1][1], 0)

    def test_replacement_wav_same_shape_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'WAV identity mismatch'):
            self.check(audio=wav(value=0.2))

    def test_replacement_wav_and_self_rehashed_metadata_cannot_replace_external_anchor(self):
        replacement = wav(value=0.2)
        self.receipt['audioSha256'] = digest_bytes(replacement)
        with self.assertRaisesRegex(ValueError, 'receipt identity mismatch'):
            self.check(audio=replacement)

    def test_placement_edit_is_bound_even_if_audio_is_unchanged(self):
        self.receipt['placements'][1]['startFrame'] = 59
        with self.assertRaisesRegex(ValueError, 'receipt identity mismatch'):
            self.check()

    def test_actual_dry_candidate_shape_is_not_a_production_receipt(self):
        dry = dict(formatId='com.project-seam.procedural-candidate', schemaVersion=11,
                   audioSha256=digest_bytes(self.audio), sampleRate=48000, frameCount=128, markers=[])
        with self.assertRaisesRegex(ValueError, 'unexpected receipt fields'):
            self.check(dry, rebind=True)

    def test_dry_surface_and_unmapped_timeline_are_refused(self):
        for field, value in [('audioSurface', 'dry-procedural-candidate'), ('timeline', 'score-origin-relative')]:
            with self.subTest(field=field):
                receipt = deepcopy(self.receipt); receipt[field] = value
                with self.assertRaisesRegex(ValueError, 'surface/timeline'):
                    self.check(receipt, rebind=True)

    def test_project_renderer_bank_must_match_invocation(self):
        for field in ('projectSha256', 'rendererSha256', 'bankSha256'):
            with self.subTest(field=field):
                receipt = deepcopy(self.receipt); receipt[field] = '4'*64
                with self.assertRaisesRegex(ValueError, field):
                    self.check(receipt, rebind=True)

    def test_rate_and_frames_must_match_bytes_and_invocation(self):
        for field, value in [('sampleRate', 44100), ('frameCount', 127), ('sampleRate', True), ('frameCount', 128.0)]:
            with self.subTest(field=field, value=value):
                receipt = deepcopy(self.receipt); receipt[field] = value
                with self.assertRaises(ValueError):self.check(receipt, rebind=True)
        for expected in (replace(self.expected, sample_rate=44100), replace(self.expected, frame_count=129)):
            with self.assertRaises(ValueError):self.check(expected=expected)

    def test_truncated_extra_and_ambiguous_wav_are_refused_after_hash_rebinding(self):
        duplicate = self.audio + self.audio[12:36]
        duplicate = duplicate[:4] + struct.pack('<I', len(duplicate)-8) + duplicate[8:]
        for audio in (self.audio[:-1], self.audio+b'x', duplicate):
            with self.subTest(size=len(audio)):
                receipt = deepcopy(self.receipt); receipt['audioSha256'] = digest_bytes(audio)
                with self.assertRaises(ValueError):self.check(receipt, audio, rebind=True)

    def test_nonfinite_samples_are_refused_even_with_matching_hashes(self):
        for value in (float('nan'), float('inf')):
            audio = wav(value=value); receipt = deepcopy(self.receipt); receipt['audioSha256'] = digest_bytes(audio)
            with self.assertRaisesRegex(ValueError, 'non-finite'):
                self.check(receipt, audio, rebind=True)

    def test_stereo_frame_count_is_not_sample_count(self):
        audio = wav(channels=2); receipt = deepcopy(self.receipt); receipt['audioSha256'] = digest_bytes(audio)
        self.assertEqual(self.check(receipt, audio, rebind=True).audio_sha256, digest_bytes(audio))

    def test_missing_duplicate_and_foreign_placements_are_refused(self):
        variants = [[], self.receipt['placements'][:1], self.receipt['placements']*2]
        foreign = deepcopy(self.receipt['placements']); foreign[1]['noteId'] = '0000000000000003'; variants.append(foreign)
        for placements in variants:
            with self.subTest(placements=placements):
                receipt = deepcopy(self.receipt); receipt['placements'] = placements
                with self.assertRaises(ValueError):self.check(receipt, rebind=True)

    def test_invalid_placement_ranges_and_types_are_refused(self):
        for field, value in [('startFrame', -1), ('startFrame', 128), ('endFrame', 129),
                             ('endFrame', 60), ('vowelOnsetFrame', 59), ('vowelOnsetFrame', 128),
                             ('startFrame', True), ('startFrame', 60.0), ('unitId', ''), ('placementId', [])]:
            with self.subTest(field=field, value=value):
                receipt = deepcopy(self.receipt); receipt['placements'][1][field] = value
                with self.assertRaises(ValueError):self.check(receipt, rebind=True)

    def test_multiple_units_and_crossfade_overlap_preserve_note_inventory(self):
        receipt = deepcopy(self.receipt)
        receipt['placements'].append(dict(receipt['placements'][1], placementId='p3', startFrame=62))
        self.assertEqual(self.check(receipt, rebind=True).placement_lead_frames[1][1], 4)

    def test_duplicate_json_keys_and_nonfinite_json_are_refused(self):
        for raw in (b'{"schemaVersion":1,"schemaVersion":1}', b'{"x":NaN}', b'[]', b'\xff'):
            expected = replace(self.expected, receipt_sha256=digest_bytes(raw))
            with self.assertRaises(ValueError):verify_production_binding(self.audio, raw, expected)

    def test_bad_expected_identity_and_note_inventory_fail_closed(self):
        for expected in (replace(self.expected, receipt_sha256=''), replace(self.expected, sample_rate=True),
                         replace(self.expected, note_starts=()),
                         replace(self.expected, note_starts=self.expected.note_starts*2),
                         replace(self.expected, note_starts=(('1', 0),)),
                         replace(self.expected, note_starts=(('0000000000000001', -1),))):
            with self.assertRaises(ValueError):self.check(expected=expected)

    def test_schema_and_acceptance_claims_are_not_silently_ignored(self):
        for field, value in [('schemaVersion', True), ('schemaVersion', 2), ('releaseEligible', True),
                             ('markers', [])]:
            receipt = deepcopy(self.receipt); receipt[field] = value
            with self.assertRaises(ValueError):self.check(receipt, rebind=True)


if __name__ == '__main__':
    unittest.main()
