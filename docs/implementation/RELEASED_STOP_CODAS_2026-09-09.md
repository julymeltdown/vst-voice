# Explicitly released stop codas

Procedural p/t/k source bindings can now render a final coda as well as an onset.
The coda requires an explicit or inferred post-nucleus start, a consistent
same-note vowel nucleus, enough room for closure plus the nominal burst, and a
nonoverlapping preceding vowel. Existing timing-policy allocation can supply the
simple untimed coda span. Voicing closes during the stop; the finite burst ends
at the coda end. Unassigned gaps remain silent.

Frication codas are not implicitly admitted by this change. Unsupported symbols
such as kcl are not relabeled as released k. Language-specific unreleased stops,
geminates, affricates and context-sensitive release behavior remain unfinished.
The recipe's chosen source describes a released gesture, not a claim that every
phonetic context or language should use that realization.

Articulation-plan and stream revisions advance to 6, and procedural renderer
revision to 11, invalidating old render identities. Existing candidate-v4 typed
Plosive markers can represent either position without changing that file schema.

## Verification

Full Release build passed. Voice design, performance snapshot, export and
aggregate-core suites passed in 28.10 seconds. New source/stream tests cover p,
t and k, exact silent closure, finite final burst, terminal zero sample, prefix
replay inside the burst, overlap rejection and unsupported-symbol rejection.

A new real export test uses an explicit English reading `aa1 k`. The resolved
coda occupies frames [21120,24000) in a 24,000-frame, 48 kHz phrase: closure
[21120,23520), burst [23520,24000). Its Float32 candidate-v4 bake reloads with a
typed final Plosive marker and sample-for-sample equality to the render. This is
not an assertion that the engineering source sounds natural or intelligible.

Logs and a generated bake are retained under `evidence/released-coda-2026-09-09/`.
This was not a new full-suite run or listener/native-UX qualification. No new
whole roadmap unit is accepted; the full Beta GO scope remains open.

No files were missing relative to `session-preservation-ciyupg`. Existing dirty
work remains preserved, with no staging, commit or push.
