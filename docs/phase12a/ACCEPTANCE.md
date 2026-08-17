# Phase 12A Acceptance — Production Plug-in Render Integration

Phase 12A is complete only when the CLAP editor preview and a direct engine
render use the same production synthesis path and exact Voicebank identity.

## Required behavior

- [x] Discover Voicebanks from standard installed roots, explicit relink roots,
      bundle/sidecar resources and the source-tree development fixture.
- [x] Resolve an exact `id + version + contentHash` reference.
- [x] Preserve the exact reference in Project JSON and `SEAMED11` plug-in state.
- [x] Reject missing, wrong-version, missing-hash, content-mismatched and
      untrusted installed banks without silent substitution.
- [x] Expose trust and resolution diagnostics to the editor presentation layer.
- [x] Provide explicit refresh, additional-root and exact-bank-selection APIs.
- [x] Render through the shared production path:

```text
Project / VocalRegion snapshot
→ PhraseSegmenter
→ RenderSnapshotFactory
→ Japanese Phonemizer
→ deterministic Unit Selector
→ Timing Solver
→ Raw / Classic PSOLA / SpectralClassic / Stretch
→ SeamComposer
→ content-addressed PcmCache
→ bounded CLAP preview publication
```

- [x] Cancel obsolete work and reject stale publications by revision.
- [x] Demonstrate direct-render and CLAP-preview PCM parity.
- [x] Demonstrate phrase cache reuse.
- [x] Demonstrate relink after a missing-bank state.
- [x] Demonstrate installed receipt/content tamper detection.

## Explicit non-goals

Phase 12A does not complete direct phoneme-boundary, unit-variant or pitch-point
editing, host tempo/loop authority, multi-track plug-in routing, broad target-OS
certification, VST3/AU delivery, or Official Voicebank 01.

The public-domain production fixture is a technical bank. It is not an official
or contracted singer product.
