# Original voice model production

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

## Training permission capture API

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
