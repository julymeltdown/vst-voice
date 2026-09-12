# SEAM Development Direction Review

## 1. Technical summary: retain the architecture, refocus on a real singer and a complete creator journey

**Development is conditionally on the right track. There is no evidence-based reason to discard the architecture or restart the project. However, describing the current product as “95% of Beta GO, just finishing touches left” would be inaccurate.** Editing, persistence, generation, review, installation, and rendering are becoming genuinely connected. General-lyric pronunciation, female singer identity and quality, advanced expression, neural singing, host coverage, and independent product acceptance remain substantial obligations.

The review reaches four separate decisions:

- **Architecture: retain.** Separating score, pronunciation, and performance intent from rendering, while explicitly managing immutable takes, ownership, versions, and reviews, fits the approved product.
- **Execution priorities: refocus.** The next increments should complete real singer-to-song capabilities, rather than primarily adding contracts, state fields, or increasingly narrow fixture tests.
- **Integration: not accepted.** The fresh full Release build fails in a newly added Studio test. A separately rebuilt Phase12B regression also fails.
- **Full-Scope Beta GO: NO-GO.** This is a release decision, not an instruction to stop development. Useful internal implementation remains available without changing the goal.

The agreed target is not merely a sampler or vowel synthesizer. It includes **creating an original female voice without a supplied recording; editing recorded or generated material into a bank; and finishing unfamiliar Japanese, English, and Korean songs through the required classical/neural, expression, and host workflows.** The recommendations below preserve that full scope.

## 2. Definitions: test pass rates are not product completion

This review examines the current local working tree on September 9, 2026. The approved plan requires all R1–R20 requirements, V01–V18 obligations, and 48 implementation units. A pilot or successful classical path is an engineering checkpoint, not an alternative Beta GO definition. [Approved completion boundary](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md:23)

Evidence is separated into four levels:

1. **Source implementation:** a real implementation and consumer path exist. This alone does not establish compilation, runtime behavior, or sound quality.
2. **Current engineering execution:** freshly built targets and tests establish particular behavior under explicitly bounded conditions.
3. **Product qualification:** actual resources, pronunciation, range, styles, listening quality, and creator tasks satisfy fixed criteria.
4. **Release qualification:** the exact signed-installed candidate passes the required platforms, hosts, and restored audit.

The execution ledger records local acceptance of U1–U5: **5/48 = 10.4%**. This is a historical unit-acceptance ratio, not fresh reacceptance of those units, code-volume completion, or an estimate of remaining time. Considerable implementation exists beyond U5, so interpreting the other 89.6% as “nothing has been implemented” would also be wrong. [Ledger’s explicit status](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/implementation/FULL_SCOPE_BETA_EXECUTION.md:544)

This report does not invent a replacement overall product percentage. Without accepted acoustic, resource, and platform evidence, assigning 60% or 80% would imply precision that the evidence cannot support. Future reporting should separately track **accepted units, complete user journeys, qualified resources, and current regression results**.

## 3. Fresh verification: important paths work, but integration is not complete

### 3.1 The full Release build fails

`cmake --build build/release -j 4` failed, and the failure was reproduced. The latest `test_studio_manifest_draft.cpp` calls the private controller method `selectedAudioPath()` at three locations. Both the core test target and the dedicated Studio draft target compile this file, so both are affected. [Failing call](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_studio_manifest_draft.cpp:293), [private declaration](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/include/seam/native_ui/voicebank_studio.hpp:324)

This is a confirmed test-integration error, not proof that the entire synthesis engine is broken. Conversely, successfully building some application targets does not make the full build green. The test can derive the path from the public manifest path and selected unit’s relative audio path; production internals do not need to become public for test convenience.

**The latest 16 Studio draft tests cannot be reported as passing.** Earlier success of 10 cases, or completion of six additional test implementations, is not execution evidence for the current 16-case target.

### 3.2 Separately rebuilt focused verification passes 23 of 25 CTest entries

After the full build failed, explicitly selected application, plugin, and focused test targets that do not require the broken test file were rebuilt successfully. The selected serial CTest run returned **23 PASS / 2 FAIL in 28.18 seconds**. There are 120 registered CTest entries, but **this was not a current 120-entry full-suite run**.

The two failures were:

