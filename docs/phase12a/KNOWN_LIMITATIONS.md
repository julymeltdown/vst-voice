# Phase 12A Known Limitations

1. The production fixture is a public-domain technical bank, not a complete or
   contracted commercial Voicebank.
2. New source-tree projects may auto-bind the development fixture. Packaged
   products are expected to resolve a trusted installed `.seambank`; a missing
   bank is an explicit error.
3. Exact refresh/relink/select APIs exist, but a full graphical Voicebank
   browser and file-picker workflow remain product UI work.
4. The embedded editor still targets the first vocal track/region and stereo
   preview publication.
5. Phoneme-boundary drag, Unit variant/renderer selection and Pitch automation
   point CRUD remain incomplete.
6. Host tempo automation, loop authority, multichannel plug-in routing and live
   lyric-driven note synthesis remain outside Phase 12A.
7. Cache-hit diagnostics preserve PCM correctness but do not currently retain
   per-placement fallback telemetry in the cache payload.
