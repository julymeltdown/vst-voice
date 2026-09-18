import hashlib
import io
import unittest
import wave

from tools.voice_model_training.segment import segment_source, crop_labels, crop_score
import copy
from tools.voice_model_training.split import split_sources


class SegmentTests(unittest.TestCase):
    def test_float_crop_preserves_sample_bytes_and_encoding(self):
        from tools.voice_model_training.audio_source import inspect_pcm_source
        from tools.voice_model_training.test_audio_source import float_wav
        samples = [0.0, -0.0, 0.25, -0.5, 0.875, -1.0]
        payload = float_wav(samples)
        source = dict(sourceId='parent', songId='song', sessionId='session', lineageId='family',
                      sourceSha256=hashlib.sha256(payload).hexdigest())
        audio, record = segment_source(payload, source=source, sample_rate=48000,
                                       segment_id='child', start_frame=1, end_frame=5)
        inspected = inspect_pcm_source(audio, expected_sha256=record['sourceSha256'], sample_rate=48000)
        self.assertEqual(inspected['sampleEncoding'], 'ieee-float32-le')
        import struct
        self.assertEqual(audio[-16:], struct.pack('<4f', *samples[1:5]))
        self.assertEqual(record['parentAudioSha256'], inspect_pcm_source(
            payload, expected_sha256=source['sourceSha256'], sample_rate=48000)['audioSha256'])

    def test_score_crop_reindexes_melisma_and_silence(self):
        label = dict(frameCount=600, phonemes=[dict(startFrame=0, endFrame=100),
            dict(startFrame=100, endFrame=500), dict(startFrame=500, endFrame=600)])
        score = dict(language="ja", silencePhones=[0, 2], syllables=[dict(lyric="あ", phoneStart=1, phoneEnd=2)],
            notes=[dict(startFrame=0, endFrame=100, midi=None, syllable=None, slur=False),
                   dict(startFrame=100, endFrame=300, midi=60, syllable=0, slur=False),
                   dict(startFrame=300, endFrame=500, midi=62, syllable=0, slur=True),
                   dict(startFrame=500, endFrame=600, midi=None, syllable=None, slur=False)])
        before = copy.deepcopy(score)
        child = crop_score(score, label, start_frame=350, end_frame=550)
        self.assertEqual(child["silencePhones"], [1])
        self.assertEqual(child["syllables"][0]["phoneStart"], 0)
        self.assertFalse(child["notes"][0]["slur"])
        self.assertEqual(child["notes"][0]["endFrame"], 150)
        self.assertEqual(score, before)
        with self.assertRaises(ValueError): crop_score(score, label, start_frame=500, end_frame=600)

    def test_label_crop_rebases_and_invalidates_review(self):
        label = dict(sourceId="parent", frameCount=1000, hopSize=100, phonemes=[
            dict(symbol="a", startFrame=0, endFrame=350, confidence=0.9),
            dict(symbol="i", startFrame=350, endFrame=1000, confidence=0.8)],
            f0Hz=list(range(220, 230)), voiced=[True] * 10, reviewRevision="parent-review")
        original = copy.deepcopy(label)
        def crop(start, end):
            return crop_labels(label, segment_id="child", start_frame=start, end_frame=end,
                               vocabulary={"a", "i"}, minimum_confidence=0.8)
        result = crop(200, 650)
        self.assertEqual(result["frameCount"], 450)
        self.assertEqual(result["f0Hz"], [222, 223, 224, 225, 226])
        self.assertEqual([(p["startFrame"], p["endFrame"]) for p in result["phonemes"]], [(0, 150), (150, 450)])
        self.assertIsNone(result["reviewRevision"])
        self.assertEqual(label, original)
        result["phonemes"][0]["confidence"] = 0
        self.assertEqual(label, original)
        for start, end in ((201, 650), (0, 1001), (False, 100), (200, 200)):
            with self.assertRaises(ValueError): crop(start, end)

    def test_exact_pcm_and_inherited_split_lineage(self):
        for width in (2, 3, 4):
            stream = io.BytesIO()
            pcm = bytes(range(32 * width))
            with wave.open(stream, "wb") as writer:
                writer.setnchannels(1); writer.setsampwidth(width); writer.setframerate(48000)
                writer.writeframes(pcm)
            payload = stream.getvalue()
            source = dict(sourceId="parent", songId="song", sessionId="session", lineageId="family",
                          sourceSha256=hashlib.sha256(payload).hexdigest())
            def crop(start, end, name="child"):
                return segment_source(payload, source=source, sample_rate=48000,
                                      segment_id=name, start_frame=start, end_frame=end)
            audio, record = crop(3, 17)
            self.assertEqual(crop(3, 17), (audio, record))
            with wave.open(io.BytesIO(audio), "rb") as reader:
                self.assertEqual(reader.readframes(100), pcm[3 * width:17 * width])
            self.assertEqual(record["parentSourceSha256"], hashlib.sha256(payload).hexdigest())
            self.assertFalse(record["trainingAdmitted"])
            other = crop(17, 32, "other")[1]
            rows = [{key: r[key] for key in ("sourceId", "songId", "sessionId", "lineageId", "audioSha256")}
                    for r in (record, other)]
            split = split_sources(rows, seed="test", held_out_songs=["song"])
            self.assertEqual(split["counts"]["test"], 2)
            for start, end in ((-1, 2), (2, 2), (0, 33), (True, 3), (0, 2.5)):
                with self.assertRaises(ValueError): crop(start, end)
            with self.assertRaises(ValueError): crop(0, 32, "parent")
            source["sourceSha256"] = "0" * 64
            with self.assertRaises(ValueError): crop(0, 32)


if __name__ == "__main__":
    unittest.main()