- `seam_phase12b_tests`: a direct rerun also exits with code 40. The test stops while preparing its positive edit path, so the later Final lifecycle assertions are not established by this run. The cause is examined in section 10.
- `seam_tracked_source_closure`: 335 required inputs were not indexed by Git at execution time. This includes policy-covered documents as well as source. It means the current Git checkpoint does not close over the local result—not that these files have disappeared.

Passing focused targets include 38 producer cases, 11 manifest-draft cases, five WAV-limit cases, nine Studio sample-review cases, three CLI end-to-end cases, and language, performance-compiler, offline-session, and actual CLAP-host checks. Cases can overlap between targets; their sum is not a count of unique features.

A separate Python run passed **33 tests in 9.367 seconds**, covering production-draft parity, source admission, the full-product gate, and the public-release state machine. This is selected gate/production regression evidence, not acceptance of a real release candidate.

### 3.3 Actual plugin processing and linked-engine smoke are different evidence

The current matrix really loads the CLAP binary and calls its processing path: **336/336 cases passed**. Its result explicitly says `engineering`, `development-fixture`, and `releaseEligible:false`. It does not establish calibrated pitch accuracy or completion of a user’s song.

The five-second soak smoke instead reports **linked-engine-v1**. Recording the plugin’s hash does not change which implementation executed. It must not be described as a long-running real-DAW plugin qualification. [Runner wiring](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/phase12c/CMakeLists.txt:1)

Because the full build failed, an older `seam_tests` binary was not run and relabeled as current core verification. Earlier whole-suite counts remain historical evidence and are not merged with this run.

The HTML report's coverage chart accounts for all 120 registered CTest entries: 23 passed, one runtime regression failed, one source-closure check failed, and 95 were not executed in this review. It shows the limits of current verification—not 95 defective tests, a complete test run, or a product-completion percentage.

## 4. Architectural strengths: safeguards are increasingly attached to real workflows

### 4.1 The score owns musical intent; renderers consume it

Pronunciation identity, phoneme timing, pitch/dynamics, articulation, and manual ownership are represented above the individual sample renderer. Sample and Procedural resources consume these shared inputs. This reduces the risk that accidental sample selection determines the meaning of the melody. Neural rendering should join the same architecture rather than introduce an unrelated neural-only score model. [Current rendering branches and input consumption](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-rendering/src/render_pipeline.cpp:143)

### 4.2 Separating source, derived take, reviewed material, and installed resource is necessary

The implementation distinguishes source bindings and revision chains so that identical WAV bytes do not imply identical ownership or permission. Explicit review, stale-generation rejection, create-new publication, and exact content identity are important for a voicebank-production tool. Removing these boundaries would let regenerated audio silently inherit old approvals or alter existing songs.

The current end-to-end test calls the real `create-sample-draft` command. It no longer relies on the test directly fabricating a ready manifest and locked pitch marks. The resulting draft starts with estimated, unlocked annotations. After review, candidate publication, packaging, and installation, the test hides producer inputs and reopens a new score to produce Final audio and master/stem WAV files. [Real draft creation](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_sample_review_cli.cpp:46), [Installation and new-score export](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_sample_review_cli.cpp:140)

**It is therefore no longer accurate to say that generation/editing into a usable bank has made no progress.** But this fixture uses a short sine wave, preconfigured source qualification, a test signing key, and a two-note “あ” score. That is engineering connectivity, not qualification of an original female singer or unfamiliar general lyrics.

### 4.3 Recent recovery and integrity repairs should be retained

Parent/aborted-journal ancestry now distinguishes a legitimately interrupted generation from missing committed history. This permits normal recovery without arbitrarily excusing lost provenance. The current producer regression passes. [Recovery ancestry recording](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voicebank-production/src/repository.cpp:240)

Draft creation verifies actual audio bytes and uses bounded decoding. Studio now hashes the same bytes that it decodes; resize only rearranges pinned analysis geometry. These changes are confirmed in source. The newest Studio draft regressions remain unexecuted because of the compile failure described above. [Current loading order](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voicebank_studio.cpp:338), [Pinned resize](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voicebank_studio.cpp:455)

## 5. First user-safety priority: closing Studio does not protect unsaved sample edits

**P1: a source-confirmed path can lose editing work.** Native Studio’s `requestClose()` calls only `allowDesignerReplacement()`. That function permits closing when the Designer is absent or clean. Sample marker/pitch edits set the separate controller’s `dirty_` flag, but that state is not included in the close decision. [Close handling](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/apps/seam-voicebank-studio-native/main.cpp:1181), [Designer-only check](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/apps/seam-voicebank-studio-native/main.cpp:82), [Sample dirty state](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voicebank_studio.cpp:462)

