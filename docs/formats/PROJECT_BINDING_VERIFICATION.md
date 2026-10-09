# Native project binding verification

Engineering prerequisite for U45; not installed-product observation or release evidence.

```
seam_voicebank_cli verify-project-binding PROJECT SHA256 TRACK_ID REGION_ID FAMILY RESOURCE_ID VERSION CONTENT_HASH LANGUAGES
```

All expectations are supplied by the caller. IDs use canonical 16-digit lowercase
hexadecimal (pad shorter stored project IDs with leading zeroes). FAMILY is sample, recipe or model. LANGUAGES is a sorted, unique,
comma-separated set drawn from en,ja,ko. A mixed-language region is supported;
the observed set must equal the requested set, not merely contain it.

The POSIX held-input reader captures at most 64 MiB from one regular, no-follow
file descriptor, checks metadata consistency, and hashes the captured bytes.
ProjectJsonCodec decodes that same buffer and validates the entire native domain
model. The native JSON parser rejects duplicate keys. Every raw lyric language
must be en,ja,ko or und, including unused tokens and other regions; unknown tags
are refused before they can be accepted as the codec's normalized und value.
A known und token may be unused, but no note in the selected region may use it.
The record names both sourceSchemaVersion and codecSchemaVersion: decoding an
old file observes the current codec's migration, not historical playback parity.

The selected track must contain the selected nonempty region. Every note's lyric
reference must exist. Only note-linked token languages contribute to the binding;
unusedLyricCount reports tokens that do not contribute. Shared-token melisma counts
one linked lyric and multiple notes. Text language tags are declarations, not
language detection, pronunciation correctness, phonemization or singing evidence.
The record does not claim another region or track was bound to these expectations.

The renderer and verifier share selectedSingerResource. Dual recipe/neural
selections refuse; otherwise neural precedes recipe, then the sample reference.
The record labels the stored voicebank SELECTED for samples and INACTIVE for the
other two families. No recipe path, media path, model helper or cached pronunciation
claim is opened or executed. Empty sample identity cannot satisfy the caller's
required nonempty identity and lowercase SHA-256 expectations.

| Family | Project reference verified | Future installed-resource linkage |
| --- | --- | --- |
| sample | Stored voicebank id, version, bank contentHash | Compare independently verified installed sample identity and content hash. |
| recipe | Recipe id, recipe schema version, canonical recipe SHA-256 (render identity) | Candidate content SHA may match this canonical recipe hash. Package id/version and installedContentHash are different identities and must not be substituted. Verify the actual recipe/manifest mapping as well. |
| model | Stored neural resource id/version/contentHash | No U14 installed-model record currently exists. Package refusal evidence cannot establish runtime admission. |

The closed `seam.u45.project-binding-verification.v1` record fixes
bindingScope to SELECTED_REGION_NOTE_REFERENCES, languageEvidence to
DECLARED_LYRIC_LANGUAGE, resourceAdmission and runtimeAvailability to NOT_CHECKED,
phonemization/playback/hostExecution/humanAcceptance to NOT_RUN, and qualification
to NOT_QUALIFIED. authorizesRelease and releaseEligible are false.

`scripts/verify_project_binding_record.py` requires caller-supplied record/project/
CLI paths and independent record/CLI SHA-256 pins. It validates the closed record,
runs the fixed native command using its expected identities as data arguments,
rechecks the CLI hash, and compares the complete fresh output. Paths and the
command cannot come from record fields. Rehashed fabricated counts or bindings
cannot pass merely by matching a retained record's schema. Changing both project
and record is a new input claim, not authenticated observation provenance.

Exit 0 is ENGINEERING_PASS only. Replay reuses bounded process handling and an
owned scratch directory. Windows is explicitly unsupported pending its held-input
and interactive evidence. This code neither selects nor promotes the canonical
83-case report. U45_RECONCILIATION_HOLD remains intact. Later observation records
must bind the exact project digest, track/region, verified installed resource,
operation, build/platform/host and resulting artifacts. Native operation,
continuity, recovery, measurement and external authority remain separate work.

## Linking verified installed contents to a project

```
seam_voicebank_cli verify-installed-project-binding PROJECT SHA TRACK REGION LANGUAGES PACKAGE PACKAGE_SHA CANDIDATE_SHA INSTALL_DIR KEY
```

