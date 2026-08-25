# Phase 12C Live Articulation Evidence

The engineering demo is `seam_phase12c_live_demo`. It builds immutable resources from the configured Voicebank candidate, publishes them through `seam::live_voice::VoiceEngine`, runs note, legato, expression, MIDI, steal, release, and all-notes-off events, then writes finite stereo PCM and a machine-readable summary.

```sh
build/phase12c/seam_phase12c_live_demo --output /tmp/project-seam-phase12c-live
```

The summary binds the output to the Voicebank ID, version, and content hash and records unit count, energy, note-ons, steals, transition fallbacks, event overflow count, rendered frames, finite-output status, and the final result. This is engineering evidence only; the official validator, target-machine deadline benchmark, and 7,200-second soak remain mandatory release gates.
