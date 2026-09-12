# Integrated Singer Execution

Authority: `SEAM_IMPLEMENTATION_PLAN_2026-09-13.md`, preserving the original
R1–R20 and Full-Scope U1–U48 obligations. This is current execution status,
not a replacement product contract or a release approval.

## Active outcome: M1 original voice → bank → unfamiliar song

| Package | Implementation | Demonstrated workflow | Qualification remaining | Next action |
|---|---|---|---|---|
| M1.P1 | In progress: inventory v2→producer v4; language-bound generation; multi-style draft/review/publication; legacy readers retained | Two-style initialization, generation collection, native-controller draft creation, review and candidate reopen regressions | Explicit legacy migration and populated-history parity remain | Complete migration, then campaign/articulation |
| M1.P2/P3 | Not completed by this increment | Existing procedural/producer foundation retained | Connected articulation, campaign, actual bank and unfamiliar-song evidence | Continue after the necessary M1.P1 producer bindings |
| M2–M6 | Remaining full scope retained | No new milestone qualification | As specified by the implementation plan | Independent neural process/data work remains available |

## Verification checkpoint — September 13, 2026

- Nine draft tests plus eight legacy inventory tests passed (17 total), including
  real CLI generation, exact numeric types, bounded IDs and legacy-writer rejection.
- CMake configure succeeded and registered `seam_draft_inventory_tests` passed.
- The focused CTest target was rerun after the bounded-ID case was added.
- The producer now defines `ProductionUnitIdentity` with exact language/style/
  coverage/layer equality and a canonical inventory-v2 SHA256. Python-generated
  rows and C++ agree on ASCII and quoted Japanese-label golden vectors; distinct
  style slugs cannot merge assignments.
- Producer schema 4 now persists a workspace language and per-assignment/take
  style, checks four-axis duplicate/retake ownership, and retains legacy 1–3
  serialization. Raw and generated import matching use style; source assessment
  retains v4 and includes language/style in its material identity. Review and
  single-style manifest paths reject relabeling. The later publication checkpoint
  below admits complete schema-4 style matrices through the canonical publisher.
- Tests import identical PCM into two distinct styles, recover the durable
  workspace, and reject cross-style retakes, missing styles, language changes,
  and relabeling existing takes without mutating saved state. Generic save cannot
  masquerade as legacy migration; the explicit migration operation is pending.
- At checkpoint `5291e652`, rebuilt the complete configured Release tree and ran all **122 CTest targets:
  122 passed, zero failed** (86.13 seconds). The producer target now has 49 cases.
- Following that checkpoint, score-job preparation and CLI collection now carry
  assignment style. Expectation v2 carries explicit language; legacy expectation
  v1 retains its format. Tests prepare/load/render a schema-4 job, collect it via
  the actual CLI, and repeat collection without creating another generation.
  Wrong-style preparation and wrong-language collection fail without changing
  producer state; malformed v2 language/version fields are rejected.
- Rebuilt the complete Release tree after the generation integration. The three
  focused CTest targets (export workflow, draft inventory, producer) passed.
  The 122-target run above predates this latest integration, not a fresh claim.
- Version-aware Python definition preparation now maps inventory v2 to producer
  v4 without repeating workspace language in assignment rows. C++ init-production
  admits empty v4 drafts and still rejects preapproved/imported material. Python
  verification checks style keys, retake binding, immutable language/take identity
  and schema-4 source-quality material hashes. Legacy generic inventory readers
  remain unchanged.
- New parity tests execute both preparation and initialization CLIs, then verify
  the resulting two-style workspace in Python. Missing language/style, duplicate
  assignments and schema relabeling are rejected by both readers. All 33 focused
  Python tests passed; registered draft inventory, external-beta Python contract
  and sample-review CLI tests also passed. This is not exhaustive full-product
  or populated multi-style publication qualification.
- No generated voice, musical review, qualified range, installed bank or Beta GO
  is claimed. The complete implementation goal remains active.

## Multi-style review integration

- Schema-4 workspaces can now prepare and apply a multi-style review packet
  through the shared review service. Legacy style-free workspaces remain rejected.
- A synthetic regression uses two takes with identical PCM, phone coverage and
  pitch but different styles. Reviewing the first does not review the second;
  the second requires a fresh packet and its own explicit decision. Stale packets
  and relabeling into the other style fail, and durable recovery retains both
  separate review records.
- Producer, native Studio review and sample-review CLI targets rebuilt and all
  three focused CTest targets passed. The producer target has 50 cases.
