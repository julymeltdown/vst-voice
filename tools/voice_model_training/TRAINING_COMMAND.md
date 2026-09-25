# Reviewed CPU training command

## Qualifying a candidate on held-out material

```sh
python -m tools.voice_model_training qualify-candidate \
  /absolute/run/qualification.json EXACT_CONFIGURATION_SHA256 \
  /absolute/build/seam_neural_worker /absolute/run/qualification.dossier.json
```

The captured configuration names the admitted bundle (directory, modelId, modelVersion,
manifestSha256, maximumBundleBytes, optional inferenceSteps in 1..1000; legacy
schema-1 captures without it use 10), 1..256 held-out items (item and song identity,
phones, frame count, target F0 and gain) and 2..5 repetitions. The command drives the
production worker over every item and writes a dossier that keeps each automatic
criterion separate: bundle admission, response binding to the exact request bytes,
model and bundle digests and the selected inference-step identity,
vocabulary coverage of the held-out phones, determinism across repeated identical
requests, finite non-silent audio, and runtime when a per-item budget is declared.
Intelligibility, identity and musicality are always UNRESOLVED, because they need
independent listeners. The verdict is FAILED when an automatic criterion fails and
UNRESOLVED otherwise: this command never prints QUALIFIED, sets releaseEligible false
and records no approval. Exit status is 0 for a dossier with no failed criterion, 4
when a criterion failed (the dossier is still written so the failure stays auditable)
and 2 for a configuration or argument error. The dossier path must be new.

Arithmetic fixture graphs pass every automatic criterion and still produce an
UNRESOLVED verdict. That is the intended result: deterministic machine behaviour is not
evidence about a voice.

## Exporting a completed local checkpoint

Install the combined `requirements-onnx-export-check.txt` environment. The export
command loads a trusted local checkpoint, checks its captured architecture and
vocabulary, and requires the exact acoustic profile used in training:

```sh
build/neural-runtime/diffsinger-model-env/bin/python -m tools.voice_model_training.export \
  --checkpoint /absolute/run/epoch-000002 \
  --receipt-sha256 EXACT_CHECKPOINT_JSON_FILE_SHA256 \
  --profile /absolute/data/profile.json --profile-sha256 EXACT_PROFILE_FILE_SHA256 \
  --trusted-checkout /absolute/trusted/DiffSinger \
  --output /absolute/run/new-acoustic-export
```

`profile.json` contains the acoustic target record's `profile` object alone. Its
exact file hash is separate from the canonical profile hash inside the checkpoint;
both are checked. Export creates `acoustic.onnx` and then `export.json` in a new
directory. The latter binds graph bytes, checkpoint receipt/binary, vocabulary,
profile and pinned upstream revision. Offline graph inspection and a five-frame
ORT smoke run must pass before publication. Existing output is never overwritten.

This is an unqualified acoustic artifact, not an admitted model bundle or audio
export. Rights are not freshly approved by this command. It does not export a
vocoder, validate singing quality or grant redistribution permission. Use only
trusted locally produced checkpoints and trusted upstream source; neither Torch
deserialization nor source execution is an untrusted-input sandbox. A graph file
without the final export record is incomplete. Check process exit status as well
as output files, because native teardown failures can occur after publication.

## Training

This entry point initializes or resumes a DiffSinger DDPM model and runs complete
train-partition epochs. It is a usable training execution primitive, not a finished
model-production workflow. It does not export
ONNX, run a vocoder, produce a singing WAV or confer release approval.
Each run receipt binds a bounded content fingerprint of the Python runtime and
installed distribution files. Exact resume requires the same fingerprint, so a
package update or in-place environment change cannot silently continue the run.
The capture hashes the installed files at startup. For the supported local
training-check target (macOS Apple Silicon, Python 3.11), use the composite
hash-locked dependency set in
`requirements-training-macos-arm64.lock.txt`; it covers acoustic/vocoder
training and ONNX export checks. Rebuild in a new environment from the repository
root with:

```sh
uv venv --python 3.11 build/neural-runtime/diffsinger-repro
uv pip sync --python build/neural-runtime/diffsinger-repro/bin/python \
  --require-hashes --strict \
  tools/voice_model_training/requirements-training-macos-arm64.lock.txt
build/neural-runtime/diffsinger-repro/bin/python -m pip check
```

This lock is target-specific, contains hashes for binary artifacts, and is not a
cross-platform lock. Runtime fingerprinting detects installed-content drift;
neither mechanism proves corpus rights, model quality, or release eligibility.

## Inputs and invocation

Run from the repository root using the isolated model environment described in
`README.md`. Supply a clean, trusted openvpi/DiffSinger checkout at revision
`336cf01b57f2ad44c6b37a79cf33993043291759`. The checkout executes Python code and
is not sandboxed; do not use an untrusted checkout merely because Git reports clean.

```sh
build/neural-runtime/diffsinger-repro/bin/python -m tools.voice_model_training.train \
  --training-config /absolute/run/training.json --training-sha256 TRAINING_FILE_SHA256 \
  --dataset-config /absolute/data/dataset.json --dataset-sha256 DATASET_CONFIG_FILE_SHA256 \
  --source-root /absolute/data \
  --targets /absolute/targets/inventory.json --targets-sha256 INVENTORY_FILE_SHA256 \
  --conditioning /absolute/data/conditioning \
  --trusted-checkout /absolute/trusted/DiffSinger \
  --output /absolute/run/new-checkpoint \
  --rights-policy-sha256 INDEPENDENT_CANONICAL_RIGHTS_POLICY_SHA256 \
  --label-policy-sha256 INDEPENDENT_CANONICAL_LABEL_POLICY_SHA256
```

Uppercase tokens are required replacements, not literal working digests. File
digests hash exact file bytes. Policy anchors hash canonical policy JSON using
the review contract; do not confuse those with raw policy-file digests. Select
current anchors independently of supplied review packets. Never use the diagnostic
fixture keys for production material.

The dataset configuration is the existing `training-dataset-config` schema used
by `assemble-dataset`, including six captured configuration/review/policy files.
Create conditioning shards with that command first. Training reuses and verifies
them without rewriting them. Every source requires fresh source and label admission;
all three partitions must exist, with no unresolved duplicate-selection issue.

## Training configuration

The schema is closed: all fields below are required and unknown fields reject.
This tiny configuration is for integration checks, **not a recommended singer
architecture or evidence that eight diffusion steps provide adequate quality**.

```json
{
  "formatId": "com.project-seam.ddpm-training-config",
  "schemaVersion": 1,
  "hiddenSize": 32,
  "encoderLayers": 1,
  "channels": 32,
  "layers": 2,
  "timesteps": 8,
  "seed": 17,
  "learningRate": 0.001,
  "maximumUpdates": 1000,
  "maximumSeconds": 600,
  "loss": "l2"
}
```

Bounds: even hidden size 16–256; encoder layers 1–8; WaveNet channels 16–256;
WaveNet layers 1–16; diffusion timesteps 8–1000; seed 0 through 2^63−1; learning
rate greater than zero and at most 0.1; updates 1–100000; seconds greater than zero
and at most 86400; loss `l1` or `l2`. Integer fields reject booleans. Settings are
configuration limits, not a hard process-memory or execution-time guarantee.

Architecture v1 uses two attention heads, GELU, zero dropout, positional embedding,
non-shallow DDPM/WaveNet, base token/alignment/F0 conditioning, linear beta schedule
with maximum 0.02, and fixed log-mel normalization bounds −12 to 0. Speaker,
language and advanced expression embeddings are not implemented in this command.
The captured vocabulary determines token count; the common acoustic target profile
determines mel-bin count. No resampling or profile conversion occurs.

## Target inventory

```json
{
  "formatId": "com.project-seam.training-target-inventory",
  "schemaVersion": 1,
  "profileSha256": "CAPTURED_PROFILE_SHA256",
  "targets": [
    {
      "sourceId": "source-001",
      "record": "source-001-target.json",
      "recordSha256": "EXACT_TARGET_RECORD_FILE_SHA256",
      "binary": "source-001.f32le"
    }
  ]
}
```

