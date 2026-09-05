# Full-Scope Beta GO Execution Evidence

This ledger records implementation evidence for [the approved plan](../plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md). It does not replace the plan or authorize release. The complete R1–R20/V01–V18 scope remains mandatory.

## Starting state: 2026-09-05

- Branch: `codex/production-readiness-completion`.
- Source commit: `69901159a27b2935bb8e40c4c96eccde781f0b9f`.
- Origin report SHA-256: `635606cfd10be803612dfcb47cf84651796a06860ff34dc8343705eac20c9c01`.
- Pre-existing work: 40 modified tracked files, comprising 2,912 insertions and 339 deletions, plus untracked U60 crash/support headers, probe, schemas and fixtures. The source report and implementation plan were also untracked. These changes are preserved, not attributed to this implementation run.
- Existing `build/dev` uses Ninja/Debug. Toolchain: Apple clang 21.0.0 targeting arm64 macOS, CMake 4.1.1. Compiler warnings-as-errors remain enabled. Local generated build identity is development identity, not release provenance; the source commit plus working diff must accompany any evidence.

## Active implementation

U1's implementation and diagnostic-runtime criteria and U2's acceptance-contract implementation are verified. The source-index closure check remains an explicit repository-integration obligation, not a waived release gate. U3's isolated value-type foundation and migration-boundary repairs are verified; canonical schema integration remains pending. U3–U48 remain uncompleted. This does not certify any release-quality singer or Beta GO.

### U1: reproducible build and auditory baseline

**Changes made:** explicit numeric conversions in WAV statistics, batch/streaming sample-rate conversion, CLAP PCM resampling, live-note fades and diagnostic-button width arithmetic. Corrected declaration-order aggregate initialization in standalone callback binding, native startup configuration and the render-status test fixture. The fixes preserve values/behavior and do not suppress compiler diagnostics.

**Pre-change evidence:** fresh strict builds failed on the implicit conversions and C++20 designated-initializer ordering. Each affected translation unit compiled after its correction. No new behavior test was added for mechanically equivalent casts; existing DSP, resampling, live-voice, native and lifecycle regressions provide behavioral coverage.

| Check | Observed result | Scope and remaining limit |
|---|---|---|
| `cmake --preset dev` | Exit 0 | Reused the verified Ninja cache; did not clear a build tree |
| `cmake --build --preset dev -j 4 -- -k 0` after repairs | Exit 0 | Full development build. Apple linker still reports duplicate static-library inputs; compiler warnings were not disabled |
| `cmake --preset release` and `cmake --build --preset release -j 4 -- -k 0` | Exit 0 | Full optimized build with the same strict compiler checks |
| Compiler negative probe using project warning flags and an unused local variable | Exit 1 with `-Werror,-Wunused-variable` | Confirms a genuine compiler warning still fails; probe read from stdin and produced no file |
| Four focused CTest suites: render coordinator, bank production, recovery/support, phase12c live voice | 4/4 passed | Freshly rebuilt binaries, not historical counts |
| `ctest --preset dev --output-on-failure --output-log build/dev/Testing/fullscope-baseline.log` | 64/65 entries passed, exit 8 | Sole failure: `seam_tracked_source_closure`, because plan and existing U60 files are not indexed. Do not waive the check or stage unrelated work solely to turn it green |
| `seam_tests` within the CTest run | 443 passed, 0 failed | Mechanical/native/domain regression coverage, not acoustic qualification |
| Release CTest: core suite, render coordinator, bank production, recovery/support, live voice | 5/5 entries passed | Optimized-build regression checks; not the entire Release CTest matrix |
| `python3 -m unittest discover -s tests/production -v` | 65 tests, 1 failure | Existing `test_support_bundle_hash_must_match_archived_raw_evidence` expects `PR-010-support-intake`; observed blocked IDs contain only `PR-002-root-chain`. The failing assertion remains intact pending focused diagnosis |
| Fresh native binary `--help` | Exit 0 | CLI argument surface |
| Native deterministic, paused, nonphysical-audio launch with isolated support root | Exit 0 through approved unsandboxed execution; AppKit frame emitted | Sandboxed launch aborted in macOS `_RegisterApplication`; the same binary/arguments worked through the approved GUI-capable path. This is not a synthesis crash or installed-release certification |

