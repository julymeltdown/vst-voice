"""CLI intake/resume identity tests; no singer-quality claim."""
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

from tools.voice_model_training.__main__ import encode_report, publish_new
from tools.voice_model_training.train_vocoder import model_settings, load_pcm_sources, resume_identity, partial_resume_identity, main
from tools.voice_model_training.gan_checkpoint_storage import require_disk_headroom, DISK_RESERVE_BYTES


def settings():
    return dict(formatId='com.project-seam.vocoder-training-config', schemaVersion=1,
        seed=928, learningRate=0.0001, learningRateDecay=0.999, maximumUpdates=10,
        maximumSeconds=600, cpuThreads=1, evaluationSeed=932,
        heldOutSources=['validation-song'], labelOrigin='renderer-intent-not-acoustic-truth')


class VocoderCommandTests(unittest.TestCase):
    def test_partial_lineage_continues_current_epoch_not_next(self):
        current = dict(configuration={"a": 1}, trainingRevision="revision")
        receipt = dict(formatId="com.project-seam.gan-partial-checkpoint",
            metadata=dict(run=current | dict(completedEpochs=3, parentReceiptSha256="c"*64),
                          profileSha256="b"*64, datasetSha256="a"*64, objectiveId="objective"),
            epoch=dict(epochComplete=False, coverageVerified=False, datasetSha256="a"*64, profileSha256="b"*64))
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)/"checkpoint.json"
            def inspect(value):
                payload = encode_report(value)
                path.write_bytes(payload)
                return partial_resume_identity(path.parent, hashlib.sha256(payload).hexdigest(), metadata=current,
                    profile="b"*64, dataset="a"*64, objective_id="objective")
            self.assertEqual(inspect(receipt), (2, "c"*64))
            first = receipt | dict(metadata=receipt["metadata"] | dict(run=current | dict(completedEpochs=1, parentReceiptSha256=None)))
            self.assertEqual(inspect(first), (0, None))
            for bad in ([], receipt | dict(formatId="com.project-seam.gan-checkpoint"),
                        receipt | dict(epoch=receipt["epoch"] | dict(epochComplete=True)),
                        receipt | dict(metadata=receipt["metadata"] | dict(run=current | dict(completedEpochs=3, parentReceiptSha256=None)))):
                with self.assertRaises(ValueError):
                    inspect(bad)

    def test_cli_refuses_conflicting_or_unpaired_resume_before_loading_inputs(self):
        required = []
        for name in ("training-config", "dataset-config", "targets", "source-root", "conditioning", "trusted-checkout", "output"):
            required += ["--"+name, "unused"]
        for name in ("training-sha256", "dataset-sha256", "targets-sha256", "rights-policy-sha256", "label-policy-sha256"):
            required += ["--"+name, "a"*64]
        for extra in (["--resume-partial", "partial"], ["--resume-partial-sha256", "b"*64],
                      ["--resume", "complete", "--resume-receipt-sha256", "c"*64,
                       "--resume-partial", "partial", "--resume-partial-sha256", "d"*64],
                      ["--checkpoint-interval-updates", "0"]):
            with patch("tools.voice_model_training.train_vocoder.load_config") as load:
                self.assertEqual(main(required+extra), 2)
                load.assert_not_called()

    def test_disk_headroom_boundary_and_invalid_budgets(self):
        with patch('tools.voice_model_training.gan_checkpoint_storage.shutil.disk_usage') as usage:
            usage.return_value = SimpleNamespace(free=DISK_RESERVE_BYTES + 100)
            self.assertEqual(require_disk_headroom(Path('/tmp'), 100)['requiredBytes'],
                             DISK_RESERVE_BYTES + 100)
            with self.assertRaises(OSError) as raised:
                require_disk_headroom(Path('/tmp'), 101)
            self.assertEqual(raised.exception.errno, 28)
            for value in (-1, True, 1.5):
                with self.subTest(value=value), self.assertRaises(ValueError):
                    require_disk_headroom(Path('/tmp'), value)
            usage.side_effect = OSError('volume unavailable')
            with self.assertRaisesRegex(OSError, 'volume unavailable'):
                require_disk_headroom(Path('/tmp'), 100)

    def test_larger_architecture_is_explicit_not_a_legacy_default_change(self):
        legacy = model_settings(settings())
        large = model_settings(settings() | dict(schemaVersion=2, architectureProfile='mini-nsf-512-mrf-v1'))
        self.assertEqual(legacy['upsample_initial_channel'], 32)
        self.assertEqual(large['upsample_initial_channel'], 512)
        self.assertEqual(large['resblock_kernel_sizes'], [3, 7, 11])
        self.assertEqual(large['resblock_dilation_sizes'], [[1, 3, 5]] * 3)
        self.assertEqual(large['hop_size'], legacy['hop_size'])
        segmented = settings() | dict(schemaVersion=3, architectureProfile='mini-nsf-512-mrf-v1',
                                      trainingSegmentFrames=128)
        self.assertEqual(model_settings(segmented), large)
        for length in (0, True, 15, 4097):
            with self.subTest(length=length), self.assertRaises(ValueError):
                model_settings(segmented | dict(trainingSegmentFrames=length))
        with self.assertRaises(ValueError):
            model_settings(settings() | dict(trainingSegmentFrames=128))
        for change in (dict(schemaVersion=2), dict(schemaVersion=2, architectureProfile='unknown'),
                       dict(architectureProfile='mini-nsf-512-mrf-v1')):
            with self.subTest(change=change), self.assertRaises(ValueError):
                model_settings(settings() | change)

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
