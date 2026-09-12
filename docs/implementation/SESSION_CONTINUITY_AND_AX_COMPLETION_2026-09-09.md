# Session continuity and AppKit completion-state repair

## Preservation check

Before editing, compared the current files against every regular-file SHA-256
in `build/recovery-checkpoints/session-preservation-1hvrgz/manifest.json`.
All 2,052 checkpoint files were present; no regular-file hashes differed.
This establishes continuity since that checkpoint, not proof that no earlier
session ever lost changes. No Git reset, staging, commit or push was performed.

## Reproduced defect

In the running Studio, a pointer click on Render produced the painted
`POSE READY / SPACE PLAY` state and enabled buttons, while accessibility still
reported `Rendering`, disabled Render/Play B/Pin A, and enabled Cancel.
This was stale accessibility state, not evidence of a synthesis worker hang.

`NativeWindowAppKit::drawInView` now invalidates the cached accessibility
snapshot after client painting, because painting polls worker completion and
rebuilds the semantic tree. Request-time invalidation alone can be consumed
by an accessibility reader before that tree rebuild.

The current frame's pending announcement is consumed before painting, retaining
any next-frame announcement requested during painting. This prevents a busy
frame from consuming the final completion frame's pending announcement.

## Verification

- Release build succeeded.
- `seam_voice_designer_tests` and `seam_tests`: 2/2 passed, 22.64 seconds.
- Rebuilt Studio launched source-free with synthetic-input flag, 1040x760.
- Created a temporary draft; clicked painted Render at screenshot coordinate
  (80, 637). The next accessibility observation reported `Vowel ready` and
  enabled Play B and Pin A without an extra interaction.
- The previous temporary unsaved QA draft was discarded through its confirmation
  dialog. No saved recipe or producer take was deleted; microphone remained
  unopened in the old process's exit diagnostics.

This qualifies this focused pointer-render/completion path only. Playback,
all source-mode buttons, compact layouts, full platform accessibility and
full Beta GO remain outside this verification result.
