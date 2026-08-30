# Project SEAM Development Status

**Date:** 2026-08-31
**Branch policy:** `master` only
**Current product gate:** **G1 Feature Alpha**

Phase 12C live articulation and Linux validation harnesses are implemented. Phase 13A VST3/AUv2, signing, installer, validator, and commercial-host certification pipelines are `SOURCE_READY / CI_CONFIGURED`, but Phase 12C and Phase 13A acceptance remain blocked by mandatory target execution.

No source file, workflow, unsigned artifact, or checklist is treated as a validator, runtime, signing, notarization, installer, or commercial-host PASS. See `docs/phase13a/MANDATORY_VALIDATION.md` and the checked-in validation matrix.


## Phase 13B

Content/IP release tooling is implemented. Official Voicebank 01 and Character 01 commercial acceptance remain blocked by real performer, recording, naming, trademark, production-asset, IP and legal evidence.

## Usable standalone product gate

The canonical product-usability gate is `docs/product/USABLE_ALPHA_ACCEPTANCE.md`. The native standalone now uses the shared `AuthoringRuntime` and `ProductionProjectRenderer` for user-owned project content, exact voicebank resolution, transport, recovery, and WAV export; it is no longer a sine-wave demo shell. The gate remains `BLOCKED`: a rights-cleared production bank, Finder/Apple Silicon evidence, physical listening and external-player checks, and the real-song stability session must still be completed through the UI. Phase 12C, Phase 13A, and Phase 13B engineering gates do not substitute for that user journey.

## Public Production gate

The canonical public contract is `docs/product/PUBLIC_RELEASE_ACCEPTANCE.md`.
The checkout is `BLOCKED`, not `PUBLIC_ACTIVE`: External Beta has not closed
for a public lineage, public target/support/update/archive approvals are absent,
and the independent Windows `PW-001` through `PW-020` rows remain `NOT_RUN`.
