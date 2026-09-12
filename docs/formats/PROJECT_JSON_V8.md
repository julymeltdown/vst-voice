# Project JSON schema 8: implementation contract

Status: in-development U3 contract, not a released format or a completed Full-Scope Beta milestone. This document describes implemented expression/style fields and region-owned performance state. Automatic revision stamping, stable phoneme-edit identities, take-acceptance commands and remaining resource/expression integration are unfinished. Coupled ownership edits and lyric-edit reconciliation are implemented within the boundaries below. Do not treat this intermediate writer as a frozen public schema.

## Version and compatibility

`ProjectJsonCodec` writes integer `schemaVersion: 8` and reads versions 1–8. Fractional or future versions fail decoding. Existing identifiers, tick coordinates, audio metadata, routing, pitch automation and override fields retain their version-specific contracts.

Versions 1–7 receive these migration defaults in memory:

- Disabled note vibrato and no phonetic hint.
- Empty region dynamics, interpreted as unity gain.
- Unresolved legacy style intent; the codec does not inspect the filesystem or select a bank.
- A default fifth technical-lane presentation entry for Dynamics.
- Empty region performance state: zero revisions, no pronunciation identity, ownership, takes or accepted selections.
- Existing phoneme/unit/seam overrides retain their payloads but become unresolved; no verified source context is invented.

Schema 7 requires its original four technical-lane entries. Schema 8 requires five, in order: Phoneme, Unit, Seam, Pitch, Dynamics. The stored fifth entry is not evidence that the Dynamics editor has been implemented. Presentation modes are `auto`, `collapsed`, `preview`, `expanded`; expanded heights are finite values from 96 through 640.

## Phoneme override resolution state

Schema-8 phoneme overrides require boolean `unresolved`. A true value retains the original key, symbol, timing and lock as user data, but the Japanese adapter does not apply it and emits an orphan-override warning. Single and batch lyric commands reconcile unedited Japanese base sequences before publication. Only unambiguous ordered correspondences rebind; removed/ambiguous sounds, unsupported contexts, invalid bounded inputs or ordinal-key collisions retain edits unresolved. Already unresolved edits do not silently reactivate on a subsequent lyric change. Undo/redo restore the exact prior/reconciled state together with lyrics.

The in-development schema-8 override also requires nullable `sourceContextId`; non-null values must be lowercase SHA-256 addresses. New/changed explicit phoneme commands bind to unedited base resolution, including a domain-separated address for appended slots. A supplied stale binding rejects a changed command before mutation. Shared resolution checks bound records against the current base and suppresses mismatches in its effective copy with an unresolved warning, retaining original saved data. Lyric reconciliation verifies a bound original before assigning the matched new context. Bindings participate in input identity and round-trip/undo data. Null means unbound, not verified: existing unbound-record migration and topology/resource-change rebinding remain open, and this field does not authorize generated take acceptance.

`TechnicalEditController::reviewPhonemeBindings()` supplies a read-only list of retained unresolved/unbound/stale edits, current unedited target tokens with context IDs, and resolver warnings. `rebindPhonemeOverride()` requires the exact reviewed source payload and chosen target context, rejects changed sources/contexts or occupied target keys, and commits a deliberate rebind as one editor undo step and one edit notification. It preserves the retained payload while replacing its key/context and clearing unresolved state. Standalone and embedded editors expose it through the Review phoneme edits overlay, with no initial target selection, navigation, Apply and Close/Escape. The modal blocks background input and excludes background notes/disabled controls from keyboard traversal. Unit/seam review is still outstanding.

Review targets exclude notes with base-resolution warnings and notes whose declared language is unsupported by the current Japanese path. Pause fallbacks for missing/unsupported lyrics are not presented as verified sounds. Their retained edits and warnings remain in the review, and unaffected notes remain selectable. Rebinding rechecks this eligibility along with the target context, so a stale target cannot bypass a later language/resolution problem.

