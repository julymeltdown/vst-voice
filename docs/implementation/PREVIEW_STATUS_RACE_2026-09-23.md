# The preview status projection raced the controller rebuild

Date: 2026-09-23
Plan unit: cross-cutting maintenance. This repairs an existing path and does not
add a roadmap unit.
Base commit: 1b58bb64
Status: **reproduced, fixed, verified.**

## How it surfaced

The `phase11-plugin-formats` workflow failed on its macOS leg at job
`clap-editor (macos-latest)`, step "Core tests", with `seam_phase11_tests`
terminating in a segfault after roughly 3.00 seconds. The failing commit was
`1b58bb64`, which changed documentation only.

That is exactly the shape of failure that gets dismissed as a flake. It was not
taken as one. Re-running the same workflow (`35846674545`) passed all five jobs,
which establishes intermittency but not innocence: a docs-only commit cannot
introduce a defect, so the defect had to pre-exist and merely surfaced there. An
intermittent segfault in a render path is a real bug regardless of who finds it.

## The race, exactly

ThreadSanitizer named both sides and the address, so this is not inference from a
crash site:

```
WARNING: ThreadSanitizer: data race
SUMMARY: ThreadSanitizer: data race unique_ptr.h:288 in
  std::__1::unique_ptr<seam::native_ui::NativeEditorController,
  std::__1::default_delete<seam::native_ui::NativeEditorController>>::reset
```

| | thread | operation | site |
|---|---|---|---|
| **write** | owner | `controller_ = std::make_unique<...>(...)` (`unique_ptr::reset`) | `EditorRuntime::configureControllerCallbacks()` -- `editor_runtime_adapter.cpp:534` |
| **read** | render worker | `controller_->setRenderStatus(...)` (`unique_ptr::operator->`) | `EditorRuntime::refreshRenderStatusView()` -- `editor_runtime_preview.cpp:48` |

The worker's path into that read is `publishPreviewFromAuthoring()`
(`editor_runtime_adapter.cpp:274`), reached from the completion callback
registered at `editor_runtime_adapter.cpp:173`, reached from
`AuthoringRenderCoordinator::workerLoop` through `notifyCompletion()`.

The owner's path into that write is `replaceProject()`
(`editor_runtime_project.cpp:107`, taking `std::lock_guard lock(mutex_)`), which
calls `rebuildController()` (`editor_runtime_project.cpp:138`) and then
`configureControllerCallbacks()`.

This is a **use-after-free window, not a benign race.** The worker dereferences the
controller to call `setRenderStatus` on it while the owner thread may be destroying
it in `unique_ptr::reset` on the same address. A `unique_ptr` read that overlaps a
destruction is the classic way to get the intermittent segfault that was observed.

## Why it existed: a header comment that was confidently wrong

The declaration carried this claim:

> `// Owner-thread projection of the render state into the editor's panel. Safe to call with`
> `// mutex_ held or not: it never takes this runtime's mutex.`

Both halves were load-bearing and both were wrong. It is not an owner-thread-only
function -- the render worker reaches it through `publishPreviewFromAuthoring` -- and
because it never took the mutex, that worker read `controller_` unsynchronised
against the owner's reassignment. The comment described the *intent* of the
function's locking and thereby concealed the hole; the fix could not be reasoned
out from reading the function alone, which is why it took ThreadSanitizer to
localise it.

The declaration is corrected in the same commit so the false assurance cannot be
inherited by the next reader.

## The fix

`refreshRenderStatusView()` now takes the runtime mutex before touching
`controller_`:

```cpp
std::lock_guard lock{mutex_};
if (!controller_) return;
```

`mutex_` is `mutable std::recursive_mutex mutex_` (`editor_runtime.hpp:387`), so
this is correct whether or not a caller already holds it. That property is
required, not incidental: `replaceProject()` holds `mutex_` across
`rebuildController()` -> `configureControllerCallbacks()`, which ends by calling
`refreshRenderStatusView()` at `editor_runtime_adapter.cpp:547`. A plain
`std::mutex` here would self-deadlock on the project-replacement path.

## Verified in both directions

**Before the fix** -- `build/thread-sanitize/seam_phase11_tests` aborted (exit 134,
SIGABRT) on two consecutive runs, both reporting the `unique_ptr` race above.
Evidence: `/tmp/tsan-1.log`, `/tmp/tsan-2.log`.

**After the fix** -- 12 consecutive runs clean, then 12 more clean after the header
correction. Zero ThreadSanitizer reports; the
`unique_ptr<seam::native_ui::NativeEditorController>` signature is absent from
every log. Every run printed identically:

```
Phase 11 tests PASS: notes=8 previewEnergy=3704 liveEnergy=14.0871
```

The exact CI configuration was also reproduced locally:
`ctest --test-dir build/phase11 -C Debug -R '^seam_phase11_tests$'` gives 1/1
passed in 3.46 s. Release CTest is 183/183 in 89.09 s, external-beta is 189/189 in
25.66 s, and the source-closure, phase11-contract and phase12b-contract gates all
pass.

## Deadlock and re-entrancy analysis

Adding a lock to a function reachable from a render worker is a deadlock risk, so
both directions were checked rather than assumed:

- **Worker side.** `AuthoringRenderCoordinator::notifyCompletion()`
  (`render_coordinator.cpp:824`) copies the callback under `callbackMutex_`, then
  releases it and invokes the callback at line 830 with no coordinator lock held.
  Crossing into `EditorRuntime::mutex_` therefore cannot invert against
  `AuthoringRenderCoordinator::mutex_` or `progressMutex_`.
- **Owner side.** Nothing holds `EditorRuntime::mutex_` while blocking on the render
  worker. `prepareOfflineRender()` deliberately calls
  `authoring_->renderer().progress()` and `latest()` *outside* the lock, taking it
  only for short project-identity checks, and re-locks per poll iteration instead of
  holding it across the wait.
- **Calls made under the lock.** Both `AuthoringRenderCoordinator::progress()`
  (`render_coordinator.cpp:371`) and `acquireCurrent()`
  (`render_coordinator.cpp:347`) are `noexcept` and non-blocking; they use
  `progressMutex_` and the lock-free audio publication respectively, not the
  coordinator's `mutex_`. `NativeEditorController::setRenderStatus` is a leaf call
  that does not re-enter `EditorRuntime`, so the recursive lock cannot recurse.
- **Shutdown.** The only join path, `EditorRuntime::~EditorRuntime()` to
  `AuthoringRuntime::shutdown()` (`authoring_runtime.cpp:170`), clears the
  completion callback first (`setCompletionCallback({})`) and does not hold `mutex_`
  while joining, so the worker cannot be joined while waiting on a lock the
  destructor holds.

## What this does not claim

This repair closes **one** race on **one** path. ThreadSanitizer reported no other
race in these runs, but that is evidence about the exercised paths, not a proof of
race-freedom for the CLAP editor.

This is maintenance. It does not change render output -- no renderer revision is
bumped, because no rendered sample changes -- and it **does not advance any roadmap
unit or Beta acceptance criterion.**

## Related, separate

An earlier one-off `seam_phase12b_tests` SIGABRT that occurred *after* the test
printed PASS (during teardown) is tracked separately. It did not reproduce in 200
serial runs and is not the race fixed here; the two should not be conflated.

