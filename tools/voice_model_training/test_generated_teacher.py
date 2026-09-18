"""The generated-teacher export must satisfy the pipeline it feeds, and must not overclaim."""
import io
import json
import unittest
import wave
import hashlib
import copy

from tools.voice_model_training.generated_teacher import (
    build_export, build_label, build_score, export_from_candidate, label_config_from_exports,
)
from tools.voice_model_training.labels import label_report, score_report


def mono_wav(frames: int, rate: int = 48000) -> bytes:
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as writer:
        writer.setnchannels(1)
        writer.setsampwidth(2)
        writer.setframerate(rate)
        writer.writeframes(b"\x00\x00" * frames)
    return buffer.getvalue()


def fixture_spans(frames: int) -> list[dict]:
    quarter = frames // 4
    return [
        dict(symbol="a", startFrame=0, endFrame=quarter, confidence=1.0),
        dict(symbol="s", startFrame=quarter, endFrame=2 * quarter, confidence=1.0),
        dict(symbol="i", startFrame=2 * quarter, endFrame=3 * quarter, confidence=1.0),
        dict(symbol="a", startFrame=3 * quarter, endFrame=frames, confidence=1.0),
    ]


def fixture_f0(frames: int, hop: int) -> tuple[list[float], list[bool]]:
    count = (frames + hop - 1) // hop
    f0 = [220.0 if index % 8 else 0.0 for index in range(count)]
    return f0, [value > 0 for value in f0]


