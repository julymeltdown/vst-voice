# Nasal voice coloration: implementation and evidence

## Result and scope

The procedural singer now has an explicit, editable nasal resonance/antiresonance
stage, connected to saved recipes, streamed rendering and native Voice Designer.
This advances U19/R3/R6/R15 by changing generated audio, not just metadata.
It does not complete U19, synthesize nasal consonants, establish intelligibility,
or qualify a female singer. All Full-Scope Beta GO requirements remain mandatory.

Previously every nonzero nasal coupling failed at `VocalTract::create`. The new
schema-3 model supplies the missing spectral parameters explicitly. Legacy
coupling-only data remains unsupported rather than acquiring guessed behavior.
See [the format and DSP contract](../formats/VOICE_RECIPE_V3.md).

## Implementation

- Recipe validation/codecs persist bounded, explicit resonance and antiresonance
  frequencies/bandwidths and preserve schema-1/2 canonical bytes when no model is
  present. Frozen version 3 requires exact payload/version agreement.
- The tract combines the old oral output with a nasal resonance branch and a
  spectral notch. Coupling zero is bit-identical to the oral-only path. Independent
  bank crossfades, copies, reset and transactional block processing include the
  new filter memories. No coefficients are interpolated across stability boundaries.
- Sustained and articulated renderer revisions were raised to 7 and 2. Existing
  render identities include these revisions and the full recipe hash.
- Five native controls use the existing numeric/drag/undo mechanism. Changes
  invalidate the current audition while retaining an independently pinned reference.
  Saved recipes reopen through the same bounded loader and renderer.

## Focused verification

Full Release build passed. Five focused suites passed in **26.97 seconds**:
Voice Designer **24/24**, voice design **16/16**, performance snapshot **42/42**,
export **23/23**, and core **840/840**. The voice-design suite is separate from
the core executable; its cases must not be added to the core count implicitly.

The subsequent full Release CTest run was **119/120 PASS, 250.05 seconds**.
Only `seam_tracked_source_closure` failed: 378 required inputs were unindexed at
that execution. Later evidence files may change the count. No files were staged
to conceal this release-provenance failure. Raw full/focused runs and the native
before/after recipe files are retained in `evidence/nasal-voice-design-2026-09-09/`.

New tests prove schema/hash roundtrip, malformed model/version rejection,
zero-coupling exact bypass, measurable resonance gain and notch attenuation,
preserved periodic input pitch, full-vs-chunk equality, nasal pose transitions,
checkpoint copies, reset, cancellation/invalid-block rollback, supported-rate
parameter extremes, actual sustained rendering after file reload, and Designer
edit/audition/undo/redo/reopen behavior.

The first notch test included the sine fixture's 128-frame fade-out and failed its
60 dB attenuation threshold. Inspection confirmed that this measurement included
transient spectral energy. The measurement was corrected to a settled,
integer-cycle interval; the threshold was not relaxed. Snapshot/export tests
already passed in that first run. This is an engineering spectral test, not a
listener judgment about naturalness.

## Live macOS check

The actual 960 x 640 Studio app loaded an oral schema-1 recipe from
`/tmp/seam-nasal-designer.TnKJGZ/oral.json`, rendered it and pinned reference A.
The new parameter page was visually inspected. Setting nasal coupling to 0.8
through its accessibility control enabled the explicit model, marked the draft
dirty, invalidated current B and preserved reference A. Rendering B reached
`Vowel ready`. Save As wrote a separate schema-3 `nasal.json` with the exact
coupling and 250/90/1000/120 Hz parameters. No old recipe file was replaced.

- Original file SHA-256:
  `90d9d0c44dce87e7a05e7e0fa16c254626ae50a9f15cd95e8ce7b0215420f709`.
- Native-saved nasal file SHA-256:
  `bc69451674566a102408be249a73d8de547380539da420f4d47fcd1d04a4216f`.
- Studio binary SHA-256:
  `16d07382c4cf3ed90c6713ab6842bd64a8a59918f27ad9d54d5631f4d7a02184`.

The app closed normally with exit 0, synthetic input, zero input callbacks and
zero recorded frames. The source fixture remained generation 2 with zero approved
units. Audition rendering was checked; no listening-quality approval is claimed.
Native Windows interaction remains unverified.

## Preservation and remaining work

No captured file was missing relative to `session-preservation-Z7xwpb` (1,935
files). The ten modified prior paths matched this increment. Existing dirty work
was preserved; there was no staging, commit, push, resource approval or release.

Remaining voice work includes true nasal-consonant articulation, closure/burst
generation, extended phonation context, calibrated poses/styles and independent
acoustic/native-speaker listening evidence. Classical/neural singing, producer
assignment/QC, host qualification and the full release gate remain unfinished.
