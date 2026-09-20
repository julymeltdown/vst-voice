"""Warm-start conditioning addition: the approved breathiness exception.

The acoustic model cannot produce noise-like spectra, and the measured cause is
that no aperiodicity channel reaches it. Retraining that repair needs the
retained baseline weights, so warm start must accept exactly one architectural
addition while staying closed to every other change.
"""
import unittest

try:
    import torch
except ImportError:
    torch = None

from tools.voice_model_training.conditioning import ADDED_CONDITIONING_PARAMETERS
from tools.voice_model_training.conditioning import (CONDITIONING_BINDING_FIELDS,
    validate_conditioning_bindings)
from tools.voice_model_training.train import (_conditioning_additions,
    _load_warm_start_state, _warm_start_identity, initialize_checkpoint, model_settings)


class _FakeAcoustic(torch.nn.Module if torch is not None else object):
    """Mirrors the upstream parameter names without loading the real model."""

    def __init__(self, conditioned):
        super().__init__()
        self.fs2 = torch.nn.Module()
        self.fs2.variance_embeds = torch.nn.ModuleDict()
        if conditioned:
            self.fs2.variance_embeds["breathiness"] = torch.nn.Linear(1, 3)
        self.backbone = torch.nn.Linear(2, 2)


@unittest.skipIf(torch is None, "Torch is optional outside the training environment")
class ConditioningAdditionTests(unittest.TestCase):
    def test_only_an_enabled_breathiness_addition_is_permitted(self):
        empty = dict(conditioningControls=[])
        enabled = dict(conditioningControls=["breathiness"])
        self.assertEqual(_conditioning_additions(empty, enabled), ADDED_CONDITIONING_PARAMETERS)
        # No change is not an addition, so it keeps the pre-existing behaviour.
        self.assertEqual(_conditioning_additions(empty, dict(empty)), frozenset())
        self.assertEqual(_conditioning_additions(enabled, dict(enabled)), frozenset())
        for prior, current in ((empty, dict(conditioningControls=["tension"])),
                               (empty, dict(conditioningControls=["breathiness", "tension"])),
                               (enabled, empty)):
            with self.subTest(prior=prior, current=current), self.assertRaises(ValueError):
                _conditioning_additions(prior, current)
        # A schema-1 configuration declares no controls at all and is still empty.
        self.assertEqual(_conditioning_additions(dict(conditioningControls=[]), enabled),
                         ADDED_CONDITIONING_PARAMETERS)

    def test_added_parameter_names_are_the_declared_embedding_only(self):
        self.assertEqual(sorted(ADDED_CONDITIONING_PARAMETERS),
                         ["fs2.variance_embeds.breathiness.bias",
                          "fs2.variance_embeds.breathiness.weight"])

    def test_identity_carries_and_bounds_the_added_parameters(self):
        identity = dict(sourceReceiptSha256="a" * 64, sourceCheckpointSha256="b" * 64,
                        sourceTrainingConfigurationSha256="c" * 64, sourceCompletedEpochs=21,
                        optimizerReset=True, rngReset=True, epochNumbering="new-experiment-from-one")
        self.assertEqual(_warm_start_identity(dict(identity)), identity)
        self.assertEqual(_warm_start_identity(dict(identity, addedParameters=[])), identity | dict(addedParameters=[]))
        for bad in (["fs2.variance_embeds.tension.weight"],
                    ["fs2.txt_embed.weight"],
                    ["fs2.variance_embeds.breathiness.weight"] * 2,
                    "fs2.variance_embeds.breathiness.weight"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                _warm_start_identity(dict(identity, addedParameters=bad))

    def test_addition_loads_captured_weights_and_zero_initializes_the_new_embedding(self):
        torch.manual_seed(4)
        captured = _FakeAcoustic(conditioned=False).state_dict()
        torch.manual_seed(9)
        model = _FakeAcoustic(conditioned=True)
        before = {k: (None if v is None else v.clone()) for k, v in model.state_dict().items()}
        _load_warm_start_state(model, captured, ADDED_CONDITIONING_PARAMETERS)
        state = model.state_dict()
        for name, value in captured.items():
            self.assertTrue(torch.equal(state[name], value), name)
        # Zero, not merely small: an untrained addition must not perturb the baseline.
        self.assertTrue(torch.equal(state["fs2.variance_embeds.breathiness.weight"],
                                    torch.zeros_like(state["fs2.variance_embeds.breathiness.weight"])))
        self.assertTrue(torch.equal(state["fs2.variance_embeds.breathiness.bias"],
                                    torch.zeros_like(state["fs2.variance_embeds.breathiness.bias"])))
        self.assertFalse(torch.equal(before["fs2.variance_embeds.breathiness.weight"],
                                     state["fs2.variance_embeds.breathiness.weight"]))

    def test_addition_refuses_any_other_architecture_difference(self):
        captured = _FakeAcoustic(conditioned=False).state_dict()
        extra = dict(captured, **{"fs2.unknown.weight": torch.zeros(1)})
        with self.assertRaises(ValueError):
            _load_warm_start_state(_FakeAcoustic(conditioned=True), extra, ADDED_CONDITIONING_PARAMETERS)
        # A capture that already carries the embedding is not an addition.
        with self.assertRaises(ValueError):
            _load_warm_start_state(_FakeAcoustic(conditioned=True),
                                   _FakeAcoustic(conditioned=True).state_dict(),
                                   ADDED_CONDITIONING_PARAMETERS)
        # Declaring no addition against a capture missing weights must also fail.
        with self.assertRaises(ValueError):
            _load_warm_start_state(_FakeAcoustic(conditioned=True), captured, frozenset())


class ConditioningBindingTests(unittest.TestCase):
    """A conditioning addition may change supervision, and nothing else."""

    def bindings(self, **updates):
        value = dict(conditioningSha256="1" * 64, labelConfigurationSha256="2" * 64,
                     labelReviewSha256="3" * 64, permissionConfigurationSha256="4" * 64,
                     rightsReviewSha256="5" * 64, split=dict(seed="s", groups=[], heldOutSongIds=[]))
        return value | updates

    def test_only_conditioning_bindings_may_change(self):
        current = self.bindings(conditioningSha256="a" * 64, labelConfigurationSha256="b" * 64,
                                labelReviewSha256="c" * 64)
        self.assertEqual(validate_conditioning_bindings(self.bindings(), current), current)
        # The reviewed material, rights and split must be untouched, so the repair
        # is measured against the same corpus the warm-start checkpoint used.
        for field in ("permissionConfigurationSha256", "rightsReviewSha256", "split"):
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_conditioning_bindings(self.bindings(), current | {field: "changed"})

    def test_unchanged_conditioning_is_not_an_addition(self):
        same = self.bindings()
        with self.assertRaises(ValueError):
            validate_conditioning_bindings(same, same)
        partial = self.bindings(conditioningSha256="a" * 64)
        with self.assertRaises(ValueError):
            validate_conditioning_bindings(self.bindings(), partial)

    def test_malformed_bindings_are_refused(self):
        current = self.bindings(conditioningSha256="a" * 64, labelConfigurationSha256="b" * 64,
                                labelReviewSha256="c" * 64)
        for source in ({}, dict(current, extra="x"), None, "bindings"):
            with self.subTest(source=source), self.assertRaises(ValueError):
                validate_conditioning_bindings(source, current)
        with self.assertRaises(ValueError):
            validate_conditioning_bindings(self.bindings(), None)
        # Every conditionable field must actually be present to be compared.
        missing = {k: v for k, v in self.bindings().items() if k not in CONDITIONING_BINDING_FIELDS}
        with self.assertRaises(ValueError):
            validate_conditioning_bindings(missing, current)


@unittest.skipIf(torch is None, "Torch is optional outside the training environment")
class WarmStartIntegrationTests(unittest.TestCase):
    """Exercises the real initializer, not the helpers, on a real-shaped state."""

    def settings(self, controls=None):
        value = dict(formatId="com.project-seam.ddpm-training-config",
                     schemaVersion=1 if controls is None else 2,
                     hiddenSize=32, encoderLayers=1, channels=32, layers=2, timesteps=8,
                     seed=17, learningRate=.001, maximumUpdates=3, maximumSeconds=60, loss="l1")
        if controls is not None:
            value["conditioningControls"] = controls
        return value

    def receipt(self, settings, objective="diffsinger-ddpm-l1-whole-phrase-v2"):
        receipt = dict(checkpointSha256="d" * 64,
            metadata=dict(run=dict(settings=settings, configuration=model_settings(settings),
                                   vocabulary=["a"], assemblyConfigurationSha256="b" * 64,
                                   targetInventorySha256="c" * 64, revision="pinned",
                                   torchVersion="t", numpyVersion="n", singerQualified=False,
                                   trainingConfigurationSha256="a" * 64, completedEpochs=21,
                                   parentReceiptSha256="e" * 64),
                          profileSha256="f" * 64, datasetSha256="1" * 64),
            epoch=dict(epochComplete=True, coverageVerified=True, datasetSha256="1" * 64,
                       objectiveId=objective))
        return receipt

    def capture(self, conditioned):
        """Weights shaped like the checkpoint for that architecture."""
        torch.manual_seed(11)
        model = _FakeAcoustic(conditioned=conditioned)
        with torch.no_grad():
            for value in model.parameters():
                value.add_(1.0)
        return {k: v.clone() for k, v in model.state_dict().items()}

    def metadata(self, settings):
        return dict(trainingConfigurationSha256="9" * 64, assemblyConfigurationSha256="b" * 64,
                    targetInventorySha256="c" * 64, configuration=model_settings(settings),
                    settings=settings, revision="pinned", torchVersion="t", numpyVersion="n",
                    vocabulary=["a"], singerQualified=False)

    def test_adding_the_control_is_the_only_reason_warm_start_accepts_a_schema_change(self):
        prior = self.settings()
        receipt = self.receipt(prior)
        captured = self.capture(conditioned=False)
        current = self.settings(["breathiness"])
        metadata = self.metadata(current)
        model = _FakeAcoustic(conditioned=True)
        optimizer = torch.optim.AdamW(model.parameters(), lr=current["learningRate"])
        result, dataset, completed = initialize_checkpoint(
            model, optimizer, dict(model=captured), receipt, metadata=metadata, profile="f" * 64,
            receipt_sha256="2" * 64, warm_start=True)
        self.assertEqual(completed, 0)
        self.assertEqual(dataset, "1" * 64)
        self.assertEqual(result["warmStart"]["addedParameters"],
                         sorted(ADDED_CONDITIONING_PARAMETERS))
        self.assertFalse(optimizer.state)
        # Captured weights survive and the sole addition starts at zero.
        state = model.state_dict()
        for name, value in captured.items():
            self.assertTrue(torch.equal(state[name], value), name)
        for name in ADDED_CONDITIONING_PARAMETERS:
            self.assertTrue(torch.equal(state[name], torch.zeros_like(state[name])), name)

    def test_schema_change_without_the_declared_addition_is_refused(self):
        prior = self.settings(["breathiness"])
        receipt = self.receipt(prior)
        captured = self.capture(conditioned=True)
        # Same schema-2 settings, but the run claims no addition while the model
        # is missing nothing: this is an unrelated architecture change attempt.
        for current, model in ((self.settings(["breathiness"]), _FakeAcoustic(conditioned=False)),
                               (self.settings(), _FakeAcoustic(conditioned=True))):
            with self.subTest(current=current["schemaVersion"]), self.assertRaises(ValueError):
                optimizer = torch.optim.AdamW(model.parameters(), lr=current["learningRate"])
                initialize_checkpoint(model, optimizer, dict(model=captured), receipt,
                    metadata=self.metadata(current), profile="f" * 64,
                    receipt_sha256="2" * 64, warm_start=True)

    def test_plain_warm_start_still_requires_the_identical_dataset(self):
        # The dataset-digest exemption belongs to the addition alone: without an
        # addition, a changed assembly configuration must still be refused.
        prior = self.settings()
        receipt = self.receipt(prior)
        captured = self.capture(conditioned=False)
        metadata = self.metadata(prior) | dict(assemblyConfigurationSha256="7" * 64)
        model = _FakeAcoustic(conditioned=False)
        optimizer = torch.optim.AdamW(model.parameters(), lr=prior["learningRate"])
        with self.assertRaises(ValueError):
            initialize_checkpoint(model, optimizer, dict(model=captured), receipt,
                metadata=metadata, profile="f" * 64, receipt_sha256="2" * 64, warm_start=True)

    def test_schema3_auxiliary_warm_start_keeps_architecture_and_dataset(self):
        # The auxiliary objective is a governed settings change like the
        # conditioning addition: schema 3 is accepted, no parameters are added
        # and the dataset identity must still match exactly.
        prior = self.settings(["breathiness"])
        receipt = self.receipt(prior)
        captured = self.capture(conditioned=True)
        current = self.settings(["breathiness"]) | dict(
            schemaVersion=3,
            auxiliaryObjective=dict(kind="unvoiced-clean-mel-shape-level", weight=0.0,
                                    unvoicedSymbols=["h", "f", "k", "s", "t", "ch", "ts"]))
        model = _FakeAcoustic(conditioned=True)
        optimizer = torch.optim.AdamW(model.parameters(), lr=current["learningRate"])
        result, dataset, completed = initialize_checkpoint(
            model, optimizer, dict(model=captured), receipt,
            metadata=self.metadata(current), profile="f" * 64,
            receipt_sha256="2" * 64, warm_start=True)
        self.assertEqual(completed, 0)
        self.assertEqual(dataset, "1" * 64)
        self.assertNotIn("addedParameters", result["warmStart"])
        state = model.state_dict()
        for name, value in captured.items():
            self.assertTrue(torch.equal(state[name], value), name)

    def test_schema3_receipt_requires_the_auxiliary_objective_identity(self):
        # A schema-3 parent that ran the auxiliary objective cannot masquerade
        # as the base objective: lineage compares the recorded objective id.
        prior = self.settings(["breathiness"]) | dict(
            schemaVersion=3,
            auxiliaryObjective=dict(kind="unvoiced-clean-mel-shape-level", weight=0.0,
                                    unvoicedSymbols=["h", "f", "k", "s", "t", "ch", "ts"]))
        receipt = self.receipt(prior)  # epoch still claims the base objective id
        captured = self.capture(conditioned=True)
        model = _FakeAcoustic(conditioned=True)
        optimizer = torch.optim.AdamW(model.parameters(), lr=prior["learningRate"])
        with self.assertRaises(ValueError):
            initialize_checkpoint(model, optimizer, dict(model=captured), receipt,
                metadata=self.metadata(prior), profile="f" * 64,
                receipt_sha256="2" * 64, warm_start=True)

    def test_schema3_warm_start_cannot_drop_the_auxiliary_declaration(self):
        # Removing the auxiliary declaration is a schema regression, not a
        # permitted loss/LR change.
        prior = self.settings(["breathiness"]) | dict(
            schemaVersion=3,
            auxiliaryObjective=dict(kind="unvoiced-clean-mel-shape-level", weight=0.0,
                                    unvoicedSymbols=["h", "f", "k", "s", "t", "ch", "ts"]))
        receipt = self.receipt(prior, objective="diffsinger-ddpm-l1-unvoiced-clean-mel-shape-level-v1")
        captured = self.capture(conditioned=True)
        current = self.settings(["breathiness"])
        model = _FakeAcoustic(conditioned=True)
        optimizer = torch.optim.AdamW(model.parameters(), lr=current["learningRate"])
        with self.assertRaises(ValueError):
            initialize_checkpoint(model, optimizer, dict(model=captured), receipt,
                metadata=self.metadata(current), profile="f" * 64,
                receipt_sha256="2" * 64, warm_start=True)


if __name__ == "__main__":
    unittest.main()
