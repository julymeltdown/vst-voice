# Phase 4.1 Acceptance Criteria

## Render identity and cache correctness

- [x] Content identity uses SHA-256 rather than the legacy diagnostic FNV hash.
- [x] Selected WAV bytes are hashed and included.
- [x] Only selected Unit metadata is included; unrelated Unit edits do not invalidate the Phrase.
- [x] Effective renderer, Pitch Curve, fallback policy, and default Seam settings are included.
- [x] Phonemizer, selector, timing, renderer, seam, and cache-format revisions are generated from CMake.
- [x] Snapshot owns the exact phoneme plan, Unit plan, and decoded selected audio consumed by rendering.
- [x] PCM cache version is incremented and validates total file size before allocation.

## Persistence and hostile-input handling

- [x] Project JSON uses durable atomic replacement and `.bak` generation.
- [x] Voicebank Manifest uses durable atomic replacement and `.bak` generation.
- [x] PCM cache uses atomic replacement without canonical-data backup.
- [x] Fault injection covers pre-replacement and post-replacement failure semantics.
- [x] JSON preserves exact signed 64-bit integers.
- [x] JSON supports valid surrogate pairs and rejects lone surrogates.
- [x] JSON input bytes, depth, nodes, string bytes, and collection entries are bounded.
- [x] Project and Manifest file sizes are bounded.
- [x] Voicebank assets cannot escape through absolute, parent, symbolic-link, or canonical paths.

## DSP correctness

- [x] SpectralClassic preserves recorded source through `stableStart`.
- [x] Stretch preserves recorded source through `stableStart`.
- [x] Both renderers keep actual `vowelOnsetOffset` for note alignment.
- [x] Phase-alignment offset remains consistent after overlap.
- [x] Equal-power crossfade uses a true equal-power gain law.
- [x] Regression tests cover the source transition and overlap-exit derivative.

## Playback and scheduler concurrency

- [x] UI transport operations use a bounded SPSC command queue.
- [x] Only the feeder thread mutates timeline, loop, playhead, and playing state.
- [x] Published playhead, playing state, and statistics are atomic.
- [x] Producer never mutates the ring consumer read index.
- [x] Consumer-owned reset epoch discards stale audio and acknowledges completion.
- [x] Threaded transport stress is covered by ThreadSanitizer.
- [x] Scheduler rechecks latest revision after cache publication.
- [x] Scheduler rechecks latest revision before completion publication.

## Editor transactions

- [x] Composite apply failure restores the complete prior Project.
- [x] Composite revert failure restores the complete prior Project.
- [x] EditorSession execute validation failure restores the Project.
- [x] Undo and redo failures restore the Project and safely invalidate affected history.

## Verification

- [x] 84 named tests pass.
- [x] Debug CTest passes with warnings as errors.
- [x] Release CTest passes with warnings as errors.
- [x] AddressSanitizer + UndefinedBehaviorSanitizer CTest passes.
- [x] The complete 84-test named suite passes under ThreadSanitizer; full TSan CTest capture is recorded separately and is non-gating.
- [x] Concurrent test processes use process-scoped temporary directories and do not corrupt one another's fixtures.
- [x] Phase 2, Phase 3, and Phase 4 demos still pass.
- [x] Master-only policy passes.
- [x] Dependency-license audit passes.
- [x] `git fsck --full` passes.
- [ ] Final distributed ZIP is extracted, rebuilt, and retested. This item is completed in the external package-verification record after the ZIP is produced.

## Explicitly outside Phase 4.1

- [ ] Native iPlug2 + Skia editor window.
- [ ] Windows TSF and macOS native IME adapters.
- [ ] Physical audio-device adapter and native long-running feeder thread.
- [ ] Complete graphical Voicebank Studio and microphone recording transport.
- [ ] True multichannel project routing.
- [ ] Contracted human-recorded official voicebank.
- [ ] Signed `.seambank` package installation.
- [ ] CLAP, VST3, and AU targets.
