# Reference Ledger

Reviewed for functional and architectural comparison; no source code or assets were copied into the Phase 1 implementation.

## OpenUtau

- Source: https://github.com/openutau/OpenUtau
- Studied: piano-roll behavior, phoneme-preview concepts, pre-rendering workflow, separation of editor and resampler.
- Adopted principle: note/phoneme rendering concerns should be separated.
- Rejected: USTX as Project SEAM's canonical model, external-resampler process model, UI replication.
- Copied code: No.

## WORLD offline reference comparison (2026-09-22)

- Source: https://github.com/mmorise/World at `f8dd5fb289db6a7f7f704497752bf32b258f9151`.
- This is OpenUtau `8c0dc4007e6e8c8181f3a12c10205671800eeb8b`'s upstream pin. One arm is unmodified; a separate arm applies only the exact D4C denominator guard from that revision's patch. Neither uses its full patch or renderer.
- Studied: separate analysis F0, spectral envelope and aperiodicity; fixed-input reconstruction and forced-unvoiced synthesis.
- Adopted: an explicit default-OFF developer reference target, not a production renderer or fallback.
- External inputs: 25 pinned upstream source/header/license files; the separate guard directory also retains the full patch, extracted hunk and OpenUtau license. Bytes are verified against Git blobs, explicit SHA-256 pins where supplied, and a derived manifest at configure/build/run.
- Guard provenance: full patch SHA-256 `daba7824bfe790455a2d37770254fbcea535fae67b5d32b1d739298f5ec7fd89`; extracted hunk `e3c30ca24cf523f339108db3b62b181e7bff48e46f14e4abd8d49aa3233a581a`; resulting D4C file `28fdbe66ef6d8c5aaca63b35aa04471accc6aaf3f9830935ae8c068ed6addfa6`.
- License: WORLD BSD-style notice and OpenUtau MIT notice (copyright 2014 StAkira) are retained in the relevant external directories. No reference binary is installed or packaged with SEAM.
- Copied code into SEAM repository: No. External upstream C++ is compiled only for the opt-in comparison; the first-party adapter calls its public API.
- Copied media/models/voicebanks: No. The comparison uses the existing frozen SEAM speech fixture and independently generated controls.
- Evidence/policy: `docs/implementation/U16_WORLD_REFERENCE_COMPARISON_2026-09-22.md`; numerical results cannot substitute for listening or U16 qualification.

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