The actual AppKit window delegate calls this method. The controller destructor waits for workers but does not save a dirty manifest. Consequently, **open draft → edit sample marker/pitch → close the window without saving** has no sample-edit protection along this path. No destructive experiment was performed on user files. Recording shutdown has a separate WAV-saving path; this finding must not be expanded into “all recordings are lost.” [Actual close delegate](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/native_window_appkit.mm:732), [Controller destruction](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voicebank_studio_production_project.cpp:268)

The repair should cover the decision lifecycle, not merely add a warning:

1. Handle Designer and sample-manifest dirty/busy states in one close policy.
2. Provide Save / Discard / Cancel; do not close after save failure, during saving, or after a modal-time revision change.
3. Use the same policy for normal window close and application/menu quit.
4. Verify actual marker/pitch values after saving and restarting.

This deserves priority over additional DSP or cosmetic expansion. A tool that loses a creator’s edits cannot be trusted even if its sound improves.

## 6. Production gaps: source reassessment, style ownership, and applicable QC

### 6.1 Executable source and qualified source are correctly separated, but the qualification workflow is incomplete

Execution policy checks source-use/transformation permissions and evidence. Candidate qualification additionally checks redistribution/commercial-render permissions and coverage/listening outcomes. Generation and unit review do not automatically approve those conditions, which is correct. [Execution policy](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voicebank-production/src/project.cpp:14), [Candidate qualification](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voicebank-production/src/project.cpp:59)

However, the current init/create/prepare/inspect/review/publish commands do not complete the user workflow for recording actual source assessments and reassessments. The positive fixture starts with coverage/listening already set to PASS. The demonstrated journey therefore has an important precondition: **source qualification has been supplied in advance**. [Current command set](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/apps/seam-voicebank-cli/sample_review_commands.cpp:202), [Fixture preconditions](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_sample_review_cli.cpp:31)

The next implementation needs explicit assessment commands and repository transactions recording the source revision, actor, evidence, applicability, and outcome. Immutable attribution and current policy changes must remain distinct. Relevant reviews/candidates must invalidate when their source conditions change. Hand-editing JSON or weakening the gate is not a solution. Actual permissions require appropriate evidence and review; test booleans are not a substitute for that determination.

### 6.2 A single-style draft does not complete multi-style bank production

`UnitAssignment` is organized around coverage key and pitch layer without language/style identity. Candidate publication explicitly rejects multiple styles. The approved target includes language coverage, reviewed range, distinct styles, and paired blending. [Assignment model](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voicebank-production/include/seam/voicebank_production/project.hpp:117), [Multi-style rejection](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voicebank-production/src/repository_candidate.cpp:474)

U10 must coordinate stable language/style/phone/pitch identities, migration, generation mapping, review ownership, and candidate mapping. Including style in a draft unit ID does not complete producer-schema migration. Paired blending requires compatible pronunciation, timing, alignment, and audible results—not merely two units with similar names.

### 6.3 Silence, closure, breath, and voiced sustain need different QC applicability

Candidate publication requires finite mono audio and RMS above a threshold. This common check alone cannot express the different purposes of pause, closure, breath, and voiced sustain. Candidate publication also still has a call to the default WAV decoder: adding a bounded API does not mean every intake consumer now applies the same limits. [Candidate audio checks](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voicebank-production/src/repository_candidate.cpp:532)

U12 needs unit-kind-specific pitch applicability, intentional silence/closure handling, voicing, noise/clipping, boundary, and duration checks. The answer is not to waive unsupported cases. It is to **make each check applicable to the material’s actual purpose**.

## 7. Voice Designer: real timbre controls, incomplete general singing articulation

**The Designer is not merely decorative UI.** Phonation open quotient, tilt, aspiration, jitter/shimmer, formants, and frication are edited, saved in recipes, auditioned, and passed into generation inputs. The request to sculpt a voice like a synthesizer is being implemented in the right form. [Actual controls](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/apps/seam-voicebank-studio-native/main.cpp:151), [Generation handoff](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/apps/seam-voicebank-studio-native/main.cpp:186)

