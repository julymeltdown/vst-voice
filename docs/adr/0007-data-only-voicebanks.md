# ADR 0007: Voicebanks Are Data-Only Packages

- Status: Accepted
- Date: 2026-08-16

## Decision

A v1 voicebank contains declarative metadata, PCM WAV audio, analysis data, dictionaries, presets, images, and checksums. It cannot contain executable code, scripts, dynamic libraries, shaders, or embedded web content.

## Rationale

Voicebanks are untrusted user-installable content. A data-only format keeps the attack surface bounded, enables deterministic validation, and prevents third-party code from entering the real-time process.

## Consequences

- language behavior lives in approved editor phonemizer modules;
- v1 third-party banks can map aliases and phonemes but cannot execute custom native code;
- all paths must be relative and cannot contain `..` segments;
- package extraction will impose file-count, expanded-size, and nesting limits;
- future extension points require a separate security and licensing decision.
