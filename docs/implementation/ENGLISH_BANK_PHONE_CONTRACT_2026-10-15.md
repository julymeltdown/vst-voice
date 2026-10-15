# English sample-bank phone contract and rendering

## Result

At baseline `4fd8ab42`, English recording inventories declare stress folding,
but sample-bank manifests have no matching policy and selection compares phone
strings exactly. An authored/resolved `iy1` therefore cannot use a recorded bare
`iy`. The phonemizer's English vocabulary-coverage helper existed only as an
isolated utility; a passing helper test did not establish sample rendering.

This unit adds an explicit English bank contract to the native manifest,
selection, source-alignment eligibility and render/cache identities. Real native
snapshot and phrase-pipeline tests now select and render a bare vowel for
`iy0`, `iy1` and `iy2` when the bank declares folding. The resolved pronunciation
keeps its original symbols, keys, timing and lexical-stress information.

This is an engineering integration step. It does not import or approve the
internet recordings, qualify a singer, or complete producer-to-manifest contract
propagation. The actual CLI fixture is a generated periodic tone, not a female
voicebank, production resource, listening result or AE1/R3 original singer.

## Data and compatibility

`voicebank::Manifest::languageContract` contains exactly `vocabularyId`,
`vocabularySha256` and `stressPolicy`. It uses the same frozen vocabulary binding
and two policy strings as the English producer inventory:
`FOLD_LEXICAL_STRESS` and `DISTINGUISH_LEXICAL_STRESS`.

The JSON codec writes **schema 4 only when this explicit contract is present**.
Existing manifests continue to encode as schema 3 with no new member; schemas
1–3 remain readable and retain exact matching. `Manifest::kSchemaVersion` remains
the compatibility default, 3; `kEnglishContractSchemaVersion` names the new
contract-bearing schema, 4. A schema-4 manifest must carry its contract. A
contract in schema 1–3 is refused rather than ignored or silently dropped.
Foreign languages, unknown contract fields, wrong vocabulary identity/hash,
unsupported stress policies and phones outside the frozen vocabulary are refused.

Folded banks contain bare vowels. Only a valid frozen English vowel's trailing
`0`, `1` or `2` is removed for matching; consonants, pause and other strings are
unchanged. The source spelling `jh` is not aliased to `j`, Japanese `a1` is not
converted to `a`, and absent units remain uncovered. A folded declaration cannot
carry stressed unit labels. Exact declarations can contain the exact frozen
forms they actually provide and never imply complete coverage.

Contract-bearing banks have at most 65,536 units and 256 phones per unit.
Candidate generation checks its existing input bounds before contract validation
and charges the added inventory-phone checks to the shared matching budget,
including cancellation. Legacy selection behavior and budgets remain unchanged.

## Code path

- `libs/seam-voicebank/src/voicebank.cpp` validates the declaration and exposes
  `Manifest::matchedPhone`, a view used for resource matching only.
- `libs/seam-voicebank/src/manifest_json.cpp` reads/writes the versioned contract.
  Its three fields participate in the existing voicebank content hash through
  normal codec serialization; presentation metadata remains excluded as before.
- `libs/seam-synthesis/src/unit_selection.cpp` uses the same matching view for
  automatic candidates, forced units, alternatives and aligned timing.
- `libs/seam-synthesis/src/timing_solver.cpp` and
  `libs/seam-rendering/src/render_pipeline.cpp` pass the bank contract when
  checking source-aligned timing. Source landmarks stay bound to their actual
  bare labels and audio; pronunciation is not rewritten to make them match.
- `libs/seam-rendering/src/render_snapshot.cpp` uses that same rule when
  discovering alignment sidecars and includes the explicit contract in both
  render content identity and selection-context identity. A policy change cannot
  reuse the prior key even if selected unit IDs and audio bytes are unchanged.
- `CMakeLists.txt` explicitly links the voicebank library to the phonemizer's
  frozen vocabulary implementation. It introduces no dependency cycle.

## Validation and retained limitations

Full dependent Release rebuilds precede native checks. The first rebuild failed
on the new test's forced-selection aggregate field order; the corrected rebuild
passed. A later full rebuild included the contract-validation budget guard.

The initial seven-lane focused CTest run had six passing lanes and one contextual
failure: a new non-silence assertion used the existing constant/DC fixture. Raw
rendering intentionally subtracts the output mean in `raw_renderer.cpp`, so the
fixture became silent. The renderer was unchanged; the three new English cases
now use an explicit 440 Hz periodic fixture. After rebuilding, the affected
contextual lane passes all **31 cases**. The initial failure logs and exact
pre-tone test source are retained.

