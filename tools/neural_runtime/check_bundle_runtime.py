"""CLI preparation -> offline inspection -> child-side reload -> native fixture inference."""
import hashlib
import json
import math
import struct
from pathlib import Path
import subprocess
import sys
import tempfile
import onnx

from check_paired_runtime import graphs
from inspect_bundle import inspect_bundle


def main():
    cli, runtime = (str(Path(path).resolve()) for path in sys.argv[1:3])
    with tempfile.TemporaryDirectory(prefix="seam-bundle-runtime-") as directory:
        root = Path(directory)
        acoustic, vocoder = graphs(steps_layout="vector1", vocoder_output="waveform")
        feature = dict(sampleRate=48000, hopSize=256, bins=80, layout="BTF",
                       amplitudeScale="ln-amplitude", multiplier=1.0, offset=0.0,
                       minimumHz=40.0, maximumHz=16000.0, fftSize=2048, windowSize=1024,
                       melFrequencyScale="slaney")
        configuration = dict(formatId="com.project-seam.neural-bundle-configuration", schemaVersion=3,
                             maximumFrames=48000, stepsLayout="vector1", vocoderOutput="waveform",
                             acousticFeatures=feature, vocoderFeatures=feature)
        (root / "acoustic").write_bytes(acoustic)
        (root / "vocoder").write_bytes(vocoder)
        (root / "configuration").write_text(json.dumps(configuration))
        source = json.dumps({"SP": 1, "a": 2, "i": 3}).encode()
        (root / "exported-phones.json").write_bytes(source)
        subprocess.run([cli, "convert-neural-vocabulary", str(root / "exported-phones.json"),
                        hashlib.sha256(source).hexdigest(), str(root / "vocabulary")],
                       check=True, capture_output=True, timeout=20)
        prepared = subprocess.run([cli, "prepare-neural-bundle", directory, "fixture", "1", "1048576"],
                                  check=True, capture_output=True, text=True, timeout=20)
        digest = json.loads(prepared.stdout)["manifestSha256"]
        assets = {name: (root / name).read_bytes() for name in ("acoustic", "vocoder", "vocabulary", "configuration")}
        inspect_bundle((root / "manifest.json").read_bytes(), assets, digest)
        arguments = [runtime, "--paired-bundle", directory, "fixture", "1", digest]
        result = subprocess.run(arguments, check=True, capture_output=True, text=True, timeout=20)
        assert json.loads(result.stdout)["status"] == "PAIRED_PROFILE_INFERENCE_ONLY"
        if "--native-inspection" in sys.argv[3:]:
            invalid_root = root / "invalid-graph"
            invalid_root.mkdir()
            invalid_model = onnx.load_model_from_string(acoustic)
            invalid_model.graph.node[0].op_type = "NotAnOnnxOperator"
            for name, data in assets.items():
                (invalid_root / name).write_bytes(invalid_model.SerializeToString() if name == "acoustic" else data)
            invalid_prepared = subprocess.run([cli, "prepare-neural-bundle", str(invalid_root), "fixture", "1", "1048576"],
                                             check=True, capture_output=True, text=True, timeout=20)
            invalid_digest = json.loads(invalid_prepared.stdout)["manifestSha256"]
            rejected = subprocess.run([runtime, "--paired-bundle", str(invalid_root), "fixture", "1", invalid_digest],
                                      capture_output=True, text=True, timeout=20)
            assert rejected.returncode == 7 and "Native frozen graph inspection" in rejected.stderr, rejected
            assert not rejected.stdout
        request_arguments = [runtime, "--paired-request", directory, "fixture", "1", digest]
        count = 731
        metadata = dict(kind="seam-neural-request-v2", requestId=91, modelId="fixture",
                        modelVersion="1", modelContentHash=digest, pronunciationHash="a" * 64,
                        sampleRate=48000, channels=1, frameCount=count,
                        featureKind="f0-dynamics-phonemes",
                        vocabularyHash=hashlib.sha256(assets["vocabulary"]).hexdigest(),
                        vocabularySize=4,
                        phonemes=[dict(tokenId=1, startFrame=0, endFrame=count)])

        def encode(fields, frequency, gain):
            header = json.dumps(fields, sort_keys=True, separators=(",", ":")).encode()
            payload = struct.pack(f"<{count * 2}f", *([frequency] * count + [gain] * count))
            return struct.pack("<4sHBBIQ", b"SNW1", 1, 1, 0, len(header), len(payload)) + header + payload

        for frequency, gain, bundled in ((180.0, 0.25, False), (240.0, 0.75, False), (210.0, 0.5, True)):
            if bundled:
                metadata.update(kind="seam-neural-request-v3", bundleContentHash=digest)
            request = encode(metadata, frequency, gain)
            response = subprocess.run(request_arguments, input=request, capture_output=True, timeout=20)
            assert response.returncode == 0, response.stderr
            magic, version, kind, reserved, size, payload_size = struct.unpack("<4sHBBIQ", response.stdout[:20])
            assert (magic, version, kind, reserved) == (b"SNW1", 1, 2, 0)
            assert len(response.stdout) == 20 + size + payload_size
            reply = json.loads(response.stdout[20:20 + size])
            assert reply["requestId"] == 91 and reply["frameCount"] == count
            assert reply["modelContentHash"] == digest
            assert reply["requestContentHash"] == hashlib.sha256(request).hexdigest()
            assert reply["kind"] == ("seam-neural-response-v3" if bundled else "seam-neural-response-v2")
            if bundled:
                assert reply["bundleContentHash"] == digest
            assert payload_size == count * 4
            pcm = struct.unpack(f"<{count}f", response.stdout[20 + size:])
            expected = (frequency * 0.0011 + 0.01 + 0.003 + 0.001) * gain
            assert all(math.isfinite(sample) and abs(sample - expected) < 1e-6 for sample in pcm)
        for invalid in (request[:-1], request + b"x",
                        encode(dict(metadata, modelContentHash="0" * 64), 180.0, 0.25),
                        encode(dict(metadata, bundleContentHash="0" * 64), 180.0, 0.25),
                        encode(dict(metadata, kind="seam-neural-request-v2"), 180.0, 0.25)):
            rejected = subprocess.run(request_arguments, input=invalid, capture_output=True, timeout=20)
            assert rejected.returncode == 7 and not rejected.stdout, rejected.stderr
        # Parent inspection cannot authorize bytes changed before child loading.
        damaged = bytearray(vocoder)
        damaged[-1] ^= 1
        (root / "vocoder").write_bytes(damaged)
        rejected = subprocess.run(arguments, capture_output=True, text=True, timeout=20)
        assert rejected.returncode == 7, (rejected.returncode, rejected.stderr)
        # Schema 4: a conditioned graph with measured defaults must pass the
        # same paired probe, not just metadata inspection.
        conditioned_root = root / "conditioned"
        conditioned_root.mkdir()
        conditioned_acoustic, conditioned_vocoder = graphs(conditioned=True)
        conditioned_configuration = dict(configuration, schemaVersion=4,
                                         stepsLayout="scalar", vocoderOutput="audio",
                                         conditioningDefaults={"breathiness": {"a": 0.4}})
        (conditioned_root / "acoustic").write_bytes(conditioned_acoustic)
        (conditioned_root / "vocoder").write_bytes(conditioned_vocoder)
        (conditioned_root / "configuration").write_text(json.dumps(conditioned_configuration))
        (conditioned_root / "vocabulary").write_bytes(assets["vocabulary"])
        conditioned_prepared = subprocess.run(
            [cli, "prepare-neural-bundle", str(conditioned_root), "fixture", "1", "1048576"],
            check=True, capture_output=True, text=True, timeout=20)
        conditioned_digest = json.loads(conditioned_prepared.stdout)["manifestSha256"]
        conditioned_assets = {name: (conditioned_root / name).read_bytes()
                              for name in ("acoustic", "vocoder", "vocabulary", "configuration")}
        inspect_bundle((conditioned_root / "manifest.json").read_bytes(),
                       conditioned_assets, conditioned_digest)
        conditioned_result = subprocess.run(
            [runtime, "--paired-bundle", str(conditioned_root), "fixture", "1", conditioned_digest],
            check=True, capture_output=True, text=True, timeout=20)
        assert json.loads(conditioned_result.stdout)["status"] == "PAIRED_PROFILE_INFERENCE_ONLY"
        # A graph that differs only by declared input order is valid ONNX but
        # outside the admitted profile: the worker binds positionally, so both
        # offline and native inspection must refuse the permutation. This is
        # the exact divergence the first conditioned export produced.
        permuted_model = onnx.load_model_from_string(conditioned_acoustic)
        swapped = [permuted_model.graph.input[index] for index in (0, 1, 2, 4, 3)]
        del permuted_model.graph.input[:]
        permuted_model.graph.input.extend(swapped)
        permuted_acoustic = permuted_model.SerializeToString()
        permuted_root = root / "permuted"
        permuted_root.mkdir()
        (permuted_root / "acoustic").write_bytes(permuted_acoustic)
        (permuted_root / "vocoder").write_bytes(conditioned_vocoder)
        (permuted_root / "configuration").write_text(json.dumps(conditioned_configuration))
        (permuted_root / "vocabulary").write_bytes(assets["vocabulary"])
        permuted_prepared = subprocess.run(
            [cli, "prepare-neural-bundle", str(permuted_root), "fixture", "1", "1048576"],
            capture_output=True, text=True, timeout=20)
        # Preparation is metadata-level and must succeed; the rejections under
        # test are the order checks in offline and native inspection, not any
        # incidental preparation failure.
        assert permuted_prepared.returncode == 0, permuted_prepared.stderr
        permuted_digest = json.loads(permuted_prepared.stdout)["manifestSha256"]
        try:
            inspect_bundle((permuted_root / "manifest.json").read_bytes(),
                           {name: (permuted_root / name).read_bytes()
                            for name in ("acoustic", "vocoder", "vocabulary", "configuration")},
                           permuted_digest)
        except ValueError:
            pass
        else:
            raise AssertionError("Offline inspection accepted a permuted input order")
        permuted_result = subprocess.run(
            [runtime, "--paired-bundle", str(permuted_root), "fixture", "1", permuted_digest],
            capture_output=True, text=True, timeout=20)
        assert permuted_result.returncode != 0, permuted_result.stdout
    print("CLI-prepared bundle passed native child reload and inference; changed bytes rejected. No learned singing claim.")


if __name__ == "__main__":
    main()
