"""An authored review must admit real sources and must not claim independence."""
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
import unittest

from tools.voice_model_training.__main__ import admit_sources, encode_report, load_config
from tools.voice_model_training.author_review import author_review, main
from tools.voice_model_training.permissions import TRAINING_PERMISSIONS
from tools.voice_model_training.test_audio_source import float_wav


def _audio(frames, amplitude=0.25):
    """Observable 48 kHz mono float PCM, so source inspection has real content."""
    return float_wav([amplitude if index % 2 else -amplitude for index in range(frames)])

SEED = bytes(range(32))


class AuthorReviewTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.seed = self.root / "signing-seed.bin"
        self.seed.write_bytes(SEED)
        os.chmod(self.seed, 0o600)
        self.write_permission_config()

    def write_permission_config(self):
        source = self.root / "source.wav"
        source.write_bytes(_audio(8192))
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        evidence = b"First-party procedural render from this repository."
        (self.root / "evidence.txt").write_bytes(evidence)
        row = dict(sourceId="author-review-source", sourceSha256=digest,
                   identityId="author-review-identity", kind="PROCEDURAL_SYNTHESIS",
                   evidenceId="evidence", evidenceSha256=hashlib.sha256(evidence).hexdigest(),
                   permissions=dict.fromkeys(TRAINING_PERMISSIONS, True),
                   reviewRevision="operator-authored")
        # The permission capture carries the exact source rows this review authorizes.
        self.configuration = dict(formatId="com.project-seam.training-permission-config", schemaVersion=1,
            sampleRate=48000,
            sources=[dict(sourceId="author-review-source", songId="song", sessionId="session",
                          lineageId="lineage", path="source.wav", sourceSha256=digest)],
            evidence={"evidence": "evidence.txt"},
            manifest=dict(formatId="com.project-seam.training-permission-manifest", schemaVersion=1,
                          sources=[row]))
        data = encode_report(self.configuration)
        self.configuration_path = self.root / "permissions.json"
        self.configuration_path.write_bytes(data)
        self.configuration_sha256 = hashlib.sha256(data).hexdigest()

    def author(self, **changes):
        arguments = dict(kind="rights", configuration=self.configuration_path,
                         configuration_sha256=self.configuration_sha256, seed=self.seed,
                         output=self.root / "review", key_id="owner-key", signer_id="owner",
                         valid_seconds=3600)
        arguments.update(changes)
        return author_review(**arguments)

    def test_authored_review_admits_the_real_captured_source(self):
        result = self.author()
        self.assertTrue(result["authoring"]["signatureValid"])
        self.assertIs(result["authoring"]["selfAuthored"], True)
        self.assertIs(result["authoring"]["independenceClaimed"], False)
        for field in ("sourceRightsAdmitted", "labelsAdmitted", "trainingAdmitted",
                      "singerQualified", "releaseEligible"):
            self.assertIs(result["authoring"][field], False)
        # The point of the tool: admission now succeeds against the real WAV bytes.
        admission = admit_sources(self.configuration_path, self.configuration_sha256, self.root,
            review=result["review"], policy=result["policy"],
            trusted_policy_sha256=result["authoring"]["policySha256"], now=int(time.time()))
        self.assertTrue(admission["reviewAuthenticated"])
        self.assertTrue(admission["sourceBytesVerified"])
        self.assertTrue(admission["sourcePermissionsAdmitted"])
        self.assertFalse(admission["trainingAdmitted"])
        self.assertFalse(admission["releaseEligible"])
        self.assertEqual(admission["signerId"], "owner")

    def test_review_is_bound_to_the_configuration_it_signed(self):
        result = self.author()
        # A review for these bytes must not verify against a different configuration.
        with self.assertRaises(ValueError):
            admit_sources(self.configuration_path, "0" * 64, self.root, review=result["review"],
                policy=result["policy"], trusted_policy_sha256=result["authoring"]["policySha256"],
                now=int(time.time()))
        # Tampering with the source bytes after signing must also fail.
        (self.root / "source.wav").write_bytes(_audio(8192, amplitude=0.5))
        with self.assertRaises(ValueError):
            admit_sources(self.configuration_path, self.configuration_sha256, self.root,
                review=result["review"], policy=result["policy"],
                trusted_policy_sha256=result["authoring"]["policySha256"], now=int(time.time()))

    def test_label_role_cannot_authorize_rights_and_kinds_stay_separate(self):
        result = self.author(kind="label")
        self.assertIs(result["authoring"]["selfAuthored"], True)
        with self.assertRaises(ValueError):
            admit_sources(self.configuration_path, self.configuration_sha256, self.root,
                review=result["review"], policy=result["policy"],
                trusted_policy_sha256=result["authoring"]["policySha256"], now=int(time.time()))

    def test_independence_requires_a_different_named_reviewer(self):
        for value in ("owner", "", "x" * 257, "bad\x01text"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.author(output=self.root / f"review-{len(value)}",
                            independent_reviewer_id=value)
        result = self.author(output=self.root / "independent", independent_reviewer_id="second-reviewer")
        self.assertIs(result["authoring"]["selfAuthored"], False)
        self.assertIs(result["authoring"]["independenceClaimed"], True)
        self.assertEqual(result["authoring"]["independentReviewerId"], "second-reviewer")

    def test_exposed_or_invalid_seed_and_bad_arguments_are_refused(self):
        os.chmod(self.seed, 0o644)
        with self.assertRaisesRegex(ValueError, "owner-only"):
            self.author()
        os.chmod(self.seed, 0o600)
        short = self.root / "short.bin"
        short.write_bytes(b"short")
        os.chmod(short, 0o600)
        with self.assertRaisesRegex(ValueError, "32 bytes"):
            self.author(seed=short)
        link = self.root / "link.bin"
        link.symlink_to(self.seed)
        with self.assertRaises(ValueError):
            self.author(seed=link)
        for changes in (dict(kind="other"), dict(valid_seconds=0), dict(valid_seconds=91 * 24 * 3600),
                        dict(configuration_sha256="z" * 64), dict(key_id=""), dict(signer_id="x" * 257)):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.author(output=self.root / f"bad-{abs(hash(str(changes)))}", **changes)

    def test_output_is_not_overwritten_and_cli_reports_the_honest_record(self):
        self.author()
        with self.assertRaises(ValueError):
            self.author()
        output = self.root / "cli"
        code = main(["--kind", "rights", "--configuration", str(self.configuration_path),
                     "--configuration-sha256", self.configuration_sha256, "--seed", str(self.seed),
                     "--output", str(output), "--key-id", "owner-key", "--signer-id", "owner",
                     "--valid-seconds", "600"])
        self.assertEqual(code, 0)
        authoring = json.loads((output / "authoring.json").read_bytes())
        self.assertIs(authoring["selfAuthored"], True)
        self.assertIs(authoring["trainingAdmitted"], False)
        self.assertIs(authoring["releaseEligible"], False)
        for name in ("policy.json", "review.json", "authoring.json"):
            self.assertTrue((output / name).is_file())


if __name__ == "__main__":
    unittest.main()
