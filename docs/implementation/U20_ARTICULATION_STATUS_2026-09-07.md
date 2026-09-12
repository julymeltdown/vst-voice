# U20 procedural articulation status

Status: active, incomplete. Vowel-only phrase rendering does not satisfy U20's pronounceable-phrase or unseen-phrase requirements.

## Default timing follow-up

Production procedural snapshots now opt into a versioned in-note timing policy for wholly untimed single-unvoiced-onset syllables. The onset receives up to 60 ms, capped at one quarter of the syllable, and the nucleus follows it. Inferred starts are distinct from explicit edits; any authored syllable timing leaves that group untouched. Prior vowels end before generated onsets, and source/recipe validation remains mandatory. Untimed supported `さ` now renders and bakes without creating saved overrides. See `PROCEDURAL_ONSET_POLICY_2026-09-07.md`. This supersedes earlier statements below that every onset requires a hand-authored start; it does not establish natural pronunciation or extended/cluster timing support.

## Mixed candidate baking and ingestion

Candidate v2 now records `planned-articulated-gestures`, explicit `oral-vowel`/`frication` marker kinds and plan/frication-source/frication-stream revisions. Vowel-only exports retain v1. Mixed baking now uses the same Final pipeline instead of rejecting at the previous temporary format boundary. The strict loader requires both gesture classes, validates recipe/style bindings and frication rate support, and retains all existing metadata/audio bounds and digest checks. See `docs/formats/PROCEDURAL_CANDIDATE_V2.md` for the exact contract.

Typed markers survive the scheduler, canonical producer import, recovered original lineage, Studio-controller loading and durable boundary edits. No review or approval is granted by import or editing. Tests compare baked PCM with the actual Final pipeline, reject malformed/cross-version semantics and unbound gestures, recover exact marker kinds and preserve original metadata after manual editing. These are structural and numerical checks; the synthetic strategy fixture is not production source qualification, and controller tests do not prove physical native interaction.

Broader articulation, default/extended onset policy, reviewed CV/VC distinction, unseen-phrase intelligibility and intended singer identity remain unfinished. Candidate v2 removes a format/integration gap, not the U20 acceptance gate.

## Production snapshot integration

The production snapshot factory now accepts explicit recipe-bound unvoiced-onset frication plus oral vowels. It retains the shared pronunciation resolver, active sample-edit rejection, complete note coverage and compiled timing; it does not infer an onset start for ordinary untimed consonant lyrics. Unsupported source classes still fail preparation. Oral-vowel-only phrases continue using `SustainedPoseStream`; mixed supported phrases use `ArticulatedStream` through `ProceduralSnapshotStream`.

The checkpoint wrapper now stores either renderer and retains the same immutable-context matching and output-ownership rules. Articulated markers are projected from recipe-derived plan spans, including onset-to-nucleus boundaries and clipped-edge flags, rather than using vowel timing for all tokens. Scheduler admission validates bounded nonempty marker text, ordinal and output geometry instead of maintaining a second vowel inventory. Supported-phone admission remains in snapshot preparation; generic marker labels are not an acoustic capability declaration.

Procedural render/cache identity now uses an articulation-v2 domain and includes articulation-plan, articulated-stream, frication-stream and frication-source revisions alongside the existing vowel/compiler revisions and frozen recipe identity. This intentionally invalidates older procedural cache entries, including vowel entries; it does not rewrite saved recipe identities or change the vowel algorithm.

Typed-source project rendering and ordinary Final audio export now use this mixed path. The initial integration rejected mixed candidates because v1 promised `planned-vowel-gestures`; candidate v2 and strict producer ingestion now supersede that temporary restriction as described above.

Verification: strict Debug/Release builds and all 12 voice-design + 41 snapshot + 19 export cases pass (31.39/5.53 seconds across the three suites). The new snapshot case uses actual resolved `さ` edits at a nonzero song origin; tests compare direct/pipeline PCM, cross-boundary clipping, cancellation without advancement, copied checkpoints, scheduler assembly/work accounting, cache-hit markers and changed-recipe identity. Malformed marker text/ordinals reject. The export case compares committed Float32 WAV with Final project PCM and verifies unsupported mixed baking creates no destination. The first scheduler run exposed a remaining vowel-only marker guard, which was corrected and regression-tested. The Release native editor target builds; `git diff --check` passes. No new physical playback, native interaction, listener, intelligibility or female-identity qualification is claimed.

## Current preparation entry point