- This review checkpoint alone did not admit publication. See the subsequent
  publication integration below. No musical approval outside the explicitly
  synthetic tests was created.

## Multi-style candidate publication integration

- The canonical publisher now admits schema-4 multi-style workspaces only when
  declared styles exactly match assignment ownership, all styles have the same
  required phone/pitch matrix, and every current assignment has its own valid
  take and retained independent review. Legacy workspaces keep the single-style
  restriction. No approval is inferred from shared PCM.
- Regression coverage publishes and reopens a two-style candidate, verifies its
  manifest and content hash, and rejects omitted styles, reused review evidence,
  extra declared styles and asymmetric phone/pitch requirements.
- Schema-4 source qualification now requires an explicit current source-quality
  assessment. C++ and Python no longer allow the historical no-assessment fallback
  for these workspaces. Source execution remains separate and does not need a
  musical PASS. Test evidence is expressly synthetic, not a real evaluation.
- Scope clarification from code inspection: Python `_production_candidate.py`
  validates a separate legacy `READY`/`unitBindings` export contract, not the
  canonical C++ `com.project-seam.resource-candidate` descriptor. Its pair-based
  legacy contract is not being reinterpreted as authority for these candidates.
- This remains an engineering candidate with `releaseEligible: false`. Native
  multi-style draft authoring, explicit legacy migration, generation campaigns,
  real singer quality and the remaining M1–M6 obligations are still unfinished.
- Verification after this integration: complete configured Release build passed;
  **122/122 registered CTest targets passed**, zero failures (82.22 seconds).

## Native multi-style draft creation

- The shared draft builder now derives all schema-4 styles from assignments and
  generates style-distinct unit IDs. Missing takes retain style-qualified labels.
  The selected identity style must belong to the workspace; it cannot relabel or
  filter its other assignments. Legacy workspaces retain explicit single-style
  behavior. CLI help and Studio's progress status explain the all-styles behavior.
- Tests create a partial and complete two-style draft, prepare it for review,
  reject an unknown style, and prove that selecting either existing style retains
  identical manifest content. A native-controller test asynchronously creates and
  opens both styles without changing producer state or creating reviews.
- Shared manifest, Studio manifest and sample-review CLI targets passed after
  rebuilding affected targets. The prior 122-target run predates this increment;
  no fresh desktop visual QA or musical qualification is claimed.

## Legacy migration preparation

- Added `python3 -m tools.external_beta.voicebank_production prepare-style-migration
  --workspace WORKSPACE --inventory LEGACY_INVENTORY --output NEW_PLAN_JSON`.
  It verifies durable history and matching inventory, captures the source bytes'
  SHA256/generation and inventory evidence, and writes only a new plan outside
  the workspace. Existing output paths are not overwritten.
- A singleton style in the validated legacy inventory can resolve ownership.
  Multi-style legacy inventories produce `UNRESOLVED` with per-assignment reasons;
  the planner does not guess a selected style. A resolved proposal retains old
  reviews/source bindings but clears active marker/pitch approval and requires
  source-quality reassessment. Its generation is not advanced by the planner.
- All 18 production-draft parity tests passed, including actual CLI invocation,
  deterministic proposal content, ambiguous styles, unchanged workspace bytes,
  preservation of historical evidence and rejection of in-workspace/overwrite
  destinations. The proposed state is validated against the target schema.
- **Not yet applied:** the C++ durable migration operation, retained migration
  receipt and history-transition verification remain to implement. Generic save
  continues to reject a schema upgrade; this plan cannot bypass that boundary.

## M1.P2 audible pilot started

- Added a reproducible `seam_singer_pilot` executable using the existing production
  export path, not a separate DSP implementation. It creates saved scores, editable
  recipes, master WAVs and unapproved baked candidates for a six-note Japanese
  vowel/fricative ladder and three phonation/formant variants.
- Retained complete local outputs in `build/release/seam-pilot-listening-02/`.
  Master peaks are about 0.0645; RMS spans 0.0126–0.0221. Distinct variant hashes
  establish different PCM, not perceived improvement or female identity.
- The first run exposed a harness metadata-as-WAV measurement error; fixed it
  and preserved the partial directory. Registered a real-CLI test for repeated
  identical audio hashes, finite/nonzero unclipped diagnostic output, distinct
  recipe identities and refusal to overwrite an existing destination. It passed.
