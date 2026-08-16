# ADR 0020: Separate physical system audio from the deterministic callback fallback

**Status:** Accepted  
**Date:** 2026-08-16

## Context

A callback simulator proves the processor contract but does not prove integration with an operating-system audio service. At the same time, CI and isolated containers frequently have no audio server or hardware.

## Decision

Define `IAudioDevice` and provide two implementations:

1. a Linux PulseAudio Simple adapter loaded at runtime from the operating system;
2. a deterministic callback-clock device that owns a real thread but writes to no physical output.

The application tries the physical adapter first unless explicitly disabled. Failure is reported, then the application may opt into the callback-clock fallback. The fallback always reports `physical=false`; it is never presented as speaker output.

The PulseAudio adapter is a system integration, not a distributed third-party dependency. No PulseAudio source or shared library is copied into the repository or package.

## Consequences

- Physical output has an actual adapter and explicit error path.
- CI remains deterministic without a sound server.
- UI can display whether playback is physical or fallback.
- Windows WASAPI and macOS CoreAudio adapters still require implementation and platform verification.
