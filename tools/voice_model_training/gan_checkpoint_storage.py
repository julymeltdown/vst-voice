"""Two-file bounded transport for trusted local GAN state, receipt published last.

Each file is at most 512 MiB, combined at most 1 GiB. Loading trusted Torch state
can require more memory than file size; this is not hostile-archive admission.
"""
import hashlib
import io
import json
import os
import stat

LIMIT = 512 * 1024 * 1024


def _bytes(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode()


def publish_checkpoint(model, optimizer, output, *, metadata, epoch, maximum_bytes=LIMIT, before_publish=None):
    import torch
    from .__main__ import publish_new
    if (type(maximum_bytes) is not int or not 1 <= maximum_bytes <= LIMIT
            or not isinstance(metadata, dict) or not isinstance(epoch, dict)
            or epoch.get("epochComplete") is not True or epoch.get("coverageVerified") is not True
            or before_publish is not None and not callable(before_publish)):
        raise ValueError("Require complete epoch and a per-file bound up to 512 MiB")
    captured = _bytes(dict(metadata=metadata, epoch=epoch))
    if len(captured) > 1024 * 1024:
        raise ValueError("GAN metadata exceeds 1 MiB")
    bound = json.loads(captured)
    model_state = model.state_dict()
    if any(not isinstance(v, torch.Tensor) or v.device.type != "cpu" for v in model_state.values()):
        raise ValueError("GAN checkpoints require CPU model tensors")
    output.mkdir(mode=0o700)
    records = []
    for name, state in (("models.pt", model_state),
                         ("training.pt", dict(optimizer=optimizer.state_dict(), rng=torch.get_rng_state()))):
        digest, count = hashlib.sha256(), 0
        with (output / name).open("xb") as stream:
            class Writer:
                def write(self, data):
                    nonlocal count
                    if count + len(data) > maximum_bytes:
                        raise ValueError("GAN state file exceeds bound; incomplete output retained")
                    written = stream.write(data)
                    if written != len(data):
                        raise OSError("Incomplete GAN checkpoint write")
                    count += written
                    digest.update(data)
                    return written
                def flush(self):
                    stream.flush()
            torch.save(state, Writer())
            stream.flush()
            os.fsync(stream.fileno())
        records.append(dict(path=name, bytes=count, sha256=digest.hexdigest()))
    if before_publish is not None:
        before_publish()
    receipt = dict(formatId="com.project-seam.gan-checkpoint", schemaVersion=1,
                   files=records, checkpointBytes=sum(r["bytes"] for r in records),
                   checkpointSha256=hashlib.sha256(_bytes(records)).hexdigest(),
                   metadataSha256=hashlib.sha256(captured).hexdigest(), torchVersion=str(torch.__version__),
                   **bound, trainingAdmitted=False, releaseEligible=False)
    publish_new(output / "checkpoint.json", receipt)
    return receipt


def load_local_checkpoint(directory, *, receipt_sha256, maximum_bytes=LIMIT):
    import torch
    from .__main__ import load_config
    from .checkpoint import load_local_checkpoint as legacy_load
    if type(maximum_bytes) is not int or not 1 <= maximum_bytes <= LIMIT:
        raise ValueError("Invalid per-file GAN checkpoint bound")
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError("GAN checkpoint directory must be real")
    receipt = load_config(directory / "checkpoint.json", receipt_sha256)
    if isinstance(receipt, dict) and receipt.get("formatId") == "com.project-seam.training-checkpoint":
        return legacy_load(directory, receipt_sha256=receipt_sha256, maximum_bytes=maximum_bytes)
    fields = {"formatId", "schemaVersion", "files", "checkpointBytes", "checkpointSha256",
              "metadataSha256", "torchVersion", "metadata", "epoch", "trainingAdmitted", "releaseEligible"}
    if (not isinstance(receipt, dict) or set(receipt) != fields
            or receipt["formatId"] != "com.project-seam.gan-checkpoint"
            or type(receipt["schemaVersion"]) is not int or receipt["schemaVersion"] != 1
            or receipt["torchVersion"] != str(torch.__version__)
            or not isinstance(receipt["metadata"], dict) or not isinstance(receipt["epoch"], dict)
            or receipt["epoch"].get("epochComplete") is not True or receipt["epoch"].get("coverageVerified") is not True
            or receipt["trainingAdmitted"] is not False or receipt["releaseEligible"] is not False
            or not isinstance(receipt["files"], list) or len(receipt["files"]) != 2):
        raise ValueError("Invalid GAN completion receipt")
    captured = _bytes(dict(metadata=receipt["metadata"], epoch=receipt["epoch"]))
    if len(captured) > 1024 * 1024 or hashlib.sha256(captured).hexdigest() != receipt["metadataSha256"]:
        raise ValueError("GAN metadata identity differs")
    for record, name in zip(receipt["files"], ("models.pt", "training.pt")):
        if (not isinstance(record, dict) or set(record) != {"path", "bytes", "sha256"}
                or record["path"] != name or type(record["bytes"]) is not int
                or not 1 <= record["bytes"] <= maximum_bytes):
            raise ValueError("Invalid GAN file record")
    if (type(receipt["checkpointBytes"]) is not int or
            receipt["checkpointBytes"] != sum(r["bytes"] for r in receipt["files"]) or
            receipt["checkpointSha256"] != hashlib.sha256(_bytes(receipt["files"])).hexdigest()):
        raise ValueError("GAN aggregate identity differs")
    # Capture and hash both files before invoking any Torch loader.
    payloads = []
    for record in receipt["files"]:
        path = directory / record["path"]
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
        if path.is_symlink():
            raise ValueError("GAN state cannot be a symlink")
        with os.fdopen(os.open(path, flags), "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size != record["bytes"]:
                raise ValueError("GAN state size or type differs")
            payload = stream.read(record["bytes"] + 1)
        if len(payload) != record["bytes"] or hashlib.sha256(payload).hexdigest() != record["sha256"]:
            raise ValueError("GAN state bytes differ")
        payloads.append(payload)
    models, training = [torch.load(io.BytesIO(p), map_location="cpu", weights_only=True) for p in payloads]
    if not isinstance(models, dict) or not isinstance(training, dict) or set(training) != {"optimizer", "rng"}:
        raise ValueError("GAN payload structure differs")
    return dict(model=models, **training, metadata=receipt["metadata"], epoch=receipt["epoch"]), receipt
