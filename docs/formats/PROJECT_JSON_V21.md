# Project JSON schema 21

Schema 21 adds the required `installation` member to every non-null
`proceduralRecipe`. It is either `null` (authored/development/legacy recipe), or
an object with exactly these eight fields:

| Field | Meaning |
|---|---|
| `distributionId` | Installed manifest ID; distinct from the recipe ID |
| `distributionVersion` | Installed manifest release version; distinct from recipe schema version |
| `installedContentHash` | Existing double-SHA256 identity over manifest and recipe bytes |
| `engineId` | Declared rendering engine |
| `engineRevision` | Positive uint32 engine revision |
| `recipeEntry` | Normalized slash-separated relative package entry |
| `packageDigest` | Receipt-reported package SHA256, or empty when unavailable |
| `signerKeyId` | Receipt-reported signer key ID, or empty when unavailable |

Digests/key IDs are lowercase 64-character hexadecimal strings when present.
The installed content hash is required. Unknown keys, missing fields, wrong
JSON types, unsafe entries, zero/fractional/overflowing engine revisions and
malformed digests are refused. Absolute bound paths must end with `recipeEntry`
component by component; this permits deriving a nested installation root.
Relative project copies may use a different filename, such as
`recipes/<recipe-content-hash>.json`.

Schemas 1–20 retain their existing shapes and load with no binding. Loading or
saving an old project does not infer installation provenance. Selecting an
installed singer, creating a new installed-singer song, or Studio's installed
song handoff captures the current candidate's binding. A stale supplied New
Project binding is refused; older unbound New Project requests are stamped only
after resolving the current candidate. Development-root selections remain
unbound. Installed-root selections retain pins even under permissive trust
policy. Receipt fields do not constitute independent signature verification.

Portable export retains the binding while replacing the path with the packaged
recipe path. Copy Installed Singer to Draft and explicit Select Procedural Recipe
create authored, unbound references. Generic relink refuses a bound reference;
the user may reselect an installation, copy an available installation to a draft,
or explicitly replace the selection with an authored recipe. Undo/redo and
performance-job freshness include the complete binding.

## Checkpoint boundary

This increment persists provenance. Shared render-time admission is still
**NOT_CHECKED**: the binding does not yet enforce installation existence, receipt
trust, manifest/content freshness or the declared engine revision at rendering.
Bound and unbound copies of the same compatible recipe retain the same PCM.
Do not promote this checkpoint to installed-runtime or human acceptance.

The next unit must consume this binding at preview, export, CLI bake and
packaging boundaries, reject missing/changed bound installations and incompatible
engines, and distinguish relative copies from installed resources. Legacy
unbound absolute files require conservative adjacent-metadata/known-root
handling; ambiguous old authored paths cannot establish installed provenance.

Fresh U45 records report `codecSchemaVersion: 21`. Their native project/composite
formats are otherwise unchanged at this checkpoint: they do not yet cross-check
the installation binding. Historical records replay with their original pinned
verifier binaries; a newer codec is not interchangeable for byte-exact replay.

## Frozen campaigns, prepared jobs and cache identities

Render snapshot identities and generation campaign template identities hash the
encoded project, including `schemaVersion` and `installation`. Schema 21 therefore
changes render identities, campaign template identities and their derived job
IDs, and the render hash pinned by each prepared generation job. Campaigns planned
and jobs prepared by a schema-20 or earlier build do not admit, load or resume
in this build. Re-plan or re-prepare from the current producer, preserving the
old artifacts and their original build for historical evidence. Earlier project
codec bumps since campaigns were introduced at schema 9 had the same effect;
ordinary project-file migration does not migrate frozen production identities.

Generation requests registered from those campaigns remain readable through list
and inspect. Advancing or retrying them is refused at admission, including a
request with a recorded outcome. A newly planned campaign can be submitted when
its normal producer-generation and take-availability checks pass. This checkpoint
does not repair historical-outcome retrieval or provide an in-place migration.
Bound and unbound copies of the same compatible recipe render identical PCM but
have different snapshot/cache identities; the provenance remains part of intent.