Native smoke-test output is local at `/private/tmp/seam-fullscope-u1.o1GgE3/native.ppm` and `native.png`. The run reported `window_backend=AppKit software raster + NSTextInputClient`, `audio_physical=false`, `voicebank_resolved=false`, and `render_state=idle`. The capture shows the empty score and the two-action missing-voicebank diagnostic. Full editor/viewport/character acceptance is still required by later units.

Two independent read-only visual reviewers passed this single captured diagnostic state. They confirmed intact labels, 112-by-28 buttons, an 8-pixel gap, and no text/button clipping or overlap. This does not certify interaction, CJK text, resizing, or the complete native UI. The temporary launch-debug journal was removed after recording the result here; no debug instrumentation or system-setting changes remain.

**Auditory baseline delivered:** [the retained packet](../../out/fullscope-beta/u1-auditory-baseline-20260905/input-provenance.json) contains two saved projects rendered in bank-selected and forced-Raw modes. Each of the two melody outputs is 41 seconds / 1,968,000 frames, and each unequal-rest output is 9.125 seconds / 438,000 frames. All four outputs have nonzero finite measured RMS. The packet retains 24 output artifacts, verified against its output manifest, plus exact input bytes, source patch/untracked-source archive, build configuration, executable identities, command logs, target timing, actual placements and fallback records. No fallback was reported for this baseline; that is not proof of pronunciation or musical quality.

The first native run rejected a missing schema-7 technical-lane field in the new fixtures. The fixtures and their locked hashes were corrected; the project validator was not weakened. Failed process diagnostics now point to retained command/stderr records.

Registered `seam_synthesis_quality_tests`, `seam_singing_quality_contract_tests`, `seam_singing_quality_workflow`, and `seam_public_release_python_tests` in CMake. The Python admission/runner suite passed 13 tests; its four optional native cases are exercised separately by the mandatory workflow target. That workflow executed all four native tests successfully in Debug (73.09 seconds) and Release (20.81 seconds), including the complete real render/analyze/save/provenance chain and invalid-input rejection. The focused synthesis executable also passed in both configurations.

CMake initially selected the bundled Python 3.12 without existing `jsonschema`/PyYAML test dependencies. Explicitly configuring `-DPython3_EXECUTABLE=/usr/local/bin/python3` selects the installed Python 3.14.3 environment that passed the complete production suite. No dependency was silently skipped or installed into the bundled runtime. The production suite now also passes through CTest. Another machine must supply an interpreter with the repository's existing test dependencies; the interpreter path is machine-local, not hardcoded into project CMake.

An independent read-only review found no actionable defect in the corpus admission, frozen snapshot/hash verification, diagnostic output or support-evidence repair. Builds, native workflows, Ruff checks and `git diff --check` were run by the root executor; review alone was not treated as runtime proof. Baseline audio remains diagnostic and does not qualify an original female singer.

U1 evidence binding: corpus SHA-256 `b5a8ab6f6a75a31e6322d23da88dfd8ea93ddc04a73d5e4445cfa48bd4275e6b`; source-evidence archive SHA-256 `cceefb2bdf98ef3c1c3ba98c0a68c208d06b3666af02e8c0e4f9e4745d918a0e`; executed Debug driver SHA-256 `6291ad5e6132591f17a6b3abefa178edb5c57512a5a30cfc863d4628d23e1d99`. The source archive contains the versioned working-tree patch and untracked-source archive against the HEAD above. Later edits invalidate reuse as evidence for a different source state.

### U2: full-scope acceptance contract

