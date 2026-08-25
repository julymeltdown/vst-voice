# Phase 12C Acceptance

Implementation deliverables: live attack/transition/sustain/release engine, legato fallback, pitch bend, note expression, MIDI 1, 32 voices, de-click stealing, official validator runner, Linux stress tools, and mandatory target OS/DAW gates.

Acceptance remains BLOCKED until the official `clap-validator` and the exact 7,200-second `--profile full` soak have produced PASS evidence. Smoke soak is not a substitute. The manual `phase12c-full-soak.yml` workflow now builds the soak target, runs the full profile, validates the live baseline plus soak record, and uploads the evidence; an actual successful runner result is still required.

Target-runtime evidence is hash-bound to its summary, screenshot, audio,
host log, and runner OS/architecture metadata. Record and packet verification
rejects malformed metadata, symlinked artifacts, and any result that is not an
actual `PASS`; these checks do not substitute for Windows/macOS execution.
