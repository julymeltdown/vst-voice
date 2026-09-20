import json
import unittest
from pathlib import Path
import tempfile

from tools.voice_model_training.derive_conditioning_supervision import derive
from tools.voice_model_training.acoustics import log_mel_targets


def _wav_bytes(samples):
    import io
    import wave
    import numpy as np
    buffer = io.BytesIO()
    with wave.open(buffer, 'wb') as writer:
        writer.setnchannels(1)
        writer.setsampwidth(2)
        writer.setframerate(48000)
        writer.writeframes((np.asarray(samples) * 32767).astype('<i2').tobytes())
    return buffer.getvalue()


class DeriveConditioningTests(unittest.TestCase):
    def fixture(self, root):
        import hashlib
        import numpy as np
        samples = (np.random.default_rng(3).normal(0, .1, 258000)).astype('float64')
        payload = _wav_bytes(samples)
        (root / 'source.wav').write_bytes(payload)
        digest = hashlib.sha256(payload).hexdigest()
        frames = 1008
        phones = [dict(symbol='s', startFrame=0, endFrame=129000, confidence=1.0),
                  dict(symbol='a', startFrame=129000, endFrame=258000, confidence=1.0)]
        label = dict(sourceId='s1', frameCount=258000, hopSize=256, phonemes=phones,
                     f0Hz=[0.0] * frames, voiced=[False] * frames, reviewRevision='r1')
        score = dict(language='ja', silencePhones=[], syllables=[dict(lyric='a', phoneStart=0, phoneEnd=1)],
                     notes=[dict(startFrame=0, endFrame=258000, midi=60, syllable=0, slur=False)])
        config = dict(formatId='com.project-seam.voice-training-label-config', schemaVersion=3,
            sampleRate=48000, minimumConfidence=0.0, vocabulary=['a', 's'],
            sources=[dict(sourceId='s1', path='source.wav', sourceSha256=digest,
                          songId='song', sessionId='sess', lineageId='lin')],
            labels=[dict(sourceSha256=digest, audioSha256=digest, label=label, score=score)])
        path = root / 'labels.json'
        path.write_text(json.dumps(config))
        return path, hashlib.sha256(path.read_bytes()).hexdigest()

    def test_derives_schema_four_with_closed_top_level_fields(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config, digest = self.fixture(root)
            output = root / 'derived.json'
            derive(config, digest, source_root=root, output=output)
            value = json.loads(output.read_bytes())
        self.assertEqual(value['schemaVersion'], 4)
        self.assertEqual(set(value), {'formatId', 'schemaVersion', 'sampleRate', 'sources',
                                      'labels', 'vocabulary', 'minimumConfidence'})
        conditioning = value['labels'][0]['conditioning']
        self.assertEqual(set(conditioning), {'revision', 'breathiness'})
        self.assertEqual(conditioning['revision'], 2)
        self.assertEqual(len(conditioning['breathiness']), len(value['labels'][0]['label']['f0Hz']))
        self.assertTrue(all(0.0 <= v <= 1.0 for v in conditioning['breathiness']))

    def test_refuses_to_overwrite_existing_conditioning(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config, digest = self.fixture(root)
            value = json.loads(config.read_bytes())
            value['labels'][0]['conditioning'] = dict(revision=2, breathiness=[0.0] * 1008)
            config.write_text(json.dumps(value))
            import hashlib
            new_digest = hashlib.sha256(config.read_bytes()).hexdigest()
            with self.assertRaises(ValueError):
                derive(config, new_digest, source_root=root, output=root / 'out.json')

    def test_rejects_a_source_whose_bytes_changed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config, digest = self.fixture(root)
            (root / 'source.wav').write_bytes(_wav_bytes(__import__('numpy').zeros(258000)))
            with self.assertRaises(ValueError):
                derive(config, digest, source_root=root, output=root / 'out.json')


if __name__ == '__main__':
    unittest.main()
