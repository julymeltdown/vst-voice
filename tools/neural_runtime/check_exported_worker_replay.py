"""Run captured acoustic and vocoder exports through the production worker.

This is an engineering integration check only. Its inputs may be synthetic and
the resulting bundle remains unqualified and ineligible for release.
"""
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import math


FRAME = struct.Struct("<4sHBBIQ")
WAV_HEADER = struct.Struct("<4sI4s4sIHHIIHH4sI")


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":")).encode("utf-8")


def parse_response(payload, *, expected_frames, expected_digest, expected_request_hash,
                   expected_request_id, expected_backend_id):
    if len(payload) < FRAME.size:
        raise ValueError("Production worker returned a truncated frame")
    magic, version, kind, reserved, header_bytes, audio_bytes = FRAME.unpack_from(payload)
    if (magic, version, kind, reserved) != (b"SNW1", 1, 2, 0):
        raise ValueError("Production worker returned an invalid response envelope")
    if audio_bytes != expected_frames * 4 or FRAME.size + header_bytes + audio_bytes != len(payload):
        raise ValueError("Production worker returned an unexpected PCM byte count")
    metadata = json.loads(payload[FRAME.size:FRAME.size + header_bytes])
    if (metadata.get("kind") != "seam-neural-response-v3"
            or metadata.get("frameCount") != expected_frames
            or metadata.get("sampleRate") != 48000
            or metadata.get("channels") != 1
            or metadata.get("modelContentHash") != expected_digest
            or metadata.get("bundleContentHash") != expected_digest
            or metadata.get("requestContentHash") != expected_request_hash
            or type(metadata.get("requestId")) is not int
            or metadata.get("requestId") != expected_request_id
            or metadata.get("backendId") != expected_backend_id):
        raise ValueError("Production worker response identity differs from the request")
    pcm_bytes = payload[FRAME.size + header_bytes:]
    pcm = struct.unpack(f"<{expected_frames}f", pcm_bytes)
    if not all(math.isfinite(sample) and -1.0 <= sample <= 1.0 for sample in pcm):
        raise ValueError("Production worker returned invalid PCM")
    return metadata, pcm_bytes, pcm


def validate_render_binding(inputs, *, expected_worker_hash, expected_manifest_hash,
                            expected_project_hash, expected_steps):
    if (inputs.get("workerSha256") != expected_worker_hash
            or inputs.get("manifestSha256") != expected_manifest_hash
            or inputs.get("projectSha256") != expected_project_hash
            or type(inputs.get("steps")) is not int
            or inputs.get("steps") != expected_steps):
        raise ValueError("Native authoring render inputs are not bound to the selected worker and replay request")


