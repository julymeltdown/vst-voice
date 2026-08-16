# Native Platform Adapter Architecture

## Dependency boundary

The platform-specific implementations satisfy existing ports:

```text
INativeWindow
IAudioDevice
IAudioInputDevice
```

No Win32, AppKit, WASAPI, or CoreAudio type appears in the domain, application, synthesis, rendering, routing, voicebank, distribution, or character package APIs.

```text
Editor/Voicebank controller
        │
        ▼
INativeWindow ─────────────┬─ X11/XIM
                           ├─ Win32/native EDIT + TSF services
                           └─ AppKit/NSTextInputClient

Playback processor
        │
        ▼
IAudioDevice ──────────────┬─ PulseAudio
                           ├─ WASAPI
                           ├─ CoreAudio
                           └─ explicit threaded fallback

RecordingSession
        │
        ▼
IAudioInputDevice ─────────┬─ PulseAudio capture
                           ├─ WASAPI capture
                           ├─ CoreAudio HAL capture
                           └─ explicit synthetic fallback
```

## Build selection

CMake selects sources by platform and defines one backend macro for each port. The unavailable factories compile only when no real platform macro is selected. `SEAM_BUILD_NATIVE_DESKTOP=OFF` removes native-window applications while preserving all headless domain, synthesis, routing, package, and render tests.

## Real-time rules

Audio callbacks may:

- access preallocated spans;
- read from the SPSC/interleaved ring processors;
- mix/copy/clamp finite samples;
- publish atomic statistics.

They may not:

- allocate or resize containers;
- parse JSON or manifests;
- touch SQLite or filesystem state;
- traverse the project or routing graph;
- perform voicebank analysis;
- write logs synchronously;
- mutate editor or character state.

## Text input rules

Composition belongs to the native text system. The native adapter converts platform text/ranges to the shared `TextInputRequest`, `textComposition`, `textCommit`, and `textCancel` contract. Final changes enter the project only through existing application commands, preserving Undo/Redo and Unicode persistence.

## Runtime verification policy

Source integration, Linux runtime verification, and multi-platform compilation are different evidence classes:

1. Linux adapters are runtime-tested in the current environment.
2. Windows and macOS source integration is protected by static source-contract tests.
3. CI matrix jobs are configured to compile and test each native target on its own operating system.
4. Hardware speaker, microphone, IME, DPI, focus, and lifecycle testing remains mandatory before public release on each platform.

A source implementation is not described as hardware-certified until step 4 is complete.
