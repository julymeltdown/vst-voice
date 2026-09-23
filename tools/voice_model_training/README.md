# Original voice model production

Vocoder runs with `excitationNoiseId` now support the same verified partial
checkpoint resume as other segmented GAN runs. A schema-2 partial cursor stores
domain-separated raw-draw and realized-noise digest chains through its exact
completed segment prefix. Resume checks the captured excitation identity,
restores the complete model/optimizer/RNG state, and advances those chains for
the remaining segments. New complete noise epochs use schema 2 and record
`excitationDigestAlgorithm=segment-chain-sha256-v1`; its digest fields are chain
commitments, not SHA-256 of concatenated PCM/noise bytes. Schema-1 cursors remain
the unchanged no-excitation format, and complete legacy schema-1 noise receipts
retain their original concatenated-digest interpretation at export. An interrupted/resumed optimizer run is
tested against an uninterrupted run for identical epoch identity and weights.
This is reproducibility infrastructure, not authorization or singer quality.

Vocoder training config schema four adds mandatory `objectiveId` to the schema
three fields. Choose `nsf-lsgan-logmel-48k80-v1` for the existing objective or
`nsf-lsgan-logmel-uvperiodic-48k80-v1` for the experimental Japanese-phone
periodicity term (fixed coefficient 1). Older configurations are unchanged.
An objective change cannot be applied with exact `--resume`. Export requires
matching objective metadata and explicit schema-four settings for the new term.
This does not authorize training data or qualify the resulting singer.

For a controlled objective/learning-rate experiment, use `--warm-start DIR`
together with `--warm-start-receipt-sha256 SHA`, instead of either resume mode.
This requires identical architecture, dataset/profile, source/target bindings,
runtime and remaining settings. It copies both generator and discriminator
weights, creates fresh optimizer/scheduler state, resets RNG, and starts a new
epoch-one lineage pointing to the parent receipt. The source checkpoint is not
modified. Subsequent exact or partial resume preserves that warm-start origin.

Frozen evaluation reference capture (not a training corpus):

```sh
python -m tools.voice_model_training.render_frozen_evaluation \
  --plan /absolute/plan.json --plan-sha256 PLAN_SHA256 \
  --history /absolute/historical-phrase-index.json \
  --pilot /absolute/build/release/seam_singer_pilot --output /absolute/new-output
```

This checks all phrase collisions before rendering, verifies captured project
fingerprints and voice recipe, and retains every failed item. `capture.json`
reports `INCOMPLETE` unless all items pass and the pilot executable is unchanged.
No training admission, candidate evaluation or musical qualification is implied.

`python -m tools.voice_model_training.frozen_evaluation_campaign` consumes
`--plan`, `--history`, `--capture`, `--preparation`, explicit SHA-256 arguments
for the plan/capture/preparation, and the usual `--bundle`, `--renderer`,
`--pitch-executable`, `--vocoder-export`, `--vocoder-checkpoint`, `--output`.
It requires every frozen phrase in order, verifies captured source/project bytes
and recipe provenance, and restricts this initial run to the model manifest frozen
in the plan. Its scope is `frozen-same-voice-unseen-phrase-engineering-only`.
It shares the native execution/measurement loop with validation campaigns without
relaxing their validation-partition admission. Failed renders stay in the report;
an execution pass does not mean a pitch or musical-quality pass.

After the final campaign receipt is published, use
`python -m tools.voice_model_training.summarize_frozen_campaign --campaign PATH
--campaign-sha256 SHA --preparation PATH --pitch-executable PATH --output NEW`.
It remeasures every successful WAV against the captured executable, checks the
full preparation cohort, and binds score/rest diagnostics to each comparison.
The full-cohort aggregate is null if any execution fails or lacks measurable
pitch. Silence is checked separately per channel; no-rest songs are not silence
passes. Reference mismatch diagnostics do not replace the product contract's
steady-frame, median-pitch or independent musical-review requirements.

The real export now passes native acoustic smoke execution and wrong-hash rejection.
To meet native finite-tensor intake, encoder export maps only recognized scalar
negative-infinity Where mask constants to finite float32 floor; other nonfinite
constants reject. A later parent diagnostic shutdown still SIGABRTed despite ORT
telemetry disabling. Thus the local native subprocess passed, but full runtime
teardown reliability remains unresolved; process exit status must remain a gate.

The optional `--native-probe build/release/seam_onnx_runtime_probe` argument to
`check_diffsinger_model ... --check-onnx` now sends the actual exported checkpoint
graph through the native C++ runtime probe. `--acoustic-export GRAPH SHA256` checks
the captured hash and native offline inspection before creating an ORT session,
then tests finite dynamic BTF mel at 3/16/23 frames. A wrong digest must reject.
This mode requires a build with native graph inspection and retains the probe's
16 MiB graph intake limit. Native telemetry is disabled. This is learned acoustic
execution in a diagnostic executable, not a production worker or vocoder output.

`python -m tools.voice_model_training.export` now publishes a persistent acoustic
graph and provenance record from a captured local checkpoint, after strict model
loading, matching profile/configuration/vocabulary, offline graph inspection and
ORT smoke execution. See `TRAINING_COMMAND.md` for invocation. This does not grant
rights or model-bundle admission and does not produce vocoder audio. Encoder export
examples now also support a one-symbol vocabulary rather than assuming three IDs.

The ONNX diagnostic now compares the actual trained denoiser with identical
PyTorch/ORT inputs at 3,16,23,257 frames and first/last diffusion timesteps. Eight
cases met tolerance; maximum error 7.450581e-8. This verifies deterministic backbone
numerics, not full stochastic sampler parity. One run printed passing numerical
results but aborted during teardown (exit 134); its macOS crash stack points to
`onnxruntime_pybind11_state.so`'s Microsoft telemetry worker locking a recursive
mutex during HTTP response handling. A pre-change repeat exited zero. Export
diagnostics now explicitly disable ORT telemetry before sessions; this setting is
not proof of a fully resolved native teardown race or a completed reliability soak.

`check_diffsinger_model ... --check-onnx` now additionally exports and merges the
actual encoder and DDPM diffusion into one `tokens/durations/f0/steps → mel` graph.
A typed non-shallow entry avoids upstream unannotated optional-argument JIT errors;
its seeded Torch outputs must match upstream exactly at steps 1, 4 and 8 before
serialization. Graph merging preserves enclosing initializer references inside
If/Loop bodies. The 845063-byte merged graph passed SEAM offline inspection and
ONNX Runtime execution at 3/16/23 frames and steps 4/1/8. Outputs are finite with
the expected [1,T,80] shape. Stochastic cross-runtime numerical parity is not yet
verified; no vocoder, retained model bundle or native singer render is claimed.

The optional `check_diffsinger_model ... --check-onnx` diagnostic now exports actual
trained duration-encoder weights and executes the graph in ONNX Runtime 1.30.0.
Install `requirements-onnx-export-check.txt` together to retain NumPy 1.26.4 and
compatible ml-dtypes 0.5.3 (ml-dtypes 0.6 requires NumPy 2). `pip check` passed.
Five runtime cases cover dynamic token/frame axes, zero-duration phones and 1025
tokens; maximum observed absolute error is 9.536743e-7. The exporter materializes
the upstream positional table for 4096 tokens before tracing to avoid freezing its
initial 1024-entry capacity. Host-side input bounds are still required; this is not
a claim that arbitrary graph inputs are safe. Tracer/constant-fold warnings remain
visible. The 644485-byte graph is an intermediate encoder only, not a complete
acoustic graph, vocoder or release artifact. Next: diffusion serialization and merge.

`export_adapter.prepare_acoustic_export` now strictly loads owned training weights
into the pinned upstream deployment architecture in an isolated process. It binds
the captured natural-log mel profile to `mel_base=e`, avoiding the upstream default
log10-to-ln multiplication. Actual-model diagnostic conditioning matches exactly
for duration vectors [5,6,5], [1,0,2] and [9,3,11]; deployment diffusion produces
finite [1,23,80] mel output. This is a Torch deployment-model bridge only: ONNX
serialization/runtime verification and vocoder/native rendering are not completed.

`train --epochs N` now runs bounded continuous epochs with fresh admission at each
boundary, live optimizer/RNG state, parent-linked epoch checkpoints and a final
`run.json` only on complete success. Total binary output and cooperative run-time
budgets are explicit. Separate resumed invocations remain supported. See
`TRAINING_COMMAND.md`; automatic quality-based stopping and review renewal remain
unfinished, alongside model export and actual singer qualification.

The training command also supports `--warm-start CHECKPOINT` with
`--warm-start-receipt-sha256 DIGEST` for a **new experiment**, not exact resume.
It loads verified model weights only, resets optimizer and CPU RNG, starts epoch
numbering at one, and records the original receipt/binary/configuration hashes
and source epoch in every new checkpoint. Only `loss` and `learningRate` may
change; unchanged settings are allowed as a reset-optimizer control arm.
Architecture, vocabulary, dataset/targets, profile, runtime and other settings
must match. Normal fresh source/label admission still applies before every epoch.
The parent checkpoint is not edited. The two initialization modes are mutually
exclusive and each requires its own paired receipt digest. Exact resume of a
warm-start experiment preserves its recorded origin and restores optimizer/RNG.
This is experiment infrastructure, not evidence that changing the loss improves
singing. Evaluate generated output between short intervals; lower loss alone
must not select a replacement model.

Compare two frozen application campaigns with:

```sh
python -m tools.voice_model_training.compare_campaigns \
  --baseline /absolute/baseline/campaign.json --baseline-sha256 BASELINE_SHA256 \
  --candidate /absolute/candidate/campaign.json --candidate-sha256 CANDIDATE_SHA256 \
  --pitch-executable /absolute/build/release/seam_voicebank_cli \
  --output /absolute/new-paired-report.json
```

This re-measures captured source/master WAVs with the recorded native extractor,
checks saved metrics, and requires the same complete source/project selection,
silence policy and executable identities. Each song retains its changes and
regressions. Failed/unmeasurable songs suppress the complete aggregate instead
of disappearing from it. A better within-tolerance fraction does not erase lost
measurable coverage. `NO_REGRESSIONS_ON_REPORTED_METRICS` is descriptive only,
never singer qualification, automatic model promotion or independent-holdout
proof. Vocoder training-overlap disclosures remain in the output.

`python -m tools.voice_model_training.training_ancestry --config CONFIG.json
--config-sha256 SHA256 --output NEW.json` audits declared complete checkpoint
histories without loading Torch weights. The hash-bound configuration names
`acousticLeaf` and `vocoderLeaf` receipt digests, a `receipts` digest-to-file
index, captured `snapshots` (`path`, `sha256`), and up to 256 explicit `candidates`
with `sourceId`, `songId`, `sessionId`, `lineageId`, and `audioSha256`.
Use format `com.project-seam.training-ancestry-audit-config`, schema 1.

It rejects missing/cyclic histories, incomplete epochs, wrong warm-start parent
links, changed resume settings, changed dataset splits and mismatched training
coverage. It joins source identities across every declared ancestor and detects
exact-audio aliases as well as shared song/session/lineage IDs. Retained receipts
are sufficient for this declared-history audit, not for restoring deleted model
binaries. `NO_OVERLAP_IN_DECLARED_FIELDS` is not independence: recipe equivalence,
undeclared pretraining, source rights and musical qualification remain separate
and are never approved by this command.

The training command supports `--resume` with a separately captured
`--resume-receipt-sha256`. It restores local model/optimizer/CPU RNG state, requires
unchanged captured configuration and environment, and matches the freshly admitted
dataset before training. New checkpoints retain completed-epoch count and parent
receipt identity. See `TRAINING_COMMAND.md` for the strict continuation contract;
review renewal and quality-based scheduling are not yet supported.

The standalone `python -m tools.voice_model_training.train` command now initializes
a bounded CPU DiffSinger DDPM model from captured settings and runs reviewed
epochs into new checkpoint directories. See [TRAINING_COMMAND.md](TRAINING_COMMAND.md)
for exact schemas, invocation and limitations. The actual upstream diagnostic
also runs this command as a subprocess and reloads its checkpoint. Quality-based
scheduling, export/vocoder/native rendering and vocal qualification remain
unfinished; the command is not a completed production-model workflow.

`load_dataset_inputs(config, sha256, root, rights_anchor=..., label_anchor=...)`
is the shared read-only configuration boundary for assembly and training owners.
It verifies the assembly document and all six referenced file hashes, preserves
independently supplied policy anchors, and returns the thirteen admission inputs
without publishing a snapshot or granting authority. The reviewed-run diagnostic
now uses this same file-backed path. Source/signature/lifetime checks still occur
at assembly/use time. The training command above uses this same loader.

The pinned `check_diffsinger_model` diagnostic also executes `check_reviewed_run`:
three temporary oscillator WAVs, actual PCM/mel capture, fixture-only signed
rights and label reviews, fresh sharded admission, the connected training service,
checkpoint reload and a separate validation source. The upstream DDPM run passed
with 43 changed parameter tensors and exact checkpoint parameter restoration.
One train phrase (16 frames / 4096 samples) was consumed; train/validation/test
each contain one distinct fixture. A separate unit integration test exercises the
same real I/O and signing path with a tiny scalar Torch model, without mocked
admission or optimization. These public test keys are never production trust
keys; oscillator labels do not establish lyric supervision or singer quality.
All temporary material/checkpoints are removed by the diagnostic's temporary
directory lifecycle. This is still not production corpus training or release GO.

`training_run.train_reviewed_epoch` connects fresh dual-review dataset admission,
read-only conditioning reuse, paired acoustic batches, exact train-partition
coverage, optimization and checkpoint publication. Supply the thirteen captured
`assemble_dataset` inputs (excluding `now` and conditioning options), independently
selected policy anchors, target records/paths, profile hash, owned CPU model and
optimizer, run metadata, budgets and a new checkpoint directory. The service
checks cancellation and review expiry between updates, and refreshes admission
after optimization and again after checkpoint serialization before the receipt.
Any failure invalidates the in-memory attempt; discard it. A failed final check
may retain an incomplete binary without a completion receipt. Whole phrases are
limited to 4096 frames; no duration-altering chunk fallback is used. Caller-owned
target metadata and model-code provenance remain explicit prerequisites.
Orchestration contract tests use mocks; they are not a real-corpus training study.
Multi-epoch production training, real reviewed corpus, model export and ordinary
native song-render integration remain unfinished. No release approval is issued.

`assemble-dataset ... --conditioning-directory EXISTING_DIRECTORY
--reuse-conditioning` now performs fresh source/review admission and reconstructs
expected conditioning, then compares existing shard bytes without writing them.
A new snapshot is still required. Unchanged inputs retain dataset identity;
changed source audio, reviews, labels or shard bytes reject rather than repairing
the cache or silently accepting an old receipt. The API exposes the same behavior
as `assemble_dataset(..., reuse_conditioning=True)`. This supplies a revalidation
path for run/epoch boundaries; it does not itself launch or authorize a training
transaction. Other unrelated files in the shard directory are neither loaded nor
removed. Stable parent directories remain a caller responsibility.

`checkpoint.load_local_checkpoint(directory, receipt_sha256=...)` now loads only
through a captured completion-record digest. It checks the fixed binary path,
current Torch version, coverage-complete metadata, byte limits, regular/non-symlink
files and exact binary/metadata hashes before `weights_only=True` loading from
owned bytes. Embedded metadata must match the receipt. The caller must trust the
producer and receipt digest: this is not a hostile third-party archive importer,
and byte limits do not prove bounded tensor allocation for arbitrary archives.
Fresh source/review authority is still required before resuming actual training.
The real DiffSinger diagnostic now uses this loader and still reproduces inference
and the next update exactly; corruption tests prove changed bytes never reach
Torch deserialization.

`checkpoint.publish_checkpoint` publishes locally produced CPU model/optimizer
and RNG state with captured run metadata and a coverage-complete epoch record.
It creates a new directory, bounds checkpoint writes (default/maximum 512 MiB),
flushes and fsyncs `checkpoint.pt`, then publishes `checkpoint.json` with exact
binary and metadata hashes. A `before_publish` callback lets the caller recheck
current authority before the completion record. Existing directories reject;
failed or expired attempts retain incomplete artifacts without that record.
This does not authenticate supplied epoch records or grant training/release rights.
Metadata is JSON-captured with a 1 MiB cap; the caller owns stable model state.
Written-byte limits are not serializer memory/time bounds. Directory-fsync
power-loss durability, hostile-directory races and untrusted checkpoint import
are not claimed. The actual DiffSinger diagnostic now uses this publication path
for its temporary restore/resume check instead of a standalone `torch.save` call.

`optimization.acoustic_evaluation_step` evaluates validation/test batches without
an optimizer or gradients, using the same objective and core/sample weighting as
training. The current CPU path preserves Torch RNG and each module's previous
training/evaluation mode, including mixed submodule modes, even on failure. It
does not erase existing gradients. An explicit evaluation seed makes diffusion
objective comparisons repeatable. GPU/distributed RNG isolation and arbitrary
custom buffer mutation are not covered. Loss is not perceptual vocal quality.
The real model diagnostic exercises this path on its existing engineering fixture
and explicitly reports that reuse; it does not call that a held-out study.

The latest real-model check replaces its earlier constant 8-bin mel fixture with
80-bin targets extracted from a digest-verified, original two-oscillator PCM WAV.
Engineering dataset identity now hashes actual source/target/profile records and
conditioning, replacing placeholder hashes. The temporary checkpoint binds this
identity too. Current measurements supersede the older constant-target numbers:
58,704 parameters; eight fixed-noise update losses 0.9959463 → 0.9717840; finite
`[1,16,80]` inference; exact restored inference and next update; 770,715-byte
temporary checkpoint. Token labels remain synthetic interface labels, not
claims of intelligible phonemes in the oscillator waveform. This is an integrated
audio-extraction/model test, not a lawful reviewed singing corpus or voice model.

Epoch execution can now require `expected_source_frames`, captured from the
selected admitted partition. Core offsets must advance contiguously per source,
halos must match the exact boolean loss mask, and exhaustion must cover every
expected frame. Duplicate cores, skipped starts, missing sources and inconsistent
masks reject. Schema-2 results distinguish `coverageVerified` from mere iterator
completion. Omitting this inventory is still permitted for generic experiments
but provides no dataset-coverage proof. The real-model check now runs eight
single-phrase epochs with explicit complete-frame coverage, rather than treating
eight repeated updates as one dataset pass.

`optimization.run_acoustic_epoch` consumes an admitted batch iterator through the
update primitive, requiring captured dataset/profile hashes and an update limit.
It aggregates loss by valid sample count, checks cancellation and a cooperative
deadline between operations, and returns completion only after iterator exhaustion.
Empty input, excess work, changed identities and late I/O errors invalidate the
attempt; reaching the limit alone does not mean completion. No checkpoint is
published. Caller still owns actual admission, sampler/coverage policy, process
supervision and failed-attempt disposal. A stalled I/O call or GPU kernel needs
external supervision; the cooperative deadline is not a hard timeout.

