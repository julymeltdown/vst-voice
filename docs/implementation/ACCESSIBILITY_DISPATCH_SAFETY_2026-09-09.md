# Accessibility dispatch and focus safety

The shared accessibility tree now rejects non-focus action dispatch on disabled
controls or disabled materialized ancestors, even when action entries remain
present. Explicit SetFocus and normal focus traversal remain available for
inspection, including disabled controls. Focusing a control does not enable it.

Dispatch now owns a copy of the validated target ID for the callback's duration.
A caller may supply an ID borrowed from the current tree; replacing that tree
inside the callback no longer invalidates the callback argument. Designer pointer
actions retain their own copied target and existing revision checks as well.

New regression cases cover rejection of disabled execution alongside preserved
inspection focus, and a callback that replaces its tree while retaining the
dispatched ID. This is a shared infrastructure repair, not a new claim that the
visible button UI was verified.

## Verified follow-up

An initial run was superseded during development. The subsequent completed run
failed two existing tests: focus on every declared semantic target, and the
visible keyboard focus ring on disabled transport. Those failures exposed an
incorrect assumption in the attempted focus restriction. That restriction was
removed; the existing inspection-focus contract is preserved while disabled
execution remains blocked. The new tests explicitly distinguish these behaviors.

The final full Release build passed. Designer and aggregate-core CTest suites
passed in 22.58 seconds, including both formerly failing tests and the new
callback-lifetime checks. Logs are retained in
`evidence/accessibility-dispatch-2026-09-09/`. This was not a fresh full-suite run
or live verification of the new Designer button layout.

The local resource check showed approximately 66 GiB available disk space and an
active compiler. Slow observation alone was not treated as a dead process or a
reason to restart the same unchanged work. No UI permission changes or unrelated
process termination occurred.

No staging, commit or push occurred. Full Beta GO remains active and incomplete.
