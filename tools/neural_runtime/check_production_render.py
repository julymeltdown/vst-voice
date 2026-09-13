"""Render a phrase through the normal authoring path with the shipped worker.

The bundle carries arithmetic fixture graphs, so a pass proves that the production
worker executed inside the render path and that its audio was published. It says
nothing about a voice.

The C++ binary is the renderer: this script only prepares the real ONNX bundle it
needs. The first invocation collects the phones the phrase requires, the second
renders with the admitted bundle.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from check_paired_runtime import graphs


def main():
    binary = Path(sys.argv[1]).resolve()
    cli = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="seam-production-render-") as temporary:
        root = Path(temporary)
        directory = root / "bundle"
        directory.mkdir()
        exported = root / "exported-phones.json"
        collected = subprocess.run([str(binary)], capture_output=True, timeout=120,
                                   env=dict(os.environ, SEAM_NEURAL_PRODUCTION_VOCABULARY_OUT=str(exported)))
        assert collected.returncode == 0, collected.stderr
        mapping = json.loads(exported.read_text())
        assert mapping.get("SP") == 1 and len(mapping) >= 1, mapping

        acoustic, vocoder = graphs(steps_layout="scalar", vocoder_output="audio")
        feature = dict(sampleRate=48000, hopSize=256, bins=80, layout="BTF",
                       amplitudeScale="ln-amplitude", multiplier=1.0, offset=0.0,
                       minimumHz=40.0, maximumHz=16000.0, fftSize=2048, windowSize=1024,
                       melFrequencyScale="slaney")
        configuration = dict(formatId="com.project-seam.neural-bundle-configuration", schemaVersion=3,
                             maximumFrames=48000, stepsLayout="scalar", vocoderOutput="audio",
                             acousticFeatures=feature, vocoderFeatures=feature)
        (directory / "acoustic").write_bytes(acoustic)
        (directory / "vocoder").write_bytes(vocoder)
        (directory / "configuration").write_text(json.dumps(configuration))
        subprocess.run([str(cli), "convert-neural-vocabulary", str(exported),
                        hashlib.sha256(exported.read_bytes()).hexdigest(), str(directory / "vocabulary")],
                       check=True, capture_output=True, timeout=20)
        prepared = subprocess.run([str(cli), "prepare-neural-bundle", str(directory), "fixture", "1", "1048576"],
                                  check=True, capture_output=True, text=True, timeout=20)
        manifest_sha256 = json.loads(prepared.stdout)["manifestSha256"]

        rendered = subprocess.run([str(binary)], capture_output=True, timeout=300,
                                  env=dict(os.environ,
                                           SEAM_NEURAL_PRODUCTION_BUNDLE=str(directory),
                                           SEAM_NEURAL_PRODUCTION_MANIFEST_SHA256=manifest_sha256,
                                           SEAM_NEURAL_PRODUCTION_MAXIMUM_BYTES="1048576"))
        assert rendered.returncode == 0, rendered.stderr.decode()
        report = rendered.stdout.decode()
        # A skipped phase would also exit zero, so require the render to name itself.
        assert "seam.neural-worker.v1" in report, report
        assert "nonzero samples" in report, report
        nonzero = int(report.split("with ")[1].split(" nonzero")[0])
        assert nonzero > 0, report
        print(report.strip())
        print("production worker render check passed; no musical claim was made")


if __name__ == "__main__":
    main()
