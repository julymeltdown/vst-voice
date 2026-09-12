# U19 vocal DSP — initial oral-resonance stage

Status: started, incomplete. U6/U15/U18 and full U19 acceptance remain open.

`VocalTract` now selects a recipe phone/style pose and constructs a parallel oral-resonance bank. The implementation uses normalized constant-0-dB-peak band-pass coefficients from the [W3C Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/). Each band's nominal Q is center frequency divided by bandwidthHz. gainDb supplies relative amplitude weights normalized across bands; overall loudness belongs to excitation/performance dynamics, not a hidden global gain from this stage.

The stage rejects centers at/above Nyquist and numerically non-stable coefficient sets rather than clamping recipe data. Nonzero nasal coupling returns Unsupported because the nasal resonance/antiresonance model is not implemented. Input must be nonempty, finite, normalized to [-1,1], and at most 32 Mi samples. Output is linear, checked for finiteness and an absolute safety bound of 8; no nonlinear saturation or silent normalization is applied.

State persists between successful blocks and can be reset. Processing uses candidate filter state and commits it only after success, so invalid or cancelled blocks do not advance the live state. Returned buffers allocate outside realtime audio callbacks. Live pose changes/coefficient interpolation are not implemented.

## Verification

The dedicated regression checks exact 127-frame-block versus whole-buffer output, reset replay, invalid/cancelled-state preservation, missing/unsupported pose/rate behavior, Nyquist rejection, and impulse responses at bandwidth extremes across 8/44.1/192/384 kHz. Steady-state zero crossings retain a 200 Hz input tone after changing resonance. A separate two-tone measurement verifies opposite 700/1000 Hz emphasis when the leading formant changes.

All three voice-design cases pass after strict Debug/Release builds (0.56/0.35 seconds); `git diff --check` passes. These are numerical and diagnostic-signal checks, not proof of a coherent female character, intelligibility, naturalness or cross-platform qualification. No complete singer, band-limited phonation source, nasal tract, articulation engine, runtime adapter or listening study was delivered.

## Compiled-F0 excitation follow-up