- No listening verdict, complete articulation coverage, installed pilot bank or
  unfamiliar-song acceptance is claimed. Next: analyze the retained phrase timing,
  pitch and transitions, expand consonant/context probes, and connect inventory
  campaign generation. Explicit legacy migration remains unfinished but does not
  prevent the new-workspace pilot work.

### Pilot steady-pitch measurement

- The pilot now emits per-variant pitch diagnostics tied to the dry candidate's
  SHA256. It uses the existing broad-range FFT pitch analyzer, with fixed central
  half-note windows derived from the project tempo map. Full analysis windows
  must fit inside those intervals. Unvoiced frames remain in the denominator;
  missing voiced estimates produce null medians, not zero error.
- Retained run `build/release/seam-pilot-listening-03/`: the baseline has 92
  analyzed windows, all voiced and within 50 cents; per-note median absolute
  errors range from 0.052 to 0.350 cents. This supports steady-pitch behavior for
  this six-note fixture only, not transitions, timing-edit accuracy, language
  intelligibility, singer identity or Beta qualification.
- Rebuilt the pilot and passed its real-CLI regression (1/1), now checking
  diagnostic hash binding, denominators and baseline pitch. No full-suite rerun
  is claimed. Next synthesis investigation should prioritize consonant/context
  transitions and articulation coverage over steady-pitch changes.

### Expanded articulation listening fixture

- Added an explicit `articulation` pilot mode: fourteen Japanese CV notes,
  `ma mi mu me mo na ni nu ne no pa ta ka sa`, rendered through ordinary export
  in the same three variants. Recipes explicitly bind nasal resonance and
  antiresonance for m/n, separate released-stop bursts for p/t/k, and s noise.
  This extends the diagnostic recipe, not the renderer's supported source types.
- Complete local audio/scores/recipes/markers/pitch diagnostics are retained at
  `build/release/seam-pilot-articulation-01/`. No auditory verdict is asserted.
- The real CLI regression now repeats both fixtures and verifies exact 28-phone
  coverage, four gesture classes, ordered contiguous planned boundaries, complete
  candidate span, actual SHA256 binding and unapproved state. Rebuilt executable
  and focused CTest passed (1/1, 2.85 seconds); no full-suite run claimed.
- M1.P2 remains open: context/transition semantics, remaining consonant families,
  held-out linguistic phrases and resumable inventory generation are not supplied
  by this diagnostic. Prioritize those gaps rather than treating marker coverage
  or steady vowel pitch as proof of an intelligible singer.

### Inventory assignment to real generation job

- Added shared `inventory_generation.hpp/.cpp`: deterministic template-v1 score
  construction from a unique schema-4 Japanese producer assignment. The score
  retains canonical coverage phones as an explicit phonetic hint, assignment
  pitch/style, stable IDs and a template hash. It uses one 960-tick note at the
  default 120 BPM; this is an initial timing template, not complete context design.
- `prepareInventoryGenerationJob` writes a create-new score and delegates to
  `prepareGenerationJobFromScore` with its exact hash and selected recipe. Job IDs
  include take identity; expectations capture producer state at preparation time.
  No producer mutation, automatic approval or alternate renderer is introduced.
- The integration test uses a durably initialized synthetic producer, prepares
  `cv:s:a`, renders the actual job, verifies ownership and unchanged producer
  bytes, and rejects duplicate output, wrong style, duplicate assignment identity,
  unsupported language and `release:a:R` (unsupported adapter phone). The initial
  test accidentally used generation zero; its rejection was retained as a fixture
  correction, not bypassed in production. Export CTest passed, 1/1 (4.45 seconds).
- Still required: inventory-file admission/CLI, coverage-wide unsupported-context
  reporting, additional timing templates and resumable campaign prepare/render/
  collect receipts. Do not prepare an entire campaign's expectations up front.
- Inspection also found `generation_batch.cpp` still deduplicates assignments by
  coverage/pitch without language/style. Repair and test this before admitting
  a multi-style campaign; this increment does not claim that path complete.

### Multi-style generation batch repair

- Repaired the preceding batch-admission gap: version-2 expectations now use
  `ProductionUnitIdentity` (language/style/coverage/pitch). Legacy expectations
  still use their original style-free identity; recipe style cannot create a
  second legacy assignment. Duplicate job/take and frame-budget checks remain.
