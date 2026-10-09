# Resource candidate descriptor v3

`candidate.json` identifies engineering material before packaging, installation or qualification.
It is not a release approval. `format` is `com.project-seam.resource-candidate` and
`schemaVersion` is 3. `releaseEligible` is always false, `evidenceScope` is
`engineering`, and `qualification` is `NOT_QUALIFIED`. Unknown fields and relaxed
honesty values are refused.

## Identity and dependency closure

The descriptor records resource id/version/display name, languages, styles, optional
character identity, root manifest and content hashes, embedded payload/evidence files,
and explicit external runtime dependencies. Every embedded file except `candidate.json`
is listed exactly once, in path order, with role, byte count and SHA-256. Missing,
additional, altered, linked and special files are refused. Empty directories are not
resource dependencies. The candidate identity is the hash of the exact descriptor bytes.
This is bounded directory verification, not a claim of a signed or immutable archive.

The canonical `resourceKind` remains distinct from the C++ payload family:

| Canonical kind | Payload family | Status |
| --- | --- | --- |
| `sample-real` | sample | `REVIEWED_CANDIDATE` |
| `sample-procedural` | sample | `REVIEWED_CANDIDATE` |
| `recipe-original` | recipe | `DECLARED_CANDIDATE` |
| `neural-original` | model | `DECLARED_CANDIDATE` |

A sample's kind must match every unit binding and the captured source strategy in
its producer snapshot. Human recordings and synthesized material cannot be relabeled
or mixed under one kind. TTS-derived material retains its procedural classification;
this does not supply missing rights. Standalone dictionary/character candidate families
are not implemented by this unit and remain separate full-scope work.

Samples include reviewed audio, manifest, source/license/history evidence and any
measured `analysis/*.json` sidecars. Sidecars use the `acoustic-analysis` payload role,
are tied to manifest unit paths, and contribute to dependency/content identity.
Unmeasurable units retain the existing explicit absence semantics; no fabricated
analysis is added. Publication synchronizes the analysis directory before committing.
A manifest character reference requires an embedded character package; the producer
publisher currently refuses references for which it cannot supply that package.

Recipes and models have declared metadata, no producer review/source claim, and an
explicit render-engine or neural-runtime dependency. A valid descriptor does not
prove that a model is learned, intelligible, rights-cleared, or musically qualified.

## Legacy formats

Schema 1 is the original sample-only descriptor. Schema 2 is master's earlier
source-aware sample format with canonical kind, languages and flat character fields.
Both remain readable under their exact original shape. Schema 2 applicability and
source kind are checked against its manifest and captured source provenance.
Neither has a complete payload list: `declaresDependencySet()` returns false, their
version is preserved, and the writer refuses to re-encode them as v3. Extra regular
files therefore cannot be ruled out by legacy named-file verification. Re-export
from the reviewed producer to obtain v3; do not treat a legacy read as proof of
complete dependency closure. The incompatible archived typed-v2 proposal is not
silently accepted as either production legacy-v2 or v3.

## Pinned publication

`publish-sample WORKSPACE MANIFEST GENERATION PROJECT_SHA256 OUTPUT_DIRECTORY`
builds from the named immutable generation, verifies its bytes and requires the same
unit approvals to remain in force in the latest durable generation. A withdrawn
review or a binding from another generation refuses publication. The producer writer
lock protects the final check/publication boundary. Existing output is never replaced.

This first integration unit does not sign packages, install resources, grant resource
qualification, or finish U14. Atomic installation and distinct package/install actions
follow as separately reviewed units.