The current articulation model, however, has **two gesture kinds: OralVowel and Frication**. Non-vowels are limited to explicitly bound unvoiced onsets; voiced consonants and codas are rejected. Nonzero nasal coupling is unsupported. [Gesture definition](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voice-design/include/seam/voice_design/articulation_plan.hpp:8), [Allowed articulation](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voice-design/src/articulation_plan.cpp:79), [Nasal limitation](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-voice-design/src/vocal_tract.cpp:16)

English “sing” illustrates the boundary: correctly classifying `ng` as a coda in the phonemizer does not make the procedural engine capable of producing that nasal/coda. **Linguistic correctness and acoustic implementation are separate acceptance conditions.**

The next step is not to register every consonant as a noise pose. Closure/burst/release, voiced consonants, nasals/liquids, codas, transitions, and phonation continuity need actual gesture/tract/source models, verified in words and phrases. Raising formants or changing base pitch is also insufficient evidence for a consistent original female singer identity.

Current audition centers on a sustained selected pose/pitch. General-lyric phrase audition and the sound after baking into a bank need to become comparable parts of the same workflow. [Current audition unit](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voice_designer_audition.cpp:39)

The meaningful next output is not another set of sliders. It is **a pilot character whose identity remains coherent across vowels, consonants, and range, and whose unfamiliar short lyrics are intelligible**. This pilot is a diagnostic milestone, not a reduced-scope release.

## 8. Classical rendering and expression: basic controls exist; advanced and composed capabilities remain open

The capability table enables pitch, timing, dynamics, vibrato, attack, and release. Formant, breathiness, tension, airiness, gender, style blend, and growl have names but are not enabled as supported controls. [Current capabilities](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-synthesis/src/renderer_capabilities.cpp:9)

**Editing a recipe’s static formants is different from editing a formant expression lane in a song.** Proposal and ownership storage do not establish complete expression DSP. Conversely, existing vibrato UI → command → compiler → frequency → sound-consumer paths mean it would be wrong to call every expression decorative.

Each mandatory expression needs an explicit supporting backend/resource, neutral value, intended acoustic effect, and pitch/timing side-effect boundary. Implementation should close **save → reopen → Final PCM change → undo → cache identity**. Every renderer need not support every control, but a required control cannot remain unsupported everywhere.

Classical quality also needs more than a single vowel or sine fixture. Separate voiced/unvoiced onset, sustain, release, and joins; evaluate real CV/VC material, fast pronunciation, leaps, and long sustains. This review did not newly measure CV/VC cents error or listening scores, so it does not invent numerical acoustic failures.

Harmony exposes another cross-layer gap. `AddHarmonyCommand` inserts simultaneous notes into the same region, while procedural snapshot construction calls a single-voice compiler that rejects overlapping notes without explicit allocation. **Having a harmony command is not the same as audible procedural harmony.** Voice separation, rendering, and mixing need an end-to-end regression that preserves shared source/ownership semantics. This is a source-path finding, not a new harmony-PCM experiment. [Harmony insertion](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-application/src/harmony_commands.cpp:99), [Procedural compilation](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-rendering/src/render_snapshot.cpp:451), [Overlap rejection](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-synthesis/src/performance_compiler.cpp:285)

## 9. Languages and neural synthesis: interface registration is not singer capability

### 9.1 Three language paths exist; general-lyric quality remains separate

English syllable boundaries, onset clusters, terminal codas, and downstream timing regressions passed in the current language target. Korean has Hangul decomposition and boundary rules; the shared resolver has identity and input bounds. These are meaningful foundations. [English role/timing regressions](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_english_phonemizer.cpp:227)

English still uses a small bootstrap dictionary and spelling-based estimates for unknown words. Korean boundary rules are not a complete morphology/lexical-exception engine. The basic Japanese kana path does not resolve arbitrary kanji lyrics. A separate reading helper/resource path does not automatically complete installed resource selection and every UI consumer. [English dictionary](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-phonemizer/src/english_phonemizer.cpp:25), [Korean rule boundary](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-phonemizer/src/korean_phonemizer.cpp:94)

Measure unknown-lyric rates, incorrect readings/stress/codas, manual correction effort, and audible results per language. Explicit mixed-language regions currently reject and should be documented accurately. This does not silently add universal code-switching as a new acceptance obligation: completing the three approved languages and promising every mixed-language combination are different scopes.

### 9.2 Neural needs actual model inputs and production dispatch