Region/track cloning and region splitting now transfer bound phoneme contexts through explicit source-to-destination note maps. Only an original binding that verifies against current source resolution, with unchanged edit payload and unchanged warning-free sounds/language, receives a destination context address. Stale/unresolved bindings are not promoted. A split that loses a continuation vowel's preceding context retains its edit unresolved. New region/note/lyric IDs are included in the destination address; immutable generated take provenance is not rewritten. Unbound-record migration and broader resource-change recovery remain separate obligations.

Selected-note duplication now also copies note-scoped phoneme overrides and transfers verified bindings in the existing composite transaction after note creation. It retains before/after override vectors alongside performance state so undo removes copied dependencies before undoing the new notes. Phoneme-key collisions and collection-limit violations reject before publication. Original note mappings are included when validating the resulting region's bound contexts; no binding is invented for an unbound record. This does not yet copy cross-note unit/seam spans for selected-note duplication.

This in-development field is required; earlier intermediate schema-8 override objects lacking it are not accepted. Versions 1–7 migrate existing manual overrides with `unresolved = true`, retaining symbols, timing, locks, unit choices and seam settings without claiming a verified historical binding. Newer binding/resolution fields injected into an older schema do not override this migration policy. Saving as schema 8 preserves the unresolved state. Legacy overrides therefore require deliberate resolution before affecting audio again; rerendered results may change and the canonical state changes cache identity. Explicit review/rebinding UI, schema-8 unbound-record handling and remaining pronunciation-changing paths are still required.

## Unit and seam resolution state

Schema-8 unit-selection and seam overrides also require boolean `unresolved` (versions 1–7 migrate as true). Unresolved unit overrides are excluded from explicit selection and from loop/pitch-residual renderer settings; unresolved seam overrides do not replace default boundary settings. Original payloads remain available for later resolution and undo.

Lyric transactions rebind a unit only when its entire span maps unambiguously and contiguously, including spans crossing note boundaries. Seams require both neighboring base sounds to map to adjacent positions in the new region stream; the region's initial boundary must remain initial. Token matches never transfer between note IDs. Key collisions retain original entries unresolved. Every participating note must have a supported, warning-free context. The region correspondence is bounded to 4,096 tokens with unique contiguous note groups of at most 256 tokens each, and computes each note's alignment once for reuse across units/joins. Missing/ambiguous/noncontiguous matches stay unresolved. Saved resolved flags still do not prove durable resolver/resource identity.

## Note fields

Every schema-8 note requires both `vibrato` and `phoneticHint`.

```json
{
  "vibrato": {
    "enabled": false,
    "startFraction": 0.65,
    "fadeInFraction": 0.1,
    "fadeOutFraction": 0.1,
    "depthCents": 50,
    "periodMilliseconds": 180,
    "phaseTurns": 0
  },
  "phoneticHint": null
}
```

This is a field fragment, not a complete project or note. Vibrato must contain exactly these seven keys. `enabled` is boolean; all other values are finite numbers with the following bounds, checked even when disabled:

| Field | Unit | Bounds |
|---|---|---|
| `startFraction` | Fraction of note | 0–1 inclusive |
| `fadeInFraction`, `fadeOutFraction` | Normalized fade fractions | Individually 0–1; combined sum at most 1 |
| `depthCents` | Cents | 0–200 inclusive |
| `periodMilliseconds` | Milliseconds per cycle | 5–500 inclusive |
| `phaseTurns` | Turns | 0 inclusive, 1 exclusive |

The domain stores IEEE-754 float values. Individual input bounds are checked before narrowing, so tiny negative values and values slightly above a limit cannot become valid through rounding. The domain's combined fade check uses float addition. To preserve exact writer round trips, a pair of exact float-representable inputs accepted by that rule remains valid even if its double-precision sum slightly exceeds one (for example the stored `0.8F`/`0.2F` pair). Other raw decimal fade-sum overshoots are rejected without an epsilon allowance. The narrowed domain value must also validate.

`phoneticHint` is `null` or a nonempty valid UTF-8 string of at most 4,096 bytes. An empty string is invalid; clearing uses `null`. The hint does not replace visible lyric text. Language-specific resolution and pronunciation reconciliation are separate implementation obligations.

