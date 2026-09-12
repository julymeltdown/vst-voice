# Voice recipe v5 — explicit voiced-frication contract

Status: contract, mixed stream, normal runtime and candidate-v5 writer/loader are
implemented. Designer now exposes voicing gain with zero-as-off semantics and
supports CV/VC audition. Live native creation/edit workflow and listening
acceptance remain unfinished.
This is not Beta GO acceptance. The sections below retain the staged implementation
history; earlier statements about closed admission are superseded by this status
and the normal-integration section in PROCEDURAL_CANDIDATE_V5.md.

## Native voicing-gain controls (2026-09-10)

Each style-filtered frication source has a fourth parameter row, VOICING GAIN.
Its numeric field, keyboard increments and pointer-drag route all edit the same
validated draft. Zero removes the optional voiced component; positive values
require the existing exact-phone/style resonance pose. A creator can explicitly
duplicate a pose with that phone/style before enabling voicing; the control does
not invent or silently borrow a resonance definition.

Source selection offsets, following stop controls and trailing nasal controls
account for the additional row. Regression tests cover mixed-style row offsets,
missing-resonance and range rejection, undo/redo, save/reopen, and exact unvoiced
identity restoration. Release build and Designer/core suites passed 2/2 in 22.80
seconds. Live field/drag operation is not yet qualified by this result.

### Live native follow-up

At 720x520 in the rebuilt Studio, created a temporary draft, explicitly duplicated
the `a / neutral` resonance as `z / neutral`, reselected vowel `a`, and added a
matching `z` frication source. The native numeric field accepted voicing gain 0.3
and advanced draft revision to 3. The visible CV button completed rendering.

Setting 1.1 reported the bounded-gain/resonance error without advancing revision
or changing the valid 0.3 value. Setting zero advanced revision to 4, displayed
OFF and invalidated the preview. Undo advanced revision to 5, restored 0.3 and
enabled Redo. VC rendering then completed with its expected ready status.

The voicing row was readable in the minimum-size screenshot. This verifies
explicit native creation, accessibility numeric editing, rejection, undo and
CV/VC render completion—not keyboard/drag interaction, live save/reopen, acoustic
quality or the complete create-to-installed-singer lifecycle. No existing saved
recipe or producer data was modified; the test draft remains temporary/unsaved.

### Preview availability follow-up

Source preview actions now use a shared availability calculation. Isolated Src
is disabled for explicitly voiced frication; CV/VC require a same-style selected
vowel pose. Stop Src remains available independently of vowel selection. Disabled
semantic names explain either the required vowel selection or the need for CV/VC.
Painting and pointer actions consume the same semantic enabled state; renderer
validation remains authoritative for shortcuts and stale requests.

Tests cover voiced/unvoiced sources, consonant versus vowel selection, style
mismatch and invalid indices. Live disabled-button interaction is not yet
qualified; earlier native gain-edit verification does not cover this increment.
Release build and Designer/core suites passed 2/2 in 28.36 seconds.

### Source/resonance removal dependencies

Removing a resonance pose used by voiced frication now reports an explicit
conflict: disable that source's voicing or remove its source first. The rejected
operation leaves draft revision and identity unchanged. Source removal remains
separate from resonance-pose removal; it preserves authored poses and invalidates
the audition buffer. A regression restores the source through Undo, checks exact
recipe identity, and rerenders identical CV PCM rather than reusing stale audio.
Live removal interaction is not qualified by this regression.
Release build and Designer/core suites passed 2/2 in 22.87 seconds.

Schema 5 retains the v4 root fields. `plosives` may be empty. Every frication row
has the existing phone/style/seed/centerHz/bandwidthHz/gain fields plus exactly
one `voicingGain` field:

- `null`: legacy unvoiced frication.
- finite number in (0, 1]: explicit voiced component, with gain independent of
  the existing noise-source gain. The recipe must contain a resonance pose with
  exactly the same phone and style. No neighboring vowel is implicitly borrowed.

At least one non-null voicing gain is required for canonical schema 5. Existing
bounded text, source spectra, resonance, count and JSON limits remain active.
Zero, negative, non-finite and greater-than-one gains reject. Unknown/missing
fields reject. Relabeling these bytes as an older schema rejects. Removing all
voicing gains deliberately selects the prior applicable schema; recipes without
voicing retain their original canonical bytes.

The optional gain participates in recipe equality and canonical resource hashing.
Freezing produces resource identity version 5, but `decodeVoiceRecipeResource`
still rejects that version during this contract-first implementation stage.
Runtime admission must not be enabled until articulation planning, voiced/noise
mixing, frozen-binding verification, candidate provenance and cancellation/seek
tests are connected. The current Designer cannot opt into this feature yet.

This is groundwork for U18–U20, not a new completed unit, phonetic inventory,
female singer qualification or source-use approval.

Verification: full Release build succeeded; Designer, voice-design, export and
core suites passed 4/4 in 29.34 seconds. Tests cover round-trip/canonical bytes,
required matching resonance, invalid gains, rejected downgrade and the temporary
runtime rejection boundary. No prior recovery-checkpoint paths were missing.

