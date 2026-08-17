# Phase 10 Acceptance — CLAP Render-Player Vertical Slice

Phase 10 is accepted when the repository provides a real loadable CLAP module,
a bounded persistent state, host transport playback, automation, and a dynamic
host smoke test without weakening the standalone editor or audio-thread
contracts.

## Required

- [x] `ProjectSEAM.clap` exports the official `clap_entry` symbol.
- [x] The factory exposes exactly one descriptor with stable reverse-domain ID.
- [x] No audio input port and one main output port are reported.
- [x] The output port reflects one through eight channels from inactive state.
- [x] The state extension handles partial stream reads and writes.
- [x] A CLI packs a real one-to-eight-channel WAV into state, inspects the
      bounded state, and extracts its PCM back to WAV.
- [x] State is bounded, versioned, little-endian, SHA-256 protected, and rejects
      corrupt, oversized, malformed, or non-finite PCM.
- [x] State load before activation prepares a deterministic host-rate render.
- [x] Active state replacement is rejected and requests a host restart instead
      of racing with the audio thread.
- [x] Host seconds transport and beat/tempo fallback map to render frames.
- [x] Stopped transport produces deterministic silence.
- [x] Master Gain is a stable, automatable, requires-process CLAP parameter.
- [x] Parameter value events are applied at their sample offset without dynamic
      allocation in `process()`.
- [x] Latency is zero and tail is zero.
- [x] Realtime and offline render modes are accepted without changing the
      already-rendered PCM.
- [x] A separate executable loads the built shared module through the operating
      system dynamic loader and validates factory, extensions, state,
      multichannel output, transport, automation, pause silence, and teardown.
- [x] CLAP source provenance, immutable revision, SHA-256, MIT license, notices,
      and SBOM relationship are recorded.
- [x] All changes are committed on `master` only.

## Explicitly not accepted as Phase 10 completion

- Embedded Piano Roll, Phoneme Lane, Unit Lane, or Character Dock inside a DAW.
- Background singing synthesis on the host audio thread.
- A GUI extension or native editor window owned by the plug-in.
- Live note-event synthesis.
- Arbitrary state loading while the plug-in is active.
- VST3 or AU binaries.
- macOS `.clap` bundle packaging and notarization.
- Validation in every third-party CLAP host.

The Phase 10 module is deliberately a **host-synchronized player for a bounded,
pre-rendered Project SEAM multichannel render**. It is the smallest trustworthy
plug-in boundary before an embedded editor and asynchronous render service are
attempted.