Helper execution, bounded framing, request/response identity, deadlines, and cancellation exist. But `NeuralRequest` contains pronunciation hash, F0, and dynamics—not the actual phoneme sequence, durations, language, or speaker/style conditioning. **A pronunciation hash cannot tell a model which lyrics to sing.** [Current request contract](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-neural-synthesis/include/seam/neural_synthesis/worker_protocol.hpp:23)

The normal `PhraseRenderPipeline` rejects resources other than Sample/Procedural. The test helper is a probe, not a qualified trained singer. IPC success is therefore not neural singing completion. [Actual pipeline dispatch](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-rendering/src/render_pipeline.cpp:155), [Probe output](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/helpers/neural_worker_probe.cpp:15)

U35 data/permissions/alignment/splits/reproducible training, U36 model/vocoder/quality qualification, and U37 actual inference/conditioning/installed helper/Final/cache integration are distinct obligations. They can advance in parallel, but the protocol should be driven by real model input/output needs. Equally, the lack of a qualified model is not a reason to stop every internal integration task.

## 10. Hosts: Fixed Audio has improved; Follow Host remains unimplemented

The actual loaded-plugin cold-score-bounce test passed. Without live note events, it checks eight score-note windows at two sample rates, rejects missing/failed Final and beats-only paths, and verifies the output WAV format. This is stronger host-boundary evidence than a linked-runtime substitute. [Actual host test](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_clap_offline_host.py:23)

`prepareOfflineRender()` currently **explicitly rejects FollowHost as Unsupported**. This is safer than silently exporting incorrectly timed audio, but it is not implementation of Follow Host. [Explicit rejection](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-clap-editor/src/editor_runtime_project.cpp:181)

The separate Phase12B regression reproducibly exits 40 while preparing the new positive edit sequence after its partial-Final negative case. An independent reviewer traced the existing Release binary twice: nucleus lookup and boundary movement succeeded; the second `selectUnitVariant()` returned **“Unit plan entry is unavailable for this phoneme.”** [Test condition](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tests/test_phase12b.cpp:155), [Returned failure](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-authoring-runtime/src/technical_edit_controller.cpp:482)

The fixture tries selection after undo before a complete replacement unit plan is asynchronously published. `currentTechnicalRenderView()` can supply the retained partial Ready publication’s active plan. The repair should use an explicit readiness condition, not an arbitrary sleep: **undo → wait for a current complete plan → select unit/renderer → apply the supported boundary edit**. This establishes a readiness/fixture problem; it does not establish that an incorrect unit was applied or user data corrupted. [Technical-view consumption](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-authoring-runtime/src/authoring_runtime.cpp:529)

The failed regression and successful cold bounce can coexist. Neither “all CLAP is broken” nor “the entire Final lifecycle passes” accurately describes the evidence.

Next implementation needs one contract covering authoritative transport, host/project offset, tempo-map revision, range invalidation, Pending/Failed output, offline preparation, and error propagation. Instantaneous BPM cannot reconstruct earlier beat positions across tempo changes. Actual host differences must be tested.

The current results do not replace signed-installed Windows/macOS qualification across all nine required DAW tuples, VST3/AUv2 delivery, or long-running stability. In particular, five seconds of linked-engine smoke must not be promoted to long host qualification.

## 11. Native UX and character: preserve work, readability, and responsiveness before adding surfaces

### 11.1 Overlap and overflow require interaction-level acceptance

Overlapping notes are not automatically a bug: polyphonic scores can legitimately overlap. Users must still know which note they are selecting and editing without distorting musical time. Short-note hit targets, selection cycling, lyric inspection, drag/resize, and undo must be tested together. Historical complaints are not proof that every old visual defect remains unchanged.

Text overflow should not be solved by shrinking everything into one line. Preserve compact editing density, but make important actions and decisions readable. Long hashes, paths, and diagnostics need detail views, wrapping/scrolling, copying, and keyboard navigation so their full content remains available.

The new Review screen currently passes font-size values of 6.0 for buttons/instructions and 7.0 for data/status. This is a readability risk for consequential decisions. These are confirmed code values, not measurements of physical pixels or proof of failure at every DPI. This review did not newly qualify the complete latest UI with screenshots. [Current typography](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voicebank_studio_production_view.cpp:118)

### 11.2 Resize improved, but ordinary unit selection is synchronous

