# Phase 13B Content and IP Release Design

## Goal

Phase 13B adds a fail-closed engineering system for Official Voicebank 01 and Character 01 release readiness. It must package and validate all software-controlled content, rights evidence, recording evidence, character assets, and product approvals without fabricating external facts.

## Binding product distinction

The following are separate states:

```text
ENGINEERING_READY
EXTERNAL_EVIDENCE_REQUIRED
ACCEPTANCE_BLOCKED
ACCEPTED
```

A public-domain or CC0 technical voice fixture may be shipped as `official=false` and `contractedSinger=false`. It can never satisfy the Official Voicebank 01 contract, directed-recording, retake, or legal-approval gates.

## In scope

1. Official Voicebank 01 release dossier schema and validator.
2. Character 01 release dossier schema and validator.
3. SHA-256 evidence inventory with safe relative paths and symlink/path traversal rejection.
4. Four-renderer listening-QA gate: Raw, Classic PSOLA, SpectralClassic, Stretch.
5. Deterministic Character 01 derived asset generation from the selected canonical source image.
6. Character development pack containing key art, portraits, thumbnails, silhouette, palette, runtime states, blockout model, provenance, and hashes.
7. Deterministic Phase 13B development content bundle containing the non-official human demo voice and Character 01 development pack.
8. G5 product release gate which requires Phase 12C/13A mandatory matrices plus accepted voicebank and character dossiers.
9. CLI and CTest integration.
10. Documentation and templates for contracts, recording sessions, retakes, listening QA, trademark/domain/social clearance, IP assignment, and product/legal approvals.

## Explicitly out of scope

- Selecting or contracting a real performer.
- Signing a performer agreement.
- Conducting or pretending to conduct recording sessions.
- Trademark clearance, domain purchase, or social-handle registration.
- Final public character name.
- A production-quality front/side/back 3D turnaround, rig, expressions, or animation that does not already exist.
- Legal approval.
- Declaring Official Voicebank 01 accepted.

Those remain mandatory external gates and are recorded as `NOT_RUN` or `BLOCKED`.

## Voicebank release dossier

The canonical file is:

```text
content/phase13b/official-voicebank-01-release-dossier.json
```

Required gates:

```text
performer-contract
rights-review
recording-session-logs
microphone-chain-calibration
complete-unit-inventory
retake-closure
marker-pitch-loop-qa
renderer-listening-qa
signed-seambank
installation-receipt
character-marketing-rights
performer-character-separation
commercial-user-output-eula
product-owner-approval
legal-approval
```

Every gate marked `PASS` must contain one or more evidence records with an existing safe relative file, SHA-256, execution/review timestamp, reviewer identity, and evidence kind. `renderer-listening-qa` additionally requires PASS evidence for all four renderers.

## Character release dossier

The canonical file is:

```text
content/phase13b/character-01-release-dossier.json
```

Required gates:

```text
public-name
trademark-clearance
domain-clearance
social-handle-clearance
ip-assignment
source-provenance
front-side-back-turnaround
production-low-poly-model
lod-set
expression-set
animation-set
runtime-state-assets
key-art
merchandise-policy
voice-character-separation
product-owner-approval
legal-approval
```

The current canonical low-poly image and Phase 1 OBJ are development assets. They may satisfy provenance, runtime-state, key-art, and development blockout checks, but they cannot satisfy the production turnaround/model/rig/animation or external clearance gates.

## Evidence security

Evidence paths are relative to a declared dossier root and must:

- use forward slashes;
- contain no empty, `.` or `..` segments;
- be regular files;
- not be symbolic links;
- resolve inside the evidence root;
- not exceed 64 MiB per evidence file;
- match the recorded SHA-256 exactly.

A PASS result with missing or mismatched evidence is invalid and becomes FAIL.

## Derived Character 01 assets

The deterministic generator produces:

```text
assets/character-01/production-development/
├── key-art-1024.png
├── portrait-512.png
├── thumbnail-256.png
├── silhouette-256.png
├── palette.json
└── asset-manifest.json
```

The output is labelled `developmentOnly=true`. It does not change the canonical source, does not create fictitious turnaround views, and does not affect synthesis or PCM cache identity.

## Product content bundle

`ProjectSEAM-0.13.1-content-development.zip` contains only:

- the public-domain/CC0 human technical fixture;
- Character 01 development assets;
- provenance and licence files;
- a deterministic manifest;
- `DEVELOPMENT_ONLY.txt`.

The official pack command refuses to run unless both dossiers are accepted and all mandatory G5 rows are PASS.

## Release gate

G5 is PASS only when:

1. Official Voicebank 01 dossier is ACCEPTED;
2. Character 01 dossier is ACCEPTED;
3. every mandatory Phase 12C target is PASS with evidence;
4. every mandatory Phase 13A target is PASS with evidence;
5. final EULA and voicebank licence evidence are present;
6. unresolved mandatory count is zero.

The checked-in baseline must remain BLOCKED.
