# Module Boundaries

## Allowed dependencies

| From | May depend on |
|---|---|
| `seam_core` | C++ standard library |
| `seam_domain` | `seam_core` |
| `seam_application` | `seam_domain`, `seam_core` |
| `seam_editor_ui` | `seam_application`, `seam_domain`, `seam_core` |
| `seam_formats` | `seam_domain`, `seam_core` |
| `seam_platform` | `seam_core`, narrowly defined application ports later |
| apps | all required library modules |

## Forbidden dependencies from domain

- iPlug2
- Skia
- JSON/CBOR libraries
- SQLite
- CLAP/VST3/AU SDKs
- file-system paths as domain identity
- operating-system window handles
- audio device APIs

## Canonical state

Only the following are canonical in Phase 1:

- Project identity and settings
- Tempo and meter events
- Tracks and regions
- Notes and lyrics
- Voicebank/character references

Viewport state, spatial indices, SVG nodes, command history, and audio buffers are transient.
