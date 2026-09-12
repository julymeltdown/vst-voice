# Audit repair and sample publication — 2026-09-09

This is implementation evidence following the Korean direction audit, not a new or reduced Beta contract. Full-Scope U1–U48 and R1–R20 remain required. Changes are local and uncommitted.

## Actual CLAP processing, not an engine-labelled report

The Phase 12C matrix now loads the supplied CLAP module, discovers the canonical factory/descriptor, and calls init/state/activate/start/process/stop/deactivate/destroy. Main-thread lifecycle and audio-thread processing are separated. The host links formats/core/voicebank helpers, not the live engine or editor runtime.

It executes 336 combinations: six sample rates, seven block sizes, four output layouts, and CLAP/MIDI1 dialects. Each row observes nonzero finite audio, onset offset, release silence, changed samples after tuning, left/right pan isolation, mono audibility, buffer guards, bounded event admission, held-note processing status, and bank/style/layout state roundtrip. The plugin and bank are rehashed after the run. A text file is rejected as a plugin, and an admitted failed run replaces an old report with FAIL.

A real binary run before the audio repair failed all 336 rows on pan. The repaired matrix and its stricter verifier pass. A second resource test inserts leading silence into a private fixture copy and verifies the plugin does not request SLEEP before its attack becomes audible.

Evidence limitations are explicit in the report: float32 output; an empty score bound to bank/style/layout; tuning verified as sample differences rather than calibrated cents; engineering-only, releaseEligible=false. No installed DAW, float64, full-song state, official validator, or release-quality singer qualification is inferred.

The legacy soak remains a linked-engine workload. Its JSON says linked-engine-v1 and records the explicit monophonic-legato policy. A five-second smoke is not a two-hour actual-plugin soak.

## Live behavior repairs

- Restored the pinned CLAP note-expression wire layout and IDs; added independent byte-offset tests. The test uses the external header through a SYSTEM include without suppressing project warnings. Updated its dependency digest.
- Preserved per-channel live PCM through the final plugin mix; mapped CLAP pan to the internal range and kept mono audible.
- Matched port/channel/key/note-ID wildcards together; separated per-note tuning from MIDI channel bend.
- Kept ordinary notes polyphonic even when their onsets are staggered. Legacy transitions now require explicit MonophonicLegato.
- Added sustain, panic/all-notes-off, controller reset, volume/expression dispatch, and persistent channel CC1/CC74/pressure inherited by new notes.
- Rejected oversized host event lists before traversal, and kept processing while an active voice can produce later sound.

The original short energy check observed only 256 frames of a quiet fixture onset and an 8 ms attack. An isolated probe measured energy 0.331538 there and 3.16084 in the following 256 frames. The regression now observes 512 frames while retaining its energy > 1.0 assertion.

## Multilingual and cache integrity

English/Korean now validate replacement and appended override bindings against unedited base pronunciation. Generic admission precedes indexing; dispatch and hashing honour cancellation. Input identities include overrides and use resolver version 2. Cross-language/resource changes preserve old edits as unresolved with exact undo/redo. Mixed-language diagnostic review offers no actionable rebind targets.

The original audit probe now reports:

~~~text
en invalid_context_applied=0 orphan_warning=1
ko invalid_context_applied=0 orphan_warning=1
ja_to_ko unresolved=1 context_replaced=0
pre_cancelled_mixed_error=Pronunciation resolution cancelled
~~~

Region cache hits restore fallback counts as well as backend/diagnostic metadata. The regression uses real rendered PCM with explicitly seeded nonzero provenance, because unsafe DSP fallback is correctly rejected by the compiled-performance path.

## Actual sample candidate publication transaction

New entrypoints in candidate_publication.hpp/repository_candidate.cpp publish exact current derived WAVs, a normal bank manifest, markers/pitch marks, content identity, source snapshot/license, and review bindings into a new directory.

Publication requires the exact durable generation/project hash, complete approved assignment coverage, and separately attributed review material bound to the complete manifest and source/processing/annotation/policy state. Original import attribution is checked against contiguous hash-bound generation/journal history; unavailable or gapped history fails closed. No approval is created by publication.

The writer holds generation and destination locks, stages privately, reopens the bounded manifest, verifies staged hashes, and commits with non-overwriting directory publication. A post-rename durability failure returns the committed path/hashes with durabilityConfirmed=false and inspect-before-retry guidance rather than pretending nothing was published.

The positive regression uses generated oscillator audio, saves/reopens a Japanese score, hides the producer workspace and raw input, then renders through ProductionRegionRenderer at Final quality. It requires audible finite Classic PSOLA output, zero fallbacks, and exact disk-cache reproduction. It proves candidate-to-audio connectivity, not the quality of a female singer.

Independent review found and corrected metadata fixture API misuse, oversized manifest acceptance, raw-importer self-review, missing intermediate history, and ambiguous postcommit durability reporting.

### Publication work still required

- U13 review UI/API must persist the new material bindings through an actual producer workflow.
- Native Studio/CLI publication, package signing/install integration, and installed resolution/render proof remain open.
- Multi-style assignment ownership, recipe/model packaging, and persistent/scalable per-take origin records remain open. The history-based safety check is bounded and may reject history that cannot establish origin.
- The old U57 template exporter is unchanged. This transaction must be wired into the actual user workflow; it is not full U14 acceptance.
- Actual lawful singer/model resources and independent musical/rights qualification are still absent.

## Verification

- Strict full Release build: PASS.
- Full Release CTest before the positive helper-budget correction: 108/110 PASS in 201.64 seconds.
- Core suite in that run and in a fresh execution after the test-budget correction: 716 passed, 0 failed.
- Producer suite, including published-bank-to-Final-audio and disk-cache proof: 14 passed.
- Actual CLAP matrix/evidence/admission: all three CTest entries passed; 336 actual binary rows passed.
- Canonical verifier unit tests: 15 passed.
- Source closure failed with 278 unindexed inputs at the full-run snapshot. This is not resolved by this change and will grow with new source/docs until reviewed integration.
- The other full-run failure was the positive neural IPC test's two-second startup override. Twelve isolated runs passed; the later core invocation also passed. The deadline includes process launch and macOS policy evaluation can vary. The positive test now uses the existing ten-second request default, without changing production behavior or the separate 50/100 ms timeout/cancellation checks. Focused protocol and helper tests pass after this test-only correction. This is not model-latency qualification.
- The full suite was not rerun merely for that test-budget change. The full-run result above is retained rather than relabelled 109/110.
- Windows execution, signed-installed hosts, full plugin soak, production resources, and full-product acceptance remain unverified.

The local build records the verified Git HEAD as its source base and binds exact plugin bytes; it does not attest a clean source checkout. Fresh Git configurations derive a base HEAD; existing caches or source archives must supply the intended SEAM_SOURCE_COMMIT explicitly. Release automation still owns exact source/build identity and clean-candidate proof.

## Next connected work

Wire the reviewed publication transaction into the normal producer review and Studio/CLI export flow, then package/install the result and render a saved song from a clean consumer environment. Continue neural dataset/model and procedural articulation work as separate mandatory tracks. No new roadmap unit or Beta GO acceptance is declared by this repair.

CLAP lifecycle/status behavior follows the [pinned process contract](https://github.com/free-audio/clap/blob/195b42a004144fab0b3cf95e9c067187d15365b7/include/clap/process.h) and [plugin lifecycle contract](https://github.com/free-audio/clap/blob/195b42a004144fab0b3cf95e9c067187d15365b7/include/clap/plugin.h).
