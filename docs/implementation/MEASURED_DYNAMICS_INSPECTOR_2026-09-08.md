# Native measured-output dynamics view

U25 now connects the measured-audio worker to the dynamics inspector. This is rendered-output inspection, not a live device meter, microphone trace, isolated singer stem, F0 measurement or Beta GO approval.

## User workflow

The fourth plot-navigation button enters Measure mode, cycles through the rendered output channels, then returns to control curves. Keyboard/accessibility activation preserves focus across channel changes. Unbound controller fixtures show the control disabled; standalone and embedded runtime adapters bind their authoring render coordinators.

Measure mode uses a separate read-only RMS dBFS plot rather than drawing amplitude on the editable linear-gain scale. Native point fields remain available, but no editable handles appear on the measured graph. The caption identifies output channel, render quality and revision. Window RMS, sample peak and at/above-full-scale sample count are shown numerically and exposed to accessibility. Full-scale counts do not prove audible clipping. Silence and values below −96 dBFS appear at the explicitly labeled floor; the upper display bound expands above 0 dBFS when needed. These are binned RMS values, not LUFS or true peak.

Measurements represent committed rendered PCM. Unsaved point forms and staged native curves do not alter them. Changes requiring a new matching render display a render-current-document message instead of reusing stale output.

## Window and source correctness

The project renderer mixes its buffer from absolute project frame zero. The adapter maps visible region-local ticks through the captured/current-matching project tempo map, region start and the publication's sample rate. It clips the requested end to available rendered frames and reports intervals with no audio. Bin-center frames map back through the tempo map for plotting; constant-tempo pixel assumptions are not used.

The pending key includes coordinator identity, render request, region and exact first/count frame window. Zoom navigation immediately cancels and clears the old result. Pending work must retire before the latest desired window starts. Same-revision rerenders receive new source keys, including sample-rate changes. Full document/session and render-source guards remain enforced.

The view holds a publication read handle and checks its identity against the window key before combining envelope values with channel/rate/quality metadata. A publication arriving between reads cannot pair an old envelope with new metadata. Source validity remains a point-in-time check, not a promise against later changes; subsequent polling invalidates stale data.

Raw PCM analysis runs only in the measurement worker. Owner-thread polling starts/adopts work and requests subsequent repaint while work is pending; no worker calls the UI. Painting projects bounded bin data and formats aggregate summaries, not raw PCM. Channel changes reuse the multi-channel result for the same window. Source/window failures remain visible until the input changes; they do not trigger an unbounded failure/retry loop.

## Verification

`tests/test_measured_dynamics_workflow.cpp` renders a real procedural vowel with a nonzero region start and an internal tempo change, then drives the native controller. It verifies leading project-buffer silence, the frame origin, exact measured x/y placement against independent envelope results, quality/revision labeling, numerical/accessibility content, read-only plot handles, zoom invalidation, keyboard channel cycling, unchanged project/history, same-revision 48→96 kHz rerender replacement and stale-document suppression.

- Release native app/core and Release/Debug focused builds pass.
- All 630 Release core cases pass (14.05 s on the final rerun).
- The measured workflow passes Release/Debug (0.53/1.03 s).
- `git diff --check` passes.
- The 480×320 render `/tmp/seam-measured-ui.JLCevb/measured.png` was inspected: channel controls, measured trace, RMS/peak summary and unit labels fit without overlap.

An earlier passing core run recorded an anomalous long wall-clock duration; macOS power logs confirmed intervening sleep. The final rerun above is the relevant elapsed-time observation, not a latency benchmark.

## Remaining qualification

The runtime adapters are wired, but this evidence is headless/controller and rendered-pixel verification—not live standalone/embedded host interaction. Measured pitch, bus/voice-isolated labeling, broader renderer/host parity, metadata-heavy performance, live accessibility/input and independent listening acceptance remain open. Exact full-project source matching is conservative and can require a fresh render after metadata-only changes. Changes remain local/uncommitted; U25/Beta GO remain incomplete.
