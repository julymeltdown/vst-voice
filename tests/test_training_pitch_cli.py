"""Native feature extraction integration; diagnostic audio, not singer approval."""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import wave
import math
import struct
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.voice_model_training.features import apply_pitch_features
from tools.voice_model_training.native_features import extract_pitch
from tools.voice_model_training.__main__ import admit_segment
from tools.voice_model_training.permissions import TRAINING_PERMISSIONS
from tools.phase13a.update_contract import ed25519_sign, ed25519_public_key
from tools.public_release.crypto_validation import canonical_signing_payload
from tools.public_release.contracts import sha256_json
import base64


def main():
    executable = sys.argv[1]
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "source.wav"
        for count in (1, 256, 257, 4097):
            encoded = io.BytesIO()
            with wave.open(encoded, "wb") as writer:
                writer.setnchannels(1); writer.setsampwidth(2); writer.setframerate(48000)
                writer.writeframes(bytes(count * 2))
            original = encoded.getvalue()
            path.write_bytes(original)
            result = subprocess.run([executable, "extract-pitch", str(path)], capture_output=True, timeout=20)
            assert result.returncode == 0, result.stderr
            value = json.loads(result.stdout)
            assert extract_pitch(Path(executable), path) == value
            assert value["sourceSha256"] == hashlib.sha256(original).hexdigest()
            assert value["frameCount"] == count
            assert len(value["pitchFrames"]) == (count + 255) // 256
            assert all(not f["voiced"] and f["f0Hz"] == 0 for f in value["pitchFrames"])
            assert not value["trainingAdmitted"]
            assert path.read_bytes() == original
            label = dict(sourceId="fixture", frameCount=count, hopSize=256,
                phonemes=[dict(symbol="SP", startFrame=0, endFrame=count, confidence=1)],
                f0Hz=[220] * ((count + 255) // 256), voiced=[True] * ((count + 255) // 256), reviewRevision="old")
            converted = apply_pitch_features(label, value, source_sha256=hashlib.sha256(original).hexdigest(),
                                            sample_rate=48000, vocabulary={"SP"}, minimum_confidence=0.8)
            assert converted["f0Hz"] == [0] * len(value["pitchFrames"])
            assert converted["reviewRevision"] is None and label["reviewRevision"] == "old"
            if count == 257:
                configuration = dict(formatId="com.project-seam.training-pitch-refresh-config", schemaVersion=1,
                    source=dict(sourceId="fixture", songId="song", sessionId="session", lineageId="lineage",
                        path="source.wav", sourceSha256=hashlib.sha256(original).hexdigest()),
                    sampleRate=48000, label=label, vocabulary=["SP"], minimumConfidence=0.8)
                payload = json.dumps(configuration).encode()
                config_path = Path(directory) / "refresh.json"; config_path.write_bytes(payload)
                output = Path(directory) / "refreshed.json"
                command = [sys.executable, "-m", "tools.voice_model_training", "refresh-pitch", str(config_path),
                    hashlib.sha256(payload).hexdigest(), directory, str(Path(executable).resolve()), str(output)]
                repo = Path(__file__).resolve().parents[1]
                completed = subprocess.run(command, cwd=repo, capture_output=True, timeout=30)
                assert completed.returncode == 0, completed.stderr
                captured = output.read_bytes()
                refreshed = json.loads(captured)
                assert refreshed["label"]["f0Hz"] == [0, 0] and refreshed["label"]["reviewRevision"] is None
                assert refreshed["features"] == value
                assert subprocess.run(command, cwd=repo, capture_output=True, timeout=30).returncode == 2
                assert output.read_bytes() == captured
                crop_config = dict(formatId="com.project-seam.voice-training-segment-config", schemaVersion=2,
                    source={k: configuration["source"][k] for k in ("sourceId", "songId", "sessionId", "lineageId", "sourceSha256")},
                    sampleRate=48000, segmentId="offgrid", startFrame=1, endFrame=257, vocabulary=["SP"], minimumConfidence=0.8,
                    parentLabel=dict(sourceSha256=configuration["source"]["sourceSha256"],
                                     audioSha256=refreshed["audioSha256"], label=label))
                payload = json.dumps(crop_config).encode(); config_path.write_bytes(payload)
                crop_output = Path(directory) / "offgrid"
                command = [sys.executable, "-m", "tools.voice_model_training", "segment", str(config_path),
                    hashlib.sha256(payload).hexdigest(), str(path), str(crop_output),
                    "--fresh-pitch-extractor", str(Path(executable).resolve())]
                completed = subprocess.run(command, cwd=repo, capture_output=True, timeout=30)
                assert completed.returncode == 0, completed.stderr
                evidence = b"fixture only, not real source authorization"
                (Path(directory) / "rights.txt").write_bytes(evidence)
                permission = dict(formatId="com.project-seam.training-permission-config", schemaVersion=1,
                    sampleRate=48000, sources=[configuration["source"]], evidence={"rights": "rights.txt"},
                    manifest=dict(formatId="com.project-seam.training-permission-manifest", schemaVersion=1,
                        sources=[dict(sourceId="fixture", sourceSha256=hashlib.sha256(original).hexdigest(), identityId="fixture-singer",
                            kind="PROCEDURAL_SYNTHESIS", evidenceId="rights", evidenceSha256=hashlib.sha256(evidence).hexdigest(),
                            reviewRevision="fixture", permissions=dict.fromkeys(TRAINING_PERMISSIONS, True))]))
                permission_bytes = json.dumps(permission).encode()
                permission_path = Path(directory) / "permission.json"; permission_path.write_bytes(permission_bytes)
                seed = bytes(range(32))
                policy = dict(policyVersion="seam-training-review-1", algorithm="Ed25519", requiredRoles=["training-rights-reviewer"],
                    trustedKeys=[dict(keyId="fixture", role="training-rights-reviewer", signerId="fixture",
                        publicKey=base64.b64encode(ed25519_public_key(seed)).decode())])
                anchor = sha256_json(policy)
                review = dict(formatId="com.project-seam.training-rights-review", schemaVersion=1,
                    policyVersion=policy["policyVersion"], policySha256=anchor, algorithm="Ed25519", keyId="fixture", signerId="fixture",
                    configurationSha256=hashlib.sha256(permission_bytes).hexdigest(), decision="APPROVE_TRAINING_SCOPES", issuedAt=100, expiresAt=200)
                review["signature"] = base64.b64encode(ed25519_sign(canonical_signing_payload(review, "recordSha256"), seed)).decode()
                review["recordSha256"] = sha256_json(review)
                admission = admit_segment(permission_path, review["configurationSha256"], Path(directory),
                    segment_config=config_path, segment_hash=hashlib.sha256(payload).hexdigest(), source_path=path,
                    output=Path(directory) / "admitted-fresh", review=review, policy=policy,
                    trusted_policy_sha256=anchor, now=150, fresh_pitch_extractor=Path(executable))
                assert admission["sourcePermissionsAdmitted"] and not admission["trainingAdmitted"]
                crop = json.loads((crop_output / "segment.json").read_bytes())
                assert crop["schemaVersion"] == 5 and crop["label"]["label"]["f0Hz"] == [0]
                assert crop["pitchCorrections"]["correctionQueue"][0]["code"] == "zero-padded-pitch-window"
                assert crop["pitchFeatures"]["sourceSha256"] == crop["sourceSha256"]
                batch = dict(formatId="com.project-seam.voice-training-segment-batch", schemaVersion=1,
                    entries=[dict(configuration=config_path.name, configurationSha256=hashlib.sha256(payload).hexdigest(),
                                  source=path.name, outputName="clip")])
                batch_payload = json.dumps(batch).encode()
                batch_path = Path(directory) / "batch.json"; batch_path.write_bytes(batch_payload)
                batch_output, batch_report = Path(directory) / "batch-clips", Path(directory) / "batch-report.json"
                batch_command = [sys.executable, "-m", "tools.voice_model_training", "segment-batch", str(batch_path),
                    hashlib.sha256(batch_payload).hexdigest(), directory, str(batch_output), str(batch_report),
                    "--fresh-pitch-extractor", str(Path(executable).resolve())]
                completed = subprocess.run(batch_command, cwd=repo, capture_output=True, timeout=30)
                assert completed.returncode == 0, completed.stderr
                assert len(json.loads(batch_report.read_bytes())["splitSources"]) == 1
                assert json.loads((batch_output / "clip/segment.json").read_bytes())["schemaVersion"] == 5
                batch_command[8] = str(Path(directory) / "batch-resume.json")
                batch_command.append("--resume")
                completed = subprocess.run(batch_command, cwd=repo, capture_output=True, timeout=30)
                assert completed.returncode == 0, completed.stderr
            try:
                apply_pitch_features(label, value, source_sha256="0" * 64, sample_rate=48000,
                                     vocabulary={"SP"}, minimum_confidence=0.8)
            except ValueError:
                pass
            else:
                raise AssertionError("Wrong source binding accepted")
        for rate, frequency in ((8000, 110), (48000, 220), (48000, 880), (192000, 440)):
            count = rate // 4 + 17
            pcm = b"".join(struct.pack("<h", round(10000 * math.sin(2 * math.pi * frequency * i / rate))) for i in range(count))
            encoded = io.BytesIO()
            with wave.open(encoded, "wb") as writer:
                writer.setnchannels(1); writer.setsampwidth(2); writer.setframerate(rate); writer.writeframes(pcm)
            original = encoded.getvalue(); path.write_bytes(original)
            result = subprocess.run([executable, "extract-pitch", str(path)], capture_output=True, timeout=20)
            assert result.returncode == 0, result.stderr
            value = json.loads(result.stdout)
            interior = [f for f in value["pitchFrames"] if f["sourceFrame"] + value["windowFrames"] <= count]
            assert interior and all(f["voiced"] for f in interior)
            assert all(abs(1200 * math.log2(f["f0Hz"] / frequency)) < 10 for f in interior)
            label = dict(sourceId="tone", frameCount=count, hopSize=256,
                phonemes=[dict(symbol="a", startFrame=0, endFrame=count, confidence=1)],
                f0Hz=[0] * ((count + 255) // 256), voiced=[False] * ((count + 255) // 256), reviewRevision=None)
            converted = apply_pitch_features(label, value, source_sha256=hashlib.sha256(original).hexdigest(),
                                            sample_rate=rate, vocabulary={"a"}, minimum_confidence=0.8)
            assert len(converted["f0Hz"]) == (count + 255) // 256
        path.write_bytes(b"invalid")
        result = subprocess.run([executable, "extract-pitch", str(path)], capture_output=True, timeout=20)
        assert result.returncode != 0 and not result.stdout


if __name__ == "__main__":
    main()
