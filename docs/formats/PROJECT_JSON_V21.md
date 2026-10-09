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
component by component and be lexically normalized (no `.` or `..` segments);
this permits deriving a nested installation root.
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

## Runtime admission

File-backed preview, single-file and set export, CLI bake-project, recipe
packaging and saved-score generation use shared procedural admission. A bound
absolute path must resolve to its pinned manifest/recipe identity and matching
receipt provenance. The actual compiled engine ID and revision must match both
the binding and manifest; there is no implicit revision migration. Required
installed trust is the existing matching-receipt policy, not fresh signature
verification. Explicit permissive application configuration remains supported:
when receipt provenance was absent at selection, an untrusted installation with
empty receipt pins can still be admitted under that policy; default strict
admission requires a matching trusted receipt.

| Reference | Admission behavior |
|---|---|
| Bound absolute installation | Derive root from recipeEntry; check full binding, manifest, recipe, engine, selected declared style and configured trust. Missing or changed required metadata or pinned provenance refuses. |
| Bound relative project copy | Require saved project directory, exact recipe identity and compatible pinned engine; retain provenance without requiring the original installation or claiming installed trust. |
| Unbound absolute legacy selection | Recognize configured/default catalogue roots or bounded typed adjacent/ancestor metadata; apply installed/development checks if recognized. Otherwise admit as authored. |
| Unbound relative recipe | Load from saved project directory as a project copy with exact recipe identity. |
| Development root | Require explicit development permission and compatible manifest engine; never claim installed provenance. |

Known roots retain lexical and canonical path classification, including macOS
path aliases, so escaping through a symlink cannot silently become authored.
Installed recipe parent directories and files cannot be symlinks. An unbound old
path outside known roots with all installation metadata removed remains
intrinsically ambiguous and can only be classified as authored. No missing
historical provenance is invented. Unrelated valid adjacent manifests are ignored;
unsafe, unreadable or malformed directly adjacent metadata refuses conservatively.

Admission freezes the same captured recipe bytes checked with the manifest and
receipt. The immutable admission object accompanies that request's frozen source;
rendering that capture does not reopen its installation. Each new product
file-backed request re-admits before PCM-cache use. A raw frozen source with a
bound absolute reference must acquire admission; low-level authored/portable
frozen sources remain supported. Direct snapshot creation checks stored engine
compatibility but does not independently inspect installation files.

Saved-score generation admits the source before preparing the job, retains the
binding on its owned relative `recipe.json`, and leaves the original score
unchanged. The prepared job can render after the source installation disappears.
Portable export likewise retains origin pins while using its own captured copy.
Earlier schema21 jobs retaining an absolute bound path are not rewritten in place;
rendering now checks that installation and may refuse it. Re-prepare those jobs
from an admitted score for portable copies, preserving the original job/build.
Checks are sequential filesystem observations, not an atomic tree snapshot or
protection against concurrent hostile replacement. The manifest/recipe identity
and matching receipt do not replace independent signed-package/tree verification,
actual installed-product operation evidence, or human acceptance.

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

Generation requests registered from those campaigns remain readable through CLI
list/inspect and Studio's request list. Studio output inspection still admits the
campaign and refuses an old frozen identity. Advancing or retrying them is refused at admission, including a
request with a recorded outcome. A newly planned campaign can be submitted when
its normal producer-generation and take-availability checks pass. This checkpoint
does not repair historical-outcome retrieval or provide an in-place migration.
Bound and unbound copies of the same compatible recipe render identical PCM but
have different snapshot/cache identities; the provenance remains part of intent.