## Timed-gesture integration stage

Articulation plan revision 8 adds VoicedFrication with retained voicing gain.
Explicit bindings must agree with the token's voicing; a voiced binding cannot
silently render an unvoiced token. Vowel/nasal binding ambiguity is rejected.
The existing onset/coda ownership and nucleus timing checks remain active.

Frication stream revision 3 distinguishes noise-bearing from voiced gestures:
voiced frication belongs to both categories, unlike vowels/nasals. Its noise lane
now renders with the same deterministic source, envelopes, chunk and seek policy
as ordinary frication. Tests compare exact noise PCM and cropped replay across
different block sizes and reject mismatched voicing or invalid gain.

This stage does not yet mix the voiced component in ArticulatedStream. Runtime
resource version 5 admission remains closed, and no new candidate type or voiced
Designer preview is claimed. Subsequent work must bind the tract, apply voicing
gain, preserve both lanes across seeks, and carry voiced identity into candidates.
Final Release build and Designer/voice-design/export/core suites passed 4/4 in
29.73 seconds for this stage. Legacy recipe and candidate tests remain passing.

## Development-only mixed stream

ArticulatedStream revision 8 can render VoicedFrication when its direct `create`
call explicitly opts into development voiced-frication support. The ordinary
resource decoder and create-from-recipe paths still reject version 5 by default;
song and candidate rendering cannot silently flatten it into unvoiced metadata.

The stream checks both the same-phone/style resonance and frozen noise/voicing
binding. It retains continuous compiled phonation, mixes the noise lane with
voiced output scaled by the authored gain, and smooths gain changes over at most
5 ms between contiguous voiced gestures. Existing tract interpolation, owned-frame
envelopes, performance gain and transactional checkpoint behavior remain active.

Tests construct a voiced `z` onset and following vowel, verify gain-dependent PCM,
reject a changed recipe against an old plan, and compare exact full/cropped,
checkpoint, cancellation and different-block-size output. This demonstrates a
working mixed DSP path, not listener-approved `z` pronunciation or singer quality.
Candidate marker/provenance support and normal runtime admission are next; the
development opt-in is not a release or approval bypass.
Release build and Designer/voice-design/export/core suites passed 4/4 in 28.95
seconds for the mixed-stream stage. All prior checkpoint paths remained present.

## Recipe-based development preparation

The explicit development opt-in now propagates through `compileRecipe` and
`createFromRecipe`, validating recipe bindings and tract coverage before mixed
rendering. Default calls remain closed to version 5; this does not enable ordinary
song rendering or candidate export.

Regression coverage compares recipe-prepared onset PCM against the directly
prepared stream, rejects absent styles and cancellation, and renders a vowel–z
coda. Cropped replay inside the coda must match full output exactly. Changing only
the frication source seed must leave preceding vowel PCM unchanged while changing
coda PCM. These are engineering checks, not pronunciation or listening approval.
Release build and Designer/voice-design/export/core suites passed 4/4 in 28.78
seconds for recipe-based preparation and coda coverage.

## Rate/pitch replay matrix

A 48-combination regression exercises rates 22050/44100/48000/96000 Hz, MIDI
36/69/96, onset/coda placement and voicing gains 0.01/1.0, with noise gain 0.25.
It compares full output with independently blocked sequential rendering and a
cropped seek, requires finite/non-silent samples and checks the internal combined
DSP safety bound. That bound is not a mastering headroom or loudness target.

The same 5 kHz noise-source configuration rejects at 8 kHz; it is not silently
retuned below Nyquist. These tests verify the specified combinations on the local
runtime, not every pitch, cross-platform equivalence, measured F0 accuracy,
phonetic intelligibility or a qualified singer range.
Release build and voice-design/core suites passed 2/2 in 23.60 seconds.

## Marker transport stage

Procedural marker kinds now share the articulation gesture type. Projection
copies the exact kind rather than using a fallback that would label a newly
introduced kind as an oral vowel. The scheduler accepts VoicedFrication while
still rejecting unknown enum values, and retains its identity through task
completion and cache-hit marker delivery.

The existing candidate exporter explicitly refuses voiced-frication markers
before staging candidate audio. Candidate v1–v4 cannot represent this type; they
must not silently serialize it as oral-vowel or unvoiced frication. A versioned
candidate writer/loader remains required before ordinary v5 runtime admission.
The marker transport test uses synthetic task PCM and is not a singing-quality
or recipe-rendering test.
Release build and snapshot/export/core suites passed 3/3 in 27.40 seconds. The
new scheduler test verifies exact marker identity on completion and cache hit,
and rejection of unknown kind values; legacy snapshot/export regressions pass.

The subsequent [candidate-v5 loader](PROCEDURAL_CANDIDATE_V5.md) now validates
voiced gesture identity against this recipe. Export and normal runtime admission
remain pending. Voiced-frication recipe and planner bindings also reject vowel
and nasal phone identities, matching the candidate contract.