`ArticulationPlan::compileRecipe(resource, performance, phones, style, stop)` derives an immutable plan from the verified frozen recipe and shared compiled timing. `ArticulatedStream::createFromRecipe` prepares and instantiates the worker renderer without caller-supplied frication parameters. Production snapshots now use these entry points after shared phrase validation, as described above.

Preparation selects only requested onset bindings from the selected style, validates all used vowel poses, derives context from compiled score notes, and rejects incomplete note coverage or gestures extending outside their own score note. A gesture fitting the whole phrase is not sufficient: extended preutterance needs a separately supported phonation context. Unused recipe frication presets do not impose their Nyquist requirements on a vowel-only request. Recipe integrity and cancellation are checked; preparation remains bounded, allocating worker work, not realtime-safe callback work. Cancellation during the low-level bounded plan/stream construction is observed at preparation checkpoints, not on every allocation.

The new integration case uses `resolveJapanesePronunciation` on a real `さ` lyric with persisted-style `phonemeOverrides`: an explicit onset at 0 microseconds and nucleus at 100,000 microseconds. Prepared PCM matches the explicit-plan renderer exactly. Missing onset timing, wrong/missing style or binding, missing vowel pose, corrupted resource identity, incomplete note coverage and an onset in a score gap reject. Pre-cancelled preparation rejects. Strict Debug voice-design tests pass all 12 cases (2.56 seconds); strict Release voice-design/snapshot/export tests pass 12 + 40 + 18 cases (3.84 seconds). `git diff --check` passes.

Mixed-candidate metadata and ingestion now use v2; the existing v1 `planned-vowel-gestures` format is not reused to label mixed output. Default onset policy, broader articulation, listening/intelligibility evidence and singer qualification remain open. No roadmap unit is newly accepted. Earlier component notes below retain historical verification boundaries; the production integration sections above supersede their statements that snapshots are vowel-only or source settings are unpersisted.

## Aperiodic excitation component

`FricationSource` now provides deterministic, sequential band-shaped noise with an explicit seed, center frequency, bandwidth and gain. It uses a normalized band-pass biquad and absolute-frame noise addressing, with a distinct source tag. The common noise function was extracted from phonation without changing its arithmetic. No F0 is embedded in this aperiodic source.

Configuration validation rejects nonfinite values, unsupported rates, negative/overflowing origins, invalid Nyquist requests, excessive gain and Q outside 0.25–20. Output is guarded to finite normalized excitation; failure/cancellation returns no committed state advance. Copies retain independent filter checkpoints, and reset restores the original zero-state context. There is no realtime-allocation claim: each render returns owned PCM.

The source has algorithm revision 1. It is not yet part of persisted `VoiceRecipe` or a phrase snapshot's resource identity. It must not be enabled in the production phrase path without those bindings and the appropriate renderer/cache revision changes. The existing oral-vowel candidate format, phoneme whitelist and unsupported-consonant checks are unchanged.

## Verification

- Strict Debug/Release voice-design builds pass; all 9 source/design cases pass (final reruns 1.81/0.81 seconds).
- Tests cover exact whole/chunk output, reset, copied checkpoints, seed variation, cancellation/no advance, invalid configuration, tested rate/Q extremes and normalized finite output.
- A deterministic spectral check verifies substantially greater energy near the requested 5-kHz band than around 400 Hz. This is filter behavior, not identification of /s/ or another phoneme.
- All 40 rebuilt Release performance-snapshot cases pass (1.74 seconds), preserving the existing vowel scheduling/checkpoint regressions after noise-helper extraction.
- `git diff --check` passes.
- The test retains a one-second mono Float32 `aperiodic-source.wav` in its temporary `project-seam-frication-source-*` directory. Its fixture uses seed 42, 48 kHz, internal origin 123, center 5 kHz, bandwidth 3 kHz and gain 0.15. This diagnostic is not a voicebank unit or approved consonant; listening qualification has not occurred.

## Required continuation

### Recipe identity follow-up

Style-scoped frication settings are now persisted in strict recipe schema/resource version 2 and included in canonical content hashes. Vowel-only recipes retain version-1 bytes and identities. The articulated worker rejects plan source settings that differ from the frozen recipe. Full-width seeds, save/reload, malformed fields, version/hash changes and a pre-change v1 identity fixture are tested. See `docs/formats/VOICE_RECIPE_V2.md`. This supersedes earlier notes about unpersisted frication parameters; production snapshot and mixed-marker/bake integration remain open.

### Explicit-plan voiced/aperiodic composition

