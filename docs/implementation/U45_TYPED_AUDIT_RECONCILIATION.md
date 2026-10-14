# U45 typed audit reconciliation

2026-10-09. Adapted from archived `2a3d23e0` onto `ed5f9a5d`.

Status: **engineering prerequisite; U45 and full Beta acceptance remain incomplete**.
The canonical full-product contract still declares its semantic validator
`UNAVAILABLE`. No qualification profile, released resource, human decision or
release state is changed by this increment.

Current operation checkpoint (2026-10-13): the native installed recipe export
was implemented in `95d093b1` and repaired/observed on 2026-10-12; see
[recipe completion](../reviews/INSTALLED_RECIPE_EXPORT_COMPLETION_2026-10-12.md).
The new [signed sample export unit](INSTALLED_SAMPLE_EXPORT_2026-10-13.md)
connects signed consumed sample bytes to actual Final export artifacts. Older
reference-only statements below describe their dated increments. The separate
[local operation replay](INSTALLED_OPERATION_REPLAY_2026-10-13.md) now compares
installed sample/recipe observations and receipt semantics with fresh rendering
through an externally pinned producer. Historical execution is not authenticated.
The [installed procedural continuity observer](INSTALLED_PROCEDURAL_CONTINUITY_2026-10-15.md)
now retains actual whole, restarted-owned-window and resumed-stream PCM with raw
float-word differences and derived plan projections. It is a selected procedural
snapshot API prerequisite, not host or release-case acceptance; classical crops
and unavailable neural execution cannot substitute for stateful chunk evidence.
Remaining continuity/measurement semantics, real authority and the canonical
U45 hold stay open.

## Executable boundary

`tools/external_beta/full_product_gate.py` checks resource declarations, case-axis
coverage, retained review/measurement record consistency, selected artifact
syntax, and candidate/workload/content digests. It runs the existing full reader
first with the explicit archive root and the same `SoakReplayContext`, including
its guarded snapshots, aliases and terminal read denials. The typed evidence
cache consults that context before its own cache and has a 256 MiB aggregate
budget for ordinary retained files. Both successful and failed ordinary reads
are memoized for the audit.

The report schema now requires `evidenceClass` and `reviewerRegistry`. The
reserved synthetic contract cannot authorize READY or CLOSED. The existing
reader fixture adds this metadata and the case-specific criterion measurements;
its legacy soak refusal remains unchanged. Public replay continues to run the
real validators with its original four exact soak refusals, without an audit
mock or fixture-admission bypass.

`verify_full_product_report.py` runs the typed adapter. It uses a bounded,
no-follow top-level report snapshot and accepts `--evidence-root` (default:
report directory). BLOCKED exits 3. A hypothetical complete synthetic report
would exit 5 with `authorizesRelease: false`; the current archived fixture is
BLOCKED because its soak blobs are not supervised-session records. Exit 0 is
reserved for canonical authorization, which the explicit
`U45_RECONCILIATION_HOLD` currently prevents.

The release gate still calls the existing report reader. **Do not replace that
call or remove the reconciliation hold as part of an incidental cleanup.**
A later reviewed integration must preserve EB-005/R17 shared context and all
existing soak refusals, while closing the semantic gaps below.

## What the checks prove

- All 83 archived component case rows can satisfy their declaration checks in a
  temporary synthetic fixture. This is separate from full-report acceptance.
- WAV parsing checks exact RIFF/chunk bounds, complete integer/float frames,
  finite samples and nonzero signal. Float negative zero is silence; extensible
  subtype GUID and valid-bit width are checked. This is not singing, language,
  timbre or audible-quality validation.
- PNG parsing supports noninterlaced 8-bit RGB/RGBA screenshots with checked
  chunk CRCs, contiguous compressed image data, complete bounded scanlines and
  legal filter bytes. Decoded data is capped at 64 MiB. Other PNG encodings are
  refused explicitly. It does not prove what the depicted controls did.
- Project parsing checks the SEAM document envelope only. It is not a native
  codec load, recovery or editor replay.
- Reviewer registry and measurement constraints are retained declarations. The
  adapter checks consistency and self-approval constraints; it does not prove
  that a declared person exists, is independent, or listened to the material.

## Remaining integration work

1. Add a separate closed U14 engineering-execution record and validator using
   actual schema-3 candidate, signed package verification, native install receipt
   and installed candidate bytes. It must return engineering success without
   release authorization, reject human/qualification fields, and stay distinct
   from the 83-case release report. Test positive sample/recipe execution and
   opaque model package verification with intentional install refusal.
