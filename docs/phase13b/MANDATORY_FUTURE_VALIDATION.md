# Mandatory validation after Phase 13B

These checks are release blockers, not recommendations. Source code, scripts,
CI configuration, or build artifacts do not constitute runtime `PASS` evidence.
A target may be marked `PASS` only after it has been executed on the real target
with evidence files and matching SHA-256 digests.

## Required states

```text
Implementation state
NOT_STARTED | SOURCE_READY | CI_CONFIGURED | TARGET_BUILD_PASS

Runtime validation result
NOT_RUN | BLOCKED | FAIL | PASS
```

## Required Phase 12C evidence

- official `clap-validator` suite;
- six sample rates, seven buffer sizes, 1/2/4/8 channels, realtime and offline;
- an exact 7,200-second wall-clock soak;
- 1,000 GUI lifecycle iterations;
- 10,000 render-cancellation revisions;
- realtime allocation, lock and I/O detection;
- actual Windows and macOS CLAP runtime evidence.

## Required Phase 13A evidence

- real VST3 builds and Steinberg validator runs on Linux, Windows and macOS;
- real AUv2 build and `auval` on macOS;
- Authenticode signing and timestamping;
- Apple Developer ID signing, notarization and stapling;
- clean-OS install, update, reinstall and uninstall;
- actual certification in REAPER, Bitwig Studio, Cubase, Ableton Live,
  Studio One, FL Studio, Logic Pro and GarageBand.

## Required Official Voicebank 01 evidence

- identified performer and signed agreement;
- rights review and directed recording-session logs;
- recording-chain calibration and complete unit inventory;
- retake closure, marker/pitch/loop QA and four-renderer listening QA;
- signed `.seambank`, installation receipt and rollback verification;
- commercial-output EULA, product-owner approval and legal approval.

The public-domain/CC0 voice fixture is not a substitute for performer contract
or directed-recording evidence.

## Required Character 01 evidence

- final public name and actual trademark/domain/social clearance;
- designer and 3D-artist IP assignment or commercial licence;
- provenance and derived-asset hashes;
- front/side/back turnaround, production model, UVs, LODs, expressions,
  runtime states, animation and final key art;
- merchandise/derivative-use policy, performer-character separation,
  product-owner approval and legal approval.

Current `production-development` assets are development evidence only.

## Minimum PASS record

Every PASS record must identify the target, exact versions, artifact hash,
execution time, executor/reviewer, executed checks, evidence paths and a
SHA-256 for every evidence file. Missing, empty, escaping, symlinked or
hash-mismatched evidence invalidates PASS.

## Promotion gates

- G2 requires all mandatory Phase 12C Linux evidence.
- G3 additionally requires Windows/macOS runtime plus REAPER, Bitwig and Logic.
- G4 additionally requires all declared formats, DAWs, signing, notarization
  and clean installer checks.
- G5 additionally requires accepted Official Voicebank 01, accepted Character
  01, final legal documents and zero unresolved mandatory items.