def validate_exported_wavs(output, *, expected_sample_rate=48000):
    """Validate the rendered files, not just their names or existence."""
    paths = sorted(Path(output).rglob("*.wav"))
    if not paths:
        raise ValueError("Native authoring export produced no WAV files")
    total_frames = 0
    non_silent_files = 0
    for path in paths:
        if path.is_symlink() or not path.is_file():
            raise ValueError("Native authoring export contains a non-regular WAV file")
        size = path.stat().st_size
        if size < WAV_HEADER.size:
            raise ValueError("Native authoring export contains a truncated WAV")
        with path.open("rb") as stream:
            header = stream.read(WAV_HEADER.size)
            (riff, riff_size, wave, fmt, fmt_size, encoding, channels, sample_rate,
             byte_rate, block_align, bits, data, data_size) = WAV_HEADER.unpack(header)
            sample_bytes = bits // 8
            if (riff != b"RIFF" or wave != b"WAVE" or fmt != b"fmt " or data != b"data"
                    or fmt_size != 16 or encoding not in (1, 3)
                    or channels not in (1, 2) or sample_rate != expected_sample_rate
                    or bits not in (16, 24, 32)
                    or (encoding == 1 and bits not in (16, 24))
                    or (encoding == 3 and bits != 32)
                    or block_align != channels * sample_bytes
                    or byte_rate != sample_rate * block_align
                    or riff_size + 8 != size or data_size != size - WAV_HEADER.size
                    or data_size == 0 or data_size % block_align != 0):
                raise ValueError("Native authoring WAV has an invalid format, clock, or byte count")
            frames = data_size // block_align
            total_frames += frames
            non_silent = False
            remaining = data_size
            while remaining:
                count = min(remaining, 64 * 1024)
                count -= count % sample_bytes
                payload = stream.read(count)
                if len(payload) != count:
                    raise ValueError("Native authoring WAV data is truncated")
                remaining -= count
                if encoding == 3:
                    values = struct.iter_unpack("<f", payload)
                    for (sample,) in values:
                        if not math.isfinite(sample) or abs(sample) > 1.0:
                            raise ValueError("Native authoring WAV contains invalid float PCM")
                        non_silent |= sample != 0.0
                else:
                    non_silent |= any(payload)
            non_silent_files += int(non_silent)
    if non_silent_files == 0:
        raise ValueError("Native authoring export produced only silent WAV files")
    return {"fileCount": len(paths), "totalFrames": total_frames,
            "nonSilentFileCount": non_silent_files}


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("acoustic_export", type=Path)
    parser.add_argument("vocoder_export", type=Path)
    parser.add_argument("worker", type=Path)
    parser.add_argument("voicebank_cli", type=Path)
    parser.add_argument("--render-binary", type=Path,
                        help="Also exercise the normal authoring render and saved-project export path")
    args = parser.parse_args()
    acoustic_export, vocoder_export, worker, cli = (
        path.resolve(strict=True) for path in
        (args.acoustic_export, args.vocoder_export, args.worker, args.voicebank_cli))
    render_binary = args.render_binary.resolve(strict=True) if args.render_binary else None
    root = Path(tempfile.mkdtemp(prefix="seam-exported-worker-replay-"))
    try:
        bundle = root / "bundle"
        composed = subprocess.run([
            sys.executable, "-m", "tools.voice_model_training.prepare_bundle",
            "--acoustic-export", str(acoustic_export), "--vocoder-export", str(vocoder_export),
            "--output", str(bundle), "--maximum-frames", "48000",
            "--vocoder-output", "waveform",
            "--resource-id", "synthetic.integration", "--resource-version", "1",
        ], capture_output=True, text=True, timeout=90)
        if composed.returncode:
            raise ValueError(f"Could not compose captured exports: {composed.stderr[-512:]}")
        receipt = json.loads(composed.stdout)
        digest = receipt["manifestSha256"]
        vocabulary_bytes = (bundle / "vocabulary").read_bytes()
        vocabulary_hash = hashlib.sha256(vocabulary_bytes).hexdigest()
        vocabulary = json.loads(vocabulary_bytes)
        tokens = vocabulary.get("tokens")
        if type(tokens) is not list or len(tokens) < 2 or type(tokens[1]) is not str:
            raise ValueError("Composed export vocabulary has no usable non-padding token")
        token_id = len(tokens) - 1
        frame_count = 64
        metadata = dict(kind="seam-neural-request-v3", requestId=731,
                        modelId="synthetic.integration", modelVersion="1",
                        modelContentHash=digest, pronunciationHash="a" * 64,
                        sampleRate=48000, channels=1, frameCount=frame_count,
                        featureKind="f0-dynamics-phonemes", vocabularyHash=vocabulary_hash,
                        vocabularySize=len(tokens), phonemes=[dict(tokenId=token_id,
                        startFrame=0, endFrame=frame_count)], bundleContentHash=digest,
                        hasBreathiness=True)
        header = canonical(metadata)
        floats = [220.0] * frame_count + [0.5] * frame_count + [0.25] * frame_count
        audio = struct.pack(f"<{len(floats)}f", *floats)
        request = FRAME.pack(b"SNW1", 1, 1, 0, len(header), len(audio)) + header + audio
        request_hash = hashlib.sha256(request).hexdigest()
        expected_backend_id = "seam-neural-worker-acoustic-vocoder-1/steps-10"
        launch = [str(worker), "--seam-neural-worker-v3", str(bundle),
                  "synthetic.integration", "1", digest, "1048576", "10"]
        outputs = []
        for _ in range(2):
            result = subprocess.run(launch, input=request, capture_output=True, timeout=90)
            if result.returncode:
                raise ValueError(f"Production worker rejected the captured exports: {result.stderr[-512:].decode(errors='replace')}")
            outputs.append(parse_response(result.stdout, expected_frames=frame_count,
                                          expected_digest=digest,
                                          expected_request_hash=request_hash,
                                          expected_request_id=731,
                                          expected_backend_id=expected_backend_id))
        for _metadata, _pcm_bytes, pcm in outputs:
            if not any(sample != 0.0 for sample in pcm):
                raise ValueError("Captured acoustic/vocoder exports returned silent PCM")
        differences = [left - right for left, right in zip(outputs[0][2], outputs[1][2])]
        maximum_absolute_difference = max((abs(value) for value in differences), default=0.0)
        rms_difference = math.sqrt(sum(value * value for value in differences) / len(differences))
        native = subprocess.run([str(cli), "inspect-neural-bundle", str(bundle),
                                 "synthetic.integration", "1", digest, "1048576"],
                                capture_output=True, text=True, timeout=20)
        if native.returncode:
            raise ValueError(f"Native CLI could not inspect the composed bundle: {native.stderr[-512:]}")
        render_report = None
        exported_wav_count = 0
        if render_binary is not None:
            project = root / "project.seam"
            phone_map_path = root / "project-phones.json"
            environment = {key: value for key, value in os.environ.items()
                           if not key.startswith("SEAM_NEURAL_PRODUCTION_")}
            environment.update(SEAM_NEURAL_PRODUCTION_VOCABULARY_OUT=str(phone_map_path),
                               SEAM_NEURAL_PRODUCTION_PROJECT_OUT=str(project))
            initialized = subprocess.run([str(render_binary)], env=environment,
                                         capture_output=True, text=True, timeout=120)
            if initialized.returncode or not project.is_file() or not phone_map_path.is_file():
                raise ValueError(f"Could not create the native render fixture project: {initialized.stderr[-512:]}")
            phone_map = json.loads(phone_map_path.read_bytes())
            for symbol, token_id in phone_map.items():
                if symbol == "SP":
                    continue
                if (type(token_id) is not int or token_id <= 0 or token_id >= len(tokens)
                        or tokens[token_id] != symbol):
                    raise ValueError(f"Exported training vocabulary cannot render the project's {symbol!r} phone")
            output = root / "application-export"
            integration = Path(__file__).resolve().with_name("check_production_render.py")
            rendered = subprocess.run([sys.executable, str(integration), str(render_binary), str(cli),
                "--candidate-bundle", str(bundle), "--project", str(project), "--output", str(output),
                "--inputs-output", str(root / "prepared-inputs.json")],
                capture_output=True, text=True, timeout=360, env=environment)
            if rendered.returncode or "candidate execution/export verified" not in rendered.stdout:
                raise ValueError(f"Native authoring render/export path failed: {rendered.stdout[-512:]} {rendered.stderr[-512:]}")
            render_report = rendered.stdout[-2048:]
            expected_worker_hash = hashlib.sha256(worker.read_bytes()).hexdigest()
            worker_report = re.search(r"workerSha256=([0-9a-f]{64})", rendered.stdout)
            inputs_path = root / "prepared-inputs.json"
            if worker_report is None or worker_report.group(1) != expected_worker_hash:
                raise ValueError("Native authoring renderer did not use the selected production worker")
            prepared_inputs = json.loads(inputs_path.read_bytes())
            project_hash = hashlib.sha256(project.read_bytes()).hexdigest()
            validate_render_binding(prepared_inputs,
                expected_worker_hash=expected_worker_hash,
                expected_manifest_hash=digest,
                expected_project_hash=project_hash,
                expected_steps=10)
            wav_validation = validate_exported_wavs(output)
            exported_wav_count = wav_validation["fileCount"]
        report = dict(status="EXPORTED_MODEL_WORKER_REPLAY", passed=True,
                      manifestSha256=digest, requestContentHash=request_hash,
                      freshWorkerProcesses=2,
                      exactPcmReplay=outputs[0][1] == outputs[1][1],
                      stochasticSamplerMayDiffer=True,
                      maximumAbsolutePcmDifference=maximum_absolute_difference,
                      rmsPcmDifference=rms_difference,
                      outputFrames=frame_count, outputNonSilent=True,
                      productionRendererPathExecuted=render_binary is not None,
                      projectExportedWavCount=exported_wav_count,
                      projectExportedWavValidation=wav_validation if render_binary is not None else None,
                      productionRenderSummary=render_report,
                      syntheticEngineeringInputs=True, singerQualified=False,
                      releaseEligible=False)
        print(json.dumps(report, sort_keys=True))
    finally:
        import shutil
        shutil.rmtree(root)


if __name__ == "__main__":
    main()