- A real integration fixture initializes two same-phone/same-pitch assignments,
  prepares each through the inventory score builder, admits/renders the batch,
  saves its manifest, collects both atomically and reopens the durable producer.
  Both styles survive; neither assignment gains marker/pitch approval. Repeated
  job references and a budget one frame below the required total are rejected.
- Focused export CTest passed (1/1, 3.96 seconds). Complete configured Release
  build also passed. This closes the batch identity mismatch, not the campaign
  scheduler, restart receipts or real singer qualification.
- Full configured regression after this repair: **123/123 CTest targets passed**,
  zero failures, 87.81 seconds. This also covers the intervening inventory-score
  and listening-pilot increments; it does not stand in for installed-host or
  independent musical acceptance.

### Bounded immutable campaign planning

- Added shared `generation_campaign.hpp/.cpp`. `planGenerationCampaign` accepts
  explicit planned take IDs, validates the producer and frozen recipe, constructs
  each inventory template and preflights it through the normal procedural snapshot
  compiler. It keeps only one temporary compiled snapshot at a time. Unsupported
  takes fail with the take ID; there is no truncation or substitute silence.
- Definitions capture exact initial producer JSON/hash (including inventory and
  source-policy evidence), recipe JSON/hash, score JSON/template identity, ordered
  take/style/coverage/pitch rows, frame totals and deterministic batch membership.
  Input order does not affect the canonical definition. Every job is UNPREPARED;
  no generation expectation is captured or producer/filesystem state changed.
- Admission retains the existing 64-job/32M-frame batch ceiling and applies
  aggregate job/frame/estimated-byte limits. Disk numbers are conservative planning
  allowances, not measured filesystem quotas. Definition serialization is bounded
  to 32 MiB. Cancellation is checked before and between template compilation.
- `verifyGenerationCampaign` requires the supplied digest, decodes frozen inputs,
  reconstructs the canonical plan and compares exact bytes. Changed totals,
  batch layouts, hidden expectation fields and numeric type spoofing fail even
  with a recomputed outer digest. This proves internal consistency against the
  selected digest, not authority to replace that digest or source approvals.
- Focused Release build and export CTest passed (1/1, 4.44 seconds), covering
  deterministic two-batch planning, resource mismatch, budgets, cancellation and
  adversarial definitions alongside real two-style generation/collection. The
  prior 123-target run predates this increment; no new full-suite run claimed.
- Still open: create/inspect CLI publication, filesystem quota enforcement during
  execution, just-in-time batch preparation, durable advancement/commit receipts,
  external-edit detection and crash/restart integration. A valid plan is not a
  completed or resumable campaign yet.

### Campaign CLI publication and inspection

- Added separate CLI command module, using shared planning/verification services:
  `draft-generation-campaign WORKSPACE RECIPE NEW_PLAN TAKE...`,
  `plan-generation-campaign WORKSPACE PLAN HASH NEW_DIRECTORY`, and
  `inspect-generation-campaign CAMPAIGN HASH`. Drafting uses default limits and
  explicit selected take IDs; publication retains the exact caller-selected
  plan bytes/hash, not regenerated expectations or a changed recipe.
- Plan publication recovers the current producer and rejects a changed initial
  state before creating output. The new directory's atomic `campaign.json` is the
  publication boundary; partial directories are retained and never overwritten.
  This is a point-in-time state check, not a workspace lock or authorization to
  run later without rechecking. Inspection proves frozen-plan consistency only.
- Real CLI tests draft, inspect and publish, reject repeated destinations and a
  wrong hash, verify unchanged producer bytes, then collect real two-style output
  and confirm stale publication fails before directory creation.
- Advancement remains unimplemented: just-in-time preparation and durable
  completion/recovery receipts must precede any resumable-execution claim.
- Verification: focused Release build and export CTest passed (1/1, 3.92 seconds).
  No fresh full-suite result or musical acceptance is claimed.

### Recoverable batch collection checkpoint

- Added `collectGenerationBatchWithReceipt`, a shared transaction building block
  for campaign advancement. It requires the original producer snapshot and frozen
  job references, uses a persistent exclusive receipt lock, validates ownership
  and budgets, then delegates atomic collection to the existing repository.
- Retry recognizes every original expectation through retained producer lineage.
  Partial recognition, changed initial state, an extra producer generation, or a
  conflicting receipt fails. Exact receipt retries neither collect nor generate
  again. Receipt bytes bind original/committed producer hashes, generation, take
  audio hashes and expectation hashes; they are create-new and never overwritten.
