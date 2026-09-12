# Strict measured-audio level backend

Status: PCM backend, source/session guards, worker and native rendered-output view are implemented. See `MEASURED_DYNAMICS_INSPECTOR_2026-09-08.md` for the connected UI checkpoint. Live playback/host qualification and full U25 acceptance remain open; the full Beta GO scope is unchanged.

## Contract

`rendering::measureAudioLevels` analyzes an explicitly selected window of interleaved floating-point PCM. It returns absolute buffer-frame offsets/counts and per-channel RMS, sample peak and the number of samples at or above digital full scale. It never downmixes, normalizes, clips, resamples or substitutes values for invalid samples. The existing source waveform preview sanitizes nonfinite values, so it is intentionally not used as the authoritative measurement path.

- Complete frames and 1–64 channels are required.
- The requested nonempty window must fit inside the buffer. Samples outside the window are not part of the measurement or its validity claim.
- Work is bounded to 16,777,216 scalar samples per window and 1–4,096 output bins. Excess work rejects with a smaller-window diagnostic, not a partial trusted result.
- Bins partition all requested frames exactly, including nonuniform final divisions. RMS denominators use each bin's actual frame count and never combine channel phase.
- Double-precision accumulation preserves floating-point headroom and safely handles finite float extrema within the work budget.
- A nonfinite sample in the requested window rejects the result; no partial measurement is returned. Stop requests are checked at entry, between bins, every 4,096 visited samples and before return. Tests verify pre-cancellation, not a timing guarantee for in-flight cancellation.
- `rmsDbfs()` returns 20×log10(RMS). Digital silence is absent rather than an invented finite display-floor value. Peak is sample peak, not oversampled true peak; full-scale counts do not prove audible clipping/distortion. These are not LUFS or perceptual loudness measurements.

The API borrows an immutable PCM span for the synchronous call and allocates bounded result storage. It must not run on the real-time audio callback. The result carries frame units, not an inferred score-tick mapping or fabricated render provenance.

## Source and tests

- `libs/seam-rendering/include/seam/rendering/audio_level_envelope.hpp`
- `libs/seam-rendering/src/audio_level_envelope.cpp`
- `tests/test_audio_level_envelope.cpp`
- Existing native dynamics Final-render regression in `tests/test_export_service.cpp` now uses this backend.

Three focused cases verify anti-phase stereo without cancellation, digital silence, float headroom/extrema, exact frame partitioning and weighted energy, invalid shapes/ranges, nonfinite samples, pre-cancellation and the work budget. The integration case measures actual procedural Final-render PCM: a native gain edit to 0.25 produces quarter RMS and peak in every bin within explicit tolerance, while reload reproduces the complete measured envelope exactly. The associated export regression still verifies exact Float32 master/stem PCM.

Release/Debug measurement and export builds pass. Three measurement cases pass Release/Debug (0.42/0.45 s); 22 export cases pass Release/Debug (3.55/10.74 s). All 626 Release core cases pass (14.91 s). `git diff --check` passes.

## Integration work identified at the backend checkpoint

Bind measurement requests/results to the originating document generation, render request/source PCM, sample rate, frame origin, quality and stream role. Reject stale publication and distinguish project mix/stem levels from voice controls. Run analysis outside paint/audio callbacks and present measured amplitude with its own explicit units rather than pretending RMS is the editable dynamics-gain curve. No measured trace has been added to the inspector yet. Live audio/host, F0 measurement, perceptual listening and release qualification remain open.

Changes are local/uncommitted. No U25 or Beta GO acceptance is claimed.

## Render-publication identity checkpoint

Production coordinator publications now carry the originating project ID, request ID and a coordinator-scoped shared identity token. Ready and failure publications both receive this metadata. Request IDs do not wrap: exhaustion invalidates current-source eligibility and stops pending work rather than recycling an identity.

`AuthoringRenderCoordinator::acquireCurrent` exposes only ready audio from its current submitted request. Existing `acquire` retains its playback semantics, including older ready PCM after cancellation/failure. `matchesCurrent` checks the coordinator token, project/request/revision, render quality, sample rate, channel count and immutable PCM storage identity. Copy-on-write sample mutation changes the storage identity and therefore cannot masquerade as the captured source.

These are in-process, point-in-time source checks, not cryptographic provenance or a lock held over future work. A measurement job must retain its captured source and revalidate it at publication/display time. Current submitted render identity is also not a substitute for the owning editor session's document generation: same-file reloads still require the separate session-context guard. Frame origin, stream role, off-thread measurement and inspector binding remain integration work; this checkpoint adds no measured UI trace.

A new coordinator regression renders real fixture audio and verifies same-revision resubmission, equal project/request numbers in another coordinator, copied PCM mutation, altered rate/quality, cancellation and failed-request retention. The last two cases keep old playback available while rejecting it as current measurement evidence. Existing direct-render parity also checks the new metadata.

Release native app/core/coordinator and Debug coordinator builds pass. All 627 Release core cases pass (13.82 s); 17 coordinator cases pass Release/Debug (1.64/5.88 s). `git diff --check` passes. Changes remain local/uncommitted; U25/Beta GO remain incomplete.

## Document-bound capture and analysis worker

Ready coordinator publications now retain an immutable copy of the actual rendered project input. Failure publications do not allocate this extra snapshot. PCM and project snapshots are shared when a measurement capture is copied; no extra PCM copy is required.

`AudioMeasurementCapture::prepare` requires a current ready render whose project ID, revision and complete input snapshot match the editor session, then captures the session's generation guard. `matches` revalidates the render source, session revision/generation and full project contents. Full-snapshot equality is intentionally conservative, including metadata-only changes; it does not claim semantic equivalence of different project snapshots. A new explicit capture may bind identical retained current audio to a new session, but a capture from the old session cannot be reused as current authority. An incremented revision needs a matching render.

The capture's read-only measurement method can continue analyzing retained immutable PCM after the document changes; that success is not publication approval. Closing the capture invalidates its owner-side match without racing the analysis copy. This separates data lifetime from permission to display a result as current.

`AudioMeasurementJob` adds a single analysis worker and owner-thread start/poll/cancel/current lifecycle. The worker owns a capture copy and calls only the bounded measurement backend; it does not access the live session, coordinator or UI. Completed results are adopted only after owner-side revalidation. `current` rechecks validity even after adoption. Cancellation discards a result that completed before cancellation but has not been adopted. A previous worker must retire before another starts; destruction joins it before worker-visible state is destroyed. Exceptions and measurement failures are returned without preserving an old ready result.

Two regressions render actual fixture PCM and check capture/re-capture, closed captures, same-data new sessions with equal revisions, same-session replacement with incremented revision, direct mute changes without revision updates, source cancellation, worker retirement, ready/in-flight staleness, cancellation-before-adoption and invalid-window failure. The initial replacement test incorrectly assumed `replaceProject` preserved revision; source inspection showed that it increments revision, so the final tests explicitly cover both lifecycle cases rather than weakening the guard.

Release native app/core/coordinator and Debug coordinator builds pass. All 629 Release core cases pass (13.54 s); 19 coordinator cases pass Release/Debug (1.81/6.37 s). `git diff --check` passes.

The worker is not yet connected to a measured inspector trace. Frame-origin/stream-role presentation, viewport request replacement, controller lifecycle wiring and live host qualification remain open. Source validity alone does not assert that a result's frame window matches a newly selected viewport; the UI adapter must also check/cancel its window request. Changes remain local/uncommitted; U25/Beta GO remain incomplete.
