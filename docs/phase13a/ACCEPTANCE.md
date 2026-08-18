# Phase 13A Acceptance

## Engineering implementation acceptance

The engineering implementation is complete when all of the following pass:

- exact permissive dependency lock and fail-closed checkout verification;
- canonical CLAP build plus VST3 and macOS AUv2 target pipelines through pinned clap-wrapper;
- VST3 validator and `auval` evidence runners;
- deterministic unsigned Linux developer package and isolated install/uninstall smoke;
- Windows NSIS 3.12 installer source using the zlib compressor and complete CLAP resource sidecar;
- Windows payload/NSIS installer signing scripts that fail closed without credentials;
- macOS nested signing, PKG, notarization, stapling and install/update/uninstall scripts;
- evidence-backed commercial host recorder;
- mandatory release-gate evaluator and explicit future-validation documents.

## Release acceptance

Engineering completion is not target certification. Phase 13A may be marked
`SOURCE_READY / CI_CONFIGURED` after local tests pass. It cannot be marked
`VALIDATOR_VERIFIED`, `HOST_VERIFIED`, `SIGNED`, `NOTARIZED`,
`INSTALL_VERIFIED`, Beta, Release Candidate or General Availability until the
actual target rows in `mandatory-validation-matrix.json` contain complete PASS
evidence.

The Phase 12C official clap-validator, exact 7,200-second full soak, Windows
runtime, macOS runtime and commercial DAW tests remain mandatory and cannot be
waived by Phase 13A.
