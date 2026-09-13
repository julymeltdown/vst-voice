"""Drive the production neural worker with admitted graphs, not a transport probe.

The fixture graphs are deterministic arithmetic, not a learned singer. They prove
that the worker admits its own bundle bytes, executes acoustic then vocoder
inference, binds its response to the exact request, and refuses anything the
transport fixture would have accepted.
"""
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from check_paired_runtime import graphs


def run(worker, arguments, request):
    return subprocess.run([worker, *arguments], input=request, capture_output=True, timeout=60)


def main():
    worker, cli = (str(Path(path).resolve()) for path in sys.argv[1:3])
    with tempfile.TemporaryDirectory(prefix="seam-production-worker-") as directory:
        root = Path(directory)
        acoustic, vocoder = graphs(steps_layout="scalar", vocoder_output="audio")
        feature = dict(sampleRate=48000, hopSize=256, bins=80, layout="BTF",
                       amplitudeScale="ln-amplitude", multiplier=1.0, offset=0.0,
                       minimumHz=40.0, maximumHz=16000.0, fftSize=2048, windowSize=1024,
                       melFrequencyScale="slaney")
        configuration = dict(formatId="com.project-seam.neural-bundle-configuration", schemaVersion=3,
                             maximumFrames=48000, stepsLayout="scalar", vocoderOutput="audio",
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
        vocabulary_hash = hashlib.sha256((root / "vocabulary").read_bytes()).hexdigest()
        launch = ["--seam-neural-worker-v2", directory, "fixture", "1", digest, "1048576"]
        count = 731
        metadata = dict(kind="seam-neural-request-v3", requestId=91, modelId="fixture",
                        modelVersion="1", modelContentHash=digest, pronunciationHash="a" * 64,
                        sampleRate=48000, channels=1, frameCount=count,
                        featureKind="f0-dynamics-phonemes", vocabularyHash=vocabulary_hash,
                        vocabularySize=4, phonemes=[dict(tokenId=2, startFrame=0, endFrame=count)],
                        bundleContentHash=digest)

        def encode(fields, frequency, gain):
            header = json.dumps(fields, sort_keys=True, separators=(",", ":")).encode()
            payload = struct.pack(f"<{count * 2}f", *([frequency] * count + [gain] * count))
            return struct.pack("<4sHBBIQ", b"SNW1", 1, 1, 0, len(header), len(payload)) + header + payload

        frequency, gain = 210.0, 0.5
        request = encode(metadata, frequency, gain)
        accepted = run(worker, launch, request)
        assert accepted.returncode == 0, accepted.stderr
        # The frame type nibble is 2 for every response; metadata kind carries v3.
        magic, version, kind, reserved, size, payload_size = struct.unpack("<4sHBBIQ", accepted.stdout[:20])
        assert (magic, version, kind, reserved) == (b"SNW1", 1, 2, 0)
        assert len(accepted.stdout) == 20 + size + payload_size
        reply = json.loads(accepted.stdout[20:20 + size])
        assert reply["kind"] == "seam-neural-response-v3", reply
        assert reply["requestId"] == 91 and reply["frameCount"] == count
        assert reply["modelContentHash"] == digest and reply["bundleContentHash"] == digest
        assert reply["requestContentHash"] == hashlib.sha256(request).hexdigest()
        assert reply["backendId"].startswith("seam-neural-worker-")
        assert reply["sampleRate"] == 48000 and reply["channels"] == 1
        assert payload_size == count * 4
        pcm = struct.unpack(f"<{count}f", accepted.stdout[20 + size:])
        # Acoustic mel term + vocoder pitch term, then exactly one dynamics gain.
        expected = (frequency * 0.0011 + 0.02 + 0.003 + 0.001) * gain
        assert all(math.isfinite(sample) and abs(sample - expected) < 1e-6 for sample in pcm), pcm[:4]
        assert any(abs(sample) > 0 for sample in pcm)

        # The v1 transport fixture contract is not this executable's contract.
        for arguments in (["--seam-neural-worker-v1"], ["--seam-neural-worker-v2", directory, "fixture"],
                          ["--seam-neural-worker-v2", directory, "fixture", "1", digest, "0"],
                          ["--seam-neural-worker-v2", directory, "fixture", "1", digest, "nonsense"]):
            rejected = run(worker, arguments, request)
            assert not rejected.stdout and rejected.returncode in (2, 3), (arguments, rejected.returncode)
        wrong_digest = run(worker, ["--seam-neural-worker-v2", directory, "fixture", "1", "0" * 64, "1048576"], request)
        assert not wrong_digest.stdout and wrong_digest.returncode == 4, wrong_digest.stderr
        # The launch identity is an expectation: a different model id loads the
        # same bytes but cannot authorize the request that names the real model.
        wrong_identity = run(worker, ["--seam-neural-worker-v2", directory, "other", "1", digest, "1048576"], request)
        assert not wrong_identity.stdout and wrong_identity.returncode == 7, wrong_identity.stderr
        # Bytes changed after preparation are rejected by the child's own reload.
        damaged = bytearray(vocoder)
        damaged[-1] ^= 1
        (root / "vocoder").write_bytes(damaged)
        changed = run(worker, launch, request)
        assert not changed.stdout and changed.returncode == 4, changed.stderr
        (root / "vocoder").write_bytes(vocoder)

        # A bundle whose graphs are not admissible fails graph admission; the
        # transport fixture accepts this same shape of bundle without executing it.
        plain = root / "plain"
        plain.mkdir()
        for name in ("acoustic", "vocoder"):
            (plain / name).write_bytes(b"uninspected graph fixture")
        (plain / "configuration").write_bytes((root / "configuration").read_bytes())
        (plain / "vocabulary").write_bytes((root / "vocabulary").read_bytes())
        plain_prepared = subprocess.run([cli, "prepare-neural-bundle", str(plain), "fixture", "1", "1048576"],
                                       check=True, capture_output=True, text=True, timeout=20)
        plain_digest = json.loads(plain_prepared.stdout)["manifestSha256"]
        uninspected = run(worker, ["--seam-neural-worker-v2", str(plain), "fixture", "1", plain_digest, "1048576"],
                          encode(dict(metadata, modelContentHash=plain_digest, bundleContentHash=plain_digest),
                                 frequency, gain))
        assert not uninspected.stdout and uninspected.returncode == 8, uninspected.stderr
        assert "admission" in uninspected.stderr.decode()

        # Identity, protocol and output-range faults must not publish a response.
        faults = [(dict(metadata, modelContentHash="0" * 64), 7), (dict(metadata, vocabularyHash="0" * 64), 7),
                  (dict(metadata, vocabularySize=3), 7), (dict(metadata, kind="seam-neural-request-v2"), 7),
                  (dict(metadata, sampleRate=44100), 7), (dict(metadata, frameCount=count + 1), 7)]
        for fields, code in faults:
            rejected = run(worker, launch, encode(fields, frequency, gain))
            assert not rejected.stdout and rejected.returncode == code, (fields, rejected.returncode, rejected.stderr)
        for invalid in (request[:-1], request + b"extra"):
            rejected = run(worker, launch, invalid)
            assert not rejected.stdout and rejected.returncode == 7, rejected.returncode
        # Dynamics are applied once in the worker: the padded sample is already
        # about 0.255, so a protocol-legal gain of 4.0 exceeds normalized PCM.
        loud = run(worker, launch, encode(metadata, frequency, 4.0))
        assert not loud.stdout and loud.returncode == 8, loud.stderr
        assert b"dynamics" in loud.stderr
        quiet = run(worker, launch, encode(metadata, frequency, 0.0))
        assert quiet.returncode == 0 and not any(struct.unpack(f"<{count}f", quiet.stdout[20 + size:]))

        # Two requests on one invocation are impossible: the worker answers once.
        doubled = run(worker, launch, request + request)
        assert not doubled.stdout and doubled.returncode == 7, doubled.returncode
    print("Production worker admitted real bundle bytes, produced bound PCM, and rejected "
          "legacy, changed and inadmissible inputs. Arithmetic fixture only; no learned singer claim.")


if __name__ == "__main__":
    main()