2. Bind every required installed resource to a retained real receipt. The
   archived `installReceiptSha256` field alone is only a declaration, and its
   conditional missing-receipt check is insufficient. Require a release
   candidate for release-installed identity checks.
3. Preserve U14 family-specific identities: sample candidate `contentSha256`
   matches receipt `contentHash`; recipe content SHA identifies the recipe,
   while the installed singer hash covers manifest plus recipe. Keep resource
   candidate SHA distinct from release candidate root identity. Runtime
   dependencies carry kind/id/revision; do not invent package digests for them.
4. Replay native project, operation, continuity and measurement semantics.
   Distinct file hashes and `MET` claims do not establish those behaviors.
   Human constraints need real retained reviews and external authority binding;
   an arbitrary retained file cannot stand in for a human protocol judgment.
   Each observation also needs a native provenance record binding platform,
   host, build, installed tree, resource, language and artifact digests, plus
   native project loading that confirms the language/resource bindings. A
   duplicate-digest ban is not a substitute: deterministic output may legitimately
   be identical across platforms. These are release-blocking P1/P2 conditions,
   together with actual receipt verification, before lifting the hold.
5. Review coverage obligations beyond the archived independent-axis coverage:
   per-resource/per-case combinations, dependency resolution and approved
   incompatibilities must match the frozen contract. No platform/host waiver.
6. Integrate the completed executor into READY/CLOSED with the existing shared
   soak context. Retain engineering-only and synthetic-contract refusals.
   Freeze the canonical profile only with the designated independent owners;
   validate real installed resources and externally anchored human evidence.

Full acceptance still requires the unaided 20-row production-bank UA journey,
learned singing and language/creator evidence, Windows and nine host tuples,
signed installed workloads and authorized immutable-archive release decisions.
None is inferred from the synthetic checks. `.github` and Windows TODO remain
unchanged.

## Installed-resource bridge increment (2026-10-09)

The separate `seam.u14.installed-candidate-verification.v2` record now derives
sample/recipe identity from a pinned signed package, actual receipt and installed
bytes through the native CLI. A bounded private package snapshot prevents ordinary
source replacement from mixing package reads. Retained-record replay requires
caller-pinned CLI/key/record bytes and fresh native output; it never authorizes
release. This addresses installed-content verification within item 1, not its full
execution-record obligation. It does not prove who installed it, actual catalog
placement, resource use in an observation, source/build/platform lineage, dependency
availability, model runtime admission or human qualification.

Real native regression paths cover sample/recipe positives and opaque model
package verification followed by install/audit refusal. The Python closed record
supports sample/recipe success only; the model extension below supplies a distinct
refusal outcome record. Installed v2 exposes signed dependency triples (empty for
samples, one render-engine for recipes), matching model v1 declaration semantics.
Runtime availability remains NOT_CHECKED. Closed historical v1 replay requires a
pinned historical CLI; cross-version output refuses. Per-observation provenance, native project/
operation/measurement replay, qualified resources, frozen profiles and external
authority still block canonical integration. `release_gate.py` and
`U45_RECONCILIATION_HOLD` remain unchanged.

### Model outcome extension (2026-10-09)

The separate closed `seam.u14.model-installation-refusal.v1` record now observes
opaque package verification and actual intentional native model-install refusal
in owned scratch. Replay pins the CLI/key/record and compares fresh native output,
including signed dependency declarations. It has no installed-tree identity and
never asserts graph execution, runtime availability, qualification or release
authority. This addresses the model outcome part of item1; it does not implement
model installation or close U14/U45. Installed v2 supplies sample/recipe dependency
output; canonical catalog placement and execution/observation provenance remain open.


### Native project binding increment (2026-10-10)

[Project binding verification](../formats/PROJECT_BINDING_VERIFICATION.md) adds
native codec/domain validation of captured, caller-pinned project bytes and a
selected track/region's singer identity and note-linked declared language set.
It shares the renderer's selection rule, rejects unknown raw language tags across
the file, and records the source/codec schema versions. Replay compares a closed
engineering record with fresh native output. This partially addresses item 4;
it does not establish actual resource admission, operation/continuity/measurement
execution, pronunciation, host/build lineage or human acceptance. Recipe render
identity remains distinct from installed package identity. The canonical report
and U45_RECONCILIATION_HOLD remain unchanged.

### Installed/project identity linkage (2026-10-10)

The combined native installed-project-binding record derives a sample or recipe
project identity from the verified package snapshot whose entries were checked
against the installed tree, then runs the native project binding against that
identity. It preserves the family-specific content hashes, verifies selected
note-language inclusion in the signed declaration, and supports pinned fresh
composite replay. No project-supplied resource path is opened. This advances items
1–3 and the project-reference part of item 4; it is not runtime admission, catalog
placement, an installation event, an
atomic cross-file snapshot or an observed render/operation. Models remain without
installed admission. The 83-case report integration and canonical hold are unchanged.