The actual upstream model experiment now uses this runner for eight synthetic
updates and still passes strict checkpoint restoration and resumed-update checks.
The synthetic experiment hashes are explicit placeholders, not real approvals.

## Real upstream model integration check

`python -m tools.voice_model_training.check_diffsinger_model TRUSTED_CHECKOUT`
requires a clean DiffSinger checkout at the pinned revision and the isolated
Python 3.11 environment in `requirements-diffsinger-model-check.txt`. It imports
the actual upstream architecture in its own process, constructs a small random
54,024-parameter non-shallow DDPM/WaveNet configuration, performs eight SEAM
optimizer updates, and runs upstream DDIM inference. It does not download/load
weights, retain a singer checkpoint, or claim an original singer.

The check now saves and restores a self-produced temporary checkpoint containing
model state, optimizer state, CPU RNG, configuration, revision and completed-step
count. Loading uses `weights_only=True` and strict model-state matching. On this
CPU experiment, restored inference and the next resumed update both matched
exactly. The 712,539-byte artifact was removed with its temporary directory; its
digest is printed for the run. This is not a general untrusted-checkpoint loader,
production checkpoint publication, GPU/distributed resume or an export artifact.

Observed locally: 43 parameter tensors changed; fixed synthetic noise loss
decreased from 0.9528179 to 0.9277189; inference returned finite `[1,16,8]` mel.
Fixed-noise repetition is an optimization sanity check, not held-out evaluation.
Upstream source remains unchanged. The environment follows its NumPy <2 and
librosa <0.10 constraints; setuptools 75.8.0 supplies the legacy pkg_resources
import required by librosa 0.9.2. Top-level dependency pins are present; this is
not yet a fully hash-locked production training environment.

`diffsinger_objective.DiffSingerDDPMObjective("l1"|"l2")` now adapts the inspected
upstream training call (`tokens`, `mel2ph`, `f0`, `gt_mel`, `infer=False`) and its
noise prediction/target layout `[1,1,M,T]` into unreduced `[1,T,M]` losses for the
SEAM optimizer. Pass its `objective_id` with the callback. The current adapter
requires non-shallow DDPM and rejects unsupported speaker/language/variance/
keyshift/speed conditioning rather than supplying invented values.

It also requires the entire phrase: the upstream text encoder derives duration
conditioning from mel2ph, so cropped frame context alone changes those durations.
Batch readers now carry `phraseAnalysisFrames`; the adapter checks it and a zero
frame offset. Source-local halo support remains useful for other adapters but
does not authorize chunked DiffSinger training. SEAM's weighted core reduction is
explicitly different from an unweighted mean over padding. Full upstream model
instantiation, extra conditioning, reflow/shallow objectives and duration-preserving
chunking remain required work, not completed by the interface fixture tests.

Batch readers now retain the complete original positive-ID `tokens` sequence
and source-relative, one-based `mel2ph` for each expanded batch. Adjacent repeated
symbols are not collapsed, and phonemes with no sampled hop remain in `tokens`.
The optimizer checks bounds, ordered alignment and agreement with frame phone
IDs, then exposes int64 `[1,N]` tokens and `[1,T]` mel2ph to the model/objective
adapter. These match the upstream task's distinction between text and acoustic
clocks; they do not yet instantiate that task or qualify its text-encoder context.

The optimization primitive also accepts a caller-supplied
`objective(model, inputs, target_mel)` and distinct `objective_id`. It must return
finite, nonnegative, differentiable float32 losses of shape `[1,T,M]`, before
reduction. SEAM then applies its core-frame mask and valid-sample weights. The
adapter owns diffusion/flow noise, timesteps, normalization and auxiliary terms;
an already reduced scalar is rejected because it would bypass frame ownership.
The default remains explicitly named `mel-l1`. This makes different objectives
possible; it does not implement or qualify the full upstream training task.

