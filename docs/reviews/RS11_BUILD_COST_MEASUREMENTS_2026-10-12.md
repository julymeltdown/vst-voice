# RS-11: measured CLI build and verification cost

Date: 2026-10-12, Asia/Seoul. Disposition: **NO_CHANGE_RECOMMENDED** for the suspected broad rebuild caused by generated source identity. All four required scenarios were measured. No CMake, identity semantics, test coverage, timeout or evaluator behavior changed.

## Operation and comparable conditions

The named feedback loop builds `seam_sample_review_cli_tests` and its real CLI/library dependencies, then runs that registered CTest entry. The initial fresh dependency build took **55.18s** (244 compilations), establishing an observed delay worth investigating. It is setup, excluded from the four warmed comparisons. This measures one target loop, not the duration of every full-project build or development milestone.

An isolated managed checkout started at current completed code **`6f8da65cf0288b68809b791dafdd7b0202ec918e`**. Apple M3 Max, 48 GiB; Apple clang 21.0.0 (`clang-2100.3.34.2`), CMake 4.1.1, Ninja 1.13.2. Release preset, six build jobs, tests/benchmarks and warnings-as-errors enabled, native desktop/CLAP/native GUI enabled. The 26 typed SEAM cache arguments mirror the integrating configuration, including existing external ONNX/Python paths; no training was launched. `SEAM_SOURCE_COMMIT` was not overridden. The [structured record](RS11_BUILD_COST_MEASUREMENTS_2026-10-12.evidence.json) includes complete options and before/after machine load. One-minute load was 6.84–9.05 at the edit-case boundaries; builds ran sequentially under the lead's cooperative heavy-validation lock.

Each build used `ninja -C SCRATCH/build/release -v -d explain -j6 seam_sample_review_cli_tests`. Each check used `ctest --test-dir SCRATCH/build/release --output-on-failure -R '^seam_sample_review_cli_tests$'` and passed. Leaf/header probes appended a harmless comment to the actual named source file, then restored exact bytes and rewarmed before the next scenario. Rewarming work is recorded separately, not hidden in the comparison.

| Warmed scenario | Build seconds | Compile commands | Total Ninja commands | CLI test seconds | Complete loop seconds |
|---|---:|---:|---:|---:|---:|
| No-op | 0.039 | 0 | 1 | 12.646 | 12.685 |
| CLI leaf: `candidate_package_commands.cpp` | 2.424 | 1 | 3 | 11.880 | 14.305 |
| Shared header: `seam/core/result.hpp` | 54.705 | 235 | 256 | 12.582 | 67.287 |
| Real documentation-only commit | 5.656 | 4 | 11 | 13.489 | 19.145 |

The no-op command is CMake's glob verification; it compiles nothing. Ninja explanations attribute the leaf to its one object and downstream links, the header to its actual transitive consumers, and the commit to CMake regeneration/generated `version.hpp`. Source identity recompiles `pcm_cache.cpp`, `render_snapshot.cpp`, `export_service.cpp` and `candidate_package_commands.cpp` in this target. The record lists all compiled units for reproducibility.

## Identity proof and conclusion

The scratch documentation commit is **`1a9526489257b924711ee31644fb15ca9dd41754`**, a genuine Git commit on `codex/rs11-build-cost-scratch`. It is an experiment, not a production development unit to merge. The generated header contains that actual HEAD. More strongly, the CLI's real exported receipt reports exactly that `applicationBuildSha`; its retained bytes and hash are recorded. The existing source-identity CTest independently passed **1/1, 17 Python cases, 7.22s** on current main CMake source.

This measurement rules out a whole-dependency-graph identity rebuild in the named loop: four translation units versus 235 for a shared header. Warmed no-op recompilation is absent. Shared-header fan-out and the approximately 12–13s real CLI test dominate their corresponding feedback scenarios. The modest identity rebuild preserves the source stamp used by rendering/cache/export evidence. These observations support retaining the current identity behavior and current target/test coverage.

The measurements do not establish a general speedup, explain total project elapsed time, or quantify every aggregate test harness. They are single sequential observations with recorded load, not benchmark distributions. The command attribution and order-of-magnitude compile difference are sufficient for this scoped decision; no noisy percentage-speedup claim is made. Temporary source edits were restored, the scratch checkout ended clean, and the shared checkout/stash were untouched by experiments. Raw explanation logs and the one-off script remain in ignored local evidence; only compact results/digests are tracked.
