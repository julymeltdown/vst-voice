# Studio background unit selection

Ordinary interactive rail and Up/Down selection now use the same serialized
background loader as Review navigation. External editable manifests do not
require a producer workspace merely to select a unit. Review capture and
publication retain their separate producer/qualification checks.

The owner thread captures immutable manifest, audio bindings, paths, dirty state,
selection/review revisions and producer identity. A worker performs bounded
read/hash/decode and waveform/spectrogram construction without referring to the
owner controller. Collection rechecks context and cancellation before adopting
the result. Existing display and edits remain intact on failure. Geometry is
relayouted against the current window size when adopting the result.

This is deliberately serialized: another selection while work is active is
rejected, not queued or advertised as latest-request-wins. Escape cancels;
collection retires the job before another can start. The normal shutdown join
also includes this worker. Inventory-only selection and synchronous setup/CLI
selection have no new asynchronous requirement.

Four regressions cover external manifests with unsaved edits and resize,
cancellation/restart, stale model mutation, missing audio, and shutdown joining.
Existing producer-bound tamper/review tests exercise the shared path as well.

Verification after rebuilding core, native Studio, Studio tests, Phase12B and
the CLAP plugin:

- Seven selected CTest entries: **7/7 PASS**, 23.06 seconds.
- Current core: **822 passed, 0 failed**.
- Studio draft/close/selection: **24 passed, 0 failed**.
- Actual CLAP cold bounce, Phase12B, Studio review, and macOS/Windows source
  contracts pass in that selection.
- `git diff --check`: PASS.

The previous 119/120 full CTest result belongs to the preceding integration
checkpoint. A new full 120-entry run is not claimed here. Windows runtime,
fresh native click/keyboard visual acceptance, measured large-WAV latency,
FFT-internal cancellation and complete product qualification remain open.

The source comparison against recovery snapshot `session-preservation-XQcnxa`
found no missing inputs and exactly six intentionally changed source/test files.
No staging, commit, push, approval, new unit acceptance or Beta GO occurred.
Raw execution is retained in the sibling `evidence/studio-background-2026-09-09/`.
