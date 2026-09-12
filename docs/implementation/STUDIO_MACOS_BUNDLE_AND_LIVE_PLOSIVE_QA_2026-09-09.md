# Studio macOS bundle and live plosive verification

Studio's macOS target now produces `SEAM Voicebank Studio.app`, with its own
`com.project-seam.voicebank-studio` identity, version/build provenance, music
category, high-resolution flag and microphone purpose string. Previously it was
a bare executable. The app bundle was successfully resolved by the same UI tool
that could not attach to the bare executable in earlier tests.

This is a development bundle, not a signed/notarized release. Launch still
requires an explicit manifest or producer workspace; a Finder-first onboarding
flow is not supplied by this packaging change. Windows/Linux target behavior is
unchanged. CMake GUI tests already use TARGET_FILE and follow the new location;
the Linux-only Xvfb evidence script retains its non-bundle path.

## Live macOS evidence

Launched the newly built bundle against the generated synthetic voicebank with
`--force-synthetic-input`. All actions below were dispatched through native UI
automation, not direct recipe mutation:

1. Opened Designer and created a temporary draft.
2. Activated Add plosive source; the actual dialog named the operation and
   explained that p/t/k and an existing style are required.
3. Created k/neutral and paged to its four editable source rows.
4. Set duration from 10 to 22 ms through the native numeric value action.
5. Activated Render selected plosive; Play became enabled and status became
   Plosive source ready.
6. Activated Play; the application reported active playback with the explicit
   source-only/50 ms closure/unapproved label. Stopped playback.
7. Undid the edit: duration returned to 10 ms and preview readiness disappeared.
   Redid it: duration returned to 22 ms.
8. Applied seed 18446744073709551615 through the native seed dialog.
9. Saved through the native file panel to an isolated build QA directory.
   The application reported Saved. Independently inspected the resulting JSON:
   schema 4, k/neutral, 22 ms, exact full-width source seed and unchanged global
   seed 0. The retained file is under the evidence directory below.
10. Closed the saved test session. Although the UI observation timed out during
    close, the original process handle confirmed normal exit 0. Its counters
    reported physical input false, zero input callbacks and zero recorded frames.

This proves focused creation/edit/render/play dispatch/history/save behavior.
It does not prove acoustic device-loopback fidelity, listener recognition,
native removal/reopen behavior, every shortcut, or Windows/Linux UI behavior.

## Visual finding and release boundary

The actual screenshot showed no overlap among the displayed burst/nasal rows,
but text was very small with most of the large window unused. The Designer needs
a dedicated typography/density/layout improvement; functioning accessibility
actions do not establish a production-quality visual interface.

Full Release build passed; generated Info.plist passed plutil validation.
Designer and aggregate-core CTest suites passed in 21.97 seconds. This was not a
fresh full-suite run. Evidence is retained under
`evidence/studio-bundle-live-2026-09-09/`.

No whole roadmap unit, release provenance, signing, source rights, musical
qualification or Beta GO acceptance is claimed. Existing dirty source remains
preserved; no staging, commit or push occurred.
