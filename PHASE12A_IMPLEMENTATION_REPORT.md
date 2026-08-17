# Project SEAM Phase 12A — Production Plug-in Render Integration

Phase 12A connects the embedded CLAP editor to the shared production
sample-concatenative synthesis path and introduces exact, trust-aware
Voicebank resolution. Detailed architecture and acceptance evidence are under
`docs/phase12a/`.

Key outcomes:

- production Phonemizer → Unit Selector → Timing → four Renderer → Seam path;
- shared Phrase cache and direct/plug-in PCM parity;
- exact Voicebank ID/version/content-hash persistence;
- signed-install receipt and tamper-aware trust resolution;
- explicit missing/version/hash/trust diagnostics with no silent fallback;
- relink-root and exact-bank-selection application APIs.

The product remains Feature Alpha because Phase 12B technical-lane editing,
host timeline/routing and subsequent platform/release gates are still open.