- Tests inject an interruption after the real two-style commit but before receipt
  publication, hide both temporary output directories, then recover the receipt
  from producer-owned assets without regenerating audio or advancing generation.
  Repeated recovery preserves receipt bytes; false receipts and an unrelated
  later producer save are rejected.
- Important remaining durability boundary: repository read-only recovery does not
  re-fsync its mutable current pointer. Recovered results therefore deliberately
  return `durabilityConfirmed=false` with a diagnostic, even though the receipt
  file itself is durably written. Add locked exact-generation pointer
  reconciliation before permitting the next campaign batch. Do not interpret
  successful recognition as permission to skip this boundary.
- This is not yet the complete campaign runner: batch preparation/resume, pointer
  reconciliation, advancement CLI and broader process-crash tests remain open.
- Verification: affected Release targets rebuilt and export CTest passed (1/1,
  4.30 seconds). No new complete-suite run claimed.

### Exact current-pointer reconciliation

- Added repository `reconcileCurrentPointer(expectedGeneration, expectedHash)`.
  It takes the existing workspace writer lock, refuses newer occupied generation
  or journal records, verifies recovered state against both supplied identities,
  and durably republishes `project.json`. It does not append generations, alter
  immutable record contents or change reviews/source policy. Cancellation is
  checked before locking and at the last pre-publication boundary.
- Batch receipt recovery now invokes this operation after recognizing the exact
  committed requests. This supersedes the preceding temporary unconfirmed-pointer
  result: recovery returns confirmed only after exact locked pointer publication
  succeeds. An error still leaves the committed take recoverable, never reimports.
- The integration fixture damages the pointer after the injected post-commit
  interruption. Wrong hash, cancellation and a competing writer are rejected;
  normal retry restores the exact pointer hash without adding a generation or
  regenerating hidden output. The existing later-external-change rejection stays.
- This verifies the pointer repair path, not arbitrary power-loss behavior across
  filesystems/platforms. Campaign batch preparation and advancement are still open.
- Affected Release targets rebuilt and focused export CTest passed (1/1, 3.89
  seconds). No fresh complete-suite or installed-host result claimed.

### Explicit generation preparation recovery

- Campaign integration exposed a prerequisite: existing job preparation required
  a brand-new directory and could not resume partially written inputs. New job
  preparation now publishes `preparation.json` first, containing the exact future
  manifest and its score/recipe/expectation digests. It then publishes inputs,
  reference and final `job.json` in that order under a preparation lock.
- Added explicit `resumeGenerationJobPreparation` using the original snapshot,
  producer and take. Recomputed intent must match exactly. All existing named
  files are checked for regular-file status and exact bytes before any missing
  file is written. Changed producer expectations, conflicting content, symlinks
  and directories without intent are not adopted. No files are overwritten.
- Existing prepare APIs retain create-new behavior. Existing complete jobs remain
  readable without the new intent. An old partial job or interruption between
  directory creation and intent publication remains unowned and is not silently
  repaired; campaign-level recovery must preserve that artifact explicitly.
- Tests retain a complete job, hide score/expectation/final manifest, reject a
  changed producer and tampered recipe before filling any gaps, then restore the
  exact inputs and recover the original manifest/expectation hashes. Repeating
  explicit resume succeeds while ordinary prepare and unowned-directory resume
  remain rejected. These are interruption-state fixtures, not OS process-kill tests.
- This is the job-preparation building block, not yet campaign batch advancement.
- Verification: affected Release build and focused export CTest passed (1/1,
  4.36 seconds). No fresh complete-suite run claimed.

### Just-in-time campaign batch preparation integration

- Added shared `prepareGenerationCampaignBatch`. It verifies the immutable plan,
  bounds the batch index, matches batch 0 to the exact initial producer, and
  requires a confirmed predecessor receipt/current-producer hash and expected
  generation offset for every later batch. This is an internal service: callers
  must supply a verified collection result, not trust arbitrary receipt JSON.
- Each batch retains its original producer JSON and campaign/index identity under
  a preparation lock. Selected score templates must still exactly match frozen
  plan bytes. Job creation/resume uses the retained-intent preparation service;
  batch manifests are create-new or verified against the exact prepared job list.
  No rendering, collection or approval happens during batch preparation.
- A real two-batch integration now plans two styles, prepares/retries batch 0,
  renders and collects it, recovers the producer, prepares batch 1 using the first
  confirmed receipt, renders and collects batch 1, and reopens both takes. The
  second job's expectation binds the first commit's hash, explicitly not the
  initial hash. Premature batch 1 and stale batch 0 are rejected before creating
  their directories. This demonstrates sequential preparation, not only planning.
