# U1 Shared Authoring Runtime Completion

## Purpose

U1 removes the architectural split in which the CLAP editor used the real
sample-concatenative production renderer while the Standalone application
used a hard-coded project and sine-wave timeline.

## Final ownership

```text
Standalone adapter ──────┐
                         ├── AuthoringRuntime
CLAP adapter ─────────────┘

AuthoringRuntime
├── ProjectDocument
├── VoicebankSession
├── TechnicalEditController
├── AuthoringRenderCoordinator
└── TransportController
```

## Completed user-visible contract

- Both adapters mutate the same canonical project model.
- Exact voicebank identity is ID + version + synthesis content hash.
- Every audio-affecting standalone edit submits production rendering.
- Older render revisions cannot replace newer edits.
- The latest production PCM is published to the shared transport.
- Physical and threaded audio adapters consume the multichannel ring buffer.
- Character presentation remains outside PCM and cache identity.

## Deliberately deferred

U1 does not implement the project lifecycle, a user-facing voicebank browser,
master/stem export, a production-quality demo voicebank, or Apple Silicon
acceptance. Those are U2 through U9 and continue to block the Usable Alpha
gate.
