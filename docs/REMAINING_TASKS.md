# Project SEAM Remaining Tasks

Phase 12A and 12B are complete:

- production phrase rendering is shared by the CLAP preview and engine;
- exact Voicebank ID/version/content-hash resolution is enforced;
- Phoneme boundaries, Unit variants/renderers and Pitch points are directly editable;
- the embedded Sample Microscope is reachable;
- host seconds/beats/tempo/loop/seek/project offset are mapped;
- all audible tracks and regions render through Phase 6 1–8-channel routing;
- CLAP audio-port configurations and realtime/offline quality are implemented.

Remaining P0 work is Phase 12C: voicebank-driven live articulation, official
CLAP validation, Windows/macOS runtime certification and the long-running
realtime/soak matrix.

P1 remains VST3/AU target binaries and validators, signed/notarized installers,
commercial host certification, Official Voicebank 01 and final Character 01
rights/assets. See `REMAINING_TASKS_KO.md` for the canonical detailed backlog.


## Phase 13B external gates

- Contract, record, label and accept Official Voicebank 01.
- Clear the Character 01 public name, trademark, domains and social handles.
- Obtain character-source IP assignment and production turnaround/model/LOD/expression/animation assets.
- Approve final EULA, voicebank licence and performer/character separation statement.

The evidence and packaging tools are implemented; the external work is not.

## Immediate product-critical milestone — Usable Standalone Alpha

Before adding more plugin formats or release-policy phases, the project must:

1. extract a shared `seam-authoring-runtime` from the CLAP editor;
2. replace standalone `makeDemoProject()` and `makeDemoTimeline()` with user projects and `ProductionProjectRenderer` output;
3. expose New/Open/Save/Save As/Autosave Recovery/Recent Projects;
4. expose installed voicebank selection, installation, diagnostics, and exact relink;
5. expose final master and stem WAV export;
6. ship a legally distributable voicebank with real phonetic coverage;
7. build and validate an Apple Silicon `.app`;
8. pass the canonical `docs/product/USABLE_ALPHA_ACCEPTANCE.md` gate.
