# Product Principles

## Audio

1. The source of every sung sound is a licensed recorded sample.
2. A phoneme boundary is a first-class editable object.
3. Sample-to-sample timbre differences are not automatically normalized away.
4. Digital impulses, clipping, DC jumps, buffer underruns, and corrupted timing are defects.
5. Audible concatenation, source-pitch residue, and loop repetition may be intentional expression.
6. Automatic decisions remain inspectable and replaceable by the user.
7. The same project, engine version, voicebank hash, and render settings must produce deterministic output.

## Editor

1. Note, lyric, phoneme, sample unit, and seam are separate concepts.
2. Musical time is stored as integer ticks.
3. Audio positions are stored as integer sample frames.
4. UI position is never the source of truth.
5. Every destructive edit must be reversible.
6. Rendering and file I/O never run in the real-time audio callback.
7. Character presentation is optional and never obstructs the editing canvas.

## Character

1. Character 01 represents the first voicebank and concatenative engine; she is not a singer biography.
2. Emo is conveyed through coherent visual language, not identity slogans or accumulated clichés.
3. Self-harm imagery, fake underground provenance, and unlicensed band marks are prohibited.
4. The low-poly identity must come from topology, texture, and shading rather than a post-processing filter.
