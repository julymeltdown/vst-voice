# U40 — Editable harmony workflow foundation (2026-09-08)

`prepareHarmony()` creates an inert interval proposal from a selected region or
an explicit note set. It validates the interval, target MIDI range, source
ownership and bounded count before allocating fresh note/lyric IDs through a
detached `ProjectFactory`. Lyric surface/language and source vibrato/articulation
are copied, but each harmony note receives its own lyric token so a copied
layer cannot silently become a shared-melisma continuation.

`AddHarmonyCommand` is the canonical acceptance boundary. It checks the exact
source note vector, rejects stale regeneration and identity collisions, validates
the complete staged region, and publishes the generated layer as one
phrase-audio undo group. Undo removes only the accepted generated IDs; redo
reapplies the same payload after the source check. The command never mutates
the source during preparation.

`seam_harmony_workflow_tests` covers side-effect-free preparation, lyric
language preservation, interval/range/missing-note rejection, stale acceptance,
atomic apply, exact undo/redo and generated identity isolation.

This is the deterministic interval fallback/foundation, not learned automatic
performance, scale-aware voicing, harmony UI, or final creator qualification.
Those remain U38–U40 acceptance work and must be connected to the neural/classical
performance contracts before Beta GO.