This command verifies the caller-selected signed package and installed directory
through the U14 native verifier, then binds the captured project to the resulting
resource identity. It derives a recipe's id, schema version and canonical content
hash from recipe bytes read within the same private verified package snapshot.
It does not reopen a project-supplied recipe path. That canonical hash must equal
the signed candidate's content identity. Samples use the verified bank identity.
Opaque models remain explicitly unsupported by installed-resource verification.

The nested installed v2 and project-binding v1 formats remain unchanged. The new
closed `seam.u45.installed-project-binding.v1` record includes those two records,
the signed `resourceLanguages`, and a `SIGNED_DECLARATION` coverage result. Every
note-linked project language must occur in that declaration. Unknown/unspecified
note languages still refuse. A resource may declare und, but that does not cover
en, ja or ko and cannot establish pronunciation or singing support.

The native verifier derives the expected project reference from actual verified
resource bytes, not from retained composite claims. The Python replay pins the
record, CLI and trusted key, invokes the fixed combined command with caller paths,
and compares its complete fresh output. It checks family-specific cross-record
identities and rejects legacy installed v1 nesting. For recipes it does not equate
package id/version or installedContentHash with the project reference: fresh
native derivation verifies recipe id/schema version, and candidateContentSha256
must match the project's canonical recipe hash. Rehashing forged nested identity
claims is insufficient.

`scripts/verify_installed_project_binding_record.py` returns ENGINEERING_PASS only.
Verification is sequential and records observed consistency, not one atomic
filesystem snapshot or a guarantee that files remain unchanged afterward. A
byte-identical copied installed directory may pass. Catalog placement, installation
event/durability, runtime resolution, execution of the stored recipe path, actual
rendering, selected-style coverage, language quality, host provenance, and human acceptance remain unproved.
The nested project's resourceAdmission therefore stays NOT_CHECKED. Both nested
records and the composite retain false release-authority flags. The canonical
U45 hold is unchanged; actual operation/artifact observation remains required.

## Schema 21 provenance checkpoint

The codec accepts the strict optional installation binding described in
[Project JSON schema 21](PROJECT_JSON_V21.md). Fresh records report codec 21;
older source schemas remain readable and are named in `sourceSchemaVersion`.
The standalone project verifier validates the binding's shape only. The composite
additionally compares every present saved installation pin with the identity
derived from its verified signed package capture: distribution id/version,
installed content hash, engine id/revision, recipe entry, package digest and signer
key id. Missing receipt-derived package/signer pins in a present binding do not
match a trusted signed installation. Re-select the verified resource to capture
its identity; the verifier never repairs or fills pins.

The comparison uses the existing single project-byte capture and applies equally
to absolute references and relative copies that retain origin provenance. It does
not open either stored path or establish its relationship to the caller's installed
directory. Legacy or explicitly unbound references remain reference-only matches;
success must not be interpreted as proof that a saved installation binding exists.
The closed v1 record shape and its content-to-reference claim remain unchanged.
Product rendering consumes the separate shared admission boundary described in the
schema document; these records do not observe runtime admission, compiled-engine
compatibility, playback or installed-product acceptance. NOT_CHECKED stays intact.
Historical byte-exact replay requires the historical pinned verifier.

## Production render source binding

Production project rendering and export share a metadata preflight before source
file capture, PCM-cache access or export staging. Explicit procedural/neural
selections must match the supplied family and resource; procedural style and
file-reference pins are also compared. Sample sources must match every persisted
nonempty id/version/content-hash field. An omitted legacy sample pin remains
unknown and is not filled or treated as verified. A wholly unselected authoring
track may still use an explicitly supplied raw source for Designer/pilot work.

Ordinary audio rendering ignores muted/non-solo tracks. Exporting project/recipe
or candidate contents checks every supplied source, including muted tracks, so
packaging cannot silently replace a saved singer. Unknown and duplicate source
tracks remain errors even when muted. This preflight compares metadata only;
sample asset integrity, installation trust, and immutable observation provenance
are separate obligations. Lower-level snapshot APIs and direct PCM publication
do not become authenticated observation producers. The U45 record's NOT_CHECKED
fields and acceptance hold remain unchanged.
