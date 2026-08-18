# Project SEAM Phase 13A Implementation Report

Phase 13A keeps the CLAP implementation canonical and adds exact, permissive,
fail-closed VST3/AUv2 projection and distribution infrastructure.

Implemented:

- exact full-commit locks for CLAP 1.2.10, clap-wrapper 0.15.1, VST3 SDK 3.8.1 and AudioUnitSDK 1.4.0;
- license, checkout and recursive-submodule verification with wrapper downloads disabled;
- Linux/Windows/macOS VST3 builds and Steinberg validator workflows;
- macOS AUv2 and `auval` workflow;
- explicit `CLAP_PATH` propagation for wrapper validation;
- deterministic unsigned Linux developer package and sandbox install/uninstall smoke;
- NSIS 3.12/zlib Windows installer, Authenticode/timestamp scripts and machine-readable clean-install evidence;
- macOS nested signing, signed PKG, notarization, stapling, Gatekeeper and machine-readable clean-install evidence;
- evidence-backed commercial-host records and mandatory G2/G3/G4/G5 gates.

Local verification completed with a warnings-as-errors build, 39/39 CTest,
33/33 Phase 13A Python tests, branch policy and license audit. Target VST3,
AUv2, signing, notarization, clean-OS and commercial-host results remain
`NOT_RUN`; therefore the engineering state is `SOURCE_READY / CI_CONFIGURED`
and release acceptance remains `BLOCKED`.

The mandatory future tests are documented in `MANDATORY_VALIDATION.md`,
`MANDATORY_FUTURE_VALIDATION.md` and `mandatory-validation-matrix.json`.
They must be executed on the actual target OS, official validator and actual
DAW; source or CI configuration is never PASS evidence.
