"""Compose a bundle from export receipts, then admit and execute it for real.

The export graphs are deterministic arithmetic ONNX fixtures, not a learned
singer. The check proves the composition chain: receipt-bound graphs -> declared
configuration and vocabulary -> canonical manifest -> production worker
admission -> paired inference -> response bound to the request.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from check_paired_runtime import graphs
from inspect_graph import inspect_bytes


PROFILE = dict(profileId="seam-full-hop-slaney-v1", sampleRate=48000, fftSize=1024,
               windowSize=1024, hopSize=256, bins=80, minimumHz=20, maximumHz=24000,
               tailPadding="zero-to-whole-hop", boundaryPadding="reflect-fft-minus-hop",
               window="periodic-hann", spectrum="unnormalized-magnitude",
               melNormalization="slaney-area", melFrequencyScale="slaney",
               amplitudeScale="ln-amplitude", floor=1e-5, layout="TF", dtype="float32-le")
FRAMES = 731
TOKENS = ["<PAD>", "SP", "aa1", "k"]


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def write_exports(root, acoustic, vocoder, *, acoustic_profile=None, vocoder_profile=None,
                  vocabulary=None, smoke=True, conditioned=False):
    acoustic_profile = PROFILE if acoustic_profile is None else acoustic_profile
    vocoder_profile = PROFILE if vocoder_profile is None else vocoder_profile
    for name, graph_bytes, profile, graph_field, sha_field, bytes_field in (
            ("acoustic-export", acoustic, acoustic_profile, "acousticPath", "acousticSha256", "acousticBytes"),
            ("vocoder-export", vocoder, vocoder_profile, "vocoderPath", "vocoderSha256", "vocoderBytes")):
        directory = root / name
        directory.mkdir(exist_ok=True)
        graph_name = "graph.onnx"
        (directory / graph_name).write_bytes(graph_bytes)
        is_acoustic = name.startswith("acoustic")
        report = dict(formatId="com.project-seam.acoustic-export" if is_acoustic
                      else "com.project-seam.vocoder-export",
                      schemaVersion=2 if is_acoustic and conditioned else 1,
                      checkpointReceiptSha256=hashlib.sha256(name.encode()).hexdigest(),
                      checkpointSha256=hashlib.sha256((name + "-checkpoint").encode()).hexdigest(),
                      profile=profile, profileSha256=hashlib.sha256(canonical(profile)).hexdigest(),
                      sourceRightsRevalidated=False, modelBundleAdmitted=False,
                      singerQualified=False, releaseEligible=False)
        report[graph_field] = graph_name
        report[sha_field] = hashlib.sha256(graph_bytes).hexdigest()
        report[bytes_field] = len(graph_bytes)
        if is_acoustic:
            report["vocabulary"] = list(TOKENS if vocabulary is None else vocabulary)
            report["runtimeSmokePassed"] = smoke
            report["revision"] = "336cf01b57f2ad44c6b37a79cf33993043291759"
            if conditioned:
                report.update(
                    conditioningRevision=2,
                    conditioningControls=[dict(name="breathiness", type="float32", shape=[1, "T"],
                        unit="normalized-periodic-aperiodic-balance", minimum=0, maximum=1,
                        default=0, supported=True)],
                    inspection=inspect_bytes(graph_bytes),
                    encoderRuntimeCheck=dict(breathinessConditionEffectPassed=True,
                                             maximumBreathinessConditionEffect=.25),
                    deploymentBridgeCheck=dict(breathinessConditionEffectPassed=True,
                                               maximumBreathinessMelEffect=.1))
        else:
            report["trainingRevision"] = "4d0889c4c180c75ad3000cc565864656344f8190"
            report["deploymentRevision"] = "336cf01b57f2ad44c6b37a79cf33993043291759"
        (directory / "export.json").write_bytes(canonical(report))
    return root / "acoustic-export", root / "vocoder-export"


def compose(script, acoustic, vocoder, output, *extra):
    return subprocess.run([sys.executable, str(script), "--acoustic-export", str(acoustic),
                           "--vocoder-export", str(vocoder), "--output", str(output),
                           "--maximum-frames", "48000", *extra],
                          capture_output=True, text=True, timeout=60)


def main():
    worker, cli = (str(Path(path).resolve()) for path in sys.argv[1:3])
    script = Path(__file__).resolve().parent.parent / "voice_model_training" / "prepare_bundle.py"
    acoustic_graph, vocoder_graph = graphs(steps_layout="scalar", vocoder_output="audio")
    with tempfile.TemporaryDirectory(prefix="seam-bundle-preparation-") as directory:
        root = Path(directory)
        acoustic, vocoder = write_exports(root, acoustic_graph, vocoder_graph)
        composed = compose(script, acoustic, vocoder, root / "bundle")
        assert composed.returncode == 0, composed.stderr
        report = json.loads(composed.stdout)
        bundle = root / "bundle"
        # Every declared asset must exist with the reported identity.
        for asset in report["assets"]:
            payload = (bundle / asset["name"]).read_bytes()
            assert len(payload) == asset["bytes"]
            assert hashlib.sha256(payload).hexdigest() == asset["sha256"], asset
        manifest = (bundle / "manifest.json").read_bytes()
        assert hashlib.sha256(manifest).hexdigest() == report["manifestSha256"]
        # The native CLI must derive identical manifest bytes for the same assets.
        # It refuses to overwrite an existing manifest, so it composes its own copy.
        native_bundle = root / "native-bundle"
        native_bundle.mkdir()
        for name in ("acoustic", "vocoder", "vocabulary", "configuration"):
            (native_bundle / name).write_bytes((bundle / name).read_bytes())
        native = subprocess.run([cli, "prepare-neural-bundle", str(native_bundle), "fixture", "1", "1048576"],
                                capture_output=True, text=True, timeout=20)
        assert native.returncode == 0, native.stderr
        assert json.loads(native.stdout)["manifestSha256"] == report["manifestSha256"], \
            (native.stdout, report["manifestSha256"])
        assert (native_bundle / "manifest.json").read_bytes() == manifest

        # Explicit silence aliases preserve trained IDs and pass native admission.
        # Fixture models establish packaging mechanics, not learned silence quality.
        for symbol in ("pau", "sil", "SP"):
            silence_root = root / ("silence-export-" + symbol)
            silence_root.mkdir()
            silence_tokens = ["<PAD>", "aa1", symbol, "k"]
            sa, sv = write_exports(silence_root, acoustic_graph, vocoder_graph,
                                   vocabulary=silence_tokens)
            unaliased = root / ("silence-unaliased-" + symbol)
            unchanged = compose(script, sa, sv, unaliased)
            assert unchanged.returncode == 0, unchanged.stderr
            original = json.loads((unaliased / "vocabulary").read_bytes())
            assert original["tokens"] == silence_tokens and original["aliases"] == {}
            sb = root / ("silence-bundle-" + symbol)
            result = compose(script, sa, sv, sb, "--silence-phone", symbol)
            assert result.returncode == 0, result.stderr
            value = json.loads((sb / "vocabulary").read_bytes())
            assert value["tokens"] == silence_tokens
            assert value["aliases"] == ({} if symbol == "SP" else {"SP": 2})
            if symbol == "SP":
                assert (sb / "vocabulary").read_bytes() == (unaliased / "vocabulary").read_bytes()
            assert json.loads(result.stdout)["silencePhone"] == symbol
            native_silence = root / ("silence-native-" + symbol)
            native_silence.mkdir()
            for name in ("acoustic", "vocoder", "vocabulary", "configuration"):
                (native_silence / name).write_bytes((sb / name).read_bytes())
            admitted = subprocess.run(
                [cli, "prepare-neural-bundle", str(native_silence), "fixture", "1", "1048576"],
                capture_output=True, text=True, timeout=20)
            assert admitted.returncode == 0, admitted.stderr
            assert (native_silence / "manifest.json").read_bytes() == (sb / "manifest.json").read_bytes()
        for label, tokens, selection in (
                ("missing", TOKENS, "pau"),
                ("conflict", ["<PAD>", "SP", "pau"], "pau"),
                ("sung", TOKENS, "aa1"),
                ("padding", TOKENS, "<PAD>")):
            invalid_root = root / ("invalid-silence-" + label)
            invalid_root.mkdir()
            sa, sv = write_exports(invalid_root, acoustic_graph, vocoder_graph, vocabulary=tokens)
            refused_output = root / ("refused-silence-" + label)
            result = compose(script, sa, sv, refused_output, "--silence-phone", selection)
            assert result.returncode == 2, result.stdout
            assert not refused_output.exists()

        # An installed bundle also carries the resource record a project saves, so
        # the identity can be resolved back to these bytes. The record's digest
        # must be the manifest digest, and the manifest must not change for it.
        recorded = root / "recorded"
        with_record = compose(script, acoustic, vocoder, recorded, "--resource-id",
                              "fixture.voice", "--resource-version", "1")
        assert with_record.returncode == 0, with_record.stderr
        assert (recorded / "manifest.json").read_bytes() == manifest
        assert json.loads(with_record.stdout)["manifestSha256"] == report["manifestSha256"]
        resource = json.loads((recorded / "resource.json").read_bytes())
        assert resource == dict(contentHash=report["manifestSha256"],
                                formatId="com.project-seam.neural-resource",
                                id="fixture.voice", schemaVersion=1, version="1"), resource
        # A half-specified identity is refused; an installed bundle is not a place
        # for a guessed version.
        refused = compose(script, acoustic, vocoder, root / "half-record", "--resource-id", "fixture.voice")
        assert refused.returncode == 2 and "together" in refused.stderr, refused.stderr

        # The composed bundle must be admissible and executable by the real worker.
        digest = report["manifestSha256"]
        vocabulary_hash = hashlib.sha256((bundle / "vocabulary").read_bytes()).hexdigest()
        metadata = dict(kind="seam-neural-request-v3", requestId=91, modelId="fixture",
                        modelVersion="1", modelContentHash=digest, pronunciationHash="a" * 64,
                        sampleRate=48000, channels=1, frameCount=FRAMES,
                        featureKind="f0-dynamics-phonemes", vocabularyHash=vocabulary_hash,
                        vocabularySize=len(TOKENS),
                        phonemes=[dict(tokenId=2, startFrame=0, endFrame=FRAMES)],
                        bundleContentHash=digest)
        header = json.dumps(metadata, sort_keys=True, separators=(",", ":")).encode()
        payload = struct.pack(f"<{FRAMES * 2}f", *([210.0] * FRAMES + [0.5] * FRAMES))
        request = struct.pack("<4sHBBIQ", b"SNW1", 1, 1, 0, len(header), len(payload)) + header + payload
        run = subprocess.run([worker, "--seam-neural-worker-v3", str(bundle), "fixture", "1",
                              digest, "1048576", "10"], input=request, capture_output=True, timeout=60)
        assert run.returncode == 0, run.stderr
        magic, version, kind, reserved, size, payload_size = struct.unpack("<4sHBBIQ", run.stdout[:20])
        assert (magic, version, kind, reserved) == (b"SNW1", 1, 2, 0)
        reply = json.loads(run.stdout[20:20 + size])
        assert reply["kind"] == "seam-neural-response-v3" and reply["frameCount"] == FRAMES
        assert reply["bundleContentHash"] == digest
        assert reply["requestContentHash"] == hashlib.sha256(request).hexdigest()
        assert payload_size == FRAMES * 4
        pcm = struct.unpack(f"<{FRAMES}f", run.stdout[20 + size:])
        expected = (210.0 * 0.0011 + 0.02 + 0.003 + 0.001) * 0.5
        assert all(abs(sample - expected) < 1e-6 for sample in pcm)

        # The revision-2 bundle path carries breathiness through its receipt, graph interface,
        # native admission, request plane, hop conversion and production worker execution.
        conditioned_root = root / "conditioned-export"
        conditioned_root.mkdir()
        conditioned_acoustic_graph, conditioned_vocoder_graph = graphs(conditioned=True)
        conditioned_acoustic, conditioned_vocoder = write_exports(
            conditioned_root, conditioned_acoustic_graph, conditioned_vocoder_graph, conditioned=True)
        conditioned_bundle = root / "conditioned-bundle"
        composed = compose(script, conditioned_acoustic, conditioned_vocoder, conditioned_bundle)
        assert composed.returncode == 0, composed.stderr
        conditioned_report = json.loads(composed.stdout)
        assert conditioned_report["conditioningRevision"] == 2
        assert conditioned_report["conditioningControls"][0]["name"] == "breathiness"
        conditioned_digest = conditioned_report["manifestSha256"]
        conditioned_metadata = dict(metadata, requestId=92, modelContentHash=conditioned_digest,
                                    bundleContentHash=conditioned_digest, hasBreathiness=True)
        conditioned_header = json.dumps(
            conditioned_metadata, sort_keys=True, separators=(",", ":")).encode()
        conditioned_payload = struct.pack(
            f"<{FRAMES * 3}f", *([210.0] * FRAMES + [0.5] * FRAMES + [0.75] * FRAMES))
        conditioned_request = (struct.pack("<4sHBBIQ", b"SNW1", 1, 1, 0,
                                           len(conditioned_header), len(conditioned_payload))
                               + conditioned_header + conditioned_payload)
        conditioned_run = subprocess.run(
            [worker, "--seam-neural-worker-v3", str(conditioned_bundle), "fixture", "1",
             conditioned_digest, "1048576", "10"], input=conditioned_request,
            capture_output=True, timeout=60)
        assert conditioned_run.returncode == 0, conditioned_run.stderr
        _, _, _, _, conditioned_size, conditioned_payload_size = struct.unpack(
            "<4sHBBIQ", conditioned_run.stdout[:20])
        conditioned_reply = json.loads(conditioned_run.stdout[20:20 + conditioned_size])
        assert conditioned_reply["requestContentHash"] == hashlib.sha256(conditioned_request).hexdigest()
        assert conditioned_payload_size == FRAMES * 4
        conditioned_pcm = struct.unpack(
            f"<{FRAMES}f", conditioned_run.stdout[20 + conditioned_size:])
        conditioned_expected = expected + (((FRAMES + 255) // 256) * 0.75 * 0.02) * 0.5
        assert all(abs(sample - conditioned_expected) < 1e-6 for sample in conditioned_pcm)

        # Composition refusals: each one is a real declaration fault.
        tampered = root / "tampered"
        (acoustic / "graph.onnx").write_bytes(acoustic_graph + b"changed")
        refused = compose(script, acoustic, vocoder, tampered)
        assert refused.returncode == 2 and "differs from its recorded identity" in refused.stderr
        (acoustic / "graph.onnx").write_bytes(acoustic_graph)
        mismatch = root / "mismatch-export"
        other_profile = dict(PROFILE, maximumHz=16000)
        mismatch.mkdir()
        other_vocoder = write_exports(mismatch, acoustic_graph, vocoder_graph,
                                      acoustic_profile=PROFILE, vocoder_profile=other_profile)[1]
        refused = compose(script, acoustic, other_vocoder, root / "mismatch")
        assert refused.returncode == 2 and "acoustic profile" in refused.stderr
        unsupported = root / "unsupported-export"
        unsupported.mkdir()
        bad_profile = dict(PROFILE, bins=128)
        bad_acoustic, bad_vocoder = write_exports(unsupported, acoustic_graph, vocoder_graph,
                                                  acoustic_profile=bad_profile, vocoder_profile=bad_profile)
        refused = compose(script, bad_acoustic, bad_vocoder, root / "unsupported")
        assert refused.returncode == 2 and "unsupported acoustic profile" in refused.stderr
        collision = root / "collision-export"
        collision.mkdir()
        bad_vocabulary = ["<PAD>", "SP", "aa1", "<PAD>"]
        col_acoustic, col_vocoder = write_exports(collision, acoustic_graph, vocoder_graph,
                                                  vocabulary=bad_vocabulary)
        refused = compose(script, col_acoustic, col_vocoder, root / "collision")
        assert refused.returncode == 2 and "padding" in refused.stderr, refused.stderr
        no_smoke = root / "no-smoke-export"
        no_smoke.mkdir()
        ns_acoustic, ns_vocoder = write_exports(no_smoke, acoustic_graph, vocoder_graph, smoke=False)
        refused = compose(script, ns_acoustic, ns_vocoder, root / "no-smoke")
        assert refused.returncode == 2 and "runtime smoke" in refused.stderr
        refused = compose(script, acoustic, vocoder, bundle)
        assert refused.returncode == 2 and "new directory" in refused.stderr
        refused = compose(script, acoustic, vocoder, root / "bad-steps", "--steps-layout", "scalar",
                          "--maximum-frames", "0")
        assert refused.returncode == 2, refused.stderr
    print("Export receipts composed into an admitted bundle; native manifest agreement and "
          "production worker execution verified. Arithmetic fixture graphs only.")


if __name__ == "__main__":
    main()