`PhonationSource` now deep-captures compiled score performance and recipe source parameters, then produces sequential excitation at absolute frames. It integrates instantaneous compiled F0, resets phase on explicit reattacks and retains it at processing-block boundaries. Recipe open quotient shapes signed harmonic weights; spectral tilt controls their rolloff. The approach uses a finite harmonic sinusoid sum, consistent with the [additive sinusoidal model described by Julius O. Smith](https://dsprelated.com/freebooks/sasp/Spectral_Modeling_Synthesis.html). This is an initial excitation model, not a claim of physiological glottal-model accuracy.

At most 256 harmonics are synthesized, with a cosine taper between 0.35 and 0.45 times sample rate. Compiled/modulated F0 must be at least 20 Hz and below 0.4 times sample rate; unsupported ranges fail rather than transpose silently. Seed/frame-derived aspiration noise is low-pass filtered. Recipe rate controls periodic pitch/shimmer variation, with zero rate disabling it. Mixed excitation is bounded to 0.5 amplitude. This stage intentionally does not apply dynamics/articulation gain: those must follow tract processing in the eventual adapter.

Successful blocks advance phase/noise/time state atomically; failed/cancelled blocks do not. Reset replays from the fixed context origin. Arbitrary seeks require replay/pre-roll from that origin, not a fresh oscillator at each chunk. Frames outside active notes produce zero source output; a selected null pitch supplies no harmonic component.

The new regression measures C4 and G4 in actual source PCM, verifies exact block/reset reproducibility with aspiration/modulation, seed changes, silence, unsupported F0 and cancellation state preservation. A 2,700 Hz source at 8 kHz has over 1,000 times the measured amplitude of its tested 2,600 Hz folded-second-harmonic location. All four voice-design cases pass in strict Debug/Release (1.01/0.48 seconds), plus `git diff --check`. That diagnostic is not general alias-free qualification: FM/AM sidebands, transitions, harmonic-cap timbre, glottal realism, cross-platform tolerances and listening quality remain open.

## Sustained-pose audition integration

`renderSustainedPose` now decodes a frozen procedural recipe, captures compiled performance, runs sequential excitation through a selected oral tract pose, applies compiled dynamics/articulation after filtering, and returns only the declared owned output window. The result carries recipe resource identity, pose/style and audition algorithm revision 1. Internal block sizes are bounded to 1–65,536 frames; full context/output limits remain those of the shared phrase contract. The source currently requires a nonnegative context origin.

Post-tract gain prevents filter ringing from reopening staccato gates. Failed/cancelled work returns no successful partial result. This is a sustained resonance-pose audition API: it does not pronounce arbitrary pose labels, infer consonants, supply phonetic transition markers or approve generated source material. The normal snapshot pipeline still has no procedural-family dispatch.

The integration regression verifies identity propagation, exact 127-versus-4,096-frame rendering, exact owned-window slices, sample-by-sample half gain, zero post-gate staccato output, and missing-pose/rate/block/cancellation failures. All five voice-design cases pass after strict Debug/Release builds (1.42/0.55 seconds); `git diff --check` passes. No new listening, general anti-aliasing, native-preview or intelligibility qualification is claimed.

The subsequent procedural snapshot factory/pipeline integration now exposes this path for whole-region sustained Japanese oral-vowel sequences, with source identity, compiled performance and recipe/cache binding. It rejects unsupported articulation/retiming/sample-only controls and returns no fabricated sample units. Exact pipeline/direct-DSP parity and owned-window checks pass as part of the 43 focused Debug/Release cases (11.98/2.37 seconds).

Background scheduling now accepts supported procedural snapshots after semantic preflight, validates full/owned output extents, reuses cached PCM and assembles matched procedural completions. The expanded 43-case snapshot/voice-design verification passes in Debug/Release (12.01/1.70 seconds). This is low-level scheduling, not native project resource selection or device playback qualification.

## Causal owned-window processing

The sustained-pose renderer now retains full supplied musical context but only advances source/tract DSP from context start through owned end. Pre-roll still establishes exact oscillator/noise/filter state; its PCM is discarded block by block. Only owned samples are retained, and no unused trailing DSP runs. `processedFrames` reports actual source/tract frames advanced. This optimization is specific to the current causal adapter, not permission to remove neural/backend lookahead.

The 4,000–9,000-frame fixture window now processes 9,000 rather than 24,000 frames and retains 5,000 output samples. Exact slices and tiny end-of-context windows match full rendering at 1/257/4,096-frame blocks; procedural scheduling/assembly regressions remain passing. All 43 focused cases pass in strict Debug/Release (13.10/2.99 seconds), plus `git diff --check`. These counts establish avoided DSP/storage, not a measured whole-application speedup. Repeated pre-roll across independent chunks remains unoptimized.

Next: connect native resource selection, share/reuse pre-roll where safe, implement smooth pose changes/nasal behavior/phoneme articulation, and retain measured and independent listening evidence. No complete voice or U19/U20 acceptance is claimed. Source remains local/uncommitted.

## Stateful sustained-pose streaming

`SustainedPoseStream` now retains oscillator, aspiration and tract state across sequential owned windows. Copying a stream captures an independent DSP checkpoint with shared immutable score data; reset restores the original context origin. Each render works on candidate source/tract state and commits only after successful DSP, gain application and output validation. Cancellation, invalid windows and failures after multiple processed blocks preserve the prior checkpoint.

Two adjacent 12,000-frame windows advance 24,000 DSP frames in total, versus 36,000 for two independent one-shot calls. Their concatenated PCM is exactly equal to full rendering. A forward gap advances/discards its intervening DSP while retaining only requested PCM; backward output requires reset or a prior checkpoint. Tests cover checkpoint independence, gap/tail equality, out-of-context rejection and rollback from both zero and a nonzero position.

This is an opt-in sequential API, not a scheduler speedup: independent snapshot jobs still call the one-shot wrapper. Binding reusable streams/checkpoints to exact snapshot context and integrating scheduler/native lifecycles remain open. No new roadmap unit is accepted by this change.

Verification: strict affected Debug/Release builds passed. All 43 focused cases (5 voice-design and 38 snapshot cases) passed in both configurations; CTest elapsed 11.94/1.76 seconds. `git diff --check` passed. This is focused regression evidence, not a fresh full native/host qualification run.

The rendering layer now supplies `ProceduralSnapshotStream`, binding this DSP state to exact shared immutable snapshot context and limiting sibling output to the original ownership range. All 38 snapshot cases pass in strict Debug/Release (11.02/1.48 seconds), with context mismatch/cancellation preservation and exact full-PCM reconstruction. This remains single-owner; independent scheduler jobs have not yet adopted stream reuse. It adds no articulation or acoustic-quality acceptance.

Scheduler workers now reuse private copies of compatible completed checkpoints, with a 16-entry retention bound, reset/revision cleanup and context-origin fallback for reverse-order or unavailable state. Sequential sibling tests prove one context's total DSP advancement and exact audio; reverse ordering and reset are also verified. All 38 snapshot cases pass in strict Debug/Release (11.16/1.49 seconds). This supersedes the preceding scheduler-adoption limitation, but does not promise zero repeated pre-roll for concurrent starts or native integration. Articulation, nasal behavior, interpolation and listening qualification remain unfinished.

## Oral-pose transition primitive

`VocalTract::transitionTo` now schedules a bounded 1-frame to 2-second transition at the existing sample rate. It validates a fresh target bank through the same stability/Nyquist/nasal checks, then runs both independently stable banks under a smoothstep output crossfade. No pole interpolation or variable-coefficient stability assumption is used. Target state starts at zero and warms from the transition boundary; this is an engineering transition, not a physiological coarticulation model.

Processing copies both bank states and transition position and commits only after the whole block succeeds. Cancellation/invalid input preserve both. Scheduling another target while a transition is active rejects; reset cancels an unfinished transition and resets the last fully committed pose. Different formant counts are supported. At completion the target bank replaces the old bank, releasing the extra state.

Tests verify exact whole/37-frame-block equality, oracle crossfade weights, exact target output from the last fade sample onward, reset, cancellation and invalid-input preservation. All 46 voice-design/snapshot cases pass after strict Debug/Release builds (14.02/2.19 seconds), plus `git diff --check`. The phrase adapter still supports one sustained vowel pose: phoneme-driven transition scheduling, retargeting policy, nasal articulation and listening qualification remain open. No U19/U20 acceptance is claimed.

## Note-bound oral-vowel scheduling — procedural revision 2

The procedural snapshot adapter now accepts sequences of oral Japanese vowel nuclei (`a/i/u/e/o`), one unretimed token per note. It verifies every required recipe pose before rendering/cache admission. `configureVowels` maps tokens to immutable compiled note IDs and schedules pose changes at absolute note starts. Same-vowel neighbors keep the existing bank. Changes use up to 20 ms of smoothstep bank crossfade, shortened to the target note duration so consecutive transitions do not overlap. This is a deterministic engineering default, not linguistically calibrated timing.

Owned-window rendering splits DSP blocks at pose events. Event index and both tract states commit atomically with source state, and checkpoints preserve in-progress crossfades. Reset restores the initial pose and schedule rather than the last settled pose. The single-pose audition wrapper remains available; result `posePhone` describes the initial pose, while pipeline phoneme metadata preserves the sequence. Procedural algorithm revision 2 enters snapshot identity.

A real `a`→`i` regression checks changed post-boundary audio with identical earlier audio, exact reset/127-frame replay, required-pose rejection, and scheduler reconstruction when a chunk boundary lands 100 frames into the transition without repeated DSP. All 47 voice-design/snapshot cases pass in strict Debug/Release (14.37/2.70 seconds), plus `git diff --check`. Consonants, multiple nuclei within one note, explicit phoneme retiming, nasal modeling and listening/intelligibility qualification remain open; U19/U20/Beta GO are incomplete.

Procedural revision 3 consumes the shared compiler-owned phoneme timing anchors, enabling multiple oral-vowel nuclei within one note. Coverage requires every note and contiguous token ordinals; timing keys/nucleus identities must match, and overlapping or explicitly retimed spans reject. Pose changes follow nucleus frames, with crossfade duration capped by the actual phoneme span. Same-pose neighbors retain their state. This removes the one-vowel-per-note limitation without introducing an independent syllable-timing algorithm.

Tests verify `a→i` inside a single note, exact equality before the shared midpoint and changed audio afterward, plus `a→i→a` inside a two-tick note with non-overlapping shortened transitions. Existing chunk/checkpoint and single-vowel regressions remain passing. All 64 compiler/voice-design/snapshot cases pass in strict Debug/Release (19.06/3.35 seconds), plus `git diff --check`. Explicit onset/end retiming, consonants, nasal modeling and acoustic/intelligibility qualification remain unfinished; U19/U20/Beta GO stay open.

Procedural revision 4 accepts explicit vowel onset/end offsets when each resulting span stays inside its owning score note and spans do not overlap. Shared timing anchors remain authoritative. A shared preflight guard rejects invalid or out-of-note timing before snapshot creation/cache admission; earlier preutterance and extended post-note phonation are not silently clamped.

The stream now tracks immutable active spans, splitting blocks at onset/end/gap boundaries. Source phase continues on the score clock, but tract excitation is zero outside active spans and published PCM is also zero there, preventing resonance tails from filling deliberately authored silence. Span cursor, pose cursor and DSP state commit together. This is exact timing/gating, not a qualified click-free onset or consonant model.

The regression checks a 100 ms delayed onset, a 200–300 ms gap and a 450 ms end inside a 500 ms note: silence is exact outside the two vowel spans and both active spans contain audio. Independently rendered owned chunks reconstruct exactly. Out-of-note early/late offsets reject. All 47 voice-design/snapshot cases pass in strict Debug/Release (16.83/2.93 seconds), plus `git diff --check`. Extended phonation context, consonants, nasal behavior and acoustic qualification remain open.

Procedural revision 5 adds short smoothstep gain ramps at authored outer onsets/endings and separated vowel spans. Ramp length is at most 5 ms and is shortened to half the span (minimum one frame). First/last active samples at ramped boundaries are zero; contiguous vowel spans retain uninterrupted gain and use the existing pose crossfade. Very short edited spans may be strongly attenuated; this is not evidence of intelligible sub-millisecond phonemes. The single-pose/default contiguous path is unchanged.

The retiming regression verifies exact zero boundary samples and exact checkpoint reconstruction with a boundary 37 frames into the onset ramp. All 47 voice-design/snapshot cases pass in strict Debug/Release (14.85/2.78 seconds), plus `git diff --check`. This removes hard edited gate edges under the tested conditions, not all possible audible artifacts or a listening qualification. Extended context, consonants and nasal modeling remain open.

Procedural revision 6 returns `ProceduralPhoneMarker` metadata for scheduled vowel gestures. Each marker carries a stable phoneme key, vowel label, absolute half-open span within returned PCM, and explicit start/end clipping flags. These are planned gesture boundaries, not acoustic/F0 measurements: accepted null pitch, dynamics or articulation may suppress audible output inside a planned gesture. Gap-only windows return no markers. A cropped marker start must not be treated as a new onset.

The phrase pipeline exposes this metadata separately from sample unit placements. Single-pose auditions with no phoneme schedule return no invented markers. Markers are constructed before committing stream state. Tests verify full edited spans, original keys, both-end clipping and empty gap windows across all 47 voice-design/snapshot cases in strict Debug/Release (14.83/2.76 seconds), plus `git diff --check`. Scheduler completion payloads still carry PCM only; durable bake/marker serialization and native marker consumption remain open, as do consonants and acoustic qualification.