This distinction was checked against the pinned
[DiffSinger acoustic task](https://github.com/openvpi/DiffSinger/blob/336cf01b57f2ad44c6b37a79cf33993043291759/training/acoustic_task.py):
its DDPM and rectified-flow paths do not simply regress final mel values with
the same objective. No arbitrary model family should be silently forced into
the default direct-regression path.

Both conditioning and supervised batch readers accept `context_frames` (default
zero). They add source-local neighboring frames around each nonoverlapping core,
return `frameOffset` for the expanded input and `coreFrameOffset/coreFrameCount`
for its owned target range, and provide a boolean `lossMask`. Acoustic target
slices use the expanded range too. Core plus both halos must fit 4096 frames;
phrase edges truncate the halo rather than borrowing another source's frames.
The optimizer applies the mask together with valid-sample weights, so overlap
does not duplicate loss. Caller-selected context must cover the chosen model's
receptive field; this is not proof of chunk equivalence for arbitrary models.

`optimization.acoustic_training_step` now performs an actual Torch gradient
update on a caller-supplied model adapter and paired training batch. The adapter
accepts frame-domain phone/F0/voicing/MIDI/rest/slur tensors and returns float32
`[1,T,mel_bins]`; this is an internal adapter contract, not an ONNX export signature.
The primitive uses sample-weighted L1 loss (partial-hop tails retain their valid
sample weight), finite-output checks, gradient clipping with nonfinite rejection,
and exact optimizer/trainable-parameter ownership checks. Held-out/validation
batches reject before model execution. Nonfinite gradients never reach `step`.

The caller still owns architecture, context/halo strategy, initialization/seeds,
current permissions, training-run identity and checkpoint transactions. An error
invalidates the attempt; no rollback of arbitrary model buffers/optimizer state
is promised. No `train` command, singer model or learned checkpoint is claimed.
Run `python -m unittest tools.voice_model_training.test_optimization` in the
isolated Torch environment; default discovery explicitly skips when Torch is absent.

## Numerical frontend comparison checkpoint

Run `python -m tools.voice_model_training.check_acoustic_parity` in the isolated
Python 3.11 environment defined by `requirements-acoustic-parity.txt`. The local
run passed all 24 comparisons: silence, tone, seeded noise and boundary impulse
across three sample-rate/FFT/hop combinations, each using float64 and float32
Torch STFT plus librosa Slaney filters. Maximum absolute log-mel error was
4.7401e-7 against float64 and 0.0016051 against float32; the largest float32
case-mean error was 2.7060e-5. Preset limits were max/mean 2e-6/1e-6 for float64
and 0.01/0.0001 for float32. Exact frame shapes also matched.

This checks independently implemented frontend operations with SEAM's explicit
whole-hop tail padding, not a trained model, augmentation path, arbitrary profile,
or all upstream execution behavior. The comparison environment is build-local;
Torch/librosa are not added to the native application. Dependency versions are
captured, but wheel hashes and other-platform reproducibility remain unqualified.

`batches.iter_supervised_batches` joins schema-3 conditioning with captured
acoustic metadata and binary paths. Supply an exact source-ID map of
`(record, binary_path)`, an independently selected `expected_profile_sha256`,
partition and batch size. It checks source/PCM identities, sample rate, hop,
sample/frame counts, float32 layout, dimensions, finite values and exact binary
digest before returning source-local `melTargets` alongside conditioning columns.
The target matrix is loaded once per source; emitted target slices are owned
copies. Each batch retains dataset, target and profile digests. This is now
paired training data, not an optimizer, trained checkpoint or permission grant.
Callers still own fresh review/source admission and captured metadata provenance.
Profile hashes bind exact canonical numeric representation; consume the hash
from the selected captured profile, not a separately reconstructed equivalent.

## Acoustic target command

`python3 -m tools.voice_model_training acoustic-targets CONFIG CONFIG_SHA256 SOURCE_WAV NEW_DIRECTORY`

The captured JSON requires exactly `formatId` =
`com.project-seam.training-acoustic-config`, integer `schemaVersion` = 1,
`sourceSha256`, `sampleRate`, `fftSize`, `hopSize`, `bins`, `minimumHz`, and
`maximumHz`. This selects the explicit full-hop Slaney profile described below;
it does not select an arbitrary upstream model's frontend. NumPy is optional
for other commands but required here; absence produces a clear exit-2 diagnostic.

Successful extraction writes `mel.f32le` followed by `target.json`. The latter
binds source, configuration, profile, target dimensions and exact target digest.
The source is read as bounded owned bytes, then hash-verified before extraction.
Output must be new; no overwrite or resume is supported. A binary without final
metadata is an incomplete attempt, retained for diagnosis. Retry to a new
directory. This publication does not claim directory-fsync power-loss durability.
Exit 0 means extraction completed, not permission admission, model compatibility,
training success or musical qualification. Dataset-to-target joining remains open.

`acoustics.wav_log_mel_targets` now connects the target extractor to exact WAV
bytes. It verifies the expected container digest, mono PCM geometry and
sample rate through the existing source inspector, decodes 16/24/32-bit signed
little-endian PCM or finite normalized IEEE float32 without resampling or normalization, and returns target data
plus source/PCM identities. The record binds the explicit profile, NumPy version,
target shape/byte count, and SHA-256 of row-major little-endian float32 targets.
The API writes no files and grants no source rights. Tests exercise identical
cross-width signal values, negative full scale, 24-bit sign extension, target
hashes and rejection of mismatched source digest/rate. Cache publication and
training integration still need to consume these records and arrays.

`acoustics.log_mel_targets` implements the explicit SEAM full-hop Slaney profile
using optional NumPy (`requirements-acoustics.txt`). Inputs are normalized mono
samples; output is float32 `[frames, bins]`. It pads the tail with zeros to a
whole hop, reflects `(fft-hop)/2` at the boundaries, applies a periodic Hann
window equal to FFT size, projects unnormalized FFT magnitude through
Slaney-area-normalized filters, and takes `ln(max(mel, 1e-5))`. Clips too short
for reflection reject. Analysis proceeds in blocks of 128 frames, with bounded
input/output and mel-projection work. This is a proposed target profile, not a
claim of compatibility with existing model weights. Keyshift/speed augmentation,
separate window sizes, source-byte intake, and target-cache integration are open.

Profile research inspected [DiffSinger nvSTFT at revision 336cf01](https://github.com/openvpi/DiffSinger/blob/336cf01b57f2ad44c6b37a79cf33993043291759/modules/nsf_hifigan/nvSTFT.py)
and [librosa 0.10.2 mel filters](https://github.com/librosa/librosa/blob/0.10.2/librosa/filters.py).
SEAM's added whole-hop tail padding is deliberate; upstream's default frame count
must not be assumed identical. Current tests establish silence floor, frame count,
log-amplitude scaling, finite float32 output and invalid input rejection. Numerical
differential comparison with the upstream Torch/librosa extractor remains required.
The optional acoustic tests explicitly skip when NumPy is absent; that skip is
not acoustic verification. NumPy 2.4.4 was present and these tests ran locally.

`batches.iter_conditioning_batches(snapshot, feature_directory, partition="train",
batch_frames=256)` reads schema-3 shards into source-local column batches. It
recomputes deterministic partition membership, verifies binding/reference hashes,
reconstructs expected conditioning from captured labels, and compares exact
shard bytes before emitting that source. Batch size is bounded to 1..4096 frames;
the last batch is short, not padded. `sourceFrame`, `frameOffset`, `validSamples`,
and the source ID retain sample-clock ownership. Held-out rows never enter the
selected training partition, and altered shards reject.

This reader is feature I/O only. It does not reauthenticate reviews, enforce
current alignment confidence policy, read original audio, or authorize training.
Its caller must freshly revalidate the dataset before starting a run. A later
shard can fail after earlier batches were consumed; do not publish a checkpoint
from a failed run. Acoustic targets, model optimization and checkpoint/export
integration remain unfinished.

`conditioning.build_conditioning` now expands validated acoustic labels and
explicit-rest score supervision into frame-aligned trainer inputs. It uses
left-edge positions on the label hop clock, ordered positive vocabulary IDs
(zero reserved for padding), separate rest masks, note/phone indices, slur and
syllable ownership, original F0/voicing, and valid sample counts for partial tails.
MIDI zero is not treated as silence. Phone timing remains independent from note
timing to preserve anticipated consonants. Unresolved consistency corrections
reject; a missing review string alone is not authentication and grants no rights.
The transform defaults to a 65,536-frame output limit and never mutates inputs.
This is a Python feature-building API, not yet a trainer or model export path;
callers must independently validate actual source bytes and current approvals.

Implementation owner for M2.P3. No learned model is trained by this directory yet.

`label_report` checks contiguous in-phrase phoneme spans, vocabulary membership,
an explicit alignment-confidence threshold, hop-domain F0/voicing geometry and
voicing consistency. Unknown phones, low confidence and missing supplied review
revisions enter a correction queue; malformed geometry rejects. A supplied review
revision is not authenticated reviewer evidence, and consistency does not admit
training. Note/slur labels, lyric binding, source-digest binding, review provenance
and the captured-config `label-report` command remain to be implemented.

`prepare_sources` connects actual source files to inspection and split records.
Each input has `sourceId`, `songId`, `sessionId`, `lineageId`, contained `path`
and captured `sourceSha256`. It reads at most 64 MiB per file/512 MiB per call,
rejects linked/escaping paths and records per-source failures. Any failure
suppresses the split-ready inventory rather than silently dropping bad sources.
Originals are unchanged. Directory containment checks are not race-free OS
isolation; trusted stable source directories remain required. This is the
preparation inspection stage, not a transform pipeline or authorized-source
admission handle.

The captured-config preparation command is available:

```sh
python3 -m tools.voice_model_training prepare CONFIG_JSON CONFIG_SHA256 SOURCE_ROOT NEW_REPORT_JSON
```

Configuration fields are exactly `formatId`
(`com.project-seam.voice-training-prepare-config`), `schemaVersion` (1),
`sampleRate`, and `sources` (the source records above). Exit 0 means all files
were inspected, not that training is authorized. Exit 3 publishes per-source
rejections and no split-ready inventory. Exit 2 signals configuration/publication
failure. Reports bind configuration SHA-256, actual source and PCM identities;
existing outputs are never replaced. No audio is copied, segmented or transformed.

`inspect_pcm_source` inspects captured mono 16/24/32-bit integer PCM or normalized float32 WAV bytes
under a 64 MiB/ten-minute bound and an explicit expected rate. It verifies the
source file hash and derives a separate geometry-plus-PCM `audioSha256` suitable
for split duplicate grouping. Container-only metadata differences do not hide
identical PCM. Different rates/widths remain distinct; this is not perceptual
duplicate detection. Float32 uses inspection schema 2 with an encoding discriminator
in the audio identity; existing integer schema-1 identities are unchanged. NaN,
infinity and samples outside [-1,1] reject without clipping. Standard and extensible
WAV formats are checked for complete frames, clock/alignment and duplicate chunks.
Stereo and compressed sources require an
explicit future conversion stage, never an implicit rewrite. Source inspection
does not approve permissions, speaker consent, labels or audio quality.

`split_sources` is the first implemented component. It accepts bounded source
identity records and an explicit held-out song set. Connected components across
song, session, retake/derivation lineage and exact audio hashes stay together.
Any component touching an explicit held-out song becomes test-only. Other groups
use seeded SHA-256 allocation (80/10/10 buckets); realized proportions depend on
group sizes and explicit holdouts. Missing partitions are reported, never filled
by splitting related material across boundaries. Freeze the returned manifest
before augmentation; changing the source inventory requires a new split revision.

Identifiers are dataset-global; missing session/lineage information must be
resolved upstream, not replaced with a shared placeholder. Audio hashes must
come from source admission. This component does not read/verify audio or rights,
detect perceptually similar recordings, or establish label quality. It cannot
prove that caller-supplied lineage is complete. `sourceInventoryHash` binds the
canonical supplied records, not an independently admitted source manifest.

Run: `python3 -m unittest tools.voice_model_training.test_split`.

The first CLI command is available:

```sh
python3 -m tools.voice_model_training split CONFIG_JSON CONFIG_SHA256 NEW_SPLIT_JSON
```

Configuration fields are exactly `formatId` (value
`com.project-seam.voice-training-split-config`), `schemaVersion` (1), `seed`,
`heldOutSongIds`, and `sources` (the records described above). The input cap is
8 MiB; duplicate JSON keys and mismatched configuration hashes are rejected.
Output is canonical JSON binding both configuration and source-inventory hashes.
Output schema 2 adds exact-audio duplicate groups, per-partition unique-audio
counts and redundant-source counts. Input configuration remains schema 1. All
source references are retained; choosing among conflicting labels/permissions
on duplicate recordings requires review. The report does not silently remove
sources or select a training representative. Existing schema-1 outputs remain
unchanged on disk; regenerate to a new output path to obtain the dossier.
The existing parent directory must permit same-directory hard links. Publication
does not overwrite an existing destination and fsyncs the temporary file, but
directory-fsync/power-loss durability and Windows runtime evidence remain open.
Run all current tests with `python3 -m unittest discover -s tools/voice_model_training -p 'test_*.py'`.

### Compare an application master with its source

`python -m tools.voice_model_training.compare_application_export --reference SOURCE.wav
--candidate MASTER.wav --pitch-executable build/release/seam_voicebank_cli --output REPORT.json`
compares a mono reference with a 48 kHz PCM16/24/32 mono or stereo application export.
Pass the command on one line. Inputs must have equal frame counts; the tool performs
no alignment, trimming, resampling or gain fitting. Stereo is explicitly averaged,
with per-channel RMS/peak/nonzero counts retained to expose cancellation. Both
original file hashes and native extractor identity are recorded. Native pitch
analysis uses captured, float32-encoded mono derivatives whose hashes and tracks
are included; temporary derivative paths are not retained audio artifacts.
The report contains the existing strict pitch verdict and multi-resolution spectral
distance. It does not grant singer qualification or release acceptance. Requires
the existing NumPy/SciPy training environment. Existing reports are never overwritten.

For a source-driven vocoder control, run `python -m
tools.voice_model_training.reconstruct_source_vocoder --source SOURCE.wav
--source-sha256 SHA256 --vocoder-export EXPORT_DIRECTORY --pitch-executable
build/release/seam_voicebank_cli --output NEW_DIRECTORY` on one line. This uses
an explicitly trusted local export, verifies its graph/profile binding, derives
mel and native measured F0 from captured source audio, and runs the ONNX vocoder
without an acoustic model. It retains source/reconstruction WAVs and a diagnostic
receipt. Only declared final-hop padding is trimmed; invalid samples in that padding
are still rejected. Inputs are bounded to 4096 hops at 48 kHz. NumPy, SciPy and
ONNX Runtime are required. This control is separate from application output: it
does not apply score dynamics or mute score rests, and is not release acceptance.

Still required: source/permission admission, audio preparation, reviewed labels,
remaining CLI subcommands, duplicate dossier, actual training and environment
locking, checkpoint/export comparison, and held-out singing qualification. No
output grants source permission or musical/release approval.
# Source-bound label CLI

## Reviewed dataset assembly

`assemble_dataset` freshly verifies both source-rights and annotation reviews,
inspects audio, requires identical source sets/hashes/clocks and matching
song/session/lineage, then runs deterministic grouped splitting. The snapshot
contains labels, vocabulary, source metadata, both review/configuration bindings,
split evidence and a reproducible binding hash. Its expiry is the earlier review
expiry. Missing partitions and unresolved duplicate selection are explicit
preparation issues, not hidden by assembly success. This first API covers directly
reviewed sources; combining separately admitted descendants and CLI publication
remain unfinished. Training execution remains unadmitted.

## Explicit label corrections

`admit-labels CONFIG HASH ROOT NEW_REPORT --review FILE --review-sha256 HASH
--policy FILE --policy-file-sha256 HASH --trusted-policy-sha256 HASH` publishes
a verified label-admission report. It uses current system time, rechecks expiry
after source inspection, rejects existing output, and accepts only the separate
annotation-review policy. Unresolved consistency errors fail without publication.
Exit 0 means annotation admission under the supplied trusted policy, not source
permissions, model quality or whole-training readiness. Fixture tests do not
constitute an actual expert review.

`admit_labels` joins signed annotation review to fresh source inspection and
schema 3 score/silence checks. Unknown phones, low alignment confidence and
inconsistent voicing cannot be bypassed by a valid signature. A missing old
review-revision string is allowed because authority comes from the independently
verified signed configuration, not that string. The time-bound result binds
configuration/policy/review and inspected source hashes and sets labelsAdmitted;
source permissions and whole training admission remain false. CLI publication
and full dataset assembly are not yet connected.

`review.verify_label_review` authenticates annotation reviews using the distinct
`seam-training-label-review-1` policy, `training-label-reviewer` role,
`com.project-seam.training-label-review` record and `APPROVE_LABELS` decision.
It retains the same independent policy/configuration hash and expiry checks as
rights review, but neither review type is accepted in place of the other. It
does not itself inspect labels or admit training; that join remains unfinished.

CLI: `python3 -m tools.voice_model_training correct-labels CONFIG HASH ROOT NEW_REPORT`.
Closed configuration fields: `formatId` =
`com.project-seam.training-label-correction-config`, `schemaVersion` = 1, `source`
(preparation row), `sampleRate`, `label`, `edits`, `vocabulary`, `minimumConfidence`.
The command re-inspects audio identity/geometry, applies the full edit transaction
and publishes a new label with the edit list, source/configuration hashes and
parent/child label hashes. Label hashes use compact sorted UTF-8 JSON plus a
newline, matching `encode_report`. Review is invalidated; remaining consistency
issues are retained. Stale edits and existing output reject, without modifying
the previous label. No musical review or training approval is issued.

`label_edits.apply_label_edits` applies 1..65536 corrections transactionally.
Every edit contains `kind` (`pitch` or `phoneme`), zero-based `index`, `expected`
old value and `replacement`. Pitch values contain F0 and voicing together;
phoneme values are the complete existing phone-label object. Adjacent boundary
changes are validated after the whole edit set, enabling coherent shared-boundary
correction. Duplicate targets, stale values and invalid final geometry reject.
Original labels remain unchanged and review revision is cleared. Correction
application is not reviewer authentication; CLI/editor publication is still open.

## Native pitch feature adapter

Refreshed-label schema 2 and fresh-pitch segment schema 5 now include
`pitchCorrections`: low-confidence voiced frames (using supplied minimumConfidence)
and every zero-padded analysis window are queued with source-frame indices.
Review remains required even with no queued issues; the threshold is a supplied
review setting, not a validated quality cutoff. Silent frames are not flagged
solely for low correlation. Older output records remain untouched; exact resume
does not rewrite schema 4 records into schema 5, so use a new destination when
upgrading a previously generated fresh-feature artifact.

The same `--fresh-pitch-extractor` option is now available on `segment-batch`
and derived `admit`. Batch entries must contain labels; exact resume re-extracts
and compares the complete record rather than trusting old pitch data. Derived
admission revalidates parent rights before extraction and checks expiry before
publishing admission. Native integration covers off-grid batch/resume and a
fixture-signed parent admission through actual fresh extraction. No real approval
or label-quality acceptance is implied by those tests.

`segment --fresh-pitch-extractor TRUSTED_CLI` now supports arbitrary crop starts
for label-bearing segment configurations. It writes the exact derived PCM to a
private temporary WAV, re-extracts features on that clip's own clock, checks the
child hash and rebases phonemes. Segment record schema 4 retains full fresh feature
evidence and cleared-review labels; existing no-extractor behavior is unchanged.
No intermediate feature placeholders are published. Temporary input is removed
on completion/failure. This is not an accuracy guarantee; batch/derived-admission
orchestration of this option and native-language boundary review remain open.

End-to-end command: `python3 -m tools.voice_model_training refresh-pitch CONFIG
CONFIG_HASH SOURCE_ROOT TRUSTED_SEAM_VOICEBANK_CLI NEW_REPORT`.
Configuration fields are exactly `formatId` =
`com.project-seam.training-pitch-refresh-config`, `schemaVersion` = 1, `source`
(one preparation record), `sampleRate`, `label`, `vocabulary`, `minimumConfidence`.
The command inspects the source, runs bounded native extraction, verifies source
and feature geometry, and publishes new labels plus the complete feature report.
Source PCM hash and configuration hash are retained; previous review is cleared.
Existing reports are never overwritten. Input labels must already have valid
structural geometry; this is not automatic phoneme alignment or arbitrary crop
repair. Output remains unreviewed and training admission remains false.

`native_features.extract_pitch(executable, source)` runs the explicitly selected
trusted first-party native extractor on POSIX, with a default 30-second deadline
(maximum 60), 16 MiB stdout and 64 KiB stderr capture limits. It drains both pipes,
kills a still-running process group on failure, waits for its direct child, and
rejects duplicate/nonfinite JSON. This is not a sandbox or executable trust
provisioning; use a stable trusted binary. Windows support is not implemented.
Actual native-output equivalence and fixture timeout/overflow/malformed-response
tests are included. CLI refreshed-label publication remains to be connected.

`features.apply_pitch_features` consumes captured JSON from
`seam_voicebank_cli extract-pitch WAV`, checks exact source hash, sample clock,
full hop grid, fixed extractor settings and frame values, then returns a copied
label with fresh F0/voicing and cleared review revision. Existing phonemes are
preserved. Callers must bind the expected hash to inspected source audio; a hash
asserted by the feature JSON alone is not authority. Feature confidence and
extractor metadata remain in the separately captured feature report. The native
CLI integration test now exercises this conversion and wrong-source rejection.
Automated extractor invocation/publication, off-grid crop orchestration and
real-singer pitch quality remain unfinished.

## Training permission capture API

Derived CLI admission extends `admit` with all four options:
`--segment-config FILE --segment-sha256 HASH --segment-source WAV --segment-output DIR`.
The positional output remains a new admission report outside the clip directory.
`--resume-segment` permits exact clip verification/recovery but never reuses an
admission report. Parent review/permissions are rechecked for every invocation;
the current system clock is checked again before admission publication. If
publication/expiry fails after extraction, the clip remains unapproved and can
be inspected or revalidated on a later attempt. No training is started.

`admit_segment` revalidates the signed parent permission configuration and actual
sources, compares the segment's parent hash and song/session/lineage, then invokes
normal byte-bound segmentation. The returned derived-source admission binds the
child WAV/PCM/segment-record hashes to the parent review/policy and singer identity.
It does not accept a saved admission receipt as authority. Unreviewed lineage
changes reject before clip output. The ordinary segment artifact remains unapproved;
the separate time-bound admission result does not grant label or training readiness.
CLI publication and dataset-wide derived-source admission are still unfinished.

Source admission is now runnable:
`python3 -m tools.voice_model_training admit CONFIG CONFIG_HASH ROOT NEW_REPORT
--review REVIEW --review-sha256 REVIEW_FILE_HASH --policy POLICY
--policy-file-sha256 POLICY_FILE_HASH --trusted-policy-sha256 CANONICAL_POLICY_HASH`.
The trusted canonical hash must come from the operator's independent trust
policy, not from the review being checked. File hashes capture exact input bytes;
the canonical hash binds policy meaning. Review expiry uses actual system Unix
time and is checked again after inspection. Exit 0 publishes a new source-only
admission report; failed verification or existing output returns exit 2. It does
not generate signatures, provision trust, schedule work or authorize an entire
training run. Future consumers must recheck current trust/expiry/content.

`admit_sources` joins a trusted signed review with fresh inspection of the exact
permission configuration. Missing scopes reject even if a signature is valid.
Only after review verification and matching audio/evidence does the result set
`sourcePermissionsAdmitted=true`; it records configuration/policy/review hashes,
signer, verification time and expiry. This is a time-bound source-permission
decision under the supplied policy, not a reusable bearer capability: consumers
must revalidate current policy, expiry and inputs. Labels, split, model/runtime
and execution readiness remain unchecked, so `trainingAdmitted` stays false.
The API is implemented; CLI admission/publication is not yet connected.

`review.verify_training_review` verifies a supplied Ed25519 review using the
existing role-bound signature implementation. The caller supplies an independently
trusted canonical policy SHA-256, the exact configuration digest, and current Unix
time. Closed policy/record fields, training-rights-reviewer role, signer identity,
purpose, configuration binding, issue/expiry times and signature are checked.
Revocation requires the caller to supply the current independently trusted policy;
the verifier does not fetch policy updates or make an embedded key trustworthy.
Fixture keys exist only in tests. Successful verification authenticates the review
under that supplied trust anchor, but does not admit execution. It must still be
joined to fresh source/evidence inspection and complete scopes. No real policy,
approval or training run has been created.

CLI: `python3 -m tools.voice_model_training permission-report CONFIG HASH ROOT NEW_REPORT`.
The configuration contains exactly `formatId` =
`com.project-seam.training-permission-config`, `schemaVersion` = 1, `manifest`,
`sources` (preparation rows), `sampleRate`, and `evidence` (evidence-ID to flat
ASCII filename mapping under ROOT). Evidence is captured with regular-file,
non-symlink, size and change checks before hashes are compared. Audio uses the
existing bounded preparation reader. Output binds the captured configuration.
Exit 0 means supplied scope assertions are complete, NOT authorized training;
exit 3 publishes missing scopes; exit 2 rejects malformed/mismatched input.
Existing reports are never overwritten. This intentionally is `permission-report`,
not `admit`: authenticated authority and policy enforcement are still absent.

`inspect_permission_sources(manifest, evidence, root=..., sources=...,
sample_rate=...)` joins assertions to freshly inspected source audio using the
existing preparation reader, not a caller-supplied inspection report. Assertion
and audio source-ID sets must match exactly, and every container digest must
agree. Report schema 2 adds PCM hash, sample geometry and song/session/lineage,
and sets `sourceBytesVerified=true` only after that join. This still does not
authenticate evidence interpretation or reviewer authority, and still leaves
training admission false. Altering either audio or its permission binding rejects.

`permissions.permission_report(manifest, evidence_bytes_by_id)` checks a closed
training permission manifest against captured evidence bytes (4 MiB/evidence,
64 MiB aggregate). It reuses existing bank permission names and separately
requires `modelTraining`, `modelRedistribution`, `commercialModels` booleans.
Each source binds source hash, original/speaker identity, source kind, evidence
hash and supplied review revision. Missing model scopes are reported, never
inferred from commercial rendered-audio or bank permission. Test fixtures are
synthetic declarations, not granted rights.

The result records assertions only: it does not verify actual source bytes,
authenticate reviewer authority, interpret a license or admit training. Even all
true assertions leave `trainingAdmitted=false`. This API is not yet the plan's
complete `admit` command; source-byte joins, authenticated review and execution
policy must be connected before it may authorize a training run.

The training tools are included in root CTest as
`seam_voice_model_training_tests` (label `voice-model-training`). Run
`ctest --test-dir build/release -R seam_voice_model_training_tests --output-on-failure`
after configuring the build, or run Python unittest discovery directly.

## Phrase extraction API

Batch report schema 2 includes each successful child's source ID, container hash
and exact `segment.json` hash, plus `splitSources` in the existing split command's
source-record shape. These records come directly from generated or byte-verified
artifacts, not unverified manifest reopening. `splitSources` is empty if any
entry fails or any child source ID occurs twice. Duplicate child IDs are listed
in `duplicateSourceIds`; exit 3 applies even if all individual clips were written.
Prepared clips remain available for diagnosis. Global uniqueness is enforced
within this batch; combining batches still requires the split tool's checks.
The exported inventory carries original song/session/lineage and PCM hashes,
allowing the existing splitter to group derivatives and count exact duplicates.
It does not confer source rights or training admission.

Batch command: `python3 -m tools.voice_model_training segment-batch CONFIG HASH
INPUT_ROOT OUTPUT_ROOT NEW_ATTEMPT_REPORT [--resume]`.
Batch configuration has `formatId` = `com.project-seam.voice-training-segment-batch`,
`schemaVersion` = 1, and `entries` (1..64). Each entry has `configuration`,
`configurationSha256`, `source`, `outputName`. Paths are flat ASCII filenames;
output names must be unique case-insensitively. Each entry uses the existing
captured segment configuration and WAV checks. Processing is sequential, at most
64 MiB per source read (up to 4 GiB across 64 reads, including repeated sources).
Each attempt writes a new report; exit 3 preserves per-entry failure diagnostics.
With `--resume`, completed matching clips are verified unchanged and missing
entries may finish. Reports are never overwritten. Batch preparation does not
admit a dataset: global child source-ID uniqueness, rights, label review and
split admission remain downstream obligations. No automatic restart is scheduled.

`segment --resume` explicitly permits recovery of a directory containing exact
expected `audio.wav` but no `segment.json`, or verification of a complete exact
artifact. It re-captures the original and re-derives expected audio and labels
from the supplied configuration, compares full bytes, then publishes only a
missing record. A complete matching artifact is unchanged. Truncated/conflicting
files, symlinks, unexpected entries and missing audio reject without overwrite.
This supersedes the blanket no-reuse policy below only when `--resume` is given.
Use a stable caller-controlled output directory; this is not adversarial
directory-race isolation or power-loss durability. Automatic interrupted-audio
repair and multi-phrase orchestration remain unfinished.

Segmentation schema 3 adds `parentScore` using the explicit-silence score shape
from label configuration schema 3. It crops notes/rests in source-frame time,
reindexes surviving syllables and phone ranges, and remaps silence indices.
The first surviving note starts a new local articulation even if the parent
note was a slur continuation; review remains invalidated. The final record's
`label` object contains the child-bound acoustic `label` and cropped `score`.
Cuts with orphaned lyric phones or sung syllables without phones reject and
require boundary relabeling. Silence-only crops are not supported by the current
sung-score schema. Schema 1/2 behavior remains unchanged. Acoustic alignment
quality and regenerated off-grid/boundary features are not established here.

Segmentation config schema 2 adds `parentLabel` (exact `sourceSha256`,
`audioSha256`, `label`), `vocabulary`, and `minimumConfidence`. Parent label ID,
frame count and both hashes must match the inspected original. The final record
schema 2 embeds a child-hash-bound `label` object. Phone spans are clipped and
rebased; F0/voicing arrays are sliced on the original hop grid. Crop starts must
be hop-aligned; off-grid starts require fresh feature extraction and are rejected
before publication. End frames may be partial-hop. Review revision is cleared
for every derived label, including a whole-source copy with a new identity.
This supports acoustic labels only: score/lyric/slur cropping and fresh boundary
feature extraction remain unfinished. No inherited musical approval is implied.

Runnable publication command:
`python3 -m tools.voice_model_training segment CONFIG_JSON CONFIG_SHA256 SOURCE_WAV NEW_DIRECTORY`.
Configuration fields are exactly `formatId` (value
`com.project-seam.voice-training-segment-config`), `schemaVersion` (1), `source`
(identity object described below), `sampleRate`, `segmentId`, `startFrame`,
`endFrame`. It reads at most 64 MiB of source WAV and verifies the captured hash.
The new private directory contains `audio.wav` and final `segment.json`; the
record binds the configuration hash, output hash, parent hashes and crop range.
Existing directories, including incomplete ones, are never reused or overwritten.
Invalid input fails before output-directory creation. I/O interruption may leave
partial output: retain it for diagnosis and use a new destination. Consumers
must require the final record and verify the WAV hash. File data is fsynced;
directory-entry power-loss durability and automatic recovery are not claimed.

`segment.segment_source(payload, source=..., sample_rate=..., segment_id=...,
start_frame=..., end_frame=...)` returns `(new_wav_bytes, provenance_record)`.
The source has exactly `sourceId`, `songId`, `sessionId`, `lineageId`,
`sourceSha256`. It verifies the original bytes, crops a half-open source-frame
interval, preserves integer or float32 PCM sample bytes/rate/width, and returns separate parent
and child container/PCM identities. Descendants inherit song/session/lineage;
do not replace these with per-clip identifiers when splitting the dataset.
Original bytes remain unchanged. Crop output is a canonical WAV, not a copy of
the original container metadata. No normalization/fades/resampling occur.

The API itself does not publish files; the single-phrase CLI above does. Neither
crops/rebases labels or authenticates source permissions. Provenance is a reproducible
transformation record, not authorization. Batch publication and interrupted-run
recovery must be connected before claiming the full preparation workflow.

Schema 3 extends schema 2 score supervision with required `silencePhones`: a
sorted, unique array of zero-based phoneme indices owned by silence rather than
lyrics. Syllable ranges and these indices must partition the full phone list
exactly once; leading, internal and trailing silence are supported. Silence
cannot overlap a lyric range. The report adds `silencePhoneCount`. This supersedes
schema 2's silence-ownership limitation below without changing its interpretation.
Silence designation is supplied annotation, not an acoustic silence detector;
rest/phone alignment and native review remain necessary.

`label-report` is the canonical command name; `labels` remains an alias.
Configuration schema 2 additionally requires `score` on every label entry.
It contains `language` (`ja`, `en`, `ko`), `syllables` and `notes`.
Each syllable has `lyric`, inclusive `phoneStart` and exclusive `phoneEnd`;
these ordered ranges partition the supplied phoneme list. Each note has
`startFrame`, `endFrame` (source sample clock), `midi`, `syllable` (zero-based
index), and boolean `slur`. Notes and explicit rests partition the entire
source duration. A rest uses null MIDI and syllable and false slur. A new
sung syllable starts with false slur; subsequent immediately adjacent notes
on that same syllable use true slur. MIDI is an integer from 0 through 127.
Batch report schema 2 carries score counts and language, while the captured
configuration hash binds the complete supplied score. Schema 1 stays unchanged.

This first score representation checks ordered ownership and frame geometry,
not acoustic phoneme-to-note alignment or lyric pronunciation correctness.
It does not yet distinguish silence phones from lyric-owned phones; datasets
requiring independent silence ownership need an explicit subsequent schema,
not fabricated syllable lyrics. Expressive F0 is not forced to equal MIDI.

Run `python3 -m tools.voice_model_training labels CONFIG_JSON CONFIG_SHA256 SOURCE_ROOT NEW_REPORT_JSON`.
The captured configuration has exact fields `formatId` (value
`com.project-seam.voice-training-label-config`), `schemaVersion` (1), `sampleRate`,
`sources` (same records as preparation), `labels`, `vocabulary` (unique symbol
strings), and explicit `minimumConfidence`. Each labels entry contains exactly
`sourceSha256`, `audioSha256`, and `label`; the label fields are defined in
`labels.py`. Supply exactly one label per source. Current geometry covers the
whole inspected file; cropped phrases must first become separately identified
sources. No implicit offset or segmentation is inferred.

The command re-inspects source bytes, compares both container and PCM digests,
and checks label frame count against the actual PCM. Output binds the captured
configuration digest and inspected identities. Exit 0 means consistency passed;
exit 3 publishes a correction queue; exit 2 rejects invalid input without publishing
a new report. Existing outputs are never overwritten. Review revision text is
not authenticated approval. Permissions, musical accuracy, notes/slurs/lyrics,
and actual training remain separate unfinished obligations.
# Reviewed dataset assembly

For phrase-sharded output, add `--conditioning-directory NEW_SIBLING_DIRECTORY`.
The directory and snapshot must share a parent, have different names, and not
already exist. Schema-3 snapshots store ordered per-source references (filename,
SHA-256, exact byte size, analysis frame count) in `conditioning`, rather than
expanded features. `conditioningDirectory` names the sibling directory. Resolve
references relative to that directory, never relative to an arbitrary working
directory. The reference-list digest participates in dataset identity.

Sharded assembly retains one expanded phrase at a time, with at most 65,536
analysis frames per phrase, 1,000,000 per attempt and 256 MiB of feature files.
The manifest is published last, after review-expiry rechecking. On failure,
already written shards remain for diagnosis; no manifest means no completed
dataset. Retry with new output names; merging/resuming incomplete attempts is
not implemented. Individual file publication prevents overwrite, but this does
not claim directory-fsync power-loss durability or hostile-parent race isolation.
Source/configuration intake limits still apply; this is not an unlimited corpus
loader. Consumers must verify referenced bytes and current authority before use.

Snapshot schema 2 includes source-sorted `conditioning` and
`conditioningFrameCount`. The dataset bindings include `conditioningSha256`,
computed from the canonical expanded feature list, so a change to feature
construction changes dataset identity. The compact snapshot writer currently
allows at most 65,536 analysis frames across all sources; it rejects larger
inputs before expanded allocation. This is a pilot-size limit, not a production
corpus strategy: streamed/sharded feature storage remains required for full
training. Old schema-1 snapshots contain no expanded conditioning and are not
silently promoted. Reassembly revalidates source and review inputs.

Run `python3 -m tools.voice_model_training assemble-dataset CONFIG CONFIG_SHA256 SOURCE_ROOT NEW_SNAPSHOT --rights-policy-sha256 RIGHTS_ANCHOR --label-policy-sha256 LABEL_ANCHOR`.

The captured configuration has `formatId: com.project-seam.training-dataset-config`,
integer `schemaVersion: 1`, `seed`, `heldOutSongs`, and six references:
`permissionConfig`, `labelConfig`, `rightsReview`, `rightsPolicy`, `labelReview`,
and `labelPolicy`. Each reference contains exactly `path` (a flat ASCII filename
within SOURCE_ROOT) and `sha256` (the exact file-byte digest). Policy command-line
anchors are independently trusted canonical policy digests, not those file-byte
digests. Each referenced JSON is limited to 8 MiB.

Assembly freshly verifies both review purposes, current expiry, actual source
audio, schema-3 score labels, and shared source/song/session/lineage identity.
It retains deterministic split bindings and reports missing partitions and exact
audio duplicates without silently dropping material. Exit 0 means no assembly
preparation issues; exit 3 publishes a snapshot with issues to resolve; exit 2
rejects invalid input or an existing output. Existing snapshots are never replaced.
The earliest review expiry applies to the result. Later consumers must revalidate
authority and source bytes; the snapshot is not a permanent admission token.

This command does not train, export, qualify a singer, or authorize release.
`trainingAdmitted` and `releaseEligible` remain false even on exit 0. The current
join accepts directly reviewed sources; a separate derived-clip admission join
is not yet implemented.

### Captured procedural teacher and real vocoder evaluation (2026-09-19)

`generated_teacher.export_from_candidate` accepts the native captured candidate,
the native extractor's complete pitch document, and one lyric/MIDI entry per
captured note (not per phone). It checks the candidate WAV digest and the pitch
document's digest, clock, complete hop grid and estimator settings. Canonical
`note-id:ordinal` marker keys group consonants/vowels into one note; `-`, `ー`,
and `〜` continue the immediately preceding syllable. Explicit rests retain
silence-phone indices. Ambiguous mixed keys, duplicate/reordered keys, incomplete
coverage and mismatched note counts reject; missing notes never become implicit
rests. Legacy unkeyed input only supports one phone per declared note.

The current adapter is Japanese and uses renderer-marker ownership intervals,
not original piano-roll note boundaries. It must not be used as evidence of
acoustic phoneme correctness. `labelOrigin=renderer-intent-not-acoustic-truth`
and all approval flags remain false. The ordinary label-config inspector and
frame conditioning code consume its output without a bypass.

`train_reviewed_vocoder_epoch(..., held_out_items=[source_id, ...])` selects
admitted validation/test source IDs, not arbitrary supplied tensors. It resolves
their source/conditioning/target bytes through `iter_vocoder_batches`. Evaluation
uses CPU float32 Torch `eval`/`no_grad`, detaches output, and restores standard
module modes and CPU RNG. Arbitrary forward-method mutations are not rolled back.
The output must contain exactly `ceil(validSamples/256)*256` samples; only declared
tail padding may be trimmed. Unknown pitch produces `UNRESOLVED`, not a successful
pitch measurement. An existing `reconstruction_directory` retains per-item float
WAVs, hashes, measurements and a final receipt. Partial output without that final
receipt is an incomplete attempt. Pitch currently compares whole-phrase medians,
not note-by-note melodic accuracy. No reconstruction receipt qualifies a singer.

For a bounded real upstream forward check, use:

```sh
python -B -m tools.voice_model_training.check_vocoder_reconstruction \
  --source CAPTURED_48K_MONO_INTEGER_WAV --source-sha256 WAV_SHA256 \
  --trusted-checkout PINNED_SINGING_VOCODERS_CHECKOUT \
  --pitch-extractor build/release/seam_voicebank_cli \
  --offset-samples 48000 --sample-count 48037 --output NEW_DIRECTORY
```

This diagnostic initializes an untrained upstream MiniNSF model, measures native
F0 and reconstructs a maximum two-second source crop. Exit 0 means tensor/audio
execution passed, even when the retained reconstruction-quality result fails.
It does not load trained weights, train, execute ONNX, establish a held-out study,
or authenticate source-use permission. A retained learned-singer deployment
remains separate from this diagnostic and the persistent training command below.

### Persistent vocoder GAN training and resume

`python -B -m tools.voice_model_training.train_vocoder --help` exposes the CPU
entrypoint. It uses the clean pinned SingingVocoders training checkout, freshly
revalidates the existing source/label reviews, reads the actual WAV/conditioning/
mel targets, and trains the export-supported deterministic MiniNSF configuration.
There is no pretrained-weight download, admission bypass or implicit conversion.

Capture this closed JSON configuration and supply its exact file SHA-256:

```json
{
  "formatId": "com.project-seam.vocoder-training-config",
  "schemaVersion": 1,
  "seed": 928,
  "learningRate": 0.0001,
  "learningRateDecay": 0.999,
  "maximumUpdates": 10,
  "maximumSeconds": 600,
  "cpuThreads": 1,
  "evaluationSeed": 932,
  "heldOutSources": ["your-validation-source-id"],
  "labelOrigin": "renderer-intent-not-acoustic-truth"
}
```

`heldOutSources` must select admitted validation/test IDs. Supply the actual label
origin for the material; the example describes a procedural teacher, not real
recordings or reviewed acoustic truth. The architecture is fixed to the supported
48 kHz/80-bin/256-hop/1024-FFT configuration. The label config supplies source WAV
paths; target inventory is the existing `training-target-inventory` schema.

Architecture selection is explicit. Schema 1 preserves the original 32-channel,
single-residual-kernel smoke model; it must not be confused with the capacity of the
pinned upstream singing-vocoder configuration. For a new capacity experiment, use
`"schemaVersion": 2` and add `"architectureProfile": "mini-nsf-512-mrf-v1"` to the
otherwise unchanged settings above. This selects 512 initial channels and kernels
3/7/11, retaining deterministic MiniNSF and SEAM's exact acoustic geometry. The
alternative `mini-nsf-32-smoke-v1` is available explicitly in schema 2 as well.
Unknown profiles and unreviewed arbitrary architecture fields are refused. Export
accepts only these exact configurations and records configuration/parameter count.

Do not resume a 32-channel checkpoint into the 512-channel model: the existing
resume identity check rejects that change. Start in a new output directory and
budget for larger optimizer/checkpoint memory and disk usage; old measured checkpoint
sizes do not apply. No claim is made that capacity alone fixes the measured pitch
failure. The larger model's upstream training/deployment PyTorch forward parity has
been checked at 1/3/16/23 frames; learned quality, new training and its ONNX export
are separate experiments that must still pass. Do not launch while storage is low.

The larger profile now also passes the actual synthetic GAN mechanics update and
ONNX Runtime parity check (maximum observed sample error below 3e-8). Reproduce with:

```sh
python -m tools.voice_model_training.check_vocoder_model TRAINING_CHECKOUT DEPLOYMENT_CHECKOUT \
  --architecture-profile mini-nsf-512-mrf-v1 --mini-only --check-gan --check-onnx
```

This writes no checkpoints unless `--check-resume` is explicitly added. It uses short
synthetic inputs, not the admitted corpus; singing quality and whole-phrase resource
requirements remain unverified. Runtime parity does not imply pitch accuracy.

Use `--fixture-frames 128` to measure a longer synthetic update; accepted lengths
are 16..4096 hops, default 16. Reports separate supervised and GAN update times.
Longer lengths increase memory demand: a 1080-hop larger-model probe exceeded the
chosen 6 GiB RSS safety budget before completing. Use a resource supervisor for
large probes; the frame bound alone is not a memory guarantee. Retain full-song
evaluation when investigating shorter training segments.

For bounded segmented updates, use training configuration `schemaVersion: 3` with
both `architectureProfile` and `trainingSegmentFrames` (16..4096 hops; e.g. 128).
Schema 1/2 retain whole-phrase behavior. The setting is part of the captured run
configuration, so changing it is not compatible with an existing resume identity.
Set `maximumUpdates` high enough for the sum of `ceil(sourceHops / segmentFrames)`
over all training sources; an epoch is never completed after only the first crop.
Each source is split into balanced contiguous pieces to avoid a one-hop remainder.
The final partial hop retains its explicit zero padding and valid sample count.
Receipts distinguish update count from source count and record each source's coverage.
Held-out evaluation always uses complete songs. There is no training halo or random
crop selection in this version: segment boundaries change optimizer context, and
their musical consequences must be measured rather than presumed harmless.

The CLI and epoch service enforce free-space headroom before allocation, around
updates/evaluation and before checkpoint serialization. The budget includes the
configured checkpoint ceiling, retained evaluation WAVs/metadata and a 256 MiB
safety reserve. It is a conservative check, not an OS reservation: concurrent disk
use can still fail a write. A failed publication without `checkpoint.json` is not
resumable. Resume only an earlier complete checkpoint into a new directory; no
automatic deletion or restart is performed by the trainer.

For future acoustic training, generate explicit pause examples with
`generate_procedural_corpus --include-pauses`; preparation retains `pau` as a distinct
rest token. Select that same symbol in the deployed surface's silence setting
(`--silence-phone pau` in `check_production_render.py`). This cannot repair an old
checkpoint that never trained on silence, and requires fresh corpus admission.

```sh
python -B -m tools.voice_model_training.train_vocoder \
  --training-config TRAINING_JSON --training-sha256 TRAINING_FILE_SHA256 \
  --dataset-config DATASET_JSON --dataset-sha256 DATASET_FILE_SHA256 \
  --targets TARGET_INVENTORY --targets-sha256 TARGET_INVENTORY_FILE_SHA256 \
  --source-root SOURCE_ROOT --conditioning CONDITIONING_DIRECTORY \
  --trusted-checkout PINNED_SINGING_VOCODERS_CHECKOUT \
  --rights-policy-sha256 TRUSTED_RIGHTS_ANCHOR \
  --label-policy-sha256 TRUSTED_LABEL_ANCHOR \
  --output NEW_RUN_DIRECTORY --epochs 2 --maximum-run-seconds 1200 \
  --maximum-total-checkpoint-bytes 2147483648
```

Each completed epoch publishes `epoch-NNNNNN/checkpoint.json` after `models.pt`
and `training.pt`. Corresponding `reconstruction-NNNNNN` directories retain exact
held-out WAVs and receipts. `run.json` appears only after every requested epoch
finishes. Models, discriminator buffers, both AdamW optimizers, both exponential
LR schedulers, Python/NumPy/Torch RNG and configuration identities are persisted.

To resume, use the same captured inputs/environment and a **new** output directory,
adding `--resume PREVIOUS_RUN/epoch-000001 --resume-receipt-sha256 RECEIPT_FILE_SHA256`.
The `--epochs` count is additional epochs. Fresh admission must still pass; expired
reviews do not become valid because a checkpoint exists. Changed data/configuration/
runtime versions reject. No exact-reproduction claim is made across different
hardware, software or thread settings.

Each state file is bounded to 512 MiB. A separate aggregate cap is enforced during
writes across both files and across the run, including partial final writes. This
binary-checkpoint budget excludes JSON and held-out audio; provision those separately.
An observed full upstream checkpoint is about 553 MB, not 10 MB. Time limits and
API cancellation are cooperative between operations, not hard process preemption;
the run deadline begins after model initialization. Ctrl-C exits 130. Invalid input
or a failed attempt exits 2. Earlier completed checkpoints remain; partial files
without a completion receipt must not be resumed or promoted.

Exit 0 means training/persistence/evaluation executed, **not** that reconstruction
passed or a singer is qualified. Check the retained reconstruction result, including
unresolved pitch. The existing `export_vocoder` command consumes a completed epoch
directory and can retain/compare its learned ONNX graph separately.

The repeatable CLI diagnostic is:

```sh
python -B -m tools.voice_model_training.check_vocoder_train_command \
  --trusted-checkout PINNED_SINGING_VOCODERS_CHECKOUT --output NEW_FIXTURE_DIRECTORY
```

It retains explicitly synthetic oscillator fixtures under public fixture-only keys,
runs two actual upstream GAN epochs, resumes epoch 1, and compares full state and
held-out WAV bytes. Allow about 1.7 GB for its three complete GAN checkpoints.
Those keys authenticate engineering fixtures only, never real-source permission
or singer approval. Fixture review expiry is intentionally short; the diagnostic
is a reproducible test, not a permanently admitted production corpus.
# Fixed validation campaign

**Partition scope:** the selected corpus validates the acoustic split, not every
model in the singer. `combinedModelHoldoutVerified` is always false. Optional
paired `--vocoder-export` and `--vocoder-checkpoint` inputs audit the candidate's
complete exported epoch training IDs. The receipt must bind to the export and
the export graph to the bundle manifest. Overlap is reported, never dropped;
missing evidence is NOT_AUDITED. One epoch without overlap does not establish
independence across all ancestry or duplicated audio. Three of the September 20
five-song regression sources were used to train the vocoder.

Companion command `python -m tools.voice_model_training.score_application_export`
accepts `--comparison`, `--comparison-sha256`, `--labels`, `--labels-sha256`,
`--master`, and `--output`. Select the label digest from the captured corpus's
`artifacts["label-config.json"]`, not from an unverified replacement label file.
It binds the original source and master identities, validates the explicit score
clock, recomputes strict pitch diagnostics from the captured full-hop tracks,
and locates mismatching windows within notes, across transitions, or in rests.
The written note is not ground truth for expressive pitch or consonants. Neither
reference nor candidate errors are octave-corrected or excluded. Exact rests
are checked on every PCM channel without downmix cancellation; scores without
rests report `allRestsExactlyZero: null`, not a vacuous pass. This diagnostic
does not independently authenticate supplied pitch tracks or qualify singing.

`python -m tools.voice_model_training.validation_campaign` reruns an explicit
selection through the native application renderer, saved-project export, and
the existing no-alignment audio comparison. Required arguments are `--selection`,
`--selection-sha256`, `--corpus`, `--bundle`, `--renderer`, `--pitch-executable`,
and `--output` (new directory). `--silence-phone` defaults to `pau`; it must match
the trained bundle, not substitute an arbitrary sung phone.

Selection JSON uses formatId `com.project-seam.validation-selection`,
schemaVersion `1`, exact `corpusSha256`, and `items` containing explicit
`sourceId` and `projectPath` pairs. Paths are local artifact locations, not
download instructions. The corpus must be a captured-teacher-corpus receipt.
Only 1–16 unique validation songs are accepted; train/test/held-out membership,
changed source/project bytes, and existing output paths are rejected before
rendering. Inputs are copied into the new campaign directory.

`selection.json` freezes input, candidate-manifest, and executable hashes before
rendering. Per-song `result.json` retains execution failures; successful songs
also retain `comparison.json` and the real application exports. `campaign.json`
lists every selected song, without a success-only quality average. Exit status
0 means all executions and measurements completed, **not** that pitch matched;
2 means at least one selected item failed. Singer/release qualification remains
false in both cases. Interrupted campaigns retain their selection and completed
per-song receipts; they must not be presented as completed campaigns.
# Phrase-content checks

`phrase_fingerprint.fingerprint(events, ppq=960)` returns four SHA-256
fingerprints: exact score, transposed/time-scaled score family, lyric sequence,
and melody/rhythm. `project_events(project)` reads a bounded, single-track,
single-region pilot project; `token_events(tokens)` reads the procedural pilot's
`lyric:midi:duration` tokens. These helpers reject overlapping notes and empty or
all-rest phrases. They are not general-purpose project validation.

Use these checks before rendering a frozen evaluation cohort and repeat them
against the captured projects afterwards. Keep voice `recipeSha256` separate:
sharing a voice recipe does not mean sharing a song. Conversely, changing IDs,
transposing notes or uniformly scaling durations does not create an independent
phrase. A non-match is not proof of acoustic, semantic or training independence;
retain decoded-audio and declared-training-ancestry checks separately.
