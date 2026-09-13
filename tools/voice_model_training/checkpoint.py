"""New-output publication for locally produced CPU training checkpoints."""
import hashlib
import io
import json
import os
from pathlib import Path
import stat


def load_local_checkpoint(directory: Path, *, receipt_sha256: str,
                           maximum_bytes: int = 512 * 1024 * 1024):
    """Load a trusted locally produced checkpoint through captured receipt identity.

    This is not a hostile third-party archive importer: byte limits and
    weights_only loading do not prove bounded tensor allocation for arbitrary
    archives. Caller must trust the receipt digest and checkpoint producer.
    """
    import torch
    from .__main__ import load_config
    if type(maximum_bytes) is not int or not 1 <= maximum_bytes <= 512 * 1024 * 1024:
        raise ValueError("Invalid checkpoint intake byte limit")
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError("Checkpoint directory must be a real directory")
    receipt = load_config(directory / "checkpoint.json", receipt_sha256)
    fields = {"formatId", "schemaVersion", "checkpointPath", "checkpointSha256", "checkpointBytes",
              "metadataSha256", "torchVersion", "metadata", "epoch", "trainingAdmitted", "releaseEligible"}
    if (not isinstance(receipt, dict) or set(receipt) != fields
            or receipt["formatId"] != "com.project-seam.training-checkpoint"
            or type(receipt["schemaVersion"]) is not int or receipt["schemaVersion"] != 1
            or receipt["checkpointPath"] != "checkpoint.pt" or receipt["torchVersion"] != str(torch.__version__)
            or type(receipt["checkpointBytes"]) is not int or not 1 <= receipt["checkpointBytes"] <= maximum_bytes
            or not isinstance(receipt["metadata"], dict) or not isinstance(receipt["epoch"], dict)
            or receipt["epoch"].get("epochComplete") is not True or receipt["epoch"].get("coverageVerified") is not True
            or receipt["trainingAdmitted"] is not False or receipt["releaseEligible"] is not False):
        raise ValueError("Unsupported or incomplete local checkpoint receipt")
    captured = json.dumps(dict(metadata=receipt["metadata"], epoch=receipt["epoch"]),
                          sort_keys=True, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode()
    if len(captured) > 1024 * 1024 or hashlib.sha256(captured).hexdigest() != receipt["metadataSha256"]:
        raise ValueError("Checkpoint metadata identity differs")
    path = directory / "checkpoint.pt"
    if path.is_symlink():
        raise ValueError("Checkpoint file cannot be a symlink")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    with os.fdopen(os.open(path, flags), "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size != receipt["checkpointBytes"]:
            raise ValueError("Checkpoint file size/type differs")
        payload = stream.read(receipt["checkpointBytes"] + 1)
    if len(payload) != receipt["checkpointBytes"] or hashlib.sha256(payload).hexdigest() != receipt["checkpointSha256"]:
        raise ValueError("Checkpoint bytes differ from the captured receipt")
    state = torch.load(io.BytesIO(payload), map_location="cpu", weights_only=True)
    if (not isinstance(state, dict) or set(state) != {"model", "optimizer", "rng", "metadata", "epoch"}
            or state["metadata"] != receipt["metadata"] or state["epoch"] != receipt["epoch"]):
        raise ValueError("Checkpoint state differs from published metadata")
    return state, receipt


def publish_checkpoint(model, optimizer, output: Path, *, metadata: dict, epoch: dict,
                       maximum_bytes: int = 512 * 1024 * 1024, before_publish=None) -> dict:
    """Write checkpoint first and completion record last; no approval or importer.

    Caller owns stable model/optimizer state and current source authority. A failed
    write leaves an incomplete directory for diagnosis, never a completed receipt.
    This bounds written bytes, not Torch's internal memory or serialization time.
    """
    import torch
    from .__main__ import publish_new
    if (type(maximum_bytes) is not int or not 1 <= maximum_bytes <= 512 * 1024 * 1024
            or not isinstance(metadata, dict) or not isinstance(epoch, dict)
            or epoch.get("epochComplete") is not True or epoch.get("coverageVerified") is not True
            or (before_publish is not None and not callable(before_publish))):
        raise ValueError("Checkpoint needs a coverage-complete epoch and bounded publication options")
    if any(not isinstance(value, torch.Tensor) or value.device.type != "cpu" for value in model.state_dict().values()):
        raise ValueError("Checkpoint owner currently supports CPU model state only")
    captured = json.dumps(dict(metadata=metadata, epoch=epoch), sort_keys=True,
                          ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode()
    if len(captured) > 1024 * 1024:
        raise ValueError("Checkpoint metadata exceeds 1 MiB")
    captured_value = json.loads(captured)
    output.mkdir(mode=0o700)
    digest, written = hashlib.sha256(), 0
    with (output / "checkpoint.pt").open("xb") as stream:
        class LimitedWriter:
            def write(self, data):
                nonlocal written
                if written + len(data) > maximum_bytes:
                    raise ValueError("Checkpoint exceeds byte budget; incomplete output retained")
                count = stream.write(data)
                if count != len(data):
                    raise OSError("Incomplete checkpoint write")
                digest.update(data)
                written += count
                return count
            def flush(self):
                stream.flush()
        torch.save(dict(model=model.state_dict(), optimizer=optimizer.state_dict(), rng=torch.get_rng_state(),
                        **captured_value), LimitedWriter())
        stream.flush()
        os.fsync(stream.fileno())
    if before_publish is not None:
        before_publish()
    receipt = dict(formatId="com.project-seam.training-checkpoint", schemaVersion=1,
                   checkpointPath="checkpoint.pt", checkpointSha256=digest.hexdigest(), checkpointBytes=written,
                   metadataSha256=hashlib.sha256(captured).hexdigest(), torchVersion=str(torch.__version__),
                   **captured_value, trainingAdmitted=False, releaseEligible=False)
    publish_new(output / "checkpoint.json", receipt)
    return receipt
