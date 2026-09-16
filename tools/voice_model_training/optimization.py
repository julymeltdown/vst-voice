"""Optional Torch acoustic update primitive, not a training admission boundary."""
import math
import time


def run_acoustic_epoch(model, optimizer, batches, *, expected_dataset_sha256: str,
                       expected_profile_sha256: str, vocabulary_size: int,
                       maximum_updates: int, maximum_seconds: float = 600,
                       cancelled=None, on_step=None, objective=None, objective_id="mel-l1",
                       expected_source_frames: dict[str, int] | None = None) -> dict:
    """Consume a freshly admitted epoch; publish no checkpoint and never resume on error.

    Deadline/cancellation are cooperative between operations, not a hard GPU or
    filesystem timeout. Exceptions invalidate the attempt, even after updates.
    Completion requires exhausting the iterator, not merely reaching a budget.
    """
    for digest in (expected_dataset_sha256, expected_profile_sha256):
        if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise ValueError("Epoch requires captured dataset and acoustic profile identities")
    if (type(maximum_updates) is not int or not 1 <= maximum_updates <= 100000
            or type(maximum_seconds) not in (int, float) or not math.isfinite(maximum_seconds)
            or not 0 < maximum_seconds <= 86400
            or (cancelled is not None and not callable(cancelled))
            or (on_step is not None and not callable(on_step))):
        raise ValueError("Invalid epoch execution limits or callbacks")
    coverage = None
    if expected_source_frames is not None:
        if (not isinstance(expected_source_frames, dict) or not 1 <= len(expected_source_frames) <= 10000
                or any(not isinstance(key, str) or not 1 <= len(key.encode()) <= 256
                       or type(value) is not int or not 1 <= value <= 65536
                       for key, value in expected_source_frames.items())
                or sum(expected_source_frames.values()) > 1000000):
            raise ValueError("Invalid epoch source coverage inventory")
        expected_source_frames = dict(expected_source_frames)
        coverage = dict.fromkeys(expected_source_frames, 0)
    deadline = time.monotonic() + maximum_seconds
    def check():
        if cancelled is not None and cancelled():
            raise RuntimeError("Acoustic epoch cancelled; attempt is incomplete")
        if time.monotonic() >= deadline:
            raise RuntimeError("Acoustic epoch deadline exceeded; attempt is incomplete")
    iterator = iter(batches)
    updates, samples, weighted_loss, sources = 0, 0, 0., set()
    while True:
        check()
        try:
            batch = next(iterator)
        except StopIteration:
            break
        check()
        if updates == maximum_updates:
            raise ValueError("Epoch exceeds update budget; attempt is incomplete")
        if (batch.get("datasetSha256") != expected_dataset_sha256
                or batch.get("profileSha256") != expected_profile_sha256):
            raise ValueError("Epoch batch identity differs; attempt is incomplete")
        if coverage is not None:
            source = batch.get("sourceId")
            start, count, offset = batch.get("coreFrameOffset"), batch.get("coreFrameCount"), batch.get("frameOffset")
            mask = batch.get("lossMask")
            if (source not in coverage or any(type(value) is not int for value in (start, count, offset))
                    or start != coverage[source] or count <= 0 or not 0 <= offset <= start
                    or start + count > expected_source_frames[source]
                    or batch.get("phraseAnalysisFrames") != expected_source_frames[source]
                    or not isinstance(mask, list) or not 1 <= len(mask) <= 4096
                    or offset + len(mask) > expected_source_frames[source]
                    or start + count > offset + len(mask)
                    or any(type(value) is not bool or value != (start <= offset + index < start + count)
                           for index, value in enumerate(mask))):
                raise ValueError("Epoch has duplicated, missing or inconsistent core frames")
        result = acoustic_training_step(model, optimizer, batch, vocabulary_size=vocabulary_size,
                                       objective=objective, objective_id=objective_id)
        if coverage is not None:
            coverage[source] += count
        updates += 1
        samples += result["validSamples"]
        weighted_loss += result["loss"] * result["validSamples"]
        sources.add(result["sourceId"])
        check()
        if on_step is not None:
            on_step(dict(result))
    check()
    if not updates:
        raise ValueError("An empty epoch cannot be completed")
    if coverage is not None and coverage != expected_source_frames:
        raise ValueError("Epoch ended before every expected source frame was consumed")
    return dict(formatId="com.project-seam.acoustic-epoch-result", schemaVersion=2,
                datasetSha256=expected_dataset_sha256, profileSha256=expected_profile_sha256,
                objectiveId=objective_id, updates=updates, sourceCount=len(sources), validSamples=samples,
                meanLoss=weighted_loss / samples, epochComplete=True, coverageVerified=coverage is not None,
                coveredSourceFrames=coverage, trainingAdmitted=False, releaseEligible=False)


