# Operator-recorded benchmark snapshot — 2026-09-28

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