## Region dynamics

Every schema-8 vocal region requires `dynamicsAutomation`, an array of at most 16,384 points:

```json
[
  {"tick": 0, "linearGain": 1},
  {"tick": 960, "linearGain": 0.5}
]
```

Each point has exactly `tick` and `linearGain`. Ticks are nonnegative integers in region-local coordinates, strictly increasing and no greater than the region duration. Gain ranges from zero through the domain float constant `3.9810717055F`, approximately +12 dB. Input gain is checked before float conversion. Duplicate, unsorted, nonfinite, oversized and out-of-region curves are rejected.

Empty curves evaluate to unity. Nonempty curves hold their first and last values outside their point range and interpolate linearly in tick space between points. Replacement is transactional. Persistence does not by itself mean the audio renderer applies this gain; audible compilation remains an explicit downstream requirement.

## Track style intent

Every schema-8 vocal track requires a two-key `styleSelection` object:

```json
{"origin": "explicit", "styleId": "soft"}
```

| Origin | Required style ID | Meaning |
|---|---|---|
| `unselected` | Empty | No deliberate choice yet |
| `explicit` | Nonempty | User selection |
| `sole-declared-style` | Nonempty | The exact bank declares only one style |
| `legacy-needs-exact-bank-resolution` | Empty | Older project awaiting its exact bank |
| `legacy-manifest-first` | Nonempty | Exact legacy bank's original first style |

Style IDs are valid UTF-8, at most 1,024 bytes. Unknown origins, contradictory empty/nonempty IDs, missing keys and extra keys fail decoding.

The shared resolver requires matching bank ID, version and 64-hex-character content hash plus trusted-installed status. Development-fixture allowance in the general catalog does not grant trusted style migration. Missing, mismatched or untrusted banks leave legacy intent unresolved. The exact trusted legacy bank preserves manifest declaration order; no sorting or substitute resource determines its style. Explicit choices are not silently replaced when absent from the manifest. New multi-style selection remains a deliberate-choice requirement.

## Persistence boundaries

Project open, autosave recovery, runtime initialization and plugin project replacement resolve eligible legacy intent before rendering or publishing the replacement. Opening an older file does not rewrite it: its durable input hash remains the base hash until a successful save. Recovery also preserves its input bytes and saved base hash, and remains dirty.

Later exact-bank relinking records resolved legacy style as an undoable document command. Repeat relinking of an already resolved track adds no style-edit revision. Bank installation verifies an actual signed package; migration tests do not manufacture trust receipts.

Replacing an unrelated singer resets prior style provenance. A same-singer update retains a known selected style, including one absent from the new manifest; it must not silently substitute another style. Changing an unresolved legacy reference resets that intent even when the singer ID stays the same. The verified sole style of a new assignment is selected in the same undoable bank-change command; a multi-style assignment stays unselected. Initial project creation applies the same exact-bank resolution before its first safe save. Without a catalog that verifies the exact bank, it leaves selection unresolved.

`EditPerformanceCommand` changes bounded batches of note vibrato/hints, region dynamics and track style as one editor revision. Undo/redo restores touched fields together, leaving unrelated project fields intact. This command does not yet implement revisioned manual ownership or take acceptance.

## Region performance state

Every schema-8 region now requires a five-key `performance` object. Its neutral value is:

```json
{
  "revision": {"musical": "0", "pronunciation": "0", "ownership": "0"},
  "pronunciation": null,
  "ownership": [],
  "takes": [],
  "accepted": []
}
```

Revision values and generation seeds are canonical lowercase unsigned hexadecimal strings, including `"0"`; all 64 bits survive serialization. Leading zeroes, signs, prefixes, uppercase digits, numeric JSON values and overflow are rejected. Tick coordinates remain signed integer JSON values, not hex strings.