def acoustic_training_step(model, optimizer, batch: dict, *, vocabulary_size: int,
                           maximum_gradient_norm: float = 1.0, objective=None,
                           objective_id: str = "mel-l1") -> dict:
    return _acoustic_step(model, optimizer, batch, vocabulary_size=vocabulary_size,
                         maximum_gradient_norm=maximum_gradient_norm, objective=objective, objective_id=objective_id)


def acoustic_evaluation_step(model, batch: dict, *, vocabulary_size: int, seed: int,
                             objective=None, objective_id: str = "mel-l1") -> dict:
    """CPU held-out objective evaluation, preserving Torch RNG and module modes.

    This measures the selected training objective, not audible synthesis quality.
    Arbitrary custom model buffer mutation is not rolled back. No checkpoint or
    optimizer is touched; GPU/distributed evaluation needs its own RNG contract.
    """
    import torch
    if type(seed) is not int or not 0 <= seed < 2**63:
        raise ValueError("Evaluation requires an explicit nonnegative 63-bit seed")
    if any(parameter.device.type != "cpu" for parameter in model.parameters()):
        raise ValueError("Evaluation RNG isolation currently supports CPU models only")
    modes = [(module, module.training) for module in model.modules()]
    try:
        with torch.random.fork_rng(devices=[]), torch.no_grad():
            torch.random.default_generator.manual_seed(seed)
            model.eval()
            return _acoustic_step(model, None, batch, vocabulary_size=vocabulary_size,
                                  objective=objective, objective_id=objective_id, evaluation=True)
    finally:
        for module, training in modes:
            module.training = training


