# Phase 12B Acceptance

Phase 12B is complete when the embedded CLAP editor exposes direct technical
editing, maps authoritative host timeline state, and renders the complete
multi-track project through the shared production pipeline and Phase 6 routing.

## Functional acceptance

- [x] Phoneme boundary offsets are directly editable and undoable.
- [x] Unit variants and per-unit renderer kinds are directly selectable.
- [x] Pitch automation points support create, move, delete and interpolation changes.
- [x] The Sample Microscope can be opened for the selected unit.
- [x] Track and region selection is explicit and stable.
- [x] Track gain, pan, mute, solo and output-route state are project commands.
- [x] A project host-start offset is persisted in Project JSON schema 5.
- [x] Host seconds are preferred; beats plus tempo are the deterministic fallback.
- [x] Host loop start/end and seek position are mapped before PCM lookup.
- [x] Realtime and offline CLAP render modes select Preview and Final render quality.
- [x] All audible vocal tracks and all regions render through `ProductionProjectRenderer`.
- [x] Phase 6 routing matrices and buses produce 1–8 interleaved output channels.
- [x] CLAP audio-port configurations expose 1–8-channel choices and notify the host.
- [x] State round trips preserve schema 5, host offset, technical overrides and routing.

## Verification acceptance

- [x] Two tracks and three regions are rendered in the Phase 12B test fixture.
- [x] Four-channel output is produced and written to a WAV evidence file.
- [x] Direct technical edits cause a newer render revision.
- [x] Final render quality completes without hidden renderer fallback.
- [x] Schema 4 migrates to schema 5 with a zero host offset.
- [x] A Linux/X11 dynamic CLAP host executes the multichannel/offline path.
- [x] Warnings-as-errors Debug and Release builds pass.
- [x] ASan/UBSan and TSan focused tests pass when supported by the build image.
- [x] The final ZIP includes `.git`, has only `master`, is clean and rebuilds from scratch.

## Explicitly outside Phase 12B

- Voicebank-driven attack/transition/legato live note synthesis beyond the existing
  technical sampler.
- Official `clap-validator` evidence.
- Windows and macOS physical host runtime certification.
- VST3/AU target binaries and validators.
- Signed/notarized installers.
- Contracted Official Voicebank 01.
