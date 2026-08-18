# Mandatory Future Phase 13A Validation

Every row in this document must be executed on the actual target operating
system, official validator, or actual commercial DAW. Source code, CI YAML,
a generated binary, or a checklist is not a runtime PASS. A row without raw
evidence remains `NOT_RUN` and blocks the release gate listed in
`mandatory-validation-matrix.json`.

Required future execution includes VST3 target builds and the Steinberg
validator on Linux, Windows and macOS; AUv2 plus `auval` on macOS; Windows
Authenticode; macOS codesign, notarization, stapling and Gatekeeper; clean
install/update/uninstall tests; the complete declared commercial DAW matrix;
the Phase 12C official clap-validator and exact 7,200-second soak; and the
contracted, recorded and accepted Official Voicebank 01.

A PASS record must include actual OS and host/validator versions, plugin
format and SHA-256, execution time, responsible tester, all required checks,
raw logs, screenshot/audio evidence and SHA-256 for every evidence file.
