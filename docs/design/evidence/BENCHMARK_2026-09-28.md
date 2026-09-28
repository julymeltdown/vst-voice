# Operator-recorded benchmark snapshot — 2026-09-28

## Update at `3fb39459`: software gradient fills of round shapes

Measured with the Release build of `3fb39459`. That commit draws two-stop gradient fills of
rounded rectangles and circles (note capsules, knob bodies) in software instead of through
CoreGraphics shading. A 40-sample full run, with load average 10.13 before and 20.37 after:

| Case | Look | p50 ms | p95 ms | max ms | p95 budget ms | Verdict |
|---|---|---:|---:|---:|---:|---|
| `cold-full-frame` | EMO | 10.888 | 12.473 | 12.490 | 14 | PASS |
| `retained-background-invalidation` | EMO | 7.549 | 7.809 | 7.900 | 14 | PASS |
| `scroll-zoom` | EMO | 5.673 | 6.645 | 6.866 | 8 | PASS |
| `playback` | EMO | 1.518 | 2.003 | 2.192 | 3 | PASS |
| `dense-10000-notes` | EMO | 2.735 | 3.224 | 3.464 | 8 | PASS |
| `cold-full-frame` | SCENE | 10.994 | 12.549 | 14.640 | 14 | PASS |
| `retained-background-invalidation` | SCENE | 7.827 | 9.525 | 19.877 | 14 | PASS |
| `scroll-zoom` | SCENE | 5.887 | 6.401 | 6.749 | 8 | PASS |
| `playback` | SCENE | 1.684 | 2.153 | 2.246 | 3 | PASS |
| `dense-10000-notes` | SCENE | 2.699 | 3.246 | 3.385 | 8 | PASS |

The executable reported the design-shell gate as a pass. The layer cache was 65,374,226 bytes
against the 80 MiB budget.

Alternating A/B runs used `SEAM_BENCHMARK_CASE=cold-full-frame` with 80 samples: five runs each
of this build and of the preceding panel-fill build, at load average 11.4–12.6. Median true-cold
p50 went from 11.90 to 10.59 ms for EMO and from 11.87 to 10.77 ms for SCENE. Median p95 went from
12.97 to 11.60 ms and from 13.77 to 11.70 ms. All ten look-runs of this build had p95 under 14 ms
(11.1–13.6 ms).

Unrelated processes later drove the load average to 20–29. A case-limited run at 20.4 measured an
EMO p50 of 93 ms, which reflects contention, not the frame. The next two runs at 28–29 passed in
three of four look-runs; the SCENE miss was a p95 of 16.0 ms. The budget is therefore met in every
run at ordinary load on this machine, but p95 still responds to heavy unrelated load.

## Update at `87d8fe02`: software glass-panel fills

Measured on this machine with the Release build of the code committed as `87d8fe02`. That commit
paints the translucent glass-panel fills in software with the wash and adds an ordered dither.
It also makes the squared-radius wash table hold the ramp samples it replaces. A 40-sample full
run at load average 8.37:

| Case | Look | p50 ms | p95 ms | max ms | p95 budget ms | Verdict |
|---|---|---:|---:|---:|---:|---|
| `cold-full-frame` | EMO | 12.067 | 12.667 | 13.127 | 14 | PASS |
| `retained-background-invalidation` | EMO | 9.262 | 9.600 | 9.849 | 14 | PASS |
| `scroll-zoom` | EMO | 5.893 | 6.492 | 7.032 | 8 | PASS |
| `playback` | EMO | 1.628 | 2.111 | 2.188 | 3 | PASS |
| `dense-10000-notes` | EMO | 3.047 | 4.505 | 5.774 | 8 | PASS |
| `cold-full-frame` | SCENE | 12.562 | 15.082 | 15.970 | 14 | MISS |
| `retained-background-invalidation` | SCENE | 9.231 | 9.611 | 9.731 | 14 | PASS |
| `scroll-zoom` | SCENE | 5.887 | 6.689 | 7.376 | 8 | PASS |
| `playback` | SCENE | 1.621 | 2.001 | 2.302 | 3 | PASS |
| `dense-10000-notes` | SCENE | 2.752 | 3.371 | 3.718 | 8 | PASS |