Review navigation uses an asynchronous path, but the ordinary rail/Up/Down path calls `selectUnit()` and performs file reading, hashing, decoding, and spectrogram construction synchronously. A maximum input size does not guarantee a responsive UI. Actual freeze duration was not measured here; this is a source-confirmed responsiveness risk. [Synchronous selection](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voicebank_studio.cpp:207), [Heavy processing](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voicebank_studio.cpp:354)

Unify ordinary Editor selection with the same latest-request, stale-safe worker model. Measure selection latency, cancellation, and window closing using large permitted WAVs. FFT-internal cancellation and worker-shutdown latency also deserve explicit measurement.

### 11.3 Character state integration exists; singing-performance integration is incomplete

State assets, bank/card identity checks, and rendering/warning presentation exist. “No asset integration code exists” is no longer accurate. A character bound to a particular bank not appearing for an arbitrary newly created bank is not automatically a rendering bug. [Identity integration](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/libs/seam-native-ui/src/voice_identity.cpp:6), [Bank binding](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/assets/character-01/manifest.json:3)

Remaining R14 work includes the actual installed singer/asset binding, mouth/performance states derived from playhead and phoneme timing, reduced motion, collapse/off, and independence from audio behavior. The priority is not more illustrations: it is making existing assets accurately communicate **who is singing, with which voice, and what is happening now**.

## 12. Release gates: strong fail-closed foundations, incomplete evidence semantics

The current canonical contract contains **20 requirements, 83 cases, and nine host tuples**. Its resource matrix and evaluation profile are `UNRESOLVED`; there are **zero released resources** and **18 FIXED / 11 UNRESOLVED criteria**. These are current contract fields, not a count of all development audio files. [Canonical contract](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/docs/product/full-product-beta-contract.json:49)

Multiple units in the development bank reference the same short WAV, and its README explicitly says it is not a release-quality singer. Phone labels or structural coverage therefore cannot establish that those phonemes were actually recorded/generated correctly. [Fixture purpose](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/assets/demo-human-voicebank-public-domain/production-bank/README.md:3)

The existing narrow operation-evidence probe was rerun against current code. Using README as operation input/output/raw evidence produces no errors from `_observation_errors()`. That code checks operation ID coverage and reference paths/hashes, but does not establish through typed replay that the file is the actual result of the claimed operation. [Operation checks](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/external_beta/full_product_report.py:376)

**This is not proof of a full-report or Beta GO bypass.** The full validator separately requires schema, matrix, criteria, reviews, and other conditions. The finding is a lower-level gap in whether evidence actually demonstrates the claimed operation.

U45 needs operation-specific input/output formats, source/candidate/resource bindings, and execution/replay or measurement semantics. A hash establishes that a file has not changed; it does not establish that its contents support the claim. Acoustic and creator evidence must not reduce to a PASS string or a copied JSON assertion.

Reporting also needs correction. The ledger’s top entry still says Studio/CLI publication and package/install integration are open, despite subsequent implementation. Conversely, the contract’s semantic-validator `UNAVAILABLE` label and a missing-report-reference diagnostic lag behind the actual validator call. **Unimplemented, unsubmitted, unverified, and failed must remain distinct states.** [Current gate call](/Users/lhs/Downloads/project-seam-usable-alpha-u3-master/tools/external_beta/release_gate.py:159)

## 13. Execution economics: reduce integration batch size and verification churn

Before adding this review’s artifacts, the working tree contained 215 changed tracked files and 336 untracked files. The tracked diff alone had 23,809 insertions and 1,250 deletions; untracked line counts are excluded. This is not evidence that the work is useless. It is a large batch to review and reproduce, making small changes more likely to trigger repeated broad verification.

The private-API test call and immediately failing replacement positive fixture demonstrate why **independent implementation completion and integration completion must be separate checkpoints**. A “stable checkpoint” message is not a substitute for an integration build.

Each implementation batch should identify the user capability, file ownership, changed identity/schema, direct positive/negative checks, and acceptance boundary. Two or three purposeful lanes with non-overlapping ownership can be sufficient. This is a recommendation for the current dirty tree, not a measured universal productivity rule.

Do not stage everything merely to make source closure green. At a publication checkpoint, inspect the necessary code/tests/docs and distinguish them from private recordings, recipes, rights evidence, and generated build output. This review did not stage, commit, or push anything.