Include exactly the dataset source IDs, including validation and test. Each record
is an acoustic target record from the existing extractor; its binary digest and
geometry bind the actual float32 little-endian matrix when consumed. Record and
binary names are flat ASCII filenames beside the inventory. Copying extractor
outputs into this layout requires preserving exact bytes, not editing recorded
digests to accommodate altered content. All records must select the same profile.
The inventory contains at most 10000 records and aggregate captured record JSON is
limited to 32 MiB. The training iterator reads only train-partition target matrices.
This command does not claim to have evaluated validation or test acoustics.

## Completion and failure

### Multiple epochs in one process

Add `--epochs N` (1–1000; default 1). For N greater than one, `--output` becomes
a new run directory with `epoch-000001`, `epoch-000002`, etc., numbered from the
resumed completed-epoch count when applicable. Every epoch keeps its own complete
checkpoint and receipt. `run.json` is published only after every requested epoch
finishes; a failed run retains earlier usable checkpoints but has no completion
record. Resume explicitly from a completed epoch directory, not the run directory.

`--maximum-run-seconds` bounds cooperative run time after model initialization
(default 3600; greater than zero and at most 86400). The smaller remaining run
budget and configured per-epoch budget applies to each epoch; this is not a hard
OS process timeout. `--maximum-total-checkpoint-bytes` bounds binary checkpoint
writes across this invocation (default 2 GiB, maximum 8 GiB), excluding JSON
receipts and the run summary. Each individual binary remains capped at 512 MiB.
All completed checkpoint files are retained; there is no automatic pruning.

Fresh source/label/shard admission repeats for each epoch. Dataset identity must
remain unchanged, optimizer/RNG state stays live between epochs, and each receipt
links to the preceding receipt. These controls do not implement validation-driven
early stopping, learning-rate scheduling, review renewal or dataset migration.

### Continuing a completed local epoch

Add both `--resume /absolute/run/previous-checkpoint` and
`--resume-receipt-sha256 EXACT_PREVIOUS_CHECKPOINT_JSON_FILE_SHA256` to the same
command, and select a new `--output` directory. Only trusted, locally produced
checkpoints are supported; this is not a safe arbitrary downloaded model importer.
The captured receipt and binary hashes are checked before loading weights.

Resume requires identical captured training configuration, assembly configuration,
target inventory, vocabulary, upstream architecture and recorded environment.
It restores strict model state, AdamW state and CPU Torch RNG, then freshly admits
the source/label/conditioning inputs and requires the same dataset identity before
any optimization. Checkpoints record `completedEpochs` and `parentReceiptSha256`.
Expired reviews reject; this first continuation contract does not yet support
review renewal or intentional dataset/configuration migration. Older checkpoints
without completed-epoch lineage are not silently upgraded.

The upstream integration diagnostic resumes the same first-epoch checkpoint in
two separate processes and requires identical second-epoch loss, model tensors
and CPU RNG state, with both outputs retaining their parent receipt identity.

### Outputs

The output must be new and its parent must exist. Success returns zero, prints a
compact JSON result and publishes `checkpoint.pt` followed by `checkpoint.json`.
Metadata binds configuration files, target inventory, upstream revision, vocabulary,
settings and observed Torch/NumPy versions. `singerQualified` and `releaseEligible`
remain false. The dependency environment is not a fully hashed production lock.

Whole phrases must contain at most 4096 analysis frames. Coverage must exhaust all
training phrases; reaching an update budget early is failure, not epoch completion.
Review expiry and cancellation are checked cooperatively between operations, and
fresh source/label/shard admission repeats before checkpoint receipt publication.
Ctrl-C returns 130; handled invalid inputs/execution failures return 2. Discard an
in-memory attempt after any failure. A failed final publication check can leave an
incomplete binary without a receipt. No automatic overwrite or cleanup is performed.

Verification currently includes a real subprocess run with the upstream model,
three distinct oscillator fixtures, fixture-only dual signatures, one fully covered
train phrase and checkpoint reload. That demonstrates execution, not lawful real
corpus availability, intelligibility, female voice identity or Beta readiness.
