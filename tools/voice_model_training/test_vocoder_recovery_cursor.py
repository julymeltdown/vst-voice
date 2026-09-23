import copy
import unittest

from tools.voice_model_training.vocoder_recovery_cursor import (
    advance_excitation_digest, build_recovery_plan, excitation_digest_seed,
    partial_cursor, verify_partial_cursor,
)


class RecoveryCursorTests(unittest.TestCase):
    def plan(self):
        return build_recovery_plan([
            dict(sourceId="b", analysisFrames=40, sourceSamples=40*256-7),
            dict(sourceId="a", analysisFrames=20, sourceSamples=20*256)],
            dataset_sha256="a"*64, profile_sha256="b"*64, run_sha256="c"*64,
            segment_frames=16, hop_size=256)

    def test_prefix_counts_partial_source_and_zero_padding_exactly(self):
        plan = self.plan()
        self.assertEqual(len(plan["segments"]), 5)
        cursor = partial_cursor(plan, completed_updates=4, generator_loss_sum=3., discriminator_loss_sum=4.)
        self.assertEqual(cursor["coveredSourceSamples"], {"a": 20*256, "b": 27*256})
        self.assertEqual(cursor["nextSegment"], dict(sourceId="b", frameOffset=27, frameCount=13,
                                                   validSamples=13*256-7))
        self.assertEqual(verify_partial_cursor(cursor, plan), cursor)
        self.assertFalse(cursor["epochComplete"])
        self.assertFalse(cursor["coverageVerified"])
        for count in (0, -1, 5, 6, True):
            with self.assertRaises(ValueError):
                partial_cursor(plan, completed_updates=count, generator_loss_sum=1., discriminator_loss_sum=1.)

    def test_changed_identity_coverage_and_completion_cannot_resume(self):
        plan = self.plan()
        cursor = partial_cursor(plan, completed_updates=2, generator_loss_sum=1., discriminator_loss_sum=2.)
        for update in (dict(epochComplete=True), dict(coverageVerified=True), dict(validSamples=0),
                       dict(datasetSha256="d"*64), dict(runSha256="e"*64), dict(completedUpdates=3),
                       dict(nextSegment=plan["segments"][0]), dict(generatorLossSum=float("nan")),
                       dict(unexpected=True), dict(schemaVersion=True), dict(coverageVerified=0),
                       dict(validSamples=float(cursor["validSamples"]))):
            with self.subTest(update=update), self.assertRaises(ValueError):
                verify_partial_cursor(cursor | update, plan)
        for key in ("frameOffset", "frameCount", "validSamples"):
            changed = copy.deepcopy(plan)
            changed["segments"][0][key] += 1
            with self.assertRaises(ValueError):
                verify_partial_cursor(cursor, changed)

    def test_plan_order_and_geometry_are_deterministic(self):
        plan = self.plan()
        self.assertEqual([r["sourceId"] for r in plan["segments"]], ["a", "a", "b", "b", "b"])
        self.assertEqual(sum(r["validSamples"] for r in plan["segments"]), 60*256-7)
        changed = copy.deepcopy(plan)
        changed["segments"].reverse()
        with self.assertRaises(ValueError):
            partial_cursor(changed, completed_updates=1, generator_loss_sum=0., discriminator_loss_sum=0.)

    def test_excitation_cursor_binds_noise_identity_and_segment_digest_chain(self):
        plan = self.plan()
        raw = excitation_digest_seed(plan, "uv-gated-v1", "raw")
        realized = excitation_digest_seed(plan, "uv-gated-v1", "realized")
        raw = advance_excitation_digest(raw, plan["segments"][0], b"raw-noise")
        realized = advance_excitation_digest(realized, plan["segments"][0], b"gated-noise")
        cursor = partial_cursor(plan, completed_updates=1, generator_loss_sum=1.,
            discriminator_loss_sum=2., excitation_noise_id="uv-gated-v1",
            raw_digest=raw, realized_digest=realized)
        self.assertEqual(cursor["schemaVersion"], 2)
        self.assertEqual(verify_partial_cursor(cursor, plan), cursor)
        self.assertNotEqual(raw, realized)
        self.assertNotEqual(raw, advance_excitation_digest(raw, plan["segments"][1], b"raw-noise"))
        for change in (dict(excitationNoiseId="invalid"),
                       dict(excitationRawChainSha256="0"),
                       dict(excitationDigestAlgorithm="other"),
                       dict(schemaVersion=1)):
            with self.subTest(change=change), self.assertRaises(ValueError):
                verify_partial_cursor(cursor | change, plan)
        with self.assertRaises(ValueError):
            partial_cursor(plan, completed_updates=1, generator_loss_sum=1.,
                           discriminator_loss_sum=2., raw_digest=raw)
