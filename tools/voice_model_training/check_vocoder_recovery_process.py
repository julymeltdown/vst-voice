"""Fresh-process engineering diagnostic over an existing captured fixture corpus."""
import hashlib
from pathlib import Path
import subprocess
import sys

from .__main__ import encode_report, load_config, publish_new


def check_process_recovery(*, root, dataset_inputs, conditioning_directory, targets, profile_sha256, pcm_sources):
    import torch
    from .gan_checkpoint_storage import load_local_checkpoint
    inputs = dict(dataset_inputs)
    for key in ("root", "permission_config", "label_config"):
        inputs[key] = str(inputs[key])
    request = dict(root=str(root), dataset_inputs=inputs, conditioning_directory=str(conditioning_directory),
        targets={key: [record, str(path)] for key, (record, path) in targets.items()},
        profile_sha256=profile_sha256, pcm_sources={key: str(path) for key, path in pcm_sources.items()})
    path = root/"process-recovery-request.json"
    publish_new(path, request)
    digest = hashlib.sha256(encode_report(request)).hexdigest()
    for mode, expected in (("baseline", 0), ("interrupt", 73), ("resume", 0)):
        result = subprocess.run([sys.executable, "-m", __name__, str(path), digest, mode],
            capture_output=True, text=True, timeout=60)
        if result.returncode != expected:
            raise AssertionError(f"Recovery {mode} process exited {result.returncode}: {result.stderr[-1500:]}")
        if mode == "interrupt" and (root/"process-interrupt/checkpoint.json").exists():
            raise AssertionError("Interrupted process incorrectly published a complete checkpoint")
    states = []
    for name in ("baseline", "resume"):
        directory = root/f"process-{name}"
        receipt_hash = hashlib.sha256((directory/"checkpoint.json").read_bytes()).hexdigest()
        state, _ = load_local_checkpoint(directory, receipt_sha256=receipt_hash, maximum_bytes=1024**2)
        states.append(state)

    def equal(a, b):
        if isinstance(a, torch.Tensor):
            return isinstance(b, torch.Tensor) and torch.equal(a, b)
        if type(a) is not type(b):
            return False
        if isinstance(a, dict):
            return a.keys() == b.keys() and all(equal(a[k], b[k]) for k in a)
        if isinstance(a, (tuple, list)):
            return len(a) == len(b) and all(equal(x, y) for x, y in zip(a, b))
        return a == b
    if not equal(*states):
        raise AssertionError("Fresh-process recovery differs in model, optimizer, RNG, metadata or epoch")
    return dict(passed=True, hardExitCode=73, freshProcesses=3, completeStateExact=True,
                fixtureOnly=True, singerQualified=False, releaseEligible=False)


def main():
    from .check_vocoder_recovery import check_vocoder_recovery
    request = load_config(Path(sys.argv[1]), sys.argv[2])
    for key in ("root", "conditioning_directory"):
        request[key] = Path(request[key])
    for key in ("root", "permission_config", "label_config"):
        request["dataset_inputs"][key] = Path(request["dataset_inputs"][key])
    request["targets"] = {key: (record, Path(path)) for key, (record, path) in request["targets"].items()}
    request["pcm_sources"] = {key: Path(path) for key, path in request["pcm_sources"].items()}
    check_vocoder_recovery(**request, _process_mode=sys.argv[3])


if __name__ == "__main__":
    main()