- Focused Release build and export CTest passed (1/1, 3.87 seconds). No new full
  suite run is claimed. The persisted advancement controller/CLI, authoritative
  receipt-chain loading, runtime disk quotas, crash-window handling before intent
  publication, and OS process-kill tests remain open. Do not label this a complete
  resumable campaign runner yet.

### Repository-backed historical receipt verification

- Added exact-hash `recoverGeneration` and optional historical generation/hash
  arguments to `findCollectedGeneration`. Historical reads validate the requested
  immutable generation and its normal repository evidence; they never move the
  current pointer or silently substitute a newer/older recoverable snapshot.
- Added `loadVerifiedGenerationBatchReceipt`. It verifies the original producer
  against stored history, checks frozen job expectations, resolves the exact next
  generation, validates take lineage/audio identities in that historical state,
  and reconstructs canonical receipt bytes. Unknown fields, altered values or
  a valid hash belonging to the wrong generation are rejected.
- The two-batch test verifies batch 0's saved receipt after batch 1 has committed:
  the returned historical state contains one take, while the current pointer and
  two-take workspace remain untouched. Negative cases replace the committed state
  hash with the latest generation hash, forge an audio hash, insert an unknown
  field, or request a missing/wrong-hash historical generation.
- This supplies authoritative persisted-receipt loading for the forthcoming
  advancement loop. Historical verification alone does not assert currentness or
  restore a pointer; the loop must compare its final state with the current
  producer and use exact reconciliation before further mutations.
- Verification: affected Release build and focused export CTest passed (1/1,
  4.22 seconds). No fresh complete-suite run claimed.

### Persisted campaign advancement loop and CLI

- Added `advanceGenerationCampaign` and the planned CLI form:
  `advance-generation-campaign WORKSPACE CAMPAIGN_JSON CAMPAIGN_SHA256 OPERATOR UTC`.
  The immutable campaign and repository history are authoritative; there is no
  mutable unchecked progress counter. A campaign lock serializes invocations.
- The loop reconstructs completed batch state using verified historical receipts
  and exact frozen templates. For the first incomplete batch it requires the
  expected producer state, prepares/resumes original jobs, renders only if the
  requests have not already been collected, and atomically collects with a durable
  receipt. Each invocation advances at most one incomplete batch.
- A producer one generation ahead is considered only when retained batch inputs
  exist, and original-request recognition must prove every take before skipping
  rendering. This handles commit-before-receipt interruption without rebinding
  expectations. Partial collection and unrelated changes fail. A completed retry
  verifies the whole chain and requires current producer equality.
- CLI reports `BATCH_COLLECTED` or `COLLECTED_UNREVIEWED`, always with
  `releaseEligible:false`. It does not manufacture source rights, independent
  reviews, qualified singer resources or release approval.
- Integration tests inject a post-commit/pre-receipt interruption in batch 0,
  recover it with one take still present, advance batch 1 through the real CLI,
  verify two takes, repeat without a generation change, then reject an external
  producer save. Focused export CTest passed (1/1, 4.20 seconds).
- Remaining hardening: directory-created/intent-not-yet-published recovery,
  hard runtime disk quota accounting, process-kill/cancellation coverage and
  large-campaign performance. Current completed-batch traversal revalidates plans
  repeatedly; measure and eliminate redundant compilation before singer-scale
  campaigns. This pilot-scale working loop does not close all M1.P2 obligations.
- Full configured Release build passed. Full CTest run: 122 passed, one failed
  (84.18 seconds); source closure correctly reported the new advancement source
  was not yet indexed. After staging that exact file, the source-closure target
  passed (1/1, 0.22 seconds). No code changed between those runs; all 123 targets
  now have passing evidence, but no second all-green full invocation is claimed.

### Reuse immutable campaign admission during traversal

- Added `VerifiedGenerationCampaign`: callers cannot construct it from unchecked
  JSON. Admission performs the existing digest/canonical reconstruction checks and
  owns an immutable parsed plan. Copies share that plan; changing the source text
  after admission cannot change its content or digest. Moved-from handles are
  rejected by preparation before filesystem writes.
- Advancement now admits once and passes the handle through each visited batch.
  Previously it performed a full-plan verification initially and again for every
  batch; each verification compiled every campaign template. This change removes
  those repeated whole-plan preflights without removing per-batch producer,
  predecessor, frozen-score, prepared-job or repository-history checks. Existing
  string-based preparation remains a wrapper that fully admits its input.