The six other passing lanes remain source/binary unchanged by that test-only
repair: synthesis quality (58), language phonemizer (39), phoneme timing (25),
sample-manifest draft (12), Studio manifest draft (37), and actual CLI validation
of the existing Japanese synthetic bank. Together with the corrected contextual
lane, this is **202 native cases plus the CLI-validation lane**. Separate native
checks pass the new English codec case (1) and legacy voicebank cases (53).
These are focused checks; no full release/sanitizer/physical acceptance is claimed.

The new cases cover contract codec round-trip and refusal, all three lexical
stress forms, actual snapshot/phrase rendering with nonzero finite output,
forced selection, original pronunciation preservation, missing/foreign-phone
refusal, cache separation, and real audio-bound alignment sidecars plus an
interior timing override. New English test fixtures clean up their own roots.

An actual retained schema-4 periodic-tone bank is refused by the prior retained
CLI with `Unsupported voicebank manifest schema`. The rebuilt retained CLI
validates those same bytes with zero errors, warnings and issues. That result
proves native format/structure handling, not source rights or musical quality.

The [evidence index](ENGLISH_BANK_PHONE_CONTRACT_2026-10-15.evidence.json) binds
the root `out/evidence/english-bank-contract-2026-10-15`. Receipts record the
11 changed code/test files and eight binary hashes before/after checks. Three
executables are retained: CLI, core tests and corrected contextual tests. The
earlier failed contextual executable has been overwritten; its original bytes
are not retained. Initial intermediate-build source versions are not all
retained either. These limits are explicit: the root is focused local consistency
evidence, not a hermetic or authenticated build.

## Next production integration

`createSampleManifestDraft` currently creates a manifest from active assignments
without reading the English inventory contract. The normal English producer
folder must propagate its hash-bound inventory policy into the editable bank;
conflicting or missing provenance must not be treated as permission to fold.
The CLI and Studio should use the same propagation path. Reopening/editing,
publication and signed installation must preserve the contract and resource
identity. Legacy exact banks must remain usable.

Real source work still requires the owner policy, a genuine producer, a distinct
reviewer, explicit `jh` mapping, the missing `axr`, stable singing spans and
musical pitch/style coverage. Draft creation already sets the manifest rate from
decoded audio and rejects mixed rates; the initial 48 kHz member value is only a
placeholder before inspection. The 16 kHz speech source does not require a new
conversion feature to be represented accurately, although production musical
quality and any deliberate conversion still require their own evidence.
Classical/neural and multilingual resources, the no-recording original singer,
R9, acoustic tolerances, human listening and target-host evidence remain open.

No canonical gate changes. Usable Alpha remains **0/20**, U45 remains held and
External Beta **BLOCKED**. Independent review of this exact unit is pending.


## 2026-10-15 — English producer-to-manifest contract integration

[The code-level dossier](ENGLISH_PRODUCER_MANIFEST_2026-10-15.md) closes normal generated English producer-to-editable-manifest policy propagation through shared CLI/Studio code. Exact inventory identity/digest/language, regular file and assignment keys are checked before creation and before commit; missing or changed authority is refused, legacy remains exact. Full dependent rebuilds pass. Effective six focused CTest lanes pass 142 native cases across retained runs; initial invalid fixture and wrong CLI-exit expectations are retained and repaired only in tests. Six code/test source bindings, seven binary bindings, four retained executables; 48 artifacts / seven pins. Prior sample-rate-gap wording is corrected: actual-rate detection/mixed-rate refusal already existed and is confirmed at 16 kHz. No new original audio or production admission. Exact review pending; singing resources, owner/actor policy and full Beta acceptance remain open. Female census 1,429; Alpha 0/20, U45 held, Beta BLOCKED.


## Later installed lifecycle evidence

The [installed lifecycle regression](ENGLISH_INSTALLED_LIFECYCLE_2026-10-15.md) exercises both declared English stress policies through actual generated workspace, review/publication, typed test-signed installation and fresh dictionary/continuation Final export. Both policies pass in the one registered native case; this later unit has separate pending review and does not change source/singer or Beta acceptance. The fresh SLT attribution packet was independently approved in [the exact attribution review](FEMALE_SLT_ATTRIBUTION_REVIEW_2026-10-15.md); the old packet remains unsuitable for import/provenance.