`ArticulatedStream` now composes continuous compiled-F0 phonation through the oral tract with the scheduled frication lane. Construction verifies the plan's rate, key coverage, spans and voicing against compiled performance and validates every requested vowel pose. Frication intervals suppress voiced excitation/output; vowel intervals contain voiced tract output. Independent vowel groups have short smoothstep boundaries, while contiguous vowels retain continuous gain and bounded tract-pose transitions. Shared performance gain is applied once after composition to both lanes.

The worker stream bounds total prefix/output replay, retains source/tract/gesture state across blocks and owned windows, supports copied checkpoints/reset, and publishes advanced state only after success. Its algorithm revision is 1. It is not the production snapshot adapter and does not freeze frication bindings into a persisted recipe/resource hash; enabling it there still requires those identity and marker-format changes.

Tests compare onset PCM with the isolated frication lane, check nonzero post-nucleus vowel output and a near-440-Hz vowel estimate, and reconstruct exact whole PCM through 1/512/4096-frame blocks, checkpoints and boundary-crossing ownership. They reject mismatched compiled timing, preserve position on cancellation, and verify half dynamics scales both sources once. All 10 voice-design cases pass in strict Debug/Release (2.64/0.76 seconds); `git diff --check` passes. A temporary `project-seam-articulated-stream-*` fixture retains `explicit-frication-vowel.wav` for diagnostic inspection. No listener judgment, intelligibility or female-voice qualification has been made.

### Scheduled frication lane

`FricationGestureStream` now consumes the immutable articulation plan, whose validated sample rate/context are retained. It renders the frication gestures at their absolute source times, preserves filter state across processing/ownership boundaries, and leaves vowel spans/unassigned gaps exactly silent. Each frication gesture has at-most-5-ms smoothstep edges, shortened for small spans. Filters start at gesture boundaries, never arbitrary processing-block boundaries. Copies are independent checkpoints; reset restores the original context.

Owned-window requests replay necessary prefixes but retain only requested PCM. Both owned PCM and accumulated frication DSP work (including discarded prefixes) are limited to 32 Mi frames per call. Silent intervals can be skipped directly. Cancellation or validation/DSP failure does not publish advanced state. The source remains a worker-oriented allocating API, not a realtime callback.

Expanded tests prove exact whole/chunk output for 1/512/4096-frame processing blocks, mid-ramp windows, copied checkpoints, reset, cancellation, reverse-window rejection, edge zeros and silent vowel spans. A synthetic large-anchor case verifies that tiny ownership cannot conceal excessive prefix work. All 10 voice-design cases pass in strict Debug/Release (see execution ledger for final timings); `git diff --check` passes. This lane alone is not a phrase renderer: voiced composition, source/tract transitions, recipe/snapshot identity integration and acoustic qualification remain open.

### Explicit articulation-plan component

`ArticulationPlan::compile` now prepares immutable ordered oral-vowel/frication gestures from supplied phoneme tokens and the shared `PhonemeTimingAnchor` contract. Frication requires an explicit caller binding, an unvoiced onset, an explicit start and a valid same-note voiced nucleus. Its automatic end is the actual nucleus frame, not the vowel's end; explicit ends cannot cross the nucleus. Unknown/unbound source classes, duplicate keys/bindings, mismatched voicing, empty/out-of-context spans and overlaps reject.

The test uses `compilePhonemeTimingPlan` on an explicitly timed s/a fixture, then instantiates and renders the frication source for the resulting 0–4,800-frame onset. It checks missing starts/bindings, duplicate timing, inconsistent voicing, unknown phone and crossing-end rejection. All 10 voice-design cases pass in Debug/Release (1.77/0.65 seconds) after strict builds; the private construction boundary was also rebuilt successfully. `git diff --check` passes.

This is not automatic phoneme-to-acoustic qualification. Caller bindings are DSP requests, not evidence that a sound realizes the named phone. The plan is not yet used by production phrase snapshots or persisted recipes, and the main renderer still rejects consonants. Extended onset policy, source envelopes/transitions and mixed-source streaming remain required.

1. Extend the explicit articulation plan with qualified inventory bindings, default/extended onset policy and closure/burst semantics; do not infer support from labels alone.
2. Bind source/tract parameters and their transitions to versioned recipe/snapshot identities; preserve deterministic chunk/cancellation behavior.
3. Integrate supported gestures into actual phrase rendering and baking, with truthful marker semantics rather than relabelled vowels.
4. Complete aspiration, closures/bursts, nasal behavior and context transitions required by the declared inventory.
5. Retain measured and independently reviewed CV/VC, unseen-phrase and intended-female-identity evidence. Neither this source primitive nor a test WAV satisfies those gates.

No new roadmap unit is accepted, no singer is qualified, and full Beta GO remains open. Source changes are local/uncommitted.
