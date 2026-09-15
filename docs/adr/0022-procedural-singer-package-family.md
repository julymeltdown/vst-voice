# ADR 0022: Make the procedural singer a second family over the shared signed container

**Status:** Accepted
**Date:** 2026-09-15

## Context

SEAM needed to distribute an original procedural singer — a recipe plus declared facts — with the
same authenticity guarantees a sample bank already has. Two designs were available: extend the
sample-bank manifest with optional fields, or add a second family over the primitives the bank
already proved.

Extending `voicebank::Manifest` was rejected. Its fields describe recorded units, takes and
coverage; a recipe has none of those. Filling them with invented or empty unit records to satisfy a
validator would make the bank contract say things that are not true, and would let a recipe be
mistaken for recorded material by every existing consumer.

Revision 1 of the development plan also required this decision to be written down before the
packaging code was written. The code landed first; this ADR records the contract as built so the
decision does not exist only in the implementation.

## Decision

A procedural singer is a **separate typed family over the shared signed container**. The container
that carries entry tables, per-entry digests, one Ed25519 signature, path policy, size bounds and
durable publish is family-neutral. A sample bank and a procedural singer are each one family over it.

```text
SignedContainer  (entry table, digests, detached Ed25519 signature, path and size policy)
        |
        +-- sample bank family        formatId "com.project-seam.voicebank"   (.seambank, v1)
        +-- procedural singer family   formatId "com.project-seam.procedural-singer" (v1)
```

Consequences that follow, and are binding:

1. **`.seambank` v1 interpretation is unchanged.** Existing packages keep their meaning; no existing
   manifest requirement was deleted or weakened.
2. **Families refuse each other explicitly.** A procedural manifest is never read as a bank manifest,
   and a bank manifest is never read as a procedural singer. A mismatched read is an error, not a
   best-effort parse.
3. **Four version fields stay distinct** and must not be collapsed into each other: the distribution
   release version, the recipe schema version, the recipe content identity, and renderer
   compatibility. `ProceduralSingerManifest::kSchemaVersion` versions the manifest shape only.
4. **A procedural package carries data for the first-party renderer.** It never carries or selects an
   arbitrary executable, and admission checks that the declared recipe decodes with this build.
5. **Signing is authenticity, not qualification.** `ProceduralSingerManifest` records what the
   producer *states* — language, styles, phones, engine revision. No field in it may be read as a
   review outcome; reviewed status is bound separately, to an exact candidate.

## Consequences

- The sample-bank validator and its consumers are untouched by procedural support.
- Procedural admission recomputes the recipe digest over the recipe's **canonical** encoding, which
  is the identity a project stores and the renderer validates. Comparing raw file bytes was refused
  for the wrong reason and accepted a merely byte-similar recipe; that is now pinned by a test.
- Native selection records the installed identity plus the installed recipe path, so an installed
  selection is portable only through the same identity resolution a bank reference uses.
- Review binding, still open, is the next required addition: a decision must resolve to a candidate
  only when the candidate's basis digest is unchanged.

## Alternatives considered

- **Optional fields on `voicebank::Manifest`.** Rejected: it makes the bank contract assert recorded
  material that does not exist and invites every consumer to misread a recipe.
- **A separate container implementation.** Rejected: it would duplicate signature, digest, path
  safety, bounds and durable-publish logic that is already tested, and create two places for the same
  vulnerability.
- **Carrying an executable in the package.** Refused outright: a package must never select arbitrary
  code to run for a render.


