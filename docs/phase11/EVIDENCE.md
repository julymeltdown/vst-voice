# Phase 11 evidence

## Executed in this Linux/X11 environment

- `ctest-release-phase11.txt`: release Phase 11 tests, 3/3 PASS.
- `ctest-sanitize-phase11.txt`: ASan+UBSan Phase 11 tests, 3/3 PASS.
- `thread-sanitizer-tests.txt`: async renderer and live instrument TSan PASS.
- `thread-sanitizer-host.txt`: dynamic CLAP GUI/live-note host TSan PASS.
- `existing-named-tests.txt`: preceding 128 tests, 128 PASS / 0 FAIL.
- `host-summary.json`: dynamic module, GUI, note input, state and restart result.
- `clap-editor.png`: actual X11 child-view capture under Xvfb.
- `live-human-sample.wav`: callback capture generated through CLAP note events.
- `plugin-exports.txt`: the release module exposes only `clap_entry`.
- `source-contract.txt`, `source-verification.txt`, `license-audit.txt`, and `branch-policy.txt`.

## Explicitly not reported as executed

- `clap-validator.txt` records `NOT_RUN` because the validator binary was absent.
- VST3/AU binaries and their validators were not produced on this Linux runner.
- macOS notarization and Windows Authenticode require real credentials.
- commercial DAW certification requires the actual host binaries and versions.
- the included public-domain voice is a technical fixture, not Official Voicebank 01.
