# English coverage policy consistency — 2026-10-15

## Finding and result

**Engineering PASS; independent P2 closure pending; Beta BLOCKED.** Exact review
of `d5ecc1f952844be2910ca5b1eedef20b60fbcc0a` approved its bank vocabulary/stress
contract, with one P2 follow-up and no required changes to that reviewed commit.
The reviewer found that `VoicebankCoverageAnalyzer` still compared unit phones
with resolved symbols literally. A folded bank's bare `ih` could render dictionary
`ih1` while coverage reported it missing. The standalone coverage API and native
Style & coverage sheet share this analyzer.

The complete independent final verdict is retained as
`out/evidence/english-coverage-contract-2026-10-15/review-d5ecc1f9.md`: 18,628 bytes,
SHA-256 `b589f06dba99268f1b1cc88265ce899e65ecbddbe41e8366cf2ae840b363f30c`.
Review turn `01a13ccb-05ef-7001-9dc5-df496d79b047` completed at Unix second
1792022404. Its execution was read-only Git/source/hash/data checking: no build,
native binary, CLI, test or retained verifier/script execution. Its verdict
applies to d5ecc1f9, not producer propagation, the later lifecycle regression or
this follow-up. The exact producer-propagation review is running separately.

## Production change

The source base is `a0259814d7e61a7745dd7481ed86f7c404417baa`.
`libs/seam-voicebank/src/coverage.cpp` now passes `const Manifest&` to its internal
`phonesMatch` helper. For each position it compares the stored unit phone with
`manifest.matchedPhone(tokens[start + index].symbol)`. The sole caller inside
`analyzeRegion` passes the same manifest it already receives.

This reuses the exact policy view used by selection/rendering. Folding requires
English, the native frozen vocabulary ID and hash, supported FOLD policy and a
recognized English vowel with stress 0/1/2. DISTINGUISH, legacy manifests and
unbound/foreign policy views retain exact matching. No `jh` alias, missing phone,
recording, style or pitch layer is created. Resolved tokens and inventory phone
labels remain unchanged; issue symbols/keys still identify the score phoneme.

The non-overlapping dynamic-programming cover, unit enabled/style/pitch checks,
diagnostic precedence and workload guards are unchanged. Both native sheet blend
arms call the analyzer, so both use the corrected policy. The unchanged standalone
caller also passes the manifest; this unit does not provide a new physical
standalone interaction capture. Coverage remains structural evidence and cannot
approve audio, alignment, renderer quality, singing range or listening.

## Regression design and failure-to-pass evidence

Four new native cases exercise observable behavior:

| Test source | Requirement pinned |
| --- | --- |
| `tests/test_voicebank_coverage.cpp`, first case | Bare `ih` covers `ih0/ih1/ih2` under bound FOLD; DISTINGUISH and legacy report all three missing. Wrong hash/foreign language in-memory views do not fold. Original token symbols and bare inventory stay intact. Invalid manifests remain rejected by validation. |
| Same file, second case | Folded multi-phone spans still require a non-overlapping cover. Adding a true alternative completes coverage. Disabled/style/pitch witnesses retain their proper classifications and leave unrelated consonants missing. |
| `tests/test_english_phonemizer.cpp` | Real dictionary `sing` resolves `s ih1 ng`, without hints. A folded bank covers it; removing `ng` reports only `ng`. DISTINGUISH/legacy still report the absent stressed vowel. |
| `tests/test_style_coverage_sheet.cpp` | Real dictionary `I`, no hints, bare `ay` under FOLD: primary and secondary soft blend both cover. DISTINGUISH/legacy remain incomplete with issue `ay1`. Preview does not edit the score or create undo history. |

An initial build failed solely because the new tests used a namespace-level type
name instead of nested `Manifest::EnglishPhoneContract`. Failed logs and exact
test sources are retained. After that test-only repair, the full baseline build
passed with **unchanged production coverage code**. Three actual filtered native
executions then failed all four new cases at the intended coverage assertions:
2 coverage cases, 1 dictionary case and 1 sheet case. Their executing binaries,
sources, receipts and stdout/stderr are retained. This is a controlled reproduced
defect, not a claim that the existing whole suite failed.

After the production change, the **full dependent Release build** passed in
6.849466 seconds, rebuilding/relinking dependent targets before CTest. One final
CTest invocation passed four lanes in 4.486679 seconds:

| CTest lane | Native cases |
| --- | ---: |
| `seam_language_phonemizer_tests` | 40 |
| `seam_style_coverage_tests` | 11 |
| `seam_u3_tests` | 18 |
| `seam_english_candidate_lifecycle_tests` | 1 |
| **Total** | **70** |

The installed lifecycle case again exercises both policies through publication,
trusted test-signed installation, reopen and Final export. This invocation uses
ordinary temporary-output mode, so its temporary packages/audio are cleaned up.
The earlier [retained lifecycle capture](ENGLISH_INSTALLED_LIFECYCLE_2026-10-15.md)
remains a separate frozen root; its bytes are not attributed to this later run.

## Evidence, review and remaining acceptance

The [evidence index](ENGLISH_COVERAGE_CONTRACT_2026-10-15.evidence.json) binds 50
artifacts and seven pins, including four source files at each retained stage,
three baseline failing binaries, five final executables, all failed/passing logs,
the exact d5 verdict and source-base Git bindings. Three unchanged policy/caller
sources are also bound. This is selected local build consistency, not all
transitive inputs or authenticated/hermetic build provenance.

The read-only verifier hashes retained artifacts/pins, checks Git source bytes,
staged source-version and executable receipt bindings, recomputes the four failed
and 70 passing case totals, and checks the exact review metadata. It executes no
native or retained test/build script. Independent review of this fix and explicit
closure of the P2 remain pending; native passing evidence alone is not that verdict.

Source admission still requires the correctly attributed original packet, owner
rights policy, genuine producer/distinct reviewer, sufficient phonetic/pitch/style
coverage and actual quality assessment. The original female source census stays
1,429, with no new original downloads in this unit. SLT speech is not singing
coverage and does not satisfy AE1/R3 original-singer requirements. Full product
classical/neural/multilingual resources, R9, immutable candidate checks, acoustic
tolerances, signed application/platform/DAW journeys and human acceptance remain
open. Canonical files are pinned unchanged: Alpha **0/20**, U45 **HELD**, External
Beta **BLOCKED**, release eligibility **false**.
