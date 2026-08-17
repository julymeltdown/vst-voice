# Phase 11 / Phase 12A Known Limitations

## Resolved by Phase 12A

- The CLAP asynchronous preview no longer uses the dedicated single-vowel
  phrase loop. It now calls the shared production Phonemizer, Unit Selector,
  Timing Solver, four Renderer, SeamComposer and Phrase cache path.
- Voicebank ID, version and content hash are persisted and resolved exactly.
  Missing, changed or untrusted installed banks produce explicit silence and a
  diagnostic rather than a hidden fallback.

## Remaining product behavior

1. Notes, lyrics and seam amount are directly editable. Phoneme timing, Unit
   variant/renderer and Pitch automation still lack full direct manipulation.
2. The embedded editor operates on the first vocal track/region and stereo
   preview. Phase 6 bus/matrix routing is not yet connected to the plug-in.
3. Host tempo changes do not yet become authoritative project rerender events.
4. Live note input remains a single-vowel sampler rather than lyric-driven
   Voicebank transitions.
5. Exact bank refresh/relink/select APIs exist, but a polished graphical bank
   browser remains.
6. Source builds may bind the development production fixture only for a newly
   created project. Release products must install and resolve trusted banks.

## Platform and release evidence

- Linux/X11 child GUI and production preview are runtime verified.
- Win32/Cocoa remain source-ready in this environment.
- Official `clap-validator`, commercial DAWs, VST3 validator and `auval` have
  not produced local PASS evidence.
- Signing, notarization and installers require release credentials and clean
  target-system tests.
- Official Voicebank 01 requires a contracted performer and directed recording.
