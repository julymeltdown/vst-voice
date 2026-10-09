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
