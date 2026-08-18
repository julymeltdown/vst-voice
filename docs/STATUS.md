# Project SEAM Development Status

**Date:** 2026-08-18
**Branch policy:** `master` only
**Current product gate:** **G1 Feature Alpha**

Phase 12C live articulation and Linux validation harnesses are implemented. Phase 13A VST3/AUv2, signing, installer, validator, and commercial-host certification pipelines are `SOURCE_READY / CI_CONFIGURED`, but Phase 12C and Phase 13A acceptance remain blocked by mandatory target execution.

No source file, workflow, unsigned artifact, or checklist is treated as a validator, runtime, signing, notarization, installer, or commercial-host PASS. See `docs/phase13a/MANDATORY_VALIDATION.md` and the checked-in validation matrix.


## Phase 13B

Content/IP release tooling is implemented. Official Voicebank 01 and Character 01 commercial acceptance remain blocked by real performer, recording, naming, trademark, production-asset, IP and legal evidence.

## Usable standalone product gate

The canonical product-usability gate is `docs/product/USABLE_ALPHA_ACCEPTANCE.md`. The current native standalone remains a demo shell: it constructs a hard-coded project through `makeDemoProject()` and plays a sine-wave timeline through `makeDemoTimeline()` instead of publishing `ProductionProjectRenderer` output. Phase 12C, Phase 13A, and Phase 13B engineering gates do not prove standalone usability. The immediate product milestone is the shared authoring runtime and Usable Apple Silicon Standalone Alpha described by that contract.
