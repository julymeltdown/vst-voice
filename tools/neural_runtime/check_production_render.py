"""Render a phrase through the normal authoring path with the shipped worker.

By default the bundle carries arithmetic fixture graphs. Optional candidate-bundle,
project and output arguments exercise a learned bundle without installing it.
A pass proves execution and export, never singer qualification or musical quality.

The C++ binary is the renderer: this script only prepares the real ONNX bundle it
needs. Fixture mode checks both output-window equivalence and saved-project export.
Exported projects retain external neural resource identities, not model binaries.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile


def run_render(binary, directory, manifest_sha256, maximum_bytes, *, project=None,
               output=None, model_id="fixture", version="1"):
    # Do not let a caller's stale probe variables silently skip rendering.
    environment = {k: v for k, v in os.environ.items()
                   if not k.startswith("SEAM_NEURAL_PRODUCTION_")}
    environment.update(SEAM_NEURAL_PRODUCTION_BUNDLE=str(directory),
                       SEAM_NEURAL_PRODUCTION_MANIFEST_SHA256=manifest_sha256,
                       SEAM_NEURAL_PRODUCTION_MAXIMUM_BYTES=str(maximum_bytes),
                       SEAM_NEURAL_PRODUCTION_MODEL_ID=model_id,
                       SEAM_NEURAL_PRODUCTION_MODEL_VERSION=version)
    if project is not None:
        if project.stat().st_size > 4 * 1024 * 1024:
            raise ValueError("Project exceeds the native intake limit")
        environment.update(SEAM_NEURAL_PRODUCTION_PROJECT=str(project),
                           SEAM_NEURAL_PRODUCTION_PROJECT_SHA256=hashlib.sha256(project.read_bytes()).hexdigest(),
                           SEAM_NEURAL_PRODUCTION_EXPORT=str(output))
    rendered = subprocess.run([str(binary)], capture_output=True, text=True,
                              timeout=300, env=environment)
    if rendered.returncode:
        raise RuntimeError(rendered.stdout + rendered.stderr)
    report = rendered.stdout
    match = re.search(r"with (\d+) nonzero samples", report)
    assert "seam.neural-worker.v1" in report and match and int(match[1]) > 0, report
    if project is not None:
        assert "singerQualified=false" in report, report
        assert output.is_dir(), report
    print(report.strip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("cli", type=Path)
    parser.add_argument("--candidate-bundle", type=Path)
    parser.add_argument("--project", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    binary, cli = args.binary.resolve(), args.cli.resolve()
    if any((args.candidate_bundle, args.project, args.output)):
        if not all((args.candidate_bundle, args.project, args.output)):
            parser.error("candidate-bundle, project and output must be supplied together")
        directory, project, output = (p.resolve() for p in
                                      (args.candidate_bundle, args.project, args.output))
        if output.exists() or not output.parent.is_dir():
            parser.error("output must be a new path with an existing parent")
        resource_path = directory / "resource.json"
        manifest_path = directory / "manifest.json"
        if resource_path.stat().st_size > 16384 or manifest_path.stat().st_size > 1048576:
            raise ValueError("Candidate metadata exceeds intake limits")
        resource = json.loads(resource_path.read_bytes())
        digest = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
        if (resource.get("formatId") != "com.project-seam.neural-resource"
                or resource.get("schemaVersion") != 1 or resource.get("contentHash") != digest):
            raise ValueError("Candidate resource identity does not match its manifest")
        run_render(binary, directory, digest, 256 * 1024 * 1024, project=project,
                   output=output, model_id=resource["id"], version=resource["version"])
        print("candidate execution/export verified; singer remains unqualified")
        return
    from check_paired_runtime import graphs
    with tempfile.TemporaryDirectory(prefix="seam-production-render-") as temporary:
        root = Path(temporary)
        directory = root / "bundle"
        directory.mkdir()
        exported = root / "exported-phones.json"
        project = root / "fixture.seam"
        collected = subprocess.run([str(binary)], capture_output=True, timeout=120,
                                   env=dict(os.environ, SEAM_NEURAL_PRODUCTION_VOCABULARY_OUT=str(exported),
                                            SEAM_NEURAL_PRODUCTION_PROJECT_OUT=str(project)))
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

        run_render(binary, directory, manifest_sha256, 1048576)
        run_render(binary, directory, manifest_sha256, 1048576,
                   project=project, output=root / "application-export")
        print("production worker render check passed; no musical claim was made")


if __name__ == "__main__":
    main()