An optional pronunciation identity records `language` (`ja`, `en`, `ko`), `resolverId`, `resolverVersion`, `resourceHash`, `inputHash`, and `sequenceHash`. Hashes are lowercase SHA-256-shaped strings. Resource identities inside takes contain `kind` (`sample`, `procedural`, `neural`), `id`, `version`, and `contentHash`. Structural validation does not establish that these resources exist, are trusted, or generated the claimed sequence. U4 and runtime resource verification must establish those facts.

Ownership records contain `channel`, `scope`, `mode` and captured `revision`. Scope is either `{"kind":"note","noteId":"1"}` or `{"kind":"range","startTick":0,"endTick":480}`. Time ranges are half-open and region-local. Mode is `replace` or pitch-only `pitch-offset`. Current note references must resolve inside their region. Distinct overlapping notes may own independent controls. Duplicate bindings to the same note, overlapping time ranges and intersecting note/range bindings for the same channel/mode are ambiguous and rejected. Pitch replacement and an explicit pitch-offset layer may coexist.

Each take contains exactly `id`, `sourceRegionId`, `capturedRevision`, `resource`, `pronunciation`, `generatorId`, `generatorVersion`, `seed`, `range`, `state` and `lanes`. Take IDs are region-scoped, nonempty UTF-8 of at most 128 bytes. A take is `proposed` or `rejected`; it has no whole-take accepted flag. A stale take is valid historical data and may retain its original source-region identity after editing or duplication. Merely loading it does not make it current or accepted.

Each lane contains `channel`, its exact `unit`, and ordered unique `points`; each point contains integer `tick` and nullable numeric `value`. Points lie within the captured take range, with an endpoint at the range end permitted as an interpolation anchor. A nonempty lane has at most 16,384 points; a take has at most one lane per channel. The units and current storage bounds are:

| Channel | Unit | Storage bounds |
|---|---|---|
| `pitch` | `midi-cents` | 0–12,700; `null` explicitly represents unvoiced |
| `timing` | `microseconds-offset` | −60,000,000 to +60,000,000 from score time |
| `dynamics` | `linear-gain` | 0 through the existing +12 dB gain constant |
| `formant` | `semitones-shift` | −24 to +24 |
| `gender` | `bipolar` | −1 to +1 |
| `attack`, `release` | `milliseconds` | 0–2,000 |
| `breathiness`, `tension`, `airiness`, `style-blend`, `growl` | `normalized` | 0–1 |

All numeric values are finite; only Pitch permits `null`. These storage bounds and units are not acoustic capability or quality claims. Timing compilation, interpolation/voicing transitions and backend-specific capability checks remain downstream obligations.

`accepted` records contain `takeId`, `channel`, `scope`, and signed integer `sourceTickOffset`. They select only the named channel and note/range from an existing non-rejected take. Source time is current region-local tick plus this offset; checked arithmetic and captured-range bounds are mandatory. Ambiguous same-channel selections are rejected, using the same distinct-note versus range rules. A persisted selection may refer to an older captured revision: validating a saved document is distinct from authorizing a new acceptance transaction. The latter must verify current score/pronunciation/resource/ownership and live job identity before mutation.

Limits per region are 4,096 ownership records, 16 takes, 4,096 accepted selections and 65,536 total take points. The decoder checks collection and aggregate budgets before building domain vectors. This payload contains no PCM or model tensors and remains subject to the enclosing project/plugin byte budgets.

Note deletion removes live note-scoped bindings while preserving time ranges and historical takes; undo restores the prior state. Region/track duplication remaps current note scopes but retains take source provenance. Region splitting clips current ranges, shifts right-side scopes and adjusts source offsets without rewriting immutable take payloads. These transformations clear the current pronunciation identity because the context changed. Automatic stamping for every other musical edit is not yet implemented.

Selected-note duplication also copies explicit note-scoped ownership and accepted selections in the same undo transaction as the new notes. A copied selection subtracts the note's translation from its source offset, preserving the same captured material. Immutable takes and region-time scopes are not copied or shifted. Conflicting ownership, invalid mappings, limit violations and source-offset overflow reject the transaction. Current pronunciation identity is cleared; undo restores it.

