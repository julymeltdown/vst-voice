# Project SEAM Phase 12A Implementation Report

## Summary

Phase 12A replaces the CLAP editor's dedicated single-vowel preview renderer
with the same production phrase-render path used by the engine. It also adds
exact Voicebank cataloguing, trust-aware resolution, persistent content
identity, relink/select APIs and a development production fixture that drives
all four renderers.

## Main modules

```text
seam-voicebank
├── content_identity
└── catalog

seam-rendering
└── ProductionRegionRenderer

seam-application
└── SetTrackVoicebankCommand

seam-clap-editor
├── trust-aware Voicebank state
├── cancellable production render requests
├── status/diagnostic publication
└── exact state persistence
```

## Production fixture

`assets/demo-human-voicebank-public-domain/production-bank` maps the Japanese
technical phrase to eight Units using Raw, Classic PSOLA, SpectralClassic and
Stretch. The WAV derives from the already documented public-domain human voice
fixture. The bank is deliberately marked as a development fixture and is not
Official Voicebank 01.

## Correctness contracts

- Direct engine rendering and CLAP preview are sample-identical for the same
  project, exact bank, sample rate, options and cache state.
- Voicebank ID/version/hash survive Project and plug-in state round trips.
- Missing or altered banks produce no preview audio.
- An installed bank becomes trusted only through a matching signed installation
  receipt.
- Character assets remain outside synthesis identity and preview PCM.
- The audio callback still consumes already-published PCM and never resolves or
  renders a Voicebank.

## Remaining product boundaries

The graphical bank browser is not yet a complete product surface, although
refresh, relink-root and exact-selection application APIs are implemented and
tested. Technical-lane direct editing and host timeline/routing remain Phase
12B. Platform validators and long-duration stress work remain Phase 12C.
