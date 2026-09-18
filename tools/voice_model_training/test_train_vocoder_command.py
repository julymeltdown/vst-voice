"""CLI intake/resume identity tests; no singer-quality claim."""
import hashlib
from pathlib import Path
import tempfile
import unittest

from tools.voice_model_training.__main__ import encode_report, publish_new
from tools.voice_model_training.train_vocoder import model_settings, load_pcm_sources, resume_identity


def settings():
    return dict(formatId='com.project-seam.vocoder-training-config', schemaVersion=1,
        seed=928, learningRate=0.0001, learningRateDecay=0.999, maximumUpdates=10,
        maximumSeconds=600, cpuThreads=1, evaluationSeed=932,
        heldOutSources=['validation-song'], labelOrigin='renderer-intent-not-acoustic-truth')


class VocoderCommandTests(unittest.TestCase):
    def test_closed_bounded_export_compatible_settings(self):
        config = model_settings(settings())
        self.assertEqual((config['sampling_rate'], config['num_mels'], config['hop_size']), (48000, 80, 256))
        self.assertTrue(config['mini_nsf'])
        self.assertEqual(config['noise_sigma'], 0.)
        for update in (dict(schemaVersion=True), dict(seed=-1), dict(cpuThreads=True),
                       dict(cpuThreads=33), dict(learningRate=float('nan')), dict(learningRateDecay=0),
                       dict(maximumUpdates=0), dict(maximumSeconds=float('inf')),
                       dict(heldOutSources=[]), dict(heldOutSources=['s', 's']),
                       dict(heldOutSources=[{}]), dict(labelOrigin=''), dict(pretrained='unchecked')):
            with self.subTest(update=update), self.assertRaises(ValueError): model_settings(settings() | update)

    def test_pcm_paths_come_from_captured_labels_and_reject_escape(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            labels = dict(sources=[dict(sourceId='s', path='nested/source.wav')])
            self.assertEqual(load_pcm_sources(root, labels), {'s': root.resolve() / 'nested/source.wav'})
            for value in (dict(sources=[dict(sourceId='s', path='../outside.wav')]),
                          dict(sources=[dict(sourceId='s', path='/tmp/source.wav')]),
                          dict(sources=[dict(sourceId='s', path='a\\b.wav')]),
                          dict(sources=labels['sources'] * 2), dict(sources=[])):
                with self.assertRaises(ValueError): load_pcm_sources(root, value)
            (root / 'nested').symlink_to(root, target_is_directory=True)
            with self.assertRaises(ValueError): load_pcm_sources(root, labels)

    def test_resume_rejects_changed_inputs_and_incomplete_lineage(self):
        current = dict(configuration={'a': 1}, trainingRevision='revision', seed=10)
        receipt = dict(formatId='com.project-seam.gan-checkpoint', schemaVersion=1,
            metadata=dict(run=current | dict(completedEpochs=2, parentReceiptSha256=None),
                          datasetSha256='a'*64, profileSha256='b'*64, objectiveId='objective'),
            epoch=dict(epochComplete=True, coverageVerified=True, objectiveId='objective', datasetSha256='a'*64))
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'checkpoint.json'
            def run(value=receipt, metadata=current):
                payload = encode_report(value)
                path.write_bytes(payload)
                return resume_identity(path.parent, hashlib.sha256(payload).hexdigest(),
                    metadata=metadata, profile='b'*64, dataset='a'*64, objective_id='objective')
            self.assertEqual(run()[1], 2)
            for non_object in ([], None, 'receipt'):
                with self.subTest(value=non_object), self.assertRaises(ValueError): run(non_object)
            with self.assertRaises(ValueError): run(metadata=current | dict(seed=11))
            for update in (dict(datasetSha256='c'*64), dict(profileSha256='c'*64),
                           dict(run=current | dict(completedEpochs=0))):
                with self.assertRaises(ValueError): run(receipt | dict(metadata=receipt['metadata'] | update))
            with self.assertRaises(ValueError):
                run(receipt | dict(epoch=receipt['epoch'] | dict(epochComplete=False)))


if __name__ == '__main__':
    unittest.main()