def captured_pitch(payload, frames, rate=48000):
    return dict(formatId='com.project-seam.training-pitch-features', schemaVersion=1,
        sourceSha256=hashlib.sha256(payload).hexdigest(), sampleRate=rate, frameCount=frames,
        windowFrames=2048, hopSize=256, minimumHz=60, maximumHz=1200, voicingThreshold=0.32,
        algorithm='fft-autocorrelation-v1', coverage='full-hop-zero-padded',
        trainingAdmitted=False, releaseEligible=False,
        pitchFrames=[dict(sourceFrame=i * 256, f0Hz=220.0, confidence=0.9, voiced=True)
                     for i in range((frames + 255) // 256)])


class GeneratedTeacherTest(unittest.TestCase):
    def test_multi_phone_notes_and_continuation_keep_score_ownership(self):
        from tools.voice_model_training.test_audio_source import float_wav
        from tools.voice_model_training.conditioning import build_conditioning
        payload = float_wav([0.25, -0.25] * 1024)
        candidate = dict(formatId='com.project-seam.procedural-candidate', schemaVersion=8,
            approval='unapproved', sampleRate=48000, frameCount=2048,
            audioSha256=hashlib.sha256(payload).hexdigest(), recipeHash='e' * 64,
            renderContentHash='f' * 64, proceduralRevision=14,
            markers=[dict(key=f'{note:016x}:{ordinal}', phone=phone, startFrame=start, endFrame=end)
                     for note, ordinal, phone, start, end in
                     [(1, 0, 's', 0, 256), (1, 1, 'a', 256, 768),
                      (2, 0, 'a', 768, 1280), (3, 0, 'm', 1280, 1536), (3, 1, 'i', 1536, 2048)]])
        pitch = captured_pitch(payload, 2048)
        def run(value=candidate, measured=pitch):
            return export_from_candidate(candidate=value, pitch_features=measured,
                source_id='p', song_id='song', session_id='session', lineage_id='lineage',
                syllable_lyrics=['さ', 'ー', 'み'], note_midi=[60, 62, 64], pcm_payload=payload)
        result = run()
        self.assertEqual(result['score']['syllables'], [dict(lyric='さ', phoneStart=0, phoneEnd=3),
                                                       dict(lyric='み', phoneStart=3, phoneEnd=5)])
        self.assertEqual([(n['startFrame'], n['endFrame'], n['midi'], n['syllable'], n['slur'])
                          for n in result['score']['notes']],
                         [(0, 768, 60, 0, False), (768, 1280, 62, 0, True), (1280, 2048, 64, 1, False)])
        conditioning = build_conditioning(result['label'], result['score'],
                                          vocabulary=['s', 'a', 'm', 'i'], minimum_confidence=0.0)
        self.assertEqual(len(conditioning['frames']), 8)
        self.assertEqual([f['noteIndex'] for f in conditioning['frames']], [0, 0, 0, 1, 1, 2, 2, 2])
        changed = dict(candidate, audioSha256='0' * 64)
        with self.assertRaises(ValueError): run(changed)
        with self.assertRaises(ValueError): run(measured=dict(pitch, sourceSha256='0' * 64))
        changed = copy.deepcopy(candidate)
        changed['markers'][2]['key'] = '0000000000000001:1'
        with self.assertRaises(ValueError): run(changed)
        for key in ('1:0', '0000000000000000:0', '0000000000000002:00', '0000000000000002:16384'):
            changed = copy.deepcopy(candidate)
            changed['markers'][2]['key'] = key
            with self.subTest(key=key), self.assertRaises(ValueError): run(changed)
        changed = copy.deepcopy(candidate)
        del changed['markers'][2]['key']
        with self.assertRaises(ValueError): run(changed)
        changed_pitch = copy.deepcopy(pitch)
        changed_pitch['pitchFrames'][1]['sourceFrame'] += 1
        with self.assertRaises(ValueError): run(measured=changed_pitch)

    def test_explicit_rest_and_continuation_ownership_are_not_inferred(self):
        payload = mono_wav(1536)
        candidate = dict(formatId='com.project-seam.procedural-candidate', approval='unapproved',
            sampleRate=48000, frameCount=1536, audioSha256=hashlib.sha256(payload).hexdigest(),
            recipeHash='a' * 64, renderContentHash='b' * 64, proceduralRevision=14,
            markers=[dict(key=f'{i+1:016x}:0', phone=phone, startFrame=i*512, endFrame=(i+1)*512)
                     for i, phone in enumerate(['a', 'sil', 'i'])])
        def run(lyrics, midi, value=candidate):
            return export_from_candidate(candidate=value, pitch_features=captured_pitch(payload, 1536),
                source_id='p', song_id='s', session_id='x', lineage_id='l',
                syllable_lyrics=lyrics, note_midi=midi, pcm_payload=payload)
        result = run(['あ', '', 'い'], [60, None, 64])
        self.assertEqual(result['score']['silencePhones'], [1])
        self.assertEqual(result['score']['syllables'][1]['phoneStart'], 2)
        self.assertIsNone(result['score']['notes'][1]['syllable'])
        for lyrics, midi in [(['あ', '', 'ー'], [60, None, 64]),
                            (['ー', 'し', 'い'], [60, 62, 64]),
                            (['あ', 'い'], [60, 64])]:
            with self.subTest(lyrics=lyrics), self.assertRaises(ValueError): run(lyrics, midi)
        changed = copy.deepcopy(candidate)
        changed['markers'][2]['key'] = '0000000000000001:1'
        with self.assertRaises(ValueError): run(['あ', '', 'い'], [60, None, 64], changed)

    def test_float_teacher_identity_and_declared_geometry_match_the_inspector(self):
        from tools.voice_model_training.test_audio_source import float_wav
        from tools.voice_model_training.audio_source import inspect_pcm_source
        from tools.voice_model_training.generated_teacher import audio_sha256
        payload = float_wav([0.25, -0.25] * 512)
        inspected = inspect_pcm_source(payload, expected_sha256=hashlib.sha256(payload).hexdigest(), sample_rate=48000)
        self.assertEqual(audio_sha256(payload), inspected['audioSha256'])
        score = build_score(language='ja', syllable_lyrics=['あ'], frame_count=1024,
                            note_spans=[dict(startFrame=0, endFrame=1024, midi=60, syllable=0, slur=False)],
                            silence_indices=[])
        def export(rate=48000, frames=1024):
            return build_export(source_id='p', song_id='song', session_id='s', lineage_id='l',
                sample_rate=rate, hop_size=256, frame_count=frames,
                phone_spans=[dict(symbol='a', startFrame=0, endFrame=frames, confidence=1.0)],
                f0_hz=[220.0] * ((frames + 255) // 256), voiced=[True] * ((frames + 255) // 256),
                score=score, pcm_payload=payload, recipe_sha256='a' * 64,
                engine_id='seam.source-filter.v1', engine_revision=14, score_sha256='b' * 64)
        self.assertEqual(export()['audioSha256'], inspected['audioSha256'])
        with self.assertRaises(ValueError): export(rate=44100)
        with self.assertRaises(ValueError): export(frames=1023)

    def test_export_satisfies_the_admitted_label_and_score_checks(self):
        frames, hop = 4096, 256
        f0, voiced = fixture_f0(frames, hop)
        score = build_score(language="ja",
                            syllable_lyrics=["a", "sa", "i", "a"],
                            note_spans=[
                                dict(startFrame=0, endFrame=1024, midi=62, syllable=0, slur=False),
                                dict(startFrame=1024, endFrame=2048, midi=64, syllable=1, slur=False),
                                dict(startFrame=2048, endFrame=3072, midi=65, syllable=2, slur=False),
                                dict(startFrame=3072, endFrame=4096, midi=64, syllable=3, slur=False),
                            ],
                            frame_count=frames, silence_indices=[])
        export = build_export(source_id="phrase-0001", song_id="song-01", session_id="session-01",
                              lineage_id="lineage-01", sample_rate=48000, hop_size=hop, frame_count=frames,
                              phone_spans=fixture_spans(frames), f0_hz=f0, voiced=voiced, score=score,
                              pcm_payload=mono_wav(frames), recipe_sha256="a" * 64,
                              engine_id="seam.source-filter.v1", engine_revision=14,
                              score_sha256="b" * 64)
        # The label must pass the real consistency check, not a copy of it.
        report = label_report(export["label"], vocabulary={"a", "i", "s"}, minimum_confidence=0.0)
        self.assertEqual(report["correctionQueue"], [{"code": "review-revision-missing"}])
        score_report(export["score"], frame_count=frames,
                     phoneme_count=len(export["label"]["phonemes"]), explicit_silence=True)

    def test_export_refuses_to_claim_rights_labels_or_training(self):
        frames, hop = 2048, 256
        f0, voiced = fixture_f0(frames, hop)
        score = build_score(language="ja", syllable_lyrics=["a"],
                            note_spans=[dict(startFrame=0, endFrame=frames, midi=60, syllable=0, slur=False)],
                            frame_count=frames, silence_indices=[])
        export = build_export(source_id="p", song_id="s", session_id="x", lineage_id="l",
                              sample_rate=48000, hop_size=hop, frame_count=frames,
                              phone_spans=[dict(symbol="a", startFrame=0, endFrame=frames, confidence=1.0)],
                              f0_hz=f0, voiced=voiced, score=score, pcm_payload=mono_wav(frames),
                              recipe_sha256="c" * 64, engine_id="seam.source-filter.v1",
                              engine_revision=14, score_sha256="d" * 64)
        for flag in ("sourceRightsAdmitted", "labelsAdmitted", "trainingAdmitted", "releaseEligible"):
            self.assertIs(export[flag], False)
        # The label origin is recorded, because the spans are renderer intent rather than proof of
        # acoustically correct phones, and a downstream reader must not read them as the latter.
        self.assertEqual(export["labelOrigin"], "renderer-intent-not-acoustic-truth")

    def test_a_gap_in_the_phone_alignment_is_refused_rather_than_inferred(self):
        with self.assertRaises(ValueError):
            build_label(source_id="p", hop_size=256, frame_count=1024,
                        phone_spans=[dict(symbol="a", startFrame=0, endFrame=512, confidence=1.0),
                                     dict(symbol="i", startFrame=600, endFrame=1024, confidence=1.0)],
                        f0_hz=[220.0] * 4, voiced=[True] * 4)

    def test_voicing_must_agree_with_f0(self):
        with self.assertRaises(ValueError):
            build_label(source_id="p", hop_size=256, frame_count=512,
                        phone_spans=[dict(symbol="a", startFrame=0, endFrame=512, confidence=1.0)],
                        f0_hz=[0.0, 220.0], voiced=[True, False])

    def test_score_notes_must_partition_the_phrase(self):
        with self.assertRaises(ValueError):
            build_score(language="ja", syllable_lyrics=["a"],
                        note_spans=[dict(startFrame=100, endFrame=512, midi=60, syllable=0, slur=False)],
                        frame_count=512, silence_indices=[])

    def test_declared_silence_is_carried_into_the_score_and_checked_there(self):
        frames = 1024
        score = build_score(language="ja", syllable_lyrics=["a", "a"],
                            note_spans=[
                                dict(startFrame=0, endFrame=512, midi=62, syllable=0, slur=False),
                                dict(startFrame=512, endFrame=1024, midi=64, syllable=1, slur=False),
                            ], frame_count=frames, silence_indices=[0])
        # The declared silence is a real phone position ahead of the sung syllables, so the score must
        # carry it rather than dropping it and silently renumbering the syllables.
        self.assertEqual(score["silencePhones"], [0])
        self.assertEqual(score["syllables"][0]["phoneStart"], 0)
        # And the admitted score check is what enforces the bound: an index past the phone count is
        # refused there, not by a second copy of the rule here.
        with self.assertRaises(ValueError):
            score_report(dict(language="ja",
                              syllables=[dict(lyric="a", phoneStart=0, phoneEnd=1)],
                              notes=[dict(startFrame=0, endFrame=1024, midi=62, syllable=0, slur=False)],
                              silencePhones=[9]),
                         frame_count=1024, phoneme_count=1, explicit_silence=True)

    def test_a_generated_export_becomes_a_label_config_the_pipeline_accepts(self):
        # The point of the adapter is that the neural path can be exercised from the procedural
        # renderer alone, so the export must round-trip through the pipeline's own label admission.
        import tempfile
        from pathlib import Path

        from tools.voice_model_training.__main__ import inspect_label_config

        frames, hop, rate = 4096, 256, 48000
        f0, voiced = fixture_f0(frames, hop)
        score = build_score(language="ja", syllable_lyrics=["a", "sa", "i", "a"],
                            note_spans=[
                                dict(startFrame=0, endFrame=1024, midi=62, syllable=0, slur=False),
                                dict(startFrame=1024, endFrame=2048, midi=64, syllable=1, slur=False),
                                dict(startFrame=2048, endFrame=3072, midi=65, syllable=2, slur=False),
                                dict(startFrame=3072, endFrame=4096, midi=64, syllable=3, slur=False),
                            ],
                            frame_count=frames, silence_indices=[])
        payload = mono_wav(frames, rate)
        export = build_export(source_id="phrase-0001", song_id="song-01", session_id="session-01",
                              lineage_id="lineage-01", sample_rate=rate, hop_size=hop, frame_count=frames,
                              phone_spans=fixture_spans(frames), f0_hz=f0, voiced=voiced, score=score,
                              pcm_payload=payload, recipe_sha256="a" * 64,
                              engine_id="seam.source-filter.v1", engine_revision=14,
                              score_sha256="b" * 64)
        config = label_config_from_exports(exports=[export], sample_rate=rate,
                                           relative_paths={"phrase-0001": "phrase-0001.wav"},
                                           vocabulary=["a", "i", "s"], minimum_confidence=0.0)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "phrase-0001.wav").write_bytes(payload)
            captured = json.dumps(config)
            config_path = root / "labels.json"
            config_path.write_text(captured)
            report = inspect_label_config(config_path, hashlib.sha256(captured.encode()).hexdigest(), root)
        # The pipeline reads exactly one label per source and reports consistency; it does not admit
        # rights, which is why the adapter never claims them.
        self.assertEqual(report["schemaVersion"], 3)
        self.assertEqual(len(report["sources"]), 1)
        self.assertFalse(report["trainingAdmitted"])
        self.assertFalse(export["sourceRightsAdmitted"])
        self.assertEqual(export["labelOrigin"], "renderer-intent-not-acoustic-truth")

    def test_the_cli_exposes_the_translation_and_still_creates_no_approval(self):
        import subprocess
        import sys

        result = subprocess.run([sys.executable, "-m", "tools.voice_model_training", "--help"],
                                capture_output=True, text=True, check=True)
        self.assertIn("generated-teacher-labels", result.stdout)
        # Producing a label configuration is not a permission and not a review, so the command help
        # says so rather than letting a caller read a written file as an approval.
        self.assertIn("creates no permission", result.stdout)

    def test_a_real_captured_candidate_becomes_a_teacher_export(self):
        # The whole point of the adapter is to consume what the renderer actually wrote, so this
        # builds the candidate metadata and measured pitch in the shapes the export and the native
        # extractor produce, rather than hand-feeding a prepared label.
        frames, rate = 1024, 48000
        payload = mono_wav(frames, rate)
        candidate = dict(formatId="com.project-seam.procedural-candidate", schemaVersion=4,
                         approval="unapproved", sampleRate=rate, frameCount=frames,
                         audioSha256=hashlib.sha256(payload).hexdigest(),
                         recipeHash="e" * 64, renderContentHash="f" * 64, proceduralRevision=14,
                         markers=[dict(phone="a", startFrame=0, endFrame=512),
                                  dict(phone="a", startFrame=512, endFrame=1024)])
        pitch = captured_pitch(payload, frames)
        export = export_from_candidate(candidate=candidate, pitch_features=pitch,
                                       source_id="p", song_id="s", session_id="x", lineage_id="l",
                                       syllable_lyrics=["a", "a"], note_midi=[62, 64],
                                       pcm_payload=payload)
        self.assertEqual([p["symbol"] for p in export["label"]["phonemes"]], ["a", "a"])
        self.assertEqual([n["midi"] for n in export["score"]["notes"]], [62, 64])
        # The measured pitch is carried, not the written score.
        self.assertEqual(export["label"]["f0Hz"], [220.0] * 4)
        label_report(export["label"], vocabulary={"a"}, minimum_confidence=0.0)
        score_report(export["score"], frame_count=frames,
                     phoneme_count=2, explicit_silence=True)

    def test_a_captured_candidate_must_still_be_unapproved(self):
        candidate = dict(formatId="com.project-seam.procedural-candidate", approval="approved",
                         sampleRate=48000, frameCount=512, recipeHash="a" * 64,
                         renderContentHash="b" * 64, proceduralRevision=14,
                         markers=[dict(phone="a", startFrame=0, endFrame=512)])
        with self.assertRaises(ValueError):
            export_from_candidate(candidate=candidate, pitch_features=dict(pitchFrames=[]),
                                  source_id="p", song_id="s", session_id="x", lineage_id="l",
                                  syllable_lyrics=["a"], note_midi=[62], pcm_payload=mono_wav(512))

    def test_a_candidate_with_a_marker_gap_is_refused(self):
        candidate = dict(formatId="com.project-seam.procedural-candidate", approval="unapproved",
                         sampleRate=48000, frameCount=1024, recipeHash="a" * 64,
                         audioSha256=hashlib.sha256(mono_wav(1024)).hexdigest(),
                         renderContentHash="b" * 64, proceduralRevision=14,
                         markers=[dict(phone="a", startFrame=0, endFrame=512),
                                  dict(phone="i", startFrame=600, endFrame=1024)])
        pitch = captured_pitch(mono_wav(1024), 1024)
        with self.assertRaises(ValueError):
            export_from_candidate(candidate=candidate, pitch_features=pitch,
                                  source_id="p", song_id="s", session_id="x", lineage_id="l",
                                  syllable_lyrics=["a", "i"], note_midi=[62, 64],
                                  pcm_payload=mono_wav(1024))


if __name__ == "__main__":
    unittest.main()