Implemented the mandatory EB-009 requirement in the External Beta contract and central READY evaluator. The typed registry covers all 20 R requirements, 18 V packages and 83 child cases; the closed evidence envelope requires a hash-bound full-product report. Both READY and CLOSED reject legacy eight-row candidates and forged ninth-row PASS summaries until U45's semantic validator is genuinely implemented. The reference hashes actual full-contract bytes, and the outer acceptance/candidate-root commitment binds that content transitively. The authority amendment preserves historical creator-study results while recording the user's superseding full-scope decision.

The root reader review reproduced an indefinitely waiting FIFO and oversized/ambiguous JSON reaching definition validation. `full_product_contract.py` now checks regular-file type and a 1 MiB ceiling before opening, checks opened-file identity/type/size, and reads at most the limit plus one byte. It rejects duplicate keys, nonfinite constants and exponent overflow, and nesting beyond 64 levels. All six reader tests pass, including exact-limit acceptance into definition validation and FIFO rejection in a timeout-bounded child process. The combined reader/gate suite passed 19 tests before the later contract-definition revisions. Ruff is clean, and an independent narrow security recheck found no actionable issue. This is reader-boundary evidence, not release authorization.

The separate contract-definition review identified missing canonical nonnumeric protocols, typed empirical result dimensions, and explicit whole-phrase versus forced-chunk continuity proof. A full suite run during those edits observed 140 tests with one canonical-definition mismatch; this intermediate state is retained as a failed run, not counted as a completed integration check.

Those definition repairs are now verified on the stable handoff. Versioned canonical protocol/check definitions prevent prose replacement from waiving counterbalancing, independent review, complete-song production or current-audio bounce requirements. Eleven empirical criteria require 175 typed cells with exact dimensions, units, comparators and environment/resource/provider/precision bindings. All checked-in qualification values remain unresolved. Eight existing cases explicitly require whole-versus-forced-chunk phoneme/timing/F0/phase evidence and neighbor-edit invalidation; no R/V outcome or existing case was removed.

The root executor ran all 147 External Beta tests and all 68 production Python tests successfully after handoff. Both registered suites also passed through Debug CTest (28.74 seconds total). Ruff passed on the changed gate/definition/reader modules and focused tests. The independent adversarial recheck reported no remaining finding in the three repaired areas. Root verified full-contract SHA-256 `d97e07403fbdc11a866eeb4b79ce5e1b5725cdd7a6c418f342a7c580b79d633f` (246,977 bytes); reader SHA-256 is `cff0ab840db37adb665f4df3d94cf52aa19c163a1ef2ff4b6e6775238966b3f0`.

Manual CLI checks: `--help` exited 0; the existing blocked candidate exited 3 for READY and CLOSED; a missing input exited 2 with structured diagnostics. The legacy eight-row test candidate sent through stdin also exited 3, with only EB-009 among blocked requirement IDs. It additionally reported the required unverified-archive diagnostic; no archive verification was fabricated. Rejection outputs are retained at `out/fullscope-beta/u2-ready-rejection.json`, `u2-closed-rejection.json` and `u2-legacy-ready-rejection.json`. These are rejection/engineering receipts, never accepted product evidence. U45/U46 remain responsible for actual raw-evidence semantics and all promotion-path closure.

### Support evidence repair discovered during baseline verification

The production-suite failure above identified a missing comparison, not a stale expectation. The support evidence record's `supportBundleSha256` was never compared with the intake's `bundleSha256`. A mismatched record triggered the generic root-chain check, while direct support-evidence validation returned no finding.

Added three focused tests in `tests/production/test_public_support_evidence.py`. Before implementation, missing and mismatched hashes both failed their rejection assertions; the matching-hash characterization passed. Added the missing semantic comparison in `tools/public_release/evidence_validation.py`, and included the matching hash in the test fixture's archived record before computing its roots. No assertion or signature/root check was removed.

The focused evidence, existing gate and restored-archive suites then passed 16 tests, including the original failing test. The complete production Python suite subsequently passed all 68 tests. This closes that observed binding defect only; U44's full support/crash/privacy and installed-platform acceptance remains incomplete.

### U3: bounded musical value types, partial

