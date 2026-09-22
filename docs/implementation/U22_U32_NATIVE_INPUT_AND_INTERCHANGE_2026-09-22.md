# U22/U32 native input and interchange integration

Date: 2026-09-22  
Baseline: `ed41968dbb4f6b46d30956130e77d225087b2d7f` plus the preserved native-import work.  
Status: integration verification in progress; not whole-unit or Beta acceptance.

## Why this batch

The finishing audit identified source PPQ copied into a 960-PPQ project, silent
conversion-report truncation, discarded MIDI controls without a loss notice,
stale unsaved/Save As decisions, and physical microphone failures replaced with
synthetic silence. This batch closes those production boundaries and connects
the existing standalone conversion review. It does not replace the singer,
language, embedded editor, host or release qualification still required.

## Implemented contracts

- MIDI retained positions normalize through one checked rational map, including
  shared note endpoints, tempo/meter/text positions and the region extent.
  Nearest-tick half-up rounding is disclosed in source coordinates. Distinct
  positions that collapse onto one tick, or normalized values beyond the budget,
  are refused before project identities are allocated. The returned source score
  retains its original PPQ and musical fields.
- Both explicit and running-status non-note MIDI channel events produce named
  loss records. Nondefault velocities/channels, unrepresented/unmatched/extra
  text and flattened multitrack structure are disclosed. Neutral velocity 100 /
  channel 1 remain roundtrip defaults, not newly supported expression controls.
- USTX diagnostics retain at most one overflow witness beyond the existing cap;
  parser/converter admission refuses that incomplete report. The common service
  checks the full incoming count before appending or writing any export file.
  Empty expression/override lists emitted by the encoder are not invented losses.
  Nonempty discarded metadata and unknown fields remain reportable.
- Affirmative unsaved, Save As, New/Open, external picker and conversion-review
  decisions are tied to their original document revision/identity. Cancellation
  preserves newer edits. The controller's own successful save may change identity;
  reentrant post-save notifications may not authorize replacing newer edits.
- A borrowed, read-only review model exposes all admitted rows, full Unicode
  selected details, source identity and an explicit unresolved-singer disclosure.
  The AppKit modal uses a lazy table and default Cancel; only explicit import
  accepts. Windows/unavailable implementations return Unsupported.
- Recording uses a reusable injected-device lifecycle. Physical input is never
  replaced with synthetic input; synthetic silence requires explicit test mode.
  Device opening is deferred until Record. A healthy completed capture remains
  pending across publication failure; invalid capture cannot be exported through
  the guarded path. Startup alone does not require microphone access.

## Failure evidence before repair

| Run | Result and meaning |
|---|---|
| Initial audit's three interchange targets | SMF 5 pass/1 fail, USTX 31/2, service 15/2. One service failure was an invalid unnamed test project, not yet the intended overflow proof. |
| Corrected service fixture | Exact-cap positive control passes; 4097-item import and overflowing USTX export both fail their expected-refusal assertions. `build/release/Testing/interchange-service-capacity-red.log`. |
| New MIDI timing tests before production repair | 5 pass/6 fail, including channel losses, 480/960/1920 timing, shared-endpoint quantization, collisions, normalized bounds and wide ticks. `interchange-timing-red.log`. |
| New lifecycle tests before production repair | 55 pass/2 fail: stale unsaved and Save As decisions. Six conversion-model cases pass separately. `interchange-lifecycle-red.log`. |

Two further MIDI controls and the later metadata-family cases were added after
the timing red checkpoint. The 2048-note empty-metadata regression came from
source cross-review after the cap repair; it is not claimed as an executed red
test against old source. Recording-input fault injection tests are new coverage,
not a physical microphone experiment.

## Integration verification

Pending final serialized builds/tests and independent review. Initial strict
Release compilation of the AppKit modal, model, standalone application and
lifecycle tests succeeded. Existing duplicate-library linker warnings remain.
No compiler warning policy or acceptance threshold was weakened.

The attempt to inspect the isolated native app through computer use returned
that the Mac was locked and automatic unlock failed. No interaction occurred.
The owned test app was terminated; that termination is not crash qualification.
Return/Escape, focus, small-screen/long-text scrolling and actual native import
acceptance remain runtime-unverified, not substituted by model tests.

## Remaining scope

Embedded-editor interchange and real OpenUtau/DAW exchange are still open.
Actual hardware recording/permission/disconnection behavior still needs a
device run. The new helper's injected failures cannot establish that evidence.
Windows remains the README TODO, not a waived platform. No complete U22/U30/
U31/U32, qualified singer, independent creator/listening acceptance, or Beta GO
is awarded by this batch. The full 48-unit/20-requirement goal remains active.