Note moves likewise subtract the note translation from note-scoped accepted source offsets. Edge resizing instead retains the existing source offset: trimming the left edge selects a later part of the same source timeline, rather than slipping the original onset to the new edge. Extension is valid only when the captured take covers the resulting selection. Both operations preserve immutable takes and fixed region-time scopes, validate the complete affected regions before publication, and reject missing coverage, ownership collisions or invalid geometry without partial edits. Undo/redo invert the move offsets or restore the resize geometry exactly. These rules preserve selection intent; audible pitch transposition and expression evaluation remain downstream compiler work.

`EditPerformanceCommand` accepts explicit region ownership replacements alongside note expressions, dynamics and style. It checks the prepared ownership vector and revision before applying any field, advances the ownership revision on a changed replacement, and stamps its records with that revision. Exhaustion is rejected, not wrapped. Undo/redo restore the exact saved values. Ownership is explicit rather than inferred from a hint or vibrato value: a hint-only change does not claim pitch, while explicitly disabled manual vibrato can still have pitch replacement ownership. This command does not supply global musical/pronunciation stamping or take acceptance authorization.

Render snapshots preserve relevant live ownership and accepted selections, include only referenced takes, and exclude unused proposals. Current ownership revision bookkeeping is excluded from that snapshot identity. This is input preservation, not audible execution of generated lanes. The domain eligibility helper applies manual replacement precedence and excludes generated pitch for manual vibrato; the audio compiler and interactive ownership/acceptance workflow still need to consume that policy.

## Verification boundaries

### Render style precedence

An explicit render API style argument is a deliberate, non-persistent override. Without it, snapshot construction uses the saved nonempty track style ID and rejects that ID if absent from the bank. Unresolved legacy intent must pass exact-bank migration before default rendering. Unselected intent can use a sole declared style, but an unselected multi-style bank returns a choice-required conflict. Whole-project rendering leaves style resolution to this same path rather than supplying the first manifest style. Missing styles never silently select substitute units; whole-project rendering preserves phrase diagnostics or returns its existing no-audible-tracks error if nothing can render. No render request changes saved style provenance.

### Runtime pronunciation identity

`resolveJapanesePronunciation` now produces the existing `PronunciationIdentity` vocabulary from actual resolution. Its bundled-resource hash is generated by CMake from an explicit list of Japanese adapter, shared phonemizer, resolver and phoneme contract sources; those files trigger reconfiguration when changed. This is exact bundled-source identity, not a voicebank/dictionary license statement or binary attestation. Resolver ID/version are `seam-builtin-ja` / `1`.

Input hashing uses length-prefixed fields for ordered note identities/start positions/lyric bindings, referenced Unicode text/language, and phoneme override symbol/timing/lock/resolution state. Cosmetic names, pitch and other values not consumed by this adapter are excluded. Sequence hashing covers ordered token keys, symbols, roles, voicing, timing and locks. Bounds precede expansion, including repeated references to one lyric; empty/missing text retains the adapter's explicit pause/warning behavior.

Technical inspection and render snapshot construction use this resolver. Snapshots resolve the full region before projecting phrase tokens, preserving context-dependent continuation vowels, freeze the full identity for inspection, and bind the cache key to bundled resources and the actual projected token sequence. This does not yet write current identities into the saved region after every edit, migrate legacy edit bindings, implement phonetic-hint interpretation, or authorize generated take acceptance. Other consumers and pronunciation-changing command paths still require the planned shared compiled-sequence integration.

Native scene population, native phoneme/unit hit-testing and embedded-editor inspection now also use `inspectJapanesePronunciation`, the shared bounded resolver's UI adapter. Normal resolver warnings are retained. A failed resolution produces no fallback token sequence and an explicit `ResolutionFailure` warning with the resolver error message. This aligns these inspected editor paths with render resolution; it does not by itself establish stable historical edit identities or validate saved resource bindings.