Added `NoteVibrato` and typed `DynamicsAutomation` as isolated domain values. Vibrato validates finite fractions, combined fades, depth 0–200 cents, period 5–500 milliseconds and phase in [0,1), even while disabled. Dynamics validates nonnegative ticks and linear gain from silence through +12 dB, holds endpoints, interpolates linearly and defaults to unity. Curve replacement rejects duplicate/unsorted points before mutation; edits retain ordered unique ticks. The current per-region bound is 16,384 dynamics points; replacing an existing point remains allowed at that limit.

The new `seam_performance_contract_tests` target was compiled first against declarations only and failed at linking the unimplemented methods. The initial six real-domain tests passed in Debug and Release. A seventh test loads the actual historical schema-2 and schema-3 vocal files, checks their score data and host-offset migration, and verifies complete canonical equality after encode/decode. It also passed in both configurations.

Added `performance_intent.hpp/.cpp`: typed parameter channels, manual replacement or explicit pitch-only offset mode, note-ID or half-open time-range ownership, and captured musical/pronunciation/ownership revisions. Four tests first linked against declarations and failed for missing implementations, then passed through the actual domain library. The revision helper reports Conflict when any captured dimension differs. It is not a take-acceptance transaction, and these ownership values are not yet persisted or consumed by rendering. Independent scoped review found no actionable value-semantics issue.

Three additional regressions exposed genuine migration-boundary defects. Notes and regions previously accepted overflowing end ticks; their validators now reject overflow before any end-tick addition, preserving the exact `INT64_MAX` boundary. The project decoder previously narrowed MIDI key 256 to 0 before validation; it now validates 0–127 before conversion. All three failures were observed before their fixes. After the final decoder repair, both the 14-test focused executable and the core suite passed through CTest in Debug (86.18 seconds total) and Release (10.18 seconds total). The focused library driver also passed by direct invocation in each configuration. Independent review of these small changes found no actionable regression.

Executed focused-binary SHA-256 identities: Debug `ec20c61c7b6cf4370f8105d56f09736279467a66a82541a663653f1acc0b4892`; Release `08efb0545819abf6ef44262b706b2c77b453bcc1767cf4256a3949fecff70464`. These bind the verified value-type/decoder slice, not an installed release or an audible expression workflow.

The writer remains schema 7. No new saved-performance field or renderer behavior is claimed. Persistence, exact-bank style migration, pronunciation identity, proposed/accepted takes, coupled commands and plugin/recovery round trips remain required before U3 completes.

## Release status

### Requested checkpoint publication: 2026-09-05

The user explicitly requested committing and pushing the current project work, including the preserved crash/support changes, then continuing implementation. U2 was committed as `79d4faef`. Staging the approved source/test/plan files made `verify_tracked_source_closure.py` pass; that prior integration obligation was satisfied without weakening its check. Build directories, local audio evidence, private temporary files and unrelated branches/worktrees are not included in the commits.

Pre-publication review found a crash-handler lifetime race: native teardown drained writers before disabling handle acquisition. A shared lock-free `CrashWriterSlot` now invalidates acquisition before draining, with sequentially consistent ordering; both native destructors and macOS installation rollback close only afterward. A deterministic driver using the original order returned the old handle to a late writer for both descriptor/handle types (exit 1); the repaired order returned the invalid sentinel (exit 0). The root ran nine recovery/support tests successfully, including real macOS crash subprocesses. Independent recheck confirmed the ordering repair; Windows runtime integration remains unverified.

The main checkout retains its existing development branches and another developer's worktree. The repository's master-only publishing audit runs in an isolated checkout of the exact committed source, which is fast-forwarded and re-audited before pushing to `origin/master`. No branch deletion, force push or history rewrite is authorized or used.

No model was trained, no new production singer or commercially qualified voicebank was delivered, no independent listening/creator study ran, and no signed installed platform/host matrix was completed in this baseline work. Beta GO is not achieved. Continue independent implementation while keeping these acceptance obligations explicit.
