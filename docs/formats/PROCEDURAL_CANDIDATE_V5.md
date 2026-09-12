# Procedural candidate v5 — voiced-frication metadata

Status: strict loader, export writer and normal recipe-v5 runtime integration
implemented. Producer round-trip regression is described below; listening and
full native workflow acceptance remain separate, unfinished requirements.

Version 5 uses the 21-field articulated candidate shape from v4, including the
plosive engine revision even when the candidate contains no stop. Markers retain
the five fields `key`, `phone`, `kind`, `startFrame`, `endFrame`; a new kind is
`voiced-frication`. Other v4 kinds remain representable.

A v5 candidate requires an expected recipe with identity version 5, at least one
oral-vowel marker and at least one voiced-frication marker. Mixed stream and plan
revisions must be at least 8, and frication-stream revision at least 3. Other
revision, bounds, canonical-key and exact recipe-identity rules remain active.

Each voiced-frication marker must resolve to its exact phone/style frication
binding with non-null voicingGain and a valid same-phone/style resonance pose.
Vowel and nasal identifiers cannot stand in for voiced frication. An ordinary
frication marker must resolve to an unvoiced binding; changing only the marker
label cannot drop the voiced component. The voicing gain is bound by recipe hash,
not duplicated as mutable marker metadata.

Approval must remain `unapproved`. Parsing planned markers does not establish
acoustic realization, listening acceptance, source rights or review completion.
The existing file loader still separately verifies audio bytes/hash/dimensions.

Regression metadata fixtures cover the accepted typed identity and rejection of
downgrade, approval claims, old mixed-renderer revision, unvoiced relabeling,
missing source, excessive marker bounds and absent voiced gestures. These are
metadata tests, not actual generated audio or producer-import qualification.
Release build and voice-design/export/core suites passed 3/3 in 31.81 seconds.

## Normal rendering and publication integration

The exporter selects schema 5 whenever rendered markers contain voiced frication,
writes that exact kind, and includes the required plosive engine revision even
without a stop. The standard resource/compiler/stream path now admits recipe v5.
Older candidate schemas remain usable for phrases that do not contain voiced
frication, even if their frozen recipe defines additional voiced sources.

The multilingual export fixture now includes English explicit `z aa1` at MIDI 69.
It renders through RenderSnapshotFactory/PhraseRenderPipeline, exports a Float32
WAV, reloads it with exact PCM equality, imports through the production repository,
recovers lineage/markers and opens the resulting workspace asynchronously in
Studio's controller. The take remains MarkerReview and reviews remain empty.
The initial expanded run reached the final marker assertion, whose old expected
kind needed updating to VoicedFrication; no rejection or approval gate was removed.

Designer CV/VC auditions now derive token voicing from the recipe. Isolated noise
audition rejects voiced sources rather than silently omitting their voiced lane.
Creating and editing voiced bindings through native controls remains further work.

Final Release build and Designer/voice-design/snapshot/export/core suites passed
5/5 in 29.32 seconds. Retained bake and test logs are under
`docs/implementation/evidence/voiced-frication-v5-2026-09-10/`. The half-second,
48 kHz mono WAV contains planned `z` frames [0,2880) and `aa1` [2880,24000).
Audio SHA-256: `b3becae482a390b10a83d0ee34a532bce71744a06c3bc27cb0f2d50173384d2b`.
The recovered test producer was generation 2 with one MARKER_REVIEW take and
zero reviews. This fixture does not qualify a voicebank, source license for public
release, female singer identity, pronunciation quality or the full native lifecycle.

## Coda round-trip evidence

The end-to-end fixture also covers English `aa1 z`. It verifies normal snapshot
rendering, exact exported/reloaded PCM, typed marker ordering, producer import,
lineage recovery and asynchronous Studio workspace opening. Planned vowel span
is [0,21120), followed by voiced frication [21120,24000) at 48 kHz. The candidate
remains MARKER_REVIEW with zero reviews. No acoustic acceptance is implied.

Release build and export/core suites passed 2/2 in 24.48 seconds. Retained bake
and logs: `docs/implementation/evidence/voiced-frication-coda-v5-2026-09-10/`.
WAV SHA-256: `9957e7adeb9eded2fabf2395871c2e0944c97c32a66ef745e1d1357276f6b475`.