def _acoustic_step(model, optimizer, batch: dict, *, vocabulary_size: int,
                   maximum_gradient_norm: float = 1.0, objective=None,
                   objective_id: str = "mel-l1", evaluation: bool = False) -> dict:
    """Update a supplied model adapter from one freshly admitted paired batch.

    Adapter: model(inputs) -> float32 [1,T,mel_bins]. Inputs are frame-domain
    phoneIds, f0Hz, voiced, midi, rest, slur, each [1,T]. This is not an export
    signature or an assumption that arbitrary upstream models accept this call.
    Caller owns architecture, seeds, context/halo policy, source authority and
    run/checkpoint transaction. On any failure, discard the training attempt:
    forward execution may mutate model buffers even before an optimizer step.
    """
    import numpy as np
    import torch
    if (not isinstance(objective_id, str) or not 1 <= len(objective_id.encode()) <= 128
            or any(ord(c) < 32 or ord(c) == 127 for c in objective_id)
            or (objective is None and objective_id != "mel-l1")
            or (objective is not None and (not callable(objective) or objective_id == "mel-l1"))):
        raise ValueError("Custom objectives require a distinct explicit identity")
    if batch.get("partition") not in (("validation", "test") if evaluation else ("train",)):
        raise ValueError("Batch partition does not match training/evaluation mode")
    if type(vocabulary_size) is not int or not 1 <= vocabulary_size <= 4096:
        raise ValueError("Invalid training vocabulary size")
    if (type(maximum_gradient_norm) not in (int, float) or not math.isfinite(maximum_gradient_norm)
            or not 0 < maximum_gradient_norm <= 1000):
        raise ValueError("Invalid gradient norm limit")
    target = batch["melTargets"]
    if (not isinstance(target, np.ndarray) or target.dtype != np.dtype("float32")
            or target.ndim != 2 or not 1 <= target.shape[0] <= 4096 or not 1 <= target.shape[1] <= 512
            or not np.isfinite(target).all()):
        raise ValueError("Expected finite bounded float32 mel targets")
    count, bins = target.shape
    loss_mask = batch.get("lossMask", [True] * count)
    if (not isinstance(loss_mask, list) or len(loss_mask) != count
            or any(type(value) is not bool for value in loss_mask) or not any(loss_mask)):
        raise ValueError("Loss mask must select at least one core frame")
    columns, hop = batch["columns"], batch["hopSize"]
    keys = ("phoneId", "f0Hz", "voiced", "midi", "rest", "slur", "validSamples")
    breathiness_values = columns.get("breathiness", [0.0] * count)
    if (type(hop) is not int or not 1 <= hop <= 8192 or any(len(columns[key]) != count for key in keys)
            or not isinstance(breathiness_values, list) or len(breathiness_values) != count):
        raise ValueError("Training batch clocks differ")
    for i in range(count):
        phone, f0, voiced, midi, rest, slur, valid = (columns[key][i] for key in keys)
        breathiness = breathiness_values[i]
        if (type(phone) is not int or not 1 <= phone <= vocabulary_size
                or type(f0) not in (int, float) or not math.isfinite(f0) or not 0 <= f0 <= 20000
                or type(voiced) is not bool or voiced != (f0 > 0)
                or type(rest) is not bool or type(slur) is not bool
                or type(breathiness) not in (int, float) or not math.isfinite(breathiness)
                or not 0.0 <= breathiness <= 1.0
                or (rest and (midi is not None or slur))
                or (not rest and (type(midi) is not int or not 0 <= midi <= 127))
                or type(valid) is not int or not 1 <= valid <= hop):
            raise ValueError("Invalid training conditioning values")
    parameters = [p for p in model.parameters() if evaluation or p.requires_grad]
    owned = parameters if evaluation else [p for group in optimizer.param_groups for p in group["params"]]
    if (not parameters or len(owned) != len(parameters) or {id(p) for p in owned} != {id(p) for p in parameters}
            or any(p.dtype != torch.float32 or p.device != parameters[0].device for p in parameters)):
        raise ValueError("Optimizer must own exactly this float32 model's trainable parameters")
    device = parameters[0].device
    inputs = {name: torch.tensor([columns[key]], dtype=dtype, device=device) for name, key, dtype in
              (("phoneIds", "phoneId", torch.int64), ("f0Hz", "f0Hz", torch.float32),
               ("voiced", "voiced", torch.bool), ("rest", "rest", torch.bool), ("slur", "slur", torch.bool))}
    inputs["breathiness"] = torch.tensor([breathiness_values], dtype=torch.float32, device=device)
    inputs["midi"] = torch.tensor([[0 if value is None else value for value in columns["midi"]]],
                                  dtype=torch.int64, device=device)
    inputs["frameOffset"] = batch.get("frameOffset")
    inputs["phraseAnalysisFrames"] = batch.get("phraseAnalysisFrames")
    if "tokens" in batch or "mel2ph" in batch:
        tokens, alignment = batch.get("tokens"), batch.get("mel2ph")
        if (not isinstance(tokens, list) or not 1 <= len(tokens) <= 4096
                or any(type(token) is not int or not 1 <= token <= vocabulary_size for token in tokens)
                or not isinstance(alignment, list) or len(alignment) != count
                or any(type(index) is not int or not 1 <= index <= len(tokens) for index in alignment)
                or alignment != sorted(alignment)
                or any(tokens[index - 1] != columns["phoneId"][frame] for frame, index in enumerate(alignment))):
            raise ValueError("Original tokens and one-based mel2ph alignment disagree")
        inputs["tokens"] = torch.tensor([tokens], dtype=torch.int64, device=device)
        inputs["mel2ph"] = torch.tensor([alignment], dtype=torch.int64, device=device)
    expected = torch.tensor(target.copy(), dtype=torch.float32, device=device)[None, :, :]
    weights = torch.tensor(columns["validSamples"], dtype=torch.float32, device=device)[None, :, None] / hop
    weights *= torch.tensor(loss_mask, dtype=torch.float32, device=device)[None, :, None]
    if not evaluation:
        model.train()
        optimizer.zero_grad(set_to_none=True)
    if objective is None:
        prediction = model(inputs)
        if (not isinstance(prediction, torch.Tensor) or prediction.shape != expected.shape
                or prediction.dtype != torch.float32 or prediction.device != device or not torch.isfinite(prediction).all()):
            raise ValueError("Acoustic model returned invalid targets")
        element_loss = (prediction - expected).abs()
    else:
        # The model-specific adapter owns noise/timestep sampling and normalization.
        # Return unreduced [1,T,M] losses so core/tail masking stays authoritative.
        element_loss = objective(model, inputs, expected)
    if (not isinstance(element_loss, torch.Tensor) or element_loss.shape != expected.shape
            or element_loss.dtype != torch.float32 or element_loss.device != device
            or (not evaluation and not element_loss.requires_grad) or not torch.isfinite(element_loss).all()
            or torch.any(element_loss < 0)):
        raise ValueError("Objective must return finite nonnegative differentiable per-element losses")
    loss = (element_loss * weights).sum() / (weights.sum() * bins)
    if not torch.isfinite(loss):
        raise ValueError("Acoustic loss is nonfinite")
    norm = None
    if not evaluation:
        loss.backward()
        norm = float(torch.nn.utils.clip_grad_norm_(parameters, maximum_gradient_norm, error_if_nonfinite=True))
        optimizer.step()
    if any(not torch.isfinite(p).all() for p in parameters):
        raise ValueError("Optimizer produced nonfinite parameters; discard this attempt")
    return dict(loss=float(loss.detach()), objectiveId=objective_id, gradientNorm=norm, evaluation=evaluation,
                partition=batch["partition"], analysisFrames=count, lossFrames=sum(loss_mask),
                validSamples=sum(value for value, included in zip(columns["validSamples"], loss_mask) if included), sourceId=batch["sourceId"],
                trainingAdmitted=False, releaseEligible=False)