### Procedural admission consistency (2026-10-10)

Procedural package verification now reads the signed recipe and checks its native
codec limit, canonical digest, engine id and declared style/phone inventories.
Packing, installation and catalogue discovery use the same consistency checks;
a signature alone cannot make absent inventory selectable. Declarations may be
subsets of the recipe's inventories. This is not proof of every style/phone pair,
phrase coverage, language quality, compatible engine revision or observed sound.
The catalogue remains discovery, not independent verification of receipt trust.

The earlier 16 MiB versus 512 KiB concern is narrower than an installed-resource
size mismatch: recipe decoding already enforced 512 KiB during family packing,
installation and catalogue loading. Previously, standalone package verification
checked only the recipe entry's existence and could report success even when
later decoding would fail. It now rejects that input too. Boundary regressions
cover a valid whitespace-padded recipe at 512 KiB and rejection one byte over.
Actual runtime operation/artifact evidence and the canonical U45 hold remain open.


### Installed-singer chooser freshness (2026-10-10)

The standalone installed-singer command captures its document context and previous
selection before either modal picker. Accepted choices must still belong to that
context, and the selected track must still exist. Before writing the reference it
refreshes the same installation's manifest, render identity, content hash, package
and signer identity, trust and selectability, then loads and decodes the exact
recipe. A removed/replaced singer or an intervening document change cannot be
committed as a fresh choice. Cancellation preserves the document.

This selection-time increment left filesystem observations sequential,
with files able to change afterward. At that checkpoint it did not solve render-time classification of
installed singers versus authored drafts, or bind an engine revision to a saved
reference. The shared admission boundary must preserve existing absolute authored
drafts and project-relative recipe workflows while enforcing installed metadata
where applicable. ProductionProjectRenderer and operation evidence should consume
that shared boundary after the product call sites are migrated. The canonical U45
hold and full product acceptance requirements are unchanged.

### Persisted procedural installation binding (2026-10-10)

[Project schema 21](../formats/PROJECT_JSON_V21.md) now separates recipe render
identity from installation/distribution and engine pins. Installed selection,
New Project and Studio song creation record the current candidate; authored
replacement and draft copies deliberately do not. Portable export preserves
origin pins. Stale/missing receipts cannot supply package/signer provenance.
Old project references stay unbound on load. Binding equality participates in
performance-job freshness and undo/redo.

At the persistence checkpoint rendering did not enforce these pins. The runtime
increment below consumes them; the later composite comparison increment checks
the new binding against the verified installation. For existing evidence records,
resourceAdmission/runtimeAvailability remain NOT_CHECKED, and no installed,
interactive, hearing or Beta acceptance is added. Old retained records require
their pinned verifier; fresh records name codec 21.

### Shared procedural runtime admission (2026-10-10)

