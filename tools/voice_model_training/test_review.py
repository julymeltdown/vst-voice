import base64
import copy
import unittest
import hashlib
import json
import tempfile
import time
import subprocess
import sys
from pathlib import Path
from tools.phase13a.update_contract import ed25519_public_key, ed25519_sign
from tools.public_release.crypto_validation import canonical_signing_payload
from tools.public_release.contracts import sha256_json
from tools.voice_model_training.review import verify_training_review, verify_label_review
from tools.voice_model_training.__main__ import admit_sources, admit_segment, admit_labels, assemble_dataset
from tools.voice_model_training.permissions import TRAINING_PERMISSIONS
from tools.voice_model_training.test_audio_source import wav


class ReviewTests(unittest.TestCase):
    def test_signed_review_identity_time_and_external_policy(self):
        seed = bytes(range(32))  # Test-only signer, never a product trust key.
        policy = dict(policyVersion="seam-training-review-1", algorithm="Ed25519", requiredRoles=["training-rights-reviewer"],
                      trustedKeys=[dict(keyId="fixture", role="training-rights-reviewer", signerId="fixture-reviewer",
                      publicKey=base64.b64encode(ed25519_public_key(seed)).decode())])
        anchor = sha256_json(policy)
        review = dict(formatId="com.project-seam.training-rights-review", schemaVersion=1,
                      policyVersion=policy["policyVersion"], policySha256=anchor, algorithm="Ed25519",
                      keyId="fixture", signerId="fixture-reviewer", configurationSha256="a" * 64,
                      decision="APPROVE_TRAINING_SCOPES", issuedAt=100, expiresAt=200)
        review["signature"] = base64.b64encode(ed25519_sign(canonical_signing_payload(review, "recordSha256"), seed)).decode()
        review["recordSha256"] = sha256_json(review)
        def verify(value=review, **kwargs):
            args = dict(policy=policy, trusted_policy_sha256=anchor, configuration_sha256="a" * 64, now=150)
            args.update(kwargs)
            return verify_training_review(value, **args)
        self.assertTrue(verify()["reviewAuthenticated"])
        self.assertFalse(verify()["trainingAdmitted"])
        with self.assertRaises(ValueError):
            verify_label_review(review, policy=policy, trusted_policy_sha256=anchor, configuration_sha256="a" * 64, now=150)
        label_policy = copy.deepcopy(policy)
        label_policy.update(policyVersion="seam-training-label-review-1", requiredRoles=["training-label-reviewer"])
        label_policy["trustedKeys"][0]["role"] = "training-label-reviewer"
        label_anchor = sha256_json(label_policy)
        label_review = dict(review, formatId="com.project-seam.training-label-review", decision="APPROVE_LABELS",
                            policyVersion=label_policy["policyVersion"], policySha256=label_anchor)
        label_review["signature"] = base64.b64encode(ed25519_sign(canonical_signing_payload(label_review, "recordSha256"), seed)).decode()
        label_review["recordSha256"] = sha256_json({k: v for k, v in label_review.items() if k != "recordSha256"})
        self.assertTrue(verify_label_review(label_review, policy=label_policy, trusted_policy_sha256=label_anchor,
                                           configuration_sha256="a" * 64, now=150)["reviewAuthenticated"])
        with self.assertRaises(ValueError):
            verify_training_review(label_review, policy=label_policy, trusted_policy_sha256=label_anchor,
                                   configuration_sha256="a" * 64, now=150)
        for key, value in (("signerId", "other"), ("decision", "REJECT"), ("signature", "A" * 88)):
            altered = dict(review, **{key: value})
            with self.assertRaises(ValueError): verify(altered)
        for args in (dict(now=200), dict(now=99), dict(configuration_sha256="b" * 64), dict(trusted_policy_sha256="0" * 64)):
            with self.assertRaises(ValueError): verify(**args)
        changed = copy.deepcopy(policy); changed["trustedKeys"][0]["role"] = "release-manager"
        with self.assertRaises(ValueError): verify(policy=changed)
        weak_policy = copy.deepcopy(policy)
        identity = bytes([1]) + bytes(31)
        weak_policy["trustedKeys"][0]["publicKey"] = base64.b64encode(identity).decode()
        weak_anchor = sha256_json(weak_policy)
        forged = dict(review, policySha256=weak_anchor, signature=base64.b64encode(identity + bytes(32)).decode())
        forged["recordSha256"] = sha256_json({k: v for k, v in forged.items() if k != "recordSha256"})
        with self.assertRaises(ValueError):
            verify(forged, policy=weak_policy, trusted_policy_sha256=weak_anchor)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            audio, evidence = wav(), b"test-only rights assertion"
            (root / "audio.wav").write_bytes(audio)
            (root / "rights.txt").write_bytes(evidence)
            source = dict(sourceId="source", sourceSha256=hashlib.sha256(audio).hexdigest(),
                          songId="song", sessionId="session", lineageId="lineage", path="audio.wav")
            permission = dict(sourceId="source", sourceSha256=source["sourceSha256"], identityId="singer",
                kind="PROCEDURAL_SYNTHESIS", evidenceId="rights", evidenceSha256=hashlib.sha256(evidence).hexdigest(),
                reviewRevision="fixture", permissions=dict.fromkeys(TRAINING_PERMISSIONS, True))
            config = dict(formatId="com.project-seam.training-permission-config", schemaVersion=1, sampleRate=48000,
                sources=[source], evidence={"rights": "rights.txt"}, manifest=dict(
                    formatId="com.project-seam.training-permission-manifest", schemaVersion=1, sources=[permission]))
            def capture():
                payload = json.dumps(config).encode()
                (root / "config.json").write_bytes(payload)
                bound = dict(review, configurationSha256=hashlib.sha256(payload).hexdigest())
                bound["signature"] = base64.b64encode(ed25519_sign(canonical_signing_payload(bound, "recordSha256"), seed)).decode()
                bound["recordSha256"] = sha256_json({k: v for k, v in bound.items() if k != "recordSha256"})
                return bound
            bound = capture()
            def admit():
                return admit_sources(root / "config.json", bound["configurationSha256"], root,
                    review=bound, policy=policy, trusted_policy_sha256=anchor, now=150)
            result = admit()
            self.assertTrue(result["sourcePermissionsAdmitted"])
            self.assertFalse(result["trainingAdmitted"])
            acoustic = dict(sourceId="source", frameCount=32, hopSize=256, f0Hz=[220], voiced=[True],
                phonemes=[dict(symbol="a", startFrame=0, endFrame=32, confidence=1)], reviewRevision=None)
            label_config = dict(formatId="com.project-seam.voice-training-label-config", schemaVersion=3,
                sampleRate=48000, sources=[source], vocabulary=["a"], minimumConfidence=0.8,
                labels=[dict(sourceSha256=source["sourceSha256"], audioSha256=result["sources"][0]["audioSha256"],
                    label=acoustic, score=dict(language="en", silencePhones=[],
                        syllables=[dict(lyric="ah", phoneStart=0, phoneEnd=1)],
                        notes=[dict(startFrame=0, endFrame=32, midi=57, syllable=0, slur=False)]))])
            def admit_annotation(cli_output=None, dataset=False):
                payload = json.dumps(label_config).encode()
                (root / "labels.json").write_bytes(payload)
                digest = hashlib.sha256(payload).hexdigest()
                signed = dict(label_review, configurationSha256=digest)
                if cli_output is not None:
                    signed.update(issuedAt=int(time.time()) - 60, expiresAt=int(time.time()) + 600)
                signed["signature"] = base64.b64encode(ed25519_sign(canonical_signing_payload(signed, "recordSha256"), seed)).decode()
                signed["recordSha256"] = sha256_json({k: v for k, v in signed.items() if k != "recordSha256"})
                if cli_output is not None:
                    signed_bytes, policy_bytes = json.dumps(signed).encode(), json.dumps(label_policy).encode()
                    (root / "label-review.json").write_bytes(signed_bytes)
                    (root / "label-policy.json").write_bytes(policy_bytes)
                    return subprocess.run([sys.executable, "-m", "tools.voice_model_training", "admit-labels",
                        str(root / "labels.json"), digest, str(root), str(root / cli_output),
                        "--review", str(root / "label-review.json"), "--review-sha256", hashlib.sha256(signed_bytes).hexdigest(),
                        "--policy", str(root / "label-policy.json"), "--policy-file-sha256", hashlib.sha256(policy_bytes).hexdigest(),
                        "--trusted-policy-sha256", label_anchor], capture_output=True, timeout=15)
                if dataset:
                    return assemble_dataset(root / "config.json", bound["configurationSha256"], root / "labels.json", digest, root,
                        rights_review=bound, rights_policy=policy, rights_anchor=anchor, label_review=signed,
                        label_policy=label_policy, label_anchor=label_anchor, now=150, seed="pilot", held_out_songs=["song"])
                return admit_labels(root / "labels.json", digest, root, review=signed, policy=label_policy,
                                    trusted_policy_sha256=label_anchor, now=150)
            annotation = admit_annotation()
            self.assertTrue(annotation["labelsAdmitted"])
            self.assertFalse(annotation["sourcePermissionsAdmitted"])
            dataset = admit_annotation(dataset=True)
            self.assertTrue(dataset["labelsAdmitted"] and dataset["sourcePermissionsAdmitted"])
            self.assertFalse(dataset["trainingAdmitted"])
            self.assertEqual(dataset["bindings"]["split"]["counts"]["test"], 1)
            self.assertEqual(dataset["datasetSha256"], admit_annotation(dataset=True)["datasetSha256"])
            self.assertEqual(len(dataset["preparationIssues"]), 2)
            self.assertEqual(dataset["schemaVersion"], 2)
            self.assertEqual(dataset["conditioningFrameCount"], 1)
            self.assertEqual(dataset["conditioning"][0]["frames"][0]["phoneId"], 1)
            self.assertEqual(dataset["conditioning"][0]["frames"][0]["midi"], 57)
            from tools.voice_model_training.__main__ import encode_report
            self.assertEqual(dataset["bindings"]["conditioningSha256"],
                             hashlib.sha256(encode_report(dataset["conditioning"])).hexdigest())
            label_config["sources"] = [dict(source, lineageId="other")]
            with self.assertRaises(ValueError): admit_annotation(dataset=True)
            label_config["sources"] = [source]
            self.assertEqual(admit_annotation("label-admission.json").returncode, 0)
            label_publication = (root / "label-admission.json").read_bytes()
            self.assertTrue(json.loads(label_publication)["labelsAdmitted"])
            self.assertEqual(admit_annotation("label-admission.json").returncode, 2)
            self.assertEqual((root / "label-admission.json").read_bytes(), label_publication)
            acoustic["phonemes"][0]["confidence"] = 0.1
            with self.assertRaises(ValueError): admit_annotation()
            self.assertEqual(admit_annotation("bad-label-admission.json").returncode, 2)
            self.assertFalse((root / "bad-label-admission.json").exists())
            acoustic["phonemes"][0]["confidence"] = 1
            label_config["schemaVersion"] = 2
            with self.assertRaises(ValueError): admit_annotation()
            segment = dict(formatId="com.project-seam.voice-training-segment-config", schemaVersion=1,
                source={k: source[k] for k in ("sourceId", "sourceSha256", "songId", "sessionId", "lineageId")},
                sampleRate=48000, segmentId="child", startFrame=0, endFrame=16)
            def derive(name):
                payload = json.dumps(segment).encode()
                (root / "segment-config.json").write_bytes(payload)
                return admit_segment(root / "config.json", bound["configurationSha256"], root,
                    segment_config=root / "segment-config.json", segment_hash=hashlib.sha256(payload).hexdigest(),
                    source_path=root / "audio.wav", output=root / name, review=bound, policy=policy,
                    trusted_policy_sha256=anchor, now=150)
            derived = derive("derived")
            self.assertTrue(derived["sourcePermissionsAdmitted"])
            self.assertFalse(derived["trainingAdmitted"])
            self.assertEqual(derived["identityId"], "singer")
            segment["source"]["lineageId"] = "unreviewed"
            with self.assertRaises(ValueError): derive("wrong-lineage")
            self.assertFalse((root / "wrong-lineage").exists())
            # Exercise the actual CLI clock with a fixture-only currently valid review.
            cli_review = dict(bound, issuedAt=int(time.time()) - 60, expiresAt=int(time.time()) + 600)
            cli_review["signature"] = base64.b64encode(ed25519_sign(canonical_signing_payload(cli_review, "recordSha256"), seed)).decode()
            cli_review["recordSha256"] = sha256_json({k: v for k, v in cli_review.items() if k != "recordSha256"})
            review_bytes, policy_bytes = json.dumps(cli_review).encode(), json.dumps(policy).encode()
            (root / "review.json").write_bytes(review_bytes)
            (root / "policy.json").write_bytes(policy_bytes)
            # Assemble through the public CLI with two independently pinned review purposes.
            label_config["schemaVersion"] = 3
            self.assertEqual(admit_annotation("dataset-label-admission.json").returncode, 0)
            assembly = dict(formatId="com.project-seam.training-dataset-config", schemaVersion=1,
                            seed="pilot", heldOutSongs=["song"])
            for key, name in {"permissionConfig": "config.json", "labelConfig": "labels.json",
                              "rightsReview": "review.json", "rightsPolicy": "policy.json",
                              "labelReview": "label-review.json", "labelPolicy": "label-policy.json"}.items():
                assembly[key] = dict(path=name, sha256=hashlib.sha256((root / name).read_bytes()).hexdigest())
            def assemble_cli(name, *options):
                payload = json.dumps(assembly).encode()
                (root / "assembly.json").write_bytes(payload)
                return subprocess.run([sys.executable, "-m", "tools.voice_model_training", "assemble-dataset",
                    str(root / "assembly.json"), hashlib.sha256(payload).hexdigest(), str(root), str(root / name),
                    "--rights-policy-sha256", anchor, "--label-policy-sha256", label_anchor, *options],
                    capture_output=True, timeout=15)
            result = assemble_cli("dataset.json")
            self.assertEqual(result.returncode, 3, result.stderr)
            snapshot_bytes = (root / "dataset.json").read_bytes()
            snapshot = json.loads(snapshot_bytes)
            self.assertEqual(len(snapshot["preparationIssues"]), 2)
            self.assertEqual(snapshot["conditioning"][0]["frames"][0]["validSamples"], 32)
            self.assertEqual(snapshot["conditioning"][0]["frames"][0]["f0Hz"], 220)
            self.assertFalse(snapshot["trainingAdmitted"])
            self.assertFalse(snapshot["releaseEligible"])
            self.assertEqual(snapshot["assemblyConfigurationSha256"],
                             hashlib.sha256((root / "assembly.json").read_bytes()).hexdigest())
            self.assertEqual(assemble_cli("dataset.json").returncode, 2)
            self.assertEqual((root / "dataset.json").read_bytes(), snapshot_bytes)
            shard_option = ("--conditioning-directory", str(root / "features"))
            result = assemble_cli("sharded.json", *shard_option)
            self.assertEqual(result.returncode, 3, result.stderr)
            sharded = json.loads((root / "sharded.json").read_bytes())
            self.assertEqual(sharded["schemaVersion"], 3)
            self.assertEqual(sharded["conditioningDirectory"], "features")
            reference = sharded["conditioning"][0]
            shard_bytes = (root / "features" / reference["path"]).read_bytes()
            self.assertEqual(reference["sha256"], hashlib.sha256(shard_bytes).hexdigest())
            self.assertEqual(reference["sizeBytes"], len(shard_bytes))
            self.assertEqual(json.loads(shard_bytes), snapshot["conditioning"][0])
            self.assertEqual(sharded["conditioningBytes"], len(shard_bytes))
            refresh = assemble_cli("refreshed.json", *shard_option, "--reuse-conditioning")
            self.assertEqual(refresh.returncode, 3, refresh.stderr)
            refreshed = json.loads((root / "refreshed.json").read_bytes())
            self.assertEqual(refreshed["datasetSha256"], sharded["datasetSha256"])
            self.assertEqual((root / "features" / reference["path"]).read_bytes(), shard_bytes)
            self.assertEqual(assemble_cli("no-directory.json", "--reuse-conditioning").returncode, 2)
            self.assertFalse((root / "no-directory.json").exists())
            current_audio = (root / "audio.wav").read_bytes()
            (root / "audio.wav").write_bytes(current_audio[:-1])
            self.assertEqual(assemble_cli("changed-source-refresh.json", *shard_option, "--reuse-conditioning").returncode, 2)
            self.assertFalse((root / "changed-source-refresh.json").exists())
            self.assertEqual((root / "features" / reference["path"]).read_bytes(), shard_bytes)
            (root / "audio.wav").write_bytes(current_audio)
            from tools.voice_model_training.batches import iter_conditioning_batches
            batches = list(iter_conditioning_batches(sharded, root / "features", partition="test", batch_frames=1))
            self.assertEqual(len(batches), 1)
            self.assertEqual(batches[0]["columns"]["phoneId"], [1])
            self.assertEqual(batches[0]["columns"]["validSamples"], [32])
            self.assertEqual(list(iter_conditioning_batches(sharded, root / "features", partition="train")), [])
            with self.assertRaises(ValueError):
                list(iter_conditioning_batches(sharded, root / "features", partition="all"))
            with self.assertRaises(ValueError):
                list(iter_conditioning_batches(sharded, root / "features", partition="test", batch_frames=0))
            shard_path = root / "features" / reference["path"]
            shard_path.write_bytes(shard_bytes.replace(b'"f0Hz":220', b'"f0Hz":221'))
            self.assertEqual(assemble_cli("bad-refresh.json", *shard_option, "--reuse-conditioning").returncode, 2)
            self.assertFalse((root / "bad-refresh.json").exists())
            with self.assertRaises(ValueError):
                list(iter_conditioning_batches(sharded, root / "features", partition="test"))
            shard_path.write_bytes(shard_bytes)
            self.assertEqual(assemble_cli("retry-sharded.json", *shard_option).returncode, 2)
            self.assertFalse((root / "retry-sharded.json").exists())
            self.assertEqual((root / "features" / reference["path"]).read_bytes(), shard_bytes)
            self.assertEqual(assemble_cli("conflict.json", "--conditioning-directory", str(root / "conflict.json")).returncode, 2)
            self.assertFalse((root / "conflict.json").exists())
            acoustic.update(hopSize=8, f0Hz=[220, 221, 222, 223], voiced=[True] * 4)
            self.assertEqual(admit_annotation("chunk-label-admission.json").returncode, 0)
            for key in ("labelConfig", "labelReview"):
                assembly[key]["sha256"] = hashlib.sha256((root / assembly[key]["path"]).read_bytes()).hexdigest()
            result = assemble_cli("chunked.json", "--conditioning-directory", str(root / "chunk-features"))
            self.assertEqual(result.returncode, 3, result.stderr)
            chunked = json.loads((root / "chunked.json").read_bytes())
            batches = list(iter_conditioning_batches(chunked, root / "chunk-features", partition="test", batch_frames=3))
            self.assertEqual([b["frameOffset"] for b in batches], [0, 3])
            self.assertEqual([b["columns"]["f0Hz"] for b in batches], [[220, 221, 222], [223]])
            self.assertEqual([b["columns"]["sourceFrame"] for b in batches], [[0, 8, 16], [24]])
            assembly["labelReview"]["sha256"] = "0" * 64
            self.assertEqual(assemble_cli("tampered-dataset.json").returncode, 2)
            self.assertFalse((root / "tampered-dataset.json").exists())
            assembly["labelReview"]["path"] = "../label-review.json"
            self.assertEqual(assemble_cli("escaped-dataset.json").returncode, 2)
            self.assertFalse((root / "escaped-dataset.json").exists())
            command = [sys.executable, "-m", "tools.voice_model_training", "admit", str(root / "config.json"),
                bound["configurationSha256"], str(root), str(root / "admission.json"), "--review", str(root / "review.json"),
                "--review-sha256", hashlib.sha256(review_bytes).hexdigest(), "--policy", str(root / "policy.json"),
                "--policy-file-sha256", hashlib.sha256(policy_bytes).hexdigest(), "--trusted-policy-sha256", anchor]
            self.assertEqual(subprocess.run(command, capture_output=True, timeout=15).returncode, 0)
            published = (root / "admission.json").read_bytes()
            self.assertTrue(json.loads(published)["sourcePermissionsAdmitted"])
            self.assertEqual(subprocess.run(command, capture_output=True, timeout=15).returncode, 2)
            self.assertEqual((root / "admission.json").read_bytes(), published)
            segment["source"]["lineageId"] = "lineage"
            segment_bytes = json.dumps(segment).encode()
            (root / "segment-config.json").write_bytes(segment_bytes)
            derived_command = command.copy()
            derived_command[7] = str(root / "clip-admission.json")
            derived_command += ["--segment-config", str(root / "segment-config.json"),
                "--segment-sha256", hashlib.sha256(segment_bytes).hexdigest(), "--segment-source", str(root / "audio.wav"),
                "--segment-output", str(root / "cli-clip")]
            self.assertEqual(subprocess.run(derived_command, capture_output=True, timeout=15).returncode, 0)
            clip_report = json.loads((root / "clip-admission.json").read_bytes())
            self.assertEqual(clip_report["sourceSha256"], hashlib.sha256((root / "cli-clip/audio.wav").read_bytes()).hexdigest())
            derived_command[7] = str(root / "clip-readmission.json")
            derived_command.append("--resume-segment")
            self.assertEqual(subprocess.run(derived_command, capture_output=True, timeout=15).returncode, 0)
            command[7] = str(root / "wrong-anchor.json")
            command[-1] = "0" * 64
            self.assertEqual(subprocess.run(command, capture_output=True, timeout=15).returncode, 2)
            self.assertFalse((root / "wrong-anchor.json").exists())
            permission["permissions"]["modelTraining"] = False
            bound = capture()
            with self.assertRaises(ValueError): admit()
            permission["permissions"]["modelTraining"] = True
            bound = capture()
            (root / "audio.wav").write_bytes(audio[:-1])
            with self.assertRaises(ValueError): admit()


if __name__ == "__main__":
    unittest.main()
