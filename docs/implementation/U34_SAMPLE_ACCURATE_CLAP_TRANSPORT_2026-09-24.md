# U34 — Sample-Accurate CLAP Transport Capture

Status: implementation slice complete; external DAW qualification remains open.

## Problem

CLAP hosts can report transport at the beginning of a process block and can send
`CLAP_EVENT_TRANSPORT` updates at sample offsets inside that block. SEAM previously
published only the block-start structure through a latest-value snapshot. Multiple
reports could overwrite each other before the editor-owner thread drained them, so a
tempo change inside a block could be absent from the frozen Follow Host map.

## Change

- Replaced the latest-value transport publication with a bounded, allocation-free SPSC
  queue (4,096 observations). The audio callback writes; the owner thread drains in order.
- The block-start transport keeps the existing forwarding quantum. In-block transport
  events are forced into the queue so an event is not suppressed as a repeated position.
- Realtime score-preview playback now uses a block-local cursor: each sample maps from the
  latest seconds-timeline anchor at or before that sample. The cursor is fixed-state and
  allocation-free. A malformed in-block transport update silences score preview from its
  offset until a later valid anchor, without disabling live-input voice rendering.
- Queue overflow produces an explicit incomplete-capture marker. `HostTimelineCapture`
  refuses to freeze such a capture; it does not silently treat the retained prefix as the
  complete map.
- Exceeding the bounded CLAP input-event count no longer drops an unprocessed transport
  suffix and continues score audio on the block-start clock. It publishes incomplete
  history, silences realtime score mapping for the block, and fails an offline block closed
  while invalidating subsequent use of that prepared authority. During active realtime
  playback, the muted overflow block returns `CLAP_PROCESS_CONTINUE` so a host does not
  sleep before the next valid transport block arrives.
- CLAP `tempo_inc` is retained as a tempo-ramp indicator. Piecewise-constant Follow Host
  maps currently reject non-zero/invalid ramps rather than approximating them as steps.
- If an in-block transport update or unsupported ramp arrives during an offline bounce,
  the affected block returns `CLAP_PROCESS_ERROR` with cleared output, and subsequent
  offline processing stays rejected until a successful Final preparation rebinds it.
- Malformed in-block transport events are treated as incomplete history.

## Verification

Executed on the macOS Release build with the CLAP editor plugin enabled:

```text
cmake --build build-u4-macos --target seam_clap_editor_host seam_clap_editor_plugin -j4
ctest --test-dir build-u4-macos -R '^(seam_host_(transport_publication|timeline_capture)_tests|seam_clap_offline_host_tests)$' --output-on-failure
100% tests passed, 0 tests failed (3/3)

ctest --test-dir build-u4-macos -R '^seam_tests$' --output-on-failure
100% tests passed, 0 tests failed (1/1)
```

Focused coverage includes ordered SPSC delivery, queue saturation and incomplete-history
signaling, refusal of incomplete/ramped captures, frame mapping before/at/after a seconds
anchor, malformed-anchor silence and recovery, and a loaded-plugin offline event-list
boundary probe at 1,024/1,025 events. The exact-limit transport event and overflowed seek
both clear the offline block; overflow also keeps subsequent offline use rejected until a
successful Final rebind. The realtime overflow fixture models CLAP's process-status
contract by attempting the recovery block only when the muted block returns CONTINUE; it
verifies subsequent audible score output. These are engineering fixtures, not proof against
every commercial DAW implementation.

## Remaining qualification

- Test transport update cadence, loop reporting, tempo-ramp semantics, and owner-thread
  callback behavior in each supported commercial DAW.
- Add exact host/version/plugin tuple evidence before claiming Follow Host qualification.
- Implement exact ramp integration if ramping hosts must be supported; otherwise surface
  the current explicit unsupported diagnostic in the user workflow.
- The loaded-plugin fixture does not yet compare realtime in-block score samples against a
  commercial DAW's transport output. Beat-only continuity and tempo-ramp playback are not
  qualified.
- GitHub Actions/CI work is intentionally deferred per user direction; no `.github` files
  were changed in this milestone.
