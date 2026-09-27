# Operator-recorded benchmark snapshot — 2026-09-28

This is a transcription of one of the recent three 40-sample runs on the parallel-layer
candidate, subsequently committed as `52fc8ff5`. It is **not** a benchmark of current master
`caabbaf5`: the exact working-tree identity at the time of execution was not preserved with the
output. The raw JSON is not tracked. No benchmark or ctest executable was run for this document.

Command, from the candidate worktree:

```sh
SEAM_BENCHMARK_SAMPLES=40 ./build/release/seam_phase5_benchmark
```

Machine: Apple M3 Max / Mac15,9. Load average was 6.57129 before and 6.36523 after this run.
The test used a 10,000-note project, a 1440×900 logical surface at 2×, and measured
`SingShell::prepareFrame + SingShell::paint` wall time. The executable returned a failed design
shell gate because true cold frames exceeded the 14 ms p95 budget in both looks.

| Case | Look | p50 ms | p95 ms | max ms | p95 budget ms | Verdict |
|---|---|---:|---:|---:|---:|---|
| `cold-full-frame` | EMO | 15.58500 | 16.49850 | 16.70570 | 14 | MISS |
| `retained-background-invalidation` | EMO | 8.50467 | 9.03442 | 9.26767 | 14 | PASS |
| `scroll-zoom` | EMO | 5.71917 | 6.28725 | 6.58108 | 8 | PASS |
| `playback` | EMO | 1.55146 | 2.04937 | 2.11525 | 3 | PASS |
| `dense-10000-notes` | EMO | 2.72142 | 3.28233 | 3.46783 | 8 | PASS |
| `cold-full-frame` | SCENE | 14.64890 | 15.25600 | 15.44400 | 14 | MISS |
| `retained-background-invalidation` | SCENE | 9.01421 | 9.74317 | 9.96429 | 14 | PASS |
| `scroll-zoom` | SCENE | 5.36550 | 6.64783 | 6.82129 | 8 | PASS |
| `playback` | SCENE | 1.33904 | 2.07221 | 2.17483 | 3 | PASS |
| `dense-10000-notes` | SCENE | 2.62250 | 3.28617 | 3.39821 | 8 | PASS |

The layer cache was 65,374,626 bytes against the 83,886,080-byte (80 MiB) budget: PASS.
In the three-run set, EMO cold p50 values were 15.1699, 15.5850 and 15.5979 ms and p95 values
were 15.8806, 16.4985 and 16.4562 ms. SCENE cold p50 values were 14.1705, 14.6489 and
14.2278 ms and p95 values were 15.3457, 15.2560 and 14.6660 ms. Every true-cold p95 missed.

The tracked documentation and `build/evidence/` were checked for a source-exact `caabbaf5`
§10 benchmark and full ctest output. Neither is present. The earlier 206/206 ctest result belongs
to `44bf8386`; it should not be promoted to a current-master result.
