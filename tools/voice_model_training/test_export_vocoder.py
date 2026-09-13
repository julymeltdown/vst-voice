import copy
import hashlib
import json
import unittest

from tools.voice_model_training.export_vocoder import export_identity, TRAINING_REVISION


class VocoderExportIdentityTests(unittest.TestCase):
    def test_identity_and_semantic_mismatches(self):
        config = dict(sampling_rate=48000, num_mels=80, hop_size=256, n_fft=1024,
            win_size=1024, fmin=20, fmax=24000, mini_nsf=True, noise_sigma=0.,
            upsample_rates=[8, 8, 2, 2], upsample_kernel_sizes=[16, 16, 4, 4],
            upsample_initial_channel=32, resblock_kernel_sizes=[3],
            resblock_dilation_sizes=[[1, 3, 5]], resblock="1", pc_aug=False)
        profile = dict(profileId="seam-full-hop-slaney-v1", sampleRate=48000,
            fftSize=1024, windowSize=1024, hopSize=256, bins=80, minimumHz=20, maximumHz=24000,
            tailPadding="zero-to-whole-hop", boundaryPadding="reflect-fft-minus-hop",
            window="periodic-hann", spectrum="unnormalized-magnitude", melNormalization="slaney-area",
            melFrequencyScale="slaney", amplitudeScale="ln-amplitude", floor=1e-5,
            layout="TF", dtype="float32-le")
        digest = hashlib.sha256(json.dumps(profile, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        identity = dict(profileSha256=digest, datasetSha256="a" * 64,
                        objectiveId="nsf-lsgan-logmel-48k80-v1")
        state = dict(metadata=dict(identity, run=dict(trainingRevision=TRAINING_REVISION,
                                                     configuration=config)),
                     epoch=dict(identity, formatId="com.project-seam.vocoder-epoch-result"))
        receipt = dict(formatId="com.project-seam.gan-checkpoint")
        self.assertEqual(export_identity(state, receipt, profile), config)
        for field, value in (("profileSha256", "0" * 64), ("datasetSha256", "b" * 64),
                             ("objectiveId", "unsupported")):
            broken = copy.deepcopy(state)
            broken["epoch"][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                export_identity(broken, receipt, profile)
        for field, value in (("amplitudeScale", "log10-amplitude"), ("hopSize", 512)):
            with self.subTest(field=field), self.assertRaises(ValueError):
                export_identity(state, receipt, dict(profile, **{field: value}))
        broken = copy.deepcopy(state)
        broken["metadata"]["run"]["configuration"]["mini_nsf"] = False
        with self.assertRaises(ValueError):
            export_identity(broken, receipt, profile)
        with self.assertRaises(ValueError):
            export_identity(state, dict(formatId="com.project-seam.training-checkpoint"), profile)


if __name__ == "__main__":
    unittest.main()
