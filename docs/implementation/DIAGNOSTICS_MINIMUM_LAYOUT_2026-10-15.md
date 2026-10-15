# Diagnostics at the minimum window — 2026-10-15

Engineering focused PASS; independent review pending. Full-suite baseline FAIL.
Alpha remains 0/20, U45 HELD and External Beta BLOCKED.

## Current full Release baseline

After `git fetch origin` and `git pull --ff-only`, source HEAD, origin/master and
the live remote agreed at `76896845c7128f427ec2e583a920eaa0fee69e7a`, tree
`2be7bf76e53c0712b53426b8d05dfa65dc725c11`. The checkout was clean; the existing
brand-fonts/tooltips stash was preserved. A full dependent Release build passed
in 25.973737 seconds. One serial full CTest invocation then completed in
806.584073 seconds with exit 8: **236/240 registered lanes passed; four failed**.

The failed lanes were character surfaces, shell input, and two registrations of
external-Beta Python discovery. They contain three native case failures and three
Python carrier subtest failures. The same Python directory ran twice: 443 tests
and the same three subtest failures in each execution. This is six distinct
failure observations, including subcases, rather than nine independent defects.
The native character case is a separate toast/playhead oracle investigation; the
Python failures are stale StyleBlend source citations. Neither is repaired here.

All **189 selected executing binary/script identities**, totalling 1,082,953,803
bytes, were unchanged before/after CTest. HEAD/tree/status were also unchanged.
The retained baseline includes command inventory, stdout/stderr, CTest logs, all
189 identity records, nine selected committed sources, and both failed native
executables. It does not retain every executing binary or every imported Python
module. These are local consistency records, not authenticated/hermetic build
provenance. A sampled broad playback test was in its timed device simulation;
the suite completed without interruption.

The baseline root is `out/evidence/release-suite-after-english-2026-10-15/`:
27 indexed artifacts, seven preserved pins; manifest 6,536 bytes, SHA-256
`652c13565fa81645b82168a9173ea7dee8325af5a03d4fbb0b4f075663bf051c`.
CTest percentages describe test lanes and are not project-completion percentages.

## Product defect and repair

In `libs/seam-native-ui/src/design/shell_overlays.cpp`, `DiagnosticsOverlay`
reserved 48 points above each 30-point title/impact row, followed by a 26-point
action row. Its independent pager needed another 34 points. At 480x320, the
shared shell slot above the diagnostics toast is only 122 points high. `perPage`
forced one issue to remain visible even when a separate pager would not fit.
The pager therefore overlapped recovery controls.

Pointer dispatch visits the recovery controls before the pager. A press aimed at
Next could activate recovery instead of changing the page. The unchanged page
fingerprint then allowed the next press too. This explains both observed native
failures without changing or relaxing the stale-frame guard.

When a paged panel cannot hold the separate footer, the recovery and pager
buttons now share one row. The full title/impact row stays above it. At most two
primary recovery actions are presented; the horizontal allocation reserves both
and both pager buttons. Pager buttons retain their ordinary 88-point widths at
the supported minimum window and can shrink within narrower embedding slots.
The minimum diagnostic panel height now accounts for a whole issue/action row.
Larger panels retain the separate pager and range label. Compact panels retain
the issue-count header and Previous/Next controls; the separate range text is
omitted because there is no free footer region for it.

Painting, accessibility and hit-testing consume the same returned rectangles.
Issue identity, action dispatch, page state, pointer ordering and drawn-content
fingerprinting are unchanged. Elided labels retain their full accessible names
and the existing hover-tooltip path.

## Verification

The existing recovery-action regression now checks every visible issue row,
every primary action, both pager controls, containment, disjoint rectangles and
minimum 26-point control dimensions on every reached page. It visits 480x320,
480x351, 480x352, 720x320 and 1600x900, including both sides of the 154-point panel
transition. Every diagnostic's actions still traverse the host callback. The
existing immediate second-pager-press regression remains unchanged.

A full dependent Release rebuild passed in **7.839058 seconds**. One final CTest
invocation passed in **36.670285 seconds**:

| Lane | Native cases passed |
| --- | ---: |
| Shell input | 109 |
| Overlay layout properties | 1 |
| Pseudo-localized layout properties | 1 |
| CLAP design shell | 12 |
| Total | 123 |

Fifteen actual raster frames were retained through the optional
`SEAM_DIAGNOSTICS_PREVIEW_DIR` test capture hook. The 480x320 bank-missing page was
also converted from PPM to PNG and visually inspected. This is native raster
evidence, not a real DAW, human usability or screen-reader acceptance session.
The narrower direct embedding slots are protected by the allocation calculation
but were not separately exercised by this viewport test.

The focused root is `out/evidence/diagnostics-minimum-layout-2026-10-15/`:
30 artifacts, eight preserved pins; manifest 6,964 bytes, SHA-256
`54a4dda3cf7e4a0cbdc96ddbada32ce16c25b11f2c4195140e4d04aa91fe47fb`.
It retains both changed source files, the executing three final binaries,
source/binary-bound build and test receipts, all frames and the read-only
verifier. That verifier passed retained hash, source, baseline and count checks.
Only selected source/binary inputs are bound; no full transitive-build claim is
made. Full-suite verification after this repair is still NOT_RUN.

## Next work and release boundary

Resolve the toast/playhead test using measured geometry and pixel differences,
refresh the capability matrix against actual refusal paths, and rerun the full
Release suite after those coherent units. The duplicate Python registrations
remain visible verification-cost debt. Independent review of this exact repair
is pending; a prior unit's approval does not approve it.

This unit downloads or admits no audio. The retained female originals remain a
source-development route, requiring owner rights policy, genuine producer and
distinct reviewer, phonetic/pitch/style coverage and quality/listening decisions.
Read speech cannot establish singing quality or AE1/R3 original-singer acceptance.
Signed immutable application/candidate, platform/DAW, acoustic, neural/classical,
multilingual, R9 and human release requirements remain open. No canonical gate,
public license, Windows README TODO or .github file changed.