Product file-backed preview/export/bake/packaging and saved-score generation now
use the [schema21 admission contract](../formats/PROJECT_JSON_V21.md#runtime-admission).
Admission checks full bound installation identity, captured recipe bytes, declared
style, matching-receipt policy and actual compiled engine revision. Relative
copies retain provenance without claiming installed status. Known-root and typed
metadata classification covers legacy paths conservatively; fully unbound paths
outside known roots remain ambiguous when metadata is gone. Fresh preview
requests admit before cache use. A captured source is valid for its owned request,
not evidence that the installation remains present afterward.

This closes the identified product-loader bypass; it does not promote the native
U45 project/composite record's NOT_CHECKED fields. After the pin comparison below,
the verifier still needs to connect observed final render/artifacts to the same admitted request. Receipt
trust is not independent package signature verification, and filesystem checks
are sequential. The canonical U45_RECONCILIATION_HOLD remains. Independent review
of this runtime increment is PENDING because the authorized reviewer chat returned
HTTP429; engineering checks and their limits are recorded in the root ledger.

### Composite comparison of saved installation pins (2026-10-10)

The native composite now compares every present schema21 installation pin against
the independently verified signed package and installed contents. The expected
identity comes from the same private package capture; comparison uses the existing
single held project decode. Both absolute references and relative copies must
match all eight fields, including nonempty signed package/signer pins. No saved
resource path is opened. Unbound projects remain reference-only matches; a PASS
does not establish that provenance was stored or that the path belongs to the
verified directory. The closed v1 record shape and its existing limited claim
remain unchanged; historical replay still requires its pinned verifier.

This does not compare the declared engine with the executing build, exercise
runtime admission, or observe rendering. Product admission and future operation
records retain those obligations. U45_RECONCILIATION_HOLD remains unchanged.

### Production source binding prerequisite (2026-10-10)

Tracing operation provenance exposed a production-render omission: supplied sample
identity could disagree with the saved selection and still render. The shared
render/export metadata preflight now rejects persisted identity or family drift
before resource capture/cache use; packaging also validates muted supplied sources.
Legacy missing sample pins and wholly unselected raw authoring sources retain
their explicitly limited behavior. See the production source-binding section of
[the binding contract](../formats/PROJECT_BINDING_VERIFICATION.md).

This repairs a prerequisite for credible operation evidence. It does not add an
operation record or prove asset bytes, signed installation, host behavior or human
acceptance. The next observation must still connect the same captured admission
and actual rendered artifacts; the existing U45 hold remains.


### Installed procedural continuity replay prerequisite (2026-10-15)

The [closed continuity reader](INSTALLED_CONTINUITY_REPLAY_2026-10-15.md) now
requires zero raw differences and matching derived phone projections, reads the
held retained outputs and observation, and compares every field with fresh
caller-pinned native execution. Coordinated PCM and phone-projection forgeries
with consistent artifact hashes are rejected by real replay. Full Release build
and focused Python443/CLI27 checks passed. Nine mocked policy methods overlap
the Python total; six new native reader calls are inside an existing CLI case.
The evidence index retains277 artifacts/20 bindings, immutable source captures,
receipts, raw controls and the previous unit's independent APPROVE0b50d8bd.
This reader's technical review is pending. It is an engineering prerequisite,
not canonical report integration or historical-execution authentication.
U45_RECONCILIATION_HOLD, UNRESOLVED tolerances, Alpha0of20 and BetaBLOCKED remain.
Actual scheduler/cache/neighbor, host/platform and released-resource/human
requirements still need their own evidence and reviewed semantic integration.


### Installed scheduler/neighbor API prerequisite (2026-10-15)

The [native scheduler regression](INSTALLED_SCHEDULER_INVALIDATION_2026-10-15.md)
connects one signed installed fixture to a single adjacent-lyric edit, changed
pronunciation identity and freshly allocated performance objects, changed chunk
identities, actual cold and warm scheduling, four submission-time stale refusals
and unrelated-region cache reuse. Performance pointer inequality alone proves no
semantic plan change. Retained
raw bytes show6344 changed words;72000 DSP frames/12 cache hits/4 stale entries.
Full rebuild and snapshot66/CLI27 pass. The read-only audit binds74 artifacts/five
inputs; real production actors/resources and canonical evidence are not added.
Independent APPROVE445b803b closes the prior reader review; its optional metric
wording P3 is corrected. Independent APPROVE667c8a19 closes this scheduler review,
engineering scope only. The reviewer decoded all 12 cache payloads and attributed
window0's 240 changes and 240 of249 in window3 to switched-on 5 ms edge fades.
Snapshot recreation
is explicit in the test: production editor dirty/publication callbacks, real
host blocks, acoustic observations, tolerance ownership and semantic integration
remain unproven. U45_RECONCILIATION_HOLD/Alpha0of20/BetaBLOCKED remain unchanged.


### Connected runtime sample-rate status repair (2026-10-15)

The [sample-rate dossier](AUDIO_FORMAT_STALE_STATUS_2026-10-15.md) reproduces a
production status defect in both queued coordinator and Rendering runtime paths:
48 kHz retained audio was not marked stale during a same-revision/quality
44.1 kHz replacement. Current publication safety already refused it. The
existing predicate now also compares sample rate. Unchanged regressions fail
against the rebuilt baseline and pass after the library repair; full untargeted
Release rebuild (10.304s) plus four focused suites (135 cases, 32.417s) pass. There is
one new case and one strengthened existing case. Setup compile/wait mistakes
and genuine failures remain distinguished and retained. The read-only evidence
audit checks 56 artifacts/six pins; exact APPROVE667c8a19 is for the prior scheduler
unit only, and this repair's independent review is pending. Production host audio
blocks, late scheduler completion controls, acoustic/classical/neural/platform/
resource/human/tolerance work and semantic integration remain. No canonical
recording or promotion is performed. U45hold/Alpha0of20/BetaBLOCKED remain.


### Sample-rate test ordering follow-up (2026-10-15)

Independent APPROVE3a0fa6f1 confirms the production predicate, regression failures
and retained135 passes. Its P2 test-only ordering finding is corrected: await
Ready progress with a cleared stale flag as well as the replacement publication,
and check both replacement handles before dereference. Full untargeted build
5.031s and runtime56/coordinator38 =94 cases in9.830s pass. No product/library
change, new counted case, forced-race reproduction or canonical promotion occurs.
The [dossier](AUDIO_FORMAT_STALE_STATUS_2026-10-15.md) pins exact review and
separate follow-up evidence. Four host-harness files remain uncommitted in
progress and are excluded from this unit. U45hold/Alpha0of20/BetaBLOCKED remain.


### Loaded CLAP saved-installed-project prerequisite (2026-10-15)

The [host dossier](INSTALLED_CLAP_PROJECT_2026-10-15.md) adds caller-pinned saved
project admission and a canonical public CLAP state round trip to the actual
loaded-module host. The existing signed installed one-note fixture is used after
producer/package deletion; resource admission rules remain unchanged. Immutable
baseline binaries fail the final regression source. A test-only short/padded-ID
assertion error and a verifier-only nested-manifest exclusion error are retained
separately. Full rebuild27.311s and two focused entries (four Python methods/34
native cases,2.435s) pass; two child fixture calls overlap the existing native
case. Direct public-process-buffer execution retains84,000 frames of48k PCM;
161,175 is the sum across both rates, not one artifact extent. Finite PCM and
project/recipe/receipt/hash/source/count checks pass for75 artifacts/11 inputs.
Exact APPROVE1c6a5851 closes the previous assertion follow-up; this host unit's
review is pending. The resource is a legacy signed procedural test fixture, not
a qualified singer or imported internet bank. No fresh package-signature check,
self-contained relocated replay, acoustic F0/phase, physical-device/DAW/Windows
or human acceptance is claimed. Canonical integration/tolerance and full-scope
resource/continuity/publication/30–60-second-song requirements remain open.
U45hold/Alpha0of20/BetaBLOCKED/full goalACTIVE are unchanged.

### Loaded CLAP full-song prerequisite (2026-10-15)

The [full-song dossier](INSTALLED_CLAP_SONG_2026-10-15.md) connects the existing
48-note/41-second tuned, saved/reopened installed song to cold loaded-plugin
execution after producer/package deletion. Baseline host limits reject it;
extending them exposes a real Final source-digest mismatch between identity
encoding without renderer provenance and publication encoding with provenance.
Matching both encodings repairs the refusal while preserving other guards.
Full untargeted rebuild4.369s and eight focused entries in33.337s pass:
five Python methods,73 native cases and the phase12b program. Direct execution
retains2,004,000 stereo48k frames;3,845,175 sums both rates, not one WAV extent.
The read-only audit binds79 artifacts/10 inputs and derives finite PCM/counts.
The297 internet originals remain unchanged and unqualified. Independent review
is pending; legacy procedural fixtures and full-song signal do not grant human,
resource or canonical integration acceptance. U45hold/Alpha0of20/BetaBLOCKED
and the full goalACTIVE remain unchanged.


### Finished-worker publication admission (2026-10-15)

The [publication dossier](PUBLICATION_ADMISSION_2026-10-15.md) closes the late
request check-to-physical-publication interval. Same-revision replacement,
cancel, idle reset and owner/debounce invalidation are forced after the early
check; all four controlled-baseline cases overwrite retained accepted audio,
and all four pass after publication/revocation share admission locking.
Playback already had current-token refusal; this repairs retained history.
Full Release rebuild5.327s, eight focused entries (five Python/246 native)
in42.720s, nine owner-runtime cases and42 direct Debug TSan cases pass. The
regressions and TSan repeat the coordinator cases rather than add unique cases.
Exact parent/observed/final source, binaries/configuration/receipts/counts are
retained for42 artifacts/six inputs; the observer hook is disclosed and is not
an unmodified historical binary. No acoustic/human/released-resource/target
qualification is granted. Independent review is pending after the preceding
live review; canonical R15/U45hold/Alpha0of20/BetaBLOCKED/full goalACTIVE stay.


### Installed full-song note-pitch diagnostic (2026-10-15)

The [pitch dossier](INSTALLED_CLAP_SONG_PITCH_2026-10-15.md) repairs the existing scorer's ignored region placement/later regions/tempo changes/source positions and assumed rate. Five parent regressions fail, then all12 pass; final focused29 methods pass. Native Direct v3 analysis of the unchanged retained48-note stereo song completes in10.569s; all48 note medians pass, worst0.680453 cents. All2530 selected middle-third frames are within50 cents (median absolute0.309123, max2.620477); this is not phonetic steady-span or human acceptance. An omitted960-tick host shift yields only6 passing notes; mismatched source and unsupported FollowHost are refused. Broader quality CTest fails:181 methods/one failing method/19 skips. The unchanged parent demo-bank root-pitch module reproduces its old >500-cent classification contradiction (-38.579896 cents) independently of the scorer. It remains unresolved and is the next bounded diagnostic repair, without inferring production suitability. No C++/DSP/criterion change or new native rebuild is claimed. Read-only audit binds71 artifacts/eight preserved inputs and independently recalculates the fixed song's retained frame statistics, with source/execution-authentication limits disclosed. Source verification again checks297 unchanged unqualified internet WAVs, with no new download/import. The ccbb39d6 review remains live; later song/publication/scorer units are unreviewed. Genuine actors/rights/coverage/failed-QC/listening and full classical/neural/resources/targets/DAW/release/tolerance/integration remain. Alpha0of20/BetaBLOCKED/U45hold/full goalACTIVE and original/canonical/stash/.github/README Windows TODO preservation remain.


### Bank root-pitch span/method diagnostic correction (2026-10-15)

The [bank-root dossier](BANK_ROOT_DIAGNOSTIC_2026-10-15.md) closes the preceding broad-suite failure without replacing audio, changing rootMIDI, widening fixture bounds or changing DSP/policy/criteria. It repairs window-origin membership, schema-3 marker lookup, first-unit-only checking and optional-fixture skipping; required shipped-bank removal is detected. Shared source/protocol/range/confidence validation is reused. Five assertions fail in each eight-method baseline and the repaired eight pass; final12 methods pass. Broader quality CTest now passes189 methods/19 skips (170 executed) in2.453s; the12 repeat its subset. Both unchanged spoken demo packages now use the same loop, median681.868Hz/+958.388c from39 centre-selected estimates; origin selection reproduces383.357Hz/-38.580c. These are sampling/estimator diagnostics, not physicalF0, production suitability or a defense of the historicalv1 renderer conclusion. Nine shipped units and two optional local tone rows are recorded, with repeated-audio/mocked-harness scope disclosed. Read-only audit binds49 artifacts/14 inputs and recomputes held-frame statistics, plus rehashes71 prior artifacts; historical test-source checks use Git after the intentional repair, and frozen roots remain intact. This test/documentation-only unit has no native rebuild. Canonical/internet source bytes and .github/README Windows TODO/stash are preserved. Confirmed live ccbb39d6 review remains; subsequent units have no independent verdict. Real rights/actors/coverage/failed-QC/listening and full acoustic/classical/neural/resources/targets/DAW/release/tolerance/integration remain. Alpha0of20/BetaBLOCKED/U45hold/full goalACTIVE are unchanged.


### 2026-10-15 — pinned CLAP host admission follow-up, on d9860c7e

The [follow-up dossier](CLAP_HOST_REVIEW_FOLLOWUP_2026-10-15.md) records the exact independent APPROVE of ccbb39d6 (no required changes, three P3 follow-ups), without extending it to later units. The prior warmed-instance negative-control wording is corrected. Parser admission now refuses duplicate/empty/missing/flag-like values; caller-pinned admission enforces the 128-beat FollowHost bound and unmuted notes; capture audio/summary destinations must be new and distinct. Existing project-alias refusal takes precedence. Three corrected baseline methods fail with15 failures, then pass. The initial too-short muted fixture is retained and disclosed. A direct private destination comparison shows baseline49 bytes replaced by672044 WAV bytes, repaired49 bytes unchanged/no summary. Full untargeted Release build passes25.673s; four actual CTest entries pass31.097s:8 Python methods,7 song and34 installation native cases, one player smoke. The separately repeated three methods/child native fixture invocations overlap; no editor GUI smoke was selected despite its nonexistent regex name. Admission-time output protection is not exclusive creation/concurrent-filesystem protection or a complete resource-closure rule. Read-only audit binds55 artifacts/eight inputs and75 prior artifact hashes; exact build-source observations are not authenticated lineage. Earlier frozen roots remain unchanged. Fixture cleanup and temporary-path-independent historical audit remain pending. fe340737 review is live; this unit/publication/pitch changes remain unreviewed. No internet sample/import or human/released-bank acceptance is added. Canonical/internet originals/.github/README Windows TODO/stash are preserved. Real actors/rights/coverage/QC/listening, classical/neural/R9 resources, acoustic/tolerance ownership, semantic integration and user/DAW/platform/release acceptance remain. Alpha0of20/BetaBLOCKED/U45hold/full goalACTIVE remain.


### 2026-10-15 — installed-song review and source-bound execution supplement, on 770dd923

The [review supplement](INSTALLED_CLAP_SONG_REVIEW_2026-10-15.md) retains exact fe340737 APPROVE, with P2 historical source/build lineage and P3 native regression findings. Historical logs do not bind the committed runtime source written after that build/CTest; the frozen root remains unchanged and the dated dossier/index now disclose this. A new native case prepares the same complete local tone-backed project unstamped and provenance-stamped. Controlled former encoding behavior fails only the stamped preparation; fixed source is restored byte-for-byte. The initial wrapper searched stdout for an error on stderr, but its expected native failure/restoration were already complete; original script/streams/receipts and corrected validator are retained. Full untargeted Release rebuild passes22.636s and compiles the exact runtime bytes matching fe340737. Two focused entries pass13 native cases/eight Python methods; final10.768s repeat explicitly hashes relevant sources/child binaries before/after. Repetitions and child native invocations are not additive; one native case is new. A fresh direct real-plugin song run passes0.567s/96 note windows/3845175 combined-rate frames. Retained48k stereo finite PCM has2004000frames/16032044bytes and is byte-identical to the original song WAV. This is later evidence, not authenticated historical/hermetic lineage, fresh signature verification or self-contained resource replay. Read-only audit binds39 artifacts/eight inputs. The new test/supplement await review; exact c27d1d6f review is dispatched separately. Fixture cleanup/durable retained-copy audit/exclusive outputs/request-timeline semantic integration, real rights/actors/coverage/QC/listening, full classical/neural/R9/acoustic criteria and user/DAW/platform/release remain. No sample download/import or human acceptance is added. Canonical gates/original audio/.github/README Windows TODO/stash/prior frozen roots are preserved. Alpha0of20/U45hold/BetaBLOCKED/full goalACTIVE remain.


### 2026-10-15 — owned CLAP host-test fixtures, on a19a8dc9

The [fixture dossier](CLAP_FIXTURE_OWNERSHIP_2026-10-15.md) closes the earlier review's temporary-fixture leak and stdout deletion-scope follow-up. Each native fixture gets a launcher-owned private root, injected TMPDIR/TMP/TEMP, pre-registered cleanup, canonical project/name/link admission and recipe containment. Song producer/package deletion stays inside the validated fixture; optional external artifact-output overrides are suppressed only for this disposable launcher. Three mocked ownership regressions produce five parent failures and pass on repair. Actual parent eight-method suite passes but leaves five directories/51files/83912602bytes in its newly created observation root. Intermediate repair and final registered CTest leave zero. Final CTest passes10.210s/11 Python methods (eight existing, three mocked); repeated/child native invocations are not additive. Python-only unit has no native rebuild; four unchanged binaries are retained and rehashed before/after. Read-only audit binds38 artifacts/eight inputs; intermediate source and hash-identical reconstructed initial wrapper are disclosed. Only newly owned disposable roots are cleaned; historical temp dependencies and frozen evidence remain. Durable retained-copy historical audit and exclusive output/semantic integration remain. c27d1d6f review is confirmed live; this/later admission/provenance/pitch units remain unreviewed. No source download/import, rights, human, listening, device, DAW or Windows acceptance is added. Real actors/permissions/coverage/QC, full classical/neural/R9/acoustic criteria and user/DAW/platform/release still remain. Canonical/original/.github/README Windows TODO/stash preservation and Alpha0of20/U45hold/BetaBLOCKED/full goalACTIVE remain.


### 2026-10-15 — independent publication-admission review, on e3ee6f00

The [review dossier](PUBLICATION_ADMISSION_REVIEW_2026-10-15.md) records exact c27d1d6f APPROVE (engineering only, no required changes, two P3 findings), its full17040-byte verdict and read-only three-artifact/eight-input/three-Git-source audit. The peer independently confirms physical publication/revocation serialization, safe observed lock order, callbacks outside admission and lock-free audio reads. Retained source/build/test ordering is consistent; no new build/native/test execution or authenticated lineage is asserted by this metadata unit. Four late controls pin the currency recheck; a held-reset exclusion regression remains. Counters/outcome reports still run after physical-publication lock release, so cancel/replacement can see older published fields; atomic outcome-report commitment remains pending, while playback rejects revoked identities. No P3 is marked fixed. Original42-artifact publication root/canonical files remain frozen. c111cbac review is dispatched only after this terminal verdict; later bank diagnostic/admission/provenance-supplement/fixture-ownership units remain unreviewed. Internet originals remain unqualified; genuine roles/rights/coverage/QC/listening, classical/neural/R9 resources, acoustic criteria/tolerance and user/DAW/platform/release still remain. .github/README Windows TODO/originals/stash preservation, Alpha0of20/U45hold/BetaBLOCKED/full goalACTIVE remain.


### 2026-10-15 — locally verified publication outcomes, reconciliation hold retained

[Publication outcome admission](PUBLICATION_OUTCOME_ADMISSION_2026-10-15.md) addresses the two c27 review P3s in a separate unit: terminal counters/reports share physical-publication admission, and a held-reset mutation distinguishes mutex exclusion from the final currency recheck. Six new cases, full Release build, eight focused entries (253 native cases/11 Python methods), nine runtime cases and Debug TSan48 pass. Initial child-binding gap is disclosed; a repeated final focused run binds actual editor host/plugin/native entries, without additive counts. The69-artifact/nine-input read-only audit passes. Current follow-up review is pending; exact c111cbac review has become APPROVE with three P3s, separate from this unit. No acoustic criterion, canonical tolerance, source/human/listening/DAW/Windows/resource acceptance is inferred. U45 remains held; Alpha0of20/BetaBLOCKED/full goalACTIVE and canonical/original/evidence/.github/README Windows TODO/stash preservation remain.


### 2026-10-15 — note-pitch WAV binding, reconciliation hold retained

[WAV-bound scoring](NOTE_PITCH_WAV_BINDING_2026-10-15.md) records exact c111 engineering APPROVE and locally addresses its three P3s. New metadata/container/offset controls pass39 methods; repaired dependent mock fixtures restore the broader199 methods/19 skips. Actual stereo and native mono routes pass48 notes/2530 selected frames each. Source-hash historical negative input loss is disclosed; fresh negative inputs are held separately from receipts. The108-artifact/12-input read-only audit preserves old roots and uses immutable historical Git. Follow-up review pending; no physical F0, criteria/tolerance, source/actors/listening/DAW/platform/resource acceptance is inferred. U45 held, Alpha0of20/BetaBLOCKED/full goalACTIVE remain.


### 2026-10-15 — exact publication-outcome review recorded

Independent peer review APPROVES `06803bb56de422626b731b921113d9183ba4b3e6` for engineering only, closing both c27 P3s. One P3 documentation finding is addressed by disclosing the first focused CTest/TSan-build overlap and nested owner-runtime filter; separate TSan and bound focused receipts remain stable, counts are not additive. Full verdict, immutable source bindings, eight pins and receipt overlap checks are retained under `out/evidence/publication-outcome-review-2026-10-15`. No new native build/test; no later-unit, source, human or Beta approval. Exact a30f7969 review is dispatched and pending. Alpha 0/20, U45 held, Beta BLOCKED; full goal ACTIVE.


### 2026-10-15 — additional female English phonetic donor source

The [CMU ARCTIC SLT intake](FEMALE_ARCTIC_SLT_INTAKE_2026-10-15.md) downloads 1,132 additional unchanged female speech WAVs (3,395.5589375 seconds, mono PCM16/16 kHz), with matching transcripts and automatic phone labels and the voice-specific commercial/copy/modify grant. Actual native reader checks pass 1,132/1,132 with no reported clipping; three illustrative Direct-v3 analyses are bound, not full pitch qualification. The final read-only audit preserves the earlier 297 original WAVs: combined census 1,429 distinct originals. Forty-one raw symbols/1,327 adjacent pair types are automatic annotations, not qualified coverage: `jh` needs an explicit proposed `j` alias, `axr` and 51 stress forms are absent. No rights flag, actor, bank assignment/import, human listening or AE1/R9 acceptance is supplied. Independent intake review pending; Alpha 0/20, U45 held, Beta BLOCKED, full goal ACTIVE.


### 2026-10-15 — unassigned English phone donor preparation

The [phone donor unit](FEMALE_PHONE_DONOR_PREPARATION_2026-10-15.md) adds hash-bound automatic-label interval extraction with exact PCM preservation and unreviewed annotations. Registered CTest passes 31 methods (16 new/15 prior) after full Release build/current-target check. Four SLT originals yield 312 byte-deterministic proposals (159 single/153 adjacent pairs), all 312 native-reader checks pass; 36.9125 summed seconds represent 12.825 unique seconds, not new recordings or bank coverage. Raw `jh`, missing `axr`/stress, speech suitability and human/rights/actor holds remain. The next production gap is explicit English workspace inventory generation: native/Python paths still reject non-Japanese profiles. Independent implementation review pending; separate source-intake review active; exact a30f7969 APPROVE captured for separate integration. Alpha 0/20, U45 held, Beta BLOCKED, full goal ACTIVE.
