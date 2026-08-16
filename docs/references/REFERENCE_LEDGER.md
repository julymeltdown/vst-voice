# Reference Ledger

Reviewed for functional and architectural comparison; no source code or assets were copied into the Phase 1 implementation.

## OpenUtau

- Source: https://github.com/openutau/OpenUtau
- Studied: piano-roll behavior, phoneme-preview concepts, pre-rendering workflow, separation of editor and resampler.
- Adopted principle: note/phoneme rendering concerns should be separated.
- Rejected: USTX as Project SEAM's canonical model, external-resampler process model, UI replication.
- Copied code: No.

## vLabeler

- Source: https://github.com/sdercolin/vlabeler
- Studied: waveform/spectrogram marker workflows and keyboard-oriented sample navigation.
- Adopted principle: source-audio landmarks need a dedicated microscope rather than generic sliders.
- Phase 1 implementation: documentation only; marker editor begins later.
- Copied code: No.

## FamiStudio

- Source: https://github.com/BleuBleu/FamiStudio
- Studied: high-density music editing, command history, timeline navigation, keyboard-driven workflows.
- Adopted principle: drag previews should commit one command.
- Copied code: No.

## iPlug2

- Source: https://github.com/iplug2/iplug2
- Studied: standalone/plugin shell boundary and graphics-backend integration.
- Phase 1 implementation: adapter plan only; source not vendored.
- Copied code: No.

## Skia

- Source: https://github.com/google/skia
- Studied: future high-density 2D rendering backend.
- Phase 1 implementation: no source or binary included.
- Copied code: No.

## Early sample-concatenative singing-synthesis literature

- Studied: singer library, diphone/sustained-vowel units, vowel-onset alignment, pitch/timbre transformation.
- Adopted principle: note, phoneme, source unit, and destination placement must be separate domain concepts.
- Phase 1 implementation: only time/editor foundations; no synthesis algorithm implemented.
