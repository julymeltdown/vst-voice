# Original voice model production

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
bytes. It verifies the expected container digest, mono integer PCM geometry and
sample rate through the existing source inspector, decodes 16/24/32-bit signed
little-endian PCM without resampling or normalization, and returns target data
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

`inspect_pcm_source` inspects captured mono 16/24/32-bit integer PCM WAV bytes
under a 64 MiB/ten-minute bound and an explicit expected rate. It verifies the
source file hash and derives a separate geometry-plus-PCM `audioSha256` suitable
for split duplicate grouping. Container-only metadata differences do not hide
identical PCM. Different rates/widths remain distinct; this is not perceptual
duplicate detection. Stereo, floating-point and compressed sources require an
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
interval, preserves integer PCM samples/rate/width, and returns separate parent
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