Resolved runtime tokens now carry `contextId` and `lyricOwner`. The context address hashes the bundled resource, region/note/lyric identities, that note's effective sound sequence and token ordinal. It remains stable for timing/lock-only changes and unrelated-note changes, but a changed sound sequence—including a repeated-sound insertion—creates a different context. Sequence hash format v2 includes these addresses/owners as well as timing and locks; input hashing includes region identity. Shared editor and snapshot paths receive the same fields. Raw adapter output may remain unbound with an empty context ID. These are content-addressed context identifiers, not globally unique edit lineage or permission to apply a saved override. Persisted historical bindings still need explicit verification/reconciliation; no new saved-project token list is introduced by this runtime field.

Single/batch lyric transactions now save the post-reconciliation identity into `performance.pronunciation` and increment `performance.revision.pronunciation` once per changed region. Unsupported languages or bounded resolution failure clear the previous identity instead of retaining a false current identity; the edited text remains saveable when domain validation permits it. Revision exhaustion rejects the transaction without wrapping. The exact before/after identity and revision travel with dependent edits through undo/redo, project/plugin state and autosave. Reconciliation itself uses the bounded resolver, retaining edits unresolved on failure. These stored values are evidence of the lyric transaction, not authorization: the separate live-job context remains invalid after undo, and admission must also verify current input/resource identity. Other musical/phoneme/resource mutations still need corresponding maintenance.

Phoneme upsert/reset now use the same pronunciation refresh logic. Changed overrides update identity/revision in a validated staged region and reconcile unit/seam dependencies against the actual effective before/after symbols, roles and voicing. Timing and lock flags are removed only from the temporary matching sequences, so a timing-only edit does not invalidate unchanged sounds. The explicitly edited phoneme itself is not rebound. On unavailable resolution, dependent units/seams remain unresolved without changing the user's explicit phoneme payload. Unchanged upserts do not consume a pronunciation revision. Undo/redo restores phoneme/unit/seam vectors, their original order, identity and pronunciation revision exactly while leaving musical/ownership revision components alone.

### Runtime completion context

`EditorSession::capturePerformanceJob()` returns an immutable source-project snapshot and an opaque runtime context. Workers may read that snapshot; validation and publication must return to the serialized editor owner thread. `ProjectDocument::executePerformanceResult()` preserves document dirty tracking, and `AuthoringRuntime::executePerformanceResult()` uses the normal diagnostic/preview path after a successful guarded transaction.

Successful musical transactions invalidate the captured generation based on actual input changes, not only the command's impact label. Undo/redo cannot revive an older generation even when musical content becomes identical again. Project replacement and another/reopened session have distinct generation identities. Each successful completion consumes its own receipt, including a metadata-only result; failed commands do not consume it. Other jobs with unchanged musical inputs are not consumed by that receipt.

The input comparison covers vocal score, timing, expression, pronunciation, ownership, accepted take payloads, sample rate, host offset, and bank/style intent. It excludes cosmetic names, presentation/mix state and unused proposals. Generation identity is retained by outstanding contexts and cannot be recycled while an old context exists; normal edits avoid this extra comparison when no current-generation context is outstanding. Contexts and receipts are runtime-only and cannot be reconstructed from saved JSON.

This guard is not durable take-admission authorization or resource-byte verification. Persisted proposal revision/hash reconciliation and backend resource validation remain separate requirements. Musical mutations must use managed transactions: the snapshot comparison catches currently visible mutations through legacy mutable accessors, but it cannot observe a transient mutation and restoration performed entirely outside that transaction boundary. No neural generation job or audible advanced-expression algorithm is implemented by this API alone.

Focused executable targets cover domain validation, schema migration, atomic commands, genuine signed-bank installation, project save/recovery and plugin state. Historical checked-in fixtures are distinguished from synthetic schema characterization and bank-reference-rebound migration inputs. The fixed schema-7 auditory corpus remains unchanged.

New serialized musical data affects the existing content-derived render identity. Re-render/cache invalidation is expected; byte-identical legacy PCM is not promised. No persistence test qualifies musical quality, a production female singer, an installed host tuple, or Beta GO.
