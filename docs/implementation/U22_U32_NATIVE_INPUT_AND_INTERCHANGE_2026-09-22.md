# U22/U32 native input and interchange integration

Date: 2026-09-22  
Baseline: `ed41968dbb4f6b46d30956130e77d225087b2d7f` plus the preserved native-import work.  
Status: partial standalone native runtime verified on 2026-09-24; not whole-unit or Beta acceptance.

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

The first attempt to inspect the isolated native app through computer use
returned that the Mac was locked and automatic unlock failed. No interaction
occurred in that attempt. The owned test app was terminated; that termination
is not crash qualification. The follow-up below supersedes only the native
actions it actually exercised.

## 2026-09-24 native runtime follow-up

On a fresh Release `Project SEAM.app`, the native File menu opened the pinned
OpenUtau 0.9 USTX fixture through the normal file picker. Its review showed the
source SHA-256 `9eb42f6e654249cc1a15e1f70244534683fc94e7897e1c30e69c749995c4fe51`,
one vocal track/region, two notes, nine losses, zero warnings, and an explicit
unresolved-singer disclosure. Selecting a long pitch-approximation loss exposed
its complete location and message. Cancel left the initial Untitled document
with zero notes. Explicit **Import With Losses** created an unsaved document
with two notes, 21 pitch points, and the unresolved singer; no automatic singer
substitution was observed. Return and Escape now cancel immediately even when
a read-only report text view owns focus.

The report layout was tightened after visual inspection: duplicate singer text
was removed, all nine fixture losses are visible at once on this Mac, and the
horizontal scrollbar no longer covers the final row. Selected full details
remain scrollable and accessible. This is one observed display size, not a
small-screen or long-report sign-off.

The same native app's **Export Score** picker initially suggested a duplicated
`.ustx.ustx` suffix. Setting allowed content types before the suggested name
fixed the observed AppKit behavior; the controller also replaces an existing
`.ustx`/`.mid`/`.midi` project-name suffix rather than adding another one. The
corrected picker exported the imported score to
`/tmp/seam-u32-native-roundtrip-20260924.ustx` (2,317 bytes, SHA-256
`c152a009e1bcf0dd3235888f0e702df2774e5c84ad1b40a2183f20af5932359f`).
The source fixture retained its original SHA-256. The output contains two
notes, time signatures, tempos, and a pitch contour; this is a local native
export observation, not an OpenUtau re-import or musical parity claim.

The File menu now presents returned import/export failures instead of silently
dropping them. A malformed `/tmp/seam-u32-malformed-20260924.ustx` opened
through the native picker produced a **Could not open external project** sheet
with the parser error. Dismissing the sheet returned to the unchanged Untitled
document with zero notes. The export-collision refusal remains service-tested,
but its complete native confirmation/error sequence was not exercised here.

Post-change Release build of `seam_editor_native` and `seam_tests` passed.
Focused `seam_conversion_review_tests` and aggregate `seam_tests` passed
(`ctest`, 2/2 targets); the earlier interchange service, standalone and
project-lifecycle target selection passed 5/5. The remaining native U32
acceptance still includes small-screen/long-report review, save/discard/cancel
with an existing user document, embedded-editor behavior, export collisions,
and DAW/OpenUtau round trips.
After the File-menu error-alert change, the Release aggregate and standalone
targets passed again (`ctest`, 2/2).

## 2026-09-24 embedded interchange follow-up

The macOS CLAP editor now treats the file chooser and review dialog as
reentrant host boundaries. An import captures the current document identity,
revision and ID allocator before opening the chooser; after path selection,
conversion and explicit review, it refuses a stale decision. The final check
and replacement run under one editor lock. Export likewise refuses to write a
path selected for a document that changed while its picker was open. This is a
retryable conflict, not implicit permission to replace or export a different
song. Uppercase `.USTX`/`.MID`/`.MIDI` suffixes are recognized.

