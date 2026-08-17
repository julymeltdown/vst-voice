# Project SEAM Current Status

**Date:** 2026-08-17  
**Branch policy:** `master` only  
**Product maturity:** **Feature Alpha**

Project SEAM has a substantial sample-concatenative synthesis core, native editor foundations, signed `.seambank` distribution and loadable CLAP vertical slices. It is not yet a commercial Release Candidate.

## Status summary

| Area | Status | Meaning |
|---|---|---|
| Score/phoneme/unit/seam domain | Implemented | Canonical project model and undoable commands exist. |
| Synthesis core | Implemented | Raw, Classic PSOLA, SpectralClassic and Stretch paths exist. |
| Linux/X11 standalone | Feature Alpha | Real window, input, playback and Voicebank Studio vertical paths run. |
| Windows/macOS adapters | Source Ready | Source and CI contracts exist; target runtime certification is absent. |
| Signed `.seambank` | Implemented | Signature, trust and transactional installation exist. |
| CLAP render player | Implemented | Pre-rendered PCM playback synchronized to host transport. |
| CLAP embedded editor | Partial | Note/lyric/seam editing, visible technical lanes, async demo preview and live sample input work on X11. |
| VST3/AU | Source Ready | Wrapper contract only; no target binaries or validator evidence. |
| Character 01 | Integrated, not final | Runtime asset exists; final naming, production 3D asset and commercial rights review remain. |
| Official Voicebank 01 | Not complete | The public-domain fixture is not a contracted production voicebank. |

## Important Phase 11 boundary

The Phase 11 plug-in directly edits notes, lyrics and seam amount. Phoneme, unit and pitch lanes are rendered, but full direct manipulation of phoneme timing, unit selection/renderer and pitch points is still outstanding.

The plug-in preview currently loops and pitch-shifts one embedded public-domain human-vowel sample. It does not yet invoke the complete production pipeline already available in the standalone engine.

See [`REMAINING_TASKS.md`](REMAINING_TASKS.md) and [`RELEASE_READINESS.md`](RELEASE_READINESS.md).