Three case-limited runs (`SEAM_BENCHMARK_CASE=cold-full-frame`, 80 samples) followed at load
average 8.5–8.6:

| Run | EMO p50 / p95 ms | SCENE p50 / p95 ms |
|---|---|---|
| 1 | 12.41 / 14.33 (MISS) | 12.66 / 13.48 (PASS) |
| 2 | 12.47 / 14.75 (MISS) | 12.42 / 13.59 (PASS) |
| 3 | 12.24 / 13.20 (PASS) | 13.01 / 15.19 (MISS) |

True-cold p50 dropped from about 15.2 ms to 12.1–13.0 ms in both looks. In an alternating A/B
against `2116f741` (five runs each, 80 samples), the median p50 went from 15.26 to 12.51 ms
for EMO and from 15.21 to 12.27 ms for SCENE. p95 now passes 14 ms in 4 of these 8 look-runs,
moving with load from unrelated processes, so the true-cold budget is **not yet a stable pass**.
The layer cache was 65,374,226 bytes against the 80 MiB budget: PASS.

The earlier sections below describe `caabbaf5` and are kept for history.

## Snapshot at `caabbaf5`

Source-exact run: the numbers in the table below were re-measured on this machine at master
`caabbaf5` (working tree clean) with `build/release/seam_phase5_benchmark`, 40 samples per case.
The earlier transcription of the `52fc8ff5` candidate is kept in the comment at the bottom for
history. The raw JSON is not tracked by this repository.

Command, from the repository root:

```sh
SEAM_BENCHMARK_SAMPLES=40 ./build/release/seam_phase5_benchmark
```

Machine: Apple M3 Max / Mac15,9. Load average was 6.57129 before and 6.36523 after this run.
The test used a 10,000-note project, a 1440×900 logical surface at 2×, and measured
`SingShell::prepareFrame + SingShell::paint` wall time. The executable returned a failed design
shell gate because true cold frames exceeded the 14 ms p95 budget in both looks.

| Case | Look | p50 ms | p95 ms | max ms | p95 budget ms | Verdict |
|---|---|---:|---:|---:|---:|---|
| `cold-full-frame` | EMO | 15.20390 | 15.55910 | — | 14 | MISS |
| `retained-background-invalidation` | EMO | 8.75316 | 9.94154 | 10.07660 | 14 | PASS |
| `scroll-zoom` | EMO | 5.77238 | 6.41233 | 6.67050 | 8 | PASS |
| `playback` | EMO | 1.55242 | 1.87413 | 2.12667 | 3 | PASS |
| `dense-10000-notes` | EMO | 2.70954 | 3.42317 | 3.72942 | 8 | PASS |
| `cold-full-frame` | SCENE | 14.76040 | 15.81320 | — | 14 | MISS |
| `retained-background-invalidation` | SCENE | 8.85450 | 9.40329 | 9.67083 | 14 | PASS |
| `scroll-zoom` | SCENE | 5.74083 | 6.48088 | 6.81271 | 8 | PASS |
| `playback` | SCENE | 1.65312 | 1.96138 | 2.02029 | 3 | PASS |
| `dense-10000-notes` | SCENE | 2.78704 | 3.32704 | 3.47746 | 8 | PASS |

The `cold-full-frame` rows were measured in a second, case-limited run of the same build
(`SEAM_BENCHMARK_CASE=cold-full-frame`), because the combined run's cold p95 is sensitive to
whatever else is running; both runs were at load average ~8.8–11.3.

The layer cache was 65,374,626 bytes against the 83,886,080-byte (80 MiB) budget: PASS.
Across repeated runs of the same build the true-cold p50 stays in a narrow band (EMO ~15.2–15.6 ms,
SCENE ~14.2–14.8 ms) while its p95 moves with system load; every true-cold p95 measured on this
machine has missed the 14 ms budget, so the design-shell gate correctly stays red.

The repository does not track a full ctest log for `caabbaf5`; the full-suite result run alongside
this snapshot (206/206, minus the two heavy Python suites which are run separately) belongs to the
same working tree and is reported in the completion report, not as a tracked artifact here.

For history: an earlier transcription recorded 15.585/16.499 ms EMO and 14.649/15.256 ms SCENE
for cold p95 on the `52fc8ff5` candidate. It is superseded by the source-exact table above.