Every embedded import now requires a review handoff, including conversion with
zero reported losses. The shared AppKit review discloses that import replaces
the current SEAM song with an unsaved document, clears its Undo route, and
warns the user to save the current song or host session first. A missing review
surface fails closed; a cancelled chooser or declined review leaves the song
untouched. Embedded Command-Shift-O/E failures are reported through a native
alert instead of being discarded by the key handler. The alert is dispatched
on the AppKit main thread when a host delivers the key event elsewhere.

Evidence: new embedded regressions cover stale picker/review approvals,
zero-loss review, stale export, uppercase suffixes, and keyboard error versus
cancellation. A deliberately lossless USTX fixture first failed against the
old implicit-acceptance code, then passed after review became mandatory.
Release builds of `seam_clap_microscope_tests`, `seam_clap_editor_plugin` and
`seam_editor_native` passed; eight focused local CTest targets (embedded host,
conversion review, SMF, USTX, interchange service, microscope, standalone and
CLAP plugin host smoke) passed. This is not a GUI-in-DAW acceptance run.
The rebuilt Release aggregate `seam_tests` also passed (one CTest target,
26.81 seconds). Existing duplicate-library linker warnings remain.

## 2026-09-24 embedded host-state follow-up

The CLAP editor had a working state serializer but did not tell a host when
non-parameter song state changed. The pinned, consolidated CLAP 1.2.10 header
now includes the official `clap_host_state_t.mark_dirty` ABI. The plugin asks
the host for a main-thread callback when a persistent edit changes the editor
revision, then calls `mark_dirty` once for that revision. Its GUI timer is a
fallback when the callback is delayed. The bounce-timing choice is stored
directly in project settings without advancing the document revision; it now
signals the same path explicitly. Loading host-owned state establishes a new
baseline; a state save does not clear a pending dirty signal, because the host
may be making a duplicate or preset snapshot rather than saving its project.

An opt-in local host probe uses the real macOS plugin window and its public
accessibility controls; it does not reach into the plugin runtime. Before the
repair, a tempo edit was serialized but the host's dirty count remained zero.
After repair, the probe showed one callback-driven dirty notification for a
tempo edit, no duplicate from the timer, one timer-driven notification for a
second tempo edit, and one notification for the persisted bounce setting.
The saved state retained the final 138 BPM and Follow Host setting, and a
separate plugin instance loaded and re-saved identical state without a false
dirty notification. Repeat locally with:

```sh
cmake --build build/release --target seam_clap_editor_host seam_clap_editor_plugin -j 8
build/release/seam_clap_editor_host --plugin build/release/ProjectSEAMEditor.clap --state-dirty-probe
```

The reported result was `edited=1 callback=1 marked=1 once=1 timer=1
bounce=1/1 tempo=1 reopened=1`. This is stronger than a serializer unit test
but remains a synthetic CLAP host, not evidence of FL Studio/REAPER/Bitwig
save behavior or of importing through a DAW's file dialog. The user deferred
GitHub CI work; this opt-in probe is not added to CI configuration.
After this repair, Release CTest passed the offline host, embedded microscope,
aggregate `seam_tests` (36.87 seconds), and plugin host smoke targets (4/4).

Remaining embedded U32 evidence: exercise the real plugin modal flow in a DAW,
verify a host save/reopen and unsaved-project decision, visually inspect the
warning and error alerts at constrained sizes, and roundtrip a score through
OpenUtau. Windows native review and alerts remain the README TODO. No CI run
or configuration change was made in this batch.

## Remaining scope

Embedded-editor lifecycle in a real DAW and real OpenUtau/DAW exchange are
still open.
Actual hardware recording/permission/disconnection behavior still needs a
device run. The new helper's injected failures cannot establish that evidence.
Windows remains the README TODO, not a waived platform. No complete U22/U30/
U31/U32, qualified singer, independent creator/listening acceptance, or Beta GO
is awarded by this batch. The full 48-unit/20-requirement goal remains active.
