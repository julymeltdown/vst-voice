# Phase 8 Native Platform Matrix

| Concern | Linux | Windows | macOS |
|---|---|---|---|
| Native window | X11 | Win32 | AppKit |
| Text composition | XIM/XIC | native `EDIT` with TSF services | `NSTextInputClient` |
| Output audio | runtime-loaded PulseAudio Simple | shared/event-driven WASAPI | DefaultOutput AudioUnit |
| Recording input | PulseAudio Simple capture | WASAPI capture | HAL AudioUnit capture |
| Output channels | 1–8 | 1–8 | 1–8 |
| Character 01 | optional dock/toolbar presentation | same backend-neutral presentation | same backend-neutral presentation |
| Headless fallback | threaded callback clock | threaded callback clock | threaded callback clock |
| Local runtime evidence in current environment | yes | no; target-host CI required | no; target-host CI required |

## Shared UI contract

All three native implementations adapt the same `INativeWindowClient` contract:

```text
Native event
→ logical-coordinate input event
→ NativeEditorController or VoicebankStudioController
→ existing command/domain paths
```

The native window owns only platform events, composition, pixel presentation, DPI/backing scale, and lifecycle. Piano Roll, Phoneme Lane, Unit Lane, automation, Character 01 layout, marker validation, and recording-session logic remain shared C++.

## Windows composition boundary

The Windows implementation activates `ITfThreadMgr` for the UI thread and embeds a Unicode native `EDIT` control for active lyric composition. Windows and installed IMEs retain ownership of composition candidates and language behavior. Project SEAM receives validated UTF-16 text/selection state, converts it to Unicode scalar values, and commits through the existing undoable lyric command.

It is intentionally not a hand-written TSF text-store implementation. This avoids replacing mature OS composition behavior while still using TSF-backed native text services.

## macOS composition boundary

The AppKit view implements `NSTextInputClient`. It publishes marked/selected ranges, handles `setMarkedText`, `insertText`, deletion and command selectors, and maps the requested lyric-cell rectangle to screen coordinates so the OS can place candidate windows correctly.

## Real-time audio boundary

```text
Render/route/feeder thread
→ multichannel SPSC ring
→ platform callback adapter
→ IAudioProcessor::process(preallocated channel spans)
```

WASAPI and CoreAudio do not traverse the Project, read files, parse JSON, allocate callback buffers, or invoke the synthesizer in the real-time path. Recording callbacks similarly forward bounded mono spans into `IAudioInputProcessor`.

## Character boundary

Character 01 is rendered by the shared native scene. A missing or disabled character package changes only presentation. The character does not alter:

- Project routing;
- selected Units or renderer settings;
- Phrase render identity;
- PCM cache identity;
- signed `.seambank` trust;
- output-device selection;
- exported audio.
