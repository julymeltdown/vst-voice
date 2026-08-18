# Usable Alpha U1.3 Implementation Report

## Scope

U1.3 extracts the production rendering lifecycle from the CLAP-specific
`AsyncPreviewRenderService` into the shared `seam-authoring-runtime` module.
The work does not claim that the standalone application is usable yet.

## Added shared components

- `AuthoringRenderCoordinator`
- `RealtimeProjectAudioPublication`
- `PublishedProjectAudio`
- `RenderProgress`
- `RenderCoordinatorStats`
- `RenderFailureKind`

## Behavior

```text
Canonical Project Snapshot
+ Exact Track Voicebank Sources
+ Active Track / Region
+ Revision / Sample Rate / Quality
        │
        ▼
Latest-only Background Worker
        │
        ├── Project and Voicebank Preflight
        ├── ProductionProjectRenderer
        ├── Existing PcmCache
        ├── Cancellation / Stale Gate
        └── Bounded Realtime Publication
```

Failure publications contain no PCM and carry an explicit diagnostic. They
never substitute another Voicebank. Preview and Final quality remain distinct
cache identities. Character display state remains outside audio identity.

## Lifecycle defect found during TDD

A normal allocator build exposed heap corruption while the ASan build passed.
The root cause was render-worker lifetime: `std::jthread` was destroyed after
publication, callback, and progress members because it was declared before
them, and the destructor requested stop without explicitly joining. A worker
could therefore access already-destroyed coordinator state.

The fix adds an idempotent `shutdown()` that clears the completion callback,
requests cancellation, requests worker stop, notifies the condition variable,
and joins the worker before member teardown. The worker is also declared last
so its normal destruction order is safe. A regression test blocks an active
worker, calls `shutdown()`, and verifies the worker has exited before the
coordinator can be destroyed.

## Adapter compatibility

`AsyncPreviewRenderService` continues to expose the existing Phase 11/12 API
and maps shared failure states to existing `PreviewStatus` values. It also
retains the stereo compatibility view while publishing the full interleaved
1–8 channel result.

## Verified tests

- shared coordinator tests;
- authoring-runtime characterization tests;
- full named C++ suite;
- Phase 12A and 12B direct tests and contracts;
- ASan + UBSan focused suite;
- ThreadSanitizer focused suite.

## Remaining boundary

The standalone application still uses the demo project and sine-wave timeline.
U1.4 and the remaining U1 tasks must be completed before the shared authoring
runtime can replace that demo shell.
