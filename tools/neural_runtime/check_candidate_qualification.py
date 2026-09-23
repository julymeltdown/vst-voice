"""End-to-end check of the qualification command against the production worker.

The bundle carries arithmetic fixture graphs, not learned weights. The point of this
check is that the command measures a real worker: every automatic criterion passes on
the fixture, and the dossier still refuses to qualify the candidate, because no
deterministic run is evidence about a voice.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from check_paired_runtime import graphs


def write_configuration(path, directory, manifest_sha256, items, repetitions=2):
    payload = json.dumps({
        "formatId": "com.project-seam.candidate-qualification",
        "schemaVersion": 1,
        "bundle": {"directory": str(directory), "modelId": "fixture", "modelVersion": "1",
                   "manifestSha256": manifest_sha256, "maximumBundleBytes": 1048576,
                   "inferenceSteps": 20},
        "heldOut": items,
        "repetitions": repetitions,
    }, sort_keys=True, separators=(",", ":")).encode()
    path.write_bytes(payload)
    return hashlib.sha256(payload).hexdigest()


def qualify(worker, configuration, configuration_sha256, output):
    return subprocess.run([sys.executable, "-m", "tools.voice_model_training", "qualify-candidate",
                           str(configuration), configuration_sha256, str(worker), str(output)],
                          capture_output=True, timeout=300,
                          cwd=Path(__file__).resolve().parents[2])


def main():
    worker = Path(sys.argv[1]).resolve()
    cli = Path(sys.argv[2]).resolve()
    root = Path.cwd()
    with tempfile.TemporaryDirectory(prefix="seam-candidate-qualification-") as temporary:
        directory = Path(temporary) / "bundle"
        directory.mkdir()
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
        source = json.dumps({"SP": 1, "a": 2, "i": 3}).encode()
        (directory / "exported-phones.json").write_bytes(source)
        subprocess.run([str(cli), "convert-neural-vocabulary", str(directory / "exported-phones.json"),
                        hashlib.sha256(source).hexdigest(), str(directory / "vocabulary")],
                       check=True, capture_output=True, timeout=20)
        prepared = subprocess.run([str(cli), "prepare-neural-bundle", str(directory), "fixture", "1", "1048576"],
                                  check=True, capture_output=True, text=True, timeout=20)
        manifest_sha256 = json.loads(prepared.stdout)["manifestSha256"]

        items = [{"itemId": "held-1", "songId": "song-9", "phones": ["a", "i"],
                  "frameCount": 731, "frequencyHz": 210.0, "gain": 0.5},
                 {"itemId": "held-2", "songId": "song-10", "phones": ["i"],
                  "frameCount": 240, "frequencyHz": 190.0, "gain": 0.75}]
        config = Path(temporary) / "qualification.json"
        config_sha256 = write_configuration(config, directory, manifest_sha256, items)
        output = Path(temporary) / "dossier.json"
        completed = qualify(worker, config, config_sha256, output)
        assert completed.returncode == 0, completed.stderr
        dossier = json.loads(output.read_text())
        assert dossier["formatId"] == "com.project-seam.neural-candidate-qualification"
        assert dossier["verdict"] == "UNRESOLVED", dossier["verdict"]
        assert dossier["failedCriteria"] == [], dossier["failedCriteria"]
        assert dossier["musicalJudgement"] == "UNRESOLVED"
        assert dossier["releaseEligible"] is False and dossier["approval"] is None
        statuses = {entry["id"]: entry["status"] for entry in dossier["criteria"]}
        for criterion in ("bundle-admission", "response-binding", "vocabulary-coverage",
                          "determinism", "finite-audio"):
            assert statuses[criterion] == "PASS", (criterion, statuses)
        # No budget was declared, so no runtime claim is made from fixture timings.
        assert statuses["runtime-budget"] == "UNRESOLVED", statuses
        for criterion in ("intelligibility", "identity", "musicality"):
            assert statuses[criterion] == "UNRESOLVED", statuses
        assert len({item["audioSha256"] for item in dossier["items"]}) == 2, dossier["items"]
        for item in dossier["items"]:
            assert item["status"] == "PASS" and item["frames"] in (731, 240)
            assert len(item["milliseconds"]) == 2
        assert dossier["worker"]["sha256"] == hashlib.sha256(worker.read_bytes()).hexdigest()
        assert dossier["bundle"]["manifestSha256"] == manifest_sha256
        assert dossier["bundle"]["inferenceSteps"] == 20

        # A closed output is refused rather than overwritten.
        repeated = qualify(worker, config, config_sha256, output)
        assert repeated.returncode == 2, repeated.returncode
        assert "failed" in repeated.stderr.decode()

        # A held-out phone the vocabulary does not carry fails that criterion, and the
        # dossier is still written so the failure stays auditable.
        missing = Path(temporary) / "missing.json"
        missing_sha256 = write_configuration(missing, directory, manifest_sha256,
                                            [{"itemId": "held-3", "songId": "song-11", "phones": ["a", "u"],
                                              "frameCount": 240, "frequencyHz": 200.0, "gain": 0.5}])
        failed_output = Path(temporary) / "failed.json"
        failed = qualify(worker, missing, missing_sha256, failed_output)
        assert failed.returncode == 4, failed.stderr
        failed_dossier = json.loads(failed_output.read_text())
        assert failed_dossier["verdict"] == "FAILED"
        assert failed_dossier["failedCriteria"] == ["vocabulary-coverage"], failed_dossier["failedCriteria"]
        assert failed_dossier["releaseEligible"] is False
        assert failed_dossier["items"][0]["status"] == "FAIL"
        assert "u" in failed_dossier["items"][0]["detail"]
        print("candidate qualification fixture check passed; no musical claim was made")


if __name__ == "__main__":
    main()