- Tests exercise shared immutable ownership, source-buffer replacement, wrong
  digest and moved-from rejection, then run the existing two-batch/CLI recovery
  flow through the admitted-handle path. This is a structural reduction in repeated
  work; no singer-scale wall-clock speedup is claimed without a benchmark.
- Remaining scaling work includes indexing selected rows and reducing repeated
  historical job loads. Runtime disk bounds and OS process-crash qualification
  also remain open; this change does not complete M1.P2.
- Verification: affected Release build and export CTest passed (1/1, 4.55 seconds).
  No new complete-suite run claimed.

### Real process termination at campaign commit boundary

- Extended the existing generation test helper with a campaign mode that raises
  SIGKILL after the producer commit and before collection-receipt publication.
  The parent uses actual process wait status and requires termination by SIGKILL,
  rather than accepting a generic failure or an injected Result error. The signal
  path is enabled on macOS/Linux; this run was on macOS.
- The fixture confirms one committed take and no receipt, moves the temporary
  output directory aside, then retries through the normal advancement CLI. Retry
  succeeds with exactly unchanged producer bytes and batch manifest hash and does
  not recreate output. It then advances the second batch and retains the existing
  completed-retry and external-edit rejection checks. This also exercises release
  of campaign/receipt OS locks when destructors cannot run.
- Focused Release build and export CTest passed (1/1, 4.40 seconds). This is real
  process-death evidence for the post-commit/pre-receipt window only, not a machine
  power-loss test or complete campaign crash matrix. Preparation-intent windows,
  cancellation during rendering, storage bounds and Windows qualification remain.

### Bounded retained-storage inspection and phase guards

- Added shared `inspectCampaignStorage`: counts logical file bytes (including
  sparse files and duplicate/hard-linked paths conservatively), bounds entries
  and nesting depth, checks cancellation, and rejects symbolic links, special
  files or enumeration errors. It does not follow links, delete artifacts or
  treat unreadable paths as empty. Only the campaign directory is counted.
- Advancement applies the admitted byte allowance before its lock/write work and
  checks retained storage after preparation, rendering and collection. Exceeding
  a boundary returns an error and preserves evidence. The producer-owned asset
  repository outside the campaign directory is not included by this scan.
- Tests count a five-byte/two-file fixture exactly, reject four-byte and one-entry
  allowances, cancellation and a symlink, then place an over-limit sparse canary
  in a real campaign directory. Advancement rejects it before preparing batch 0
  or collecting any take. Moving the canary aside allows the existing SIGKILL/
  CLI-recovery scenario to proceed.
- This is phase-boundary enforcement, not a hard per-write disk quota: a phase
  can overshoot before its post-check, and external writers can race a scan.
  Reservation/accounted writers and combined campaign/producer growth remain
  necessary before declaring aggregate storage control complete.
- Verification: affected Release build, export CTest (1/1, 4.41 seconds), and
  staged-source closure (1/1, 0.25 seconds) passed. No new full-suite run claimed.

Next concrete implementation owners: explicit evidence-backed legacy migration,
then complete populated-workspace parity and candidate
review/publication parity and the resumable inventory campaign. The generation
test uses synthetic diagnostic material, not a qualified singer. No M1 completion
is claimed.

## Voiced reattack boundary repair

### Actionable recipe coverage errors

Recipe articulation failures now retain the original error code and identify
the recipe ID and selected style alongside the existing phone/note diagnostic.
Missing VocalTract poses identify phone, style and recipe rather than only
reporting a missing pose. Tests verify missing phone/style and the stderr from
an actual unsupported `ば:60` production pilot export. No source substitution,
DSP change, approval or expanded phonetic support is implied. Targeted Release
build and voice-design/pilot CTests passed (2/2, 7.78 seconds).

### Bounded user-authored pilot phrases

The pilot CLI now accepts `phrase LYRIC:MIDI ...` rather than only fixed
fixtures. It validates 1–64 UTF-8 lyric/pitch pairs, MIDI 24–96, and retains
ordinary editable `.seam` projects plus recipes, WAVs and unapproved candidates.
Notes currently use 480 ticks at 120 BPM; richer editing belongs to the saved
score/native editor, not a second CLI score engine. The combined explicit pilot
recipe supports the existing phone models and leaves unsupported phones as
render errors. A custom m/a/t/a/continuation-a/N/a phrase exported successfully
to `build/release/seam-pilot-custom-01`. Tests cover actual marker output,
invalid syntax/pitch/count rejection before directory creation and unsupported
voiced-stop rejection without a successful report. This is not complete phone
coverage, a populated bank or the full new-song acceptance journey.
Verification: Release pilot target built; updated pilot CTest passed (1/1,
4.92 seconds). No full-suite rerun or listening acceptance claimed.