Following the owner's concern about possible session-saving loss, a local recovery snapshot captured 1,905 tracked/non-ignored worktree files, with per-file identities, a source archive, and worktree/index patches. The capture verified that files stayed unchanged and that the archive could be read. Nothing was uploaded. This protects the current baseline; it does **not** prove or disprove loss of earlier uncommitted work. Previously reported changes must be checked against actual files and executable tests, not assumed present from conversation history. A historical loss determination requires an earlier filesystem/archive or exact patch to compare.

## 14. The next big step should complete a singer-to-song journey

### Step A — Close integration failures and work-loss risks

Address the private-API test calls, Phase12B exit 40, and sample dirty-close guard. Ordinary Editor asynchronous selection can join the same user-safety batch. Acceptance requires affected targets and the full Release build to succeed, a fresh whole-suite result, and actual edit preservation across save failure, cancellation, and restart. Do not remove tests or tolerate incomplete Final audio to obtain green results.

### Step B — Produce an editable real-source singer pilot

Connect assessment/reassessment commands, exact origin/take ownership, generation or recording/import, applicable unit-kind QC, manual marker/pitch editing, and explicit review. A user should reach candidate publication without hand-editing files. Start with a small diagnostic corpus to expose problems quickly; do not rename it a reduced Beta.

The evidence chain is **actual source → draft → edit → save/restart → independent review → package → install → unfamiliar-lyric Final without producer paths**. Keep the sine end-to-end test as a fast engineering regression, then add actual phonetic material.

### Step C — Let observed pronunciation and sound failures drive DSP work

Compare nasals, voiced consonants, stops, codas, transitions, voiced transients, joins, range, and distinct styles on the same short songs. Implement the necessary categories, then choose the next priority from actual failing phones and spans. Evaluate original female identity and intelligibility on unfamiliar phrases.

Language/style assignment migration and paired alignment should represent this real production material. Do not postpone identity modeling until the end and merely label an existing collection afterward.

### Step D — Advance neural and required expression against actual models

Build the training-data/alignment/split/reproduction path, a real model/vocoder prototype, conditioning protocol, first-party native helper, and shared Final/cache/ownership integration. Start actual neural input/output work early rather than hiding this mandatory scope behind mock success. Required timbral expressions also need complete audible paths on their supporting backends.

### Step E — Complete creator and host experiences using real singers and songs

Exercise JA/EN/KO lyrics, note/phoneme/expression edits, interchange, Follow Host tempo changes, bounce error propagation, readable review, work preservation, and character performance in real creator tasks. Development may begin with one host, but all nine tuples and both platforms remain final obligations.

### Step F — Freeze the actual candidate and execute Full-Scope acceptance

Resolve real resources and measured criteria, finish acoustic/listener/creator qualification and preserved U60/support work, then execute installed host/long-run/restoration checks against the same signed candidate. Pass the typed product gate. Only then is the agreed Beta GO complete.

The central change is to **stop treating steps B/C as distant “asset work.”** If real voice quality remains unknown while contracts and fixtures expand, the most important failures will be found last. Preserve the full scope while developing the acoustic pilot and actual model/data path alongside engineering integration.

## 15. Limitations, open qualification questions, and final decision

This review combines current source inspection, the approved plan, actual rebuilds and focused execution, and three independent source-review lanes. It is not a claim to have read every line or tested every environment. Product source, tests, and existing plans were not edited; only new report artifacts and evidence were written.

Not performed: a fresh clean-checkout full build; execution of the latest 16-case Studio draft target; current full core execution; Windows runtime; complete latest native visual/input QA; nine signed-installed DAW tuples; long qualification soak; actual female-singer listening; actual trained neural-singer evaluation; and independent creator/language/music acceptance. None is assumed complete.

The following questions belong to the next experiments, not another round of user permission requests:

- Which phones and range areas fail most for the first original singer, and is the cause linguistic, source-related, synthesis-related, or a unit join?
- Does the intended recipe identity survive baking, editing, installation, and rendering?
- Can users complete real source assessment with the evidence and controls available inside the product?
- What conditioning, CPU time, and memory does the actual neural model require?
- Does assisted performance reduce correction work under matched conditions without increasing unresolved pronunciation or timing errors?

**Final decision: this is an appropriate production architecture that has advanced ahead of the actual singer product—not evidence that the project should be abandoned.** Recent draft/review/install/Final integration is a valuable correction. The next substantial advance should build real singers and songs on that foundation while closing the confirmed work-loss and integration issues. What needs to change is the execution sequence: prove the existing full goal through sound and complete creator work, rather than replace it with a smaller goal or another broad status document.