### Standalone nasal production fixture

Added `seam_singer_pilot NEW_DIRECTORY nasals`: six alternating standalone
Japanese `N` and oral vowel notes through normal ExportService. Inspection
confirmed the existing syllabic-N fallback timing path; no new timing or DSP
semantics were needed. A dedicated explicit nasal recipe pose supplies full
nasal coupling with resonance/antiresonance instead of inserting an oral vowel.
The CLI regression checks exact N/a/N/i/N/u markers, full-note spans, unapproved
metadata, voiced analysis windows in every note, audio hashes and deterministic
repeat exports across three variants. Release pilot build and updated pilot
CTest passed (1/1, 4.67 seconds); no fresh full-suite run claimed. Listening
artifacts are retained in `build/release/seam-pilot-syllabic-nasal-01`.
This exercises the required standalone-N path but does not qualify its sound,
close all phonetic classes, or complete M1.P2.

### Vowel-only production-path follow-up

The new `seam_singer_pilot NEW_DIRECTORY boundaries` fixture renders the same
four-note melody as separate vowels and as a melisma, across three recipe
variants. Its real exported dry-PCM assertion initially failed: the baseline
first reattack boundary had summed adjacent absolute amplitude 0.01975246.
Pure-vowel phrases select SustainedPoseStream, so the mixed renderer repair
alone did not fix this production path. Sustained vowel scheduling now applies
the existing 5 ms taper at compiled reattacks without tapering continuations.
Its renderer revision increased from 12 to 13 to invalidate prior identities.

The CLI regression checks eight vowel markers, hash-bound Float32 mono audio,
zero boundary samples for separate attacks, nonzero continuation boundaries,
and exact repeat hashes for all three variants. Targeted Release build and
pilot/voice-design/export CTests passed (3/3, 8.38 seconds). No fresh full-suite
run is claimed for this follow-up. Listening artifacts are retained in
`build/release/seam-pilot-boundaries-02`; `-01` retains the pre-fix comparison.
Neither fixture is a listening-quality acceptance result.

- Source inspection found that PhonationSource restarts phase on compiled score
  reattacks, but ArticulatedStream previously joined all adjacent voiced gestures
  without an envelope taper. Optional authored attack/release controls do not
  supply a default taper. A new adjacent-vowel regression failed before repair
  at the final sample preceding the reattack.
- The mixed renderer now applies its existing bounded 5 ms smoothstep taper at
  gesture boundaries that coincide with a compiled note reattack. Intra-note
  phone transitions and compiled melisma continuations remain connected. Filter
  and source state are retained; this is not a filter reset or score mutation.
- ArticulatedStream revision increased from 8 to 9. Existing snapshot/cache and
  candidate metadata paths consume that constant. Recipe schemas are unchanged;
  affected old render identities must not be treated as newly rendered evidence.
- Regression covers separate Japanese vowel notes, a continuation vowel,
  exact boundary silence only for reattack, and sample-identical whole/chunked
  rendering with an owned-window split one frame before the boundary. The
  expanded matrix exercises 22.05/44.1/48/96 kHz, starting MIDI pitches 36/61/84,
  both reattack and continuation (24 combinations), cancellation rollback,
  checkpoint replay and reset replay.
- Verification: affected Release targets built; voice-design and export CTest
  targets passed (2/2, 7.57 seconds). The new regression was observed failing
  before the implementation change. No full-suite or listening-quality pass is
  claimed. Voiced stops, expanded phonetic context, qualified singer assets and
  the remaining six-milestone plan are still open.
- Regenerated three articulation variants through the production ExportService
  in `build/release/seam-pilot-articulation-reattack-01`. These are retained
  unqualified listening artifacts, not phonetic or identity acceptance evidence.
- Follow-up verification: complete Release build passed, followed by a fresh
  full CTest invocation: 123/123 passed in 84.76 seconds, including the expanded
  boundary matrix and production pilot CLI. This supersedes the earlier narrow
  test boundary for this repair, but does not establish musical qualification.
