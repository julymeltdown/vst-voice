# U30 acceptance audit: native USTX 0.6–0.9 exchange

Date: 2026-09-23. Plan source:
`docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md`, U30.

**Verdict: PARTIAL — do not accept U30 yet.** The bounded native codec and
0.9 export work locally, and an independent OpenUtau core loader accepts a
production SEAM export. The supported input versions now include 0.6–0.9, but
the old-version regression is based on a version-adjusted 0.9 musical fixture,
not files saved by each historical OpenUtau release. Full field breadth and
desktop application behavior are not proved by this audit. This is not a
Beta-GO verdict.

## Version contract and source provenance

The pinned local OpenUtau reference is `8c0dc4007e6e8c8181f3a12c10205671800eeb8b`
(`OpenUtau.Core/Format/USTx.cs`). Its `Ustx.Load` migrates pre-0.6 scalar
tempo/meter and pre-0.7 expression selectors, and has no 0.7/0.8 musical
timing migration. SEAM refuses pre-0.6 files rather than guessing at that
different tempo/meter representation.

Historical OpenUtau tags checked against their own `kUstxVersion` declaration:

| Tag | Commit | USTX | Relevant evidence |
|---|---|---:|---|
| `0.1.542` | `b4949940f1a1d928fd5bd8ed1f6c940500a23527` | 0.6 | `UProject.cs` declares five default expression selectors, plus tempo/meter lists and 480 resolution. |
| `0.1.565` | `a60ca5830b9064556157245d4bf8f5920d93e5f8` | 0.7 | `UProject.cs` declares ten selectors; tempo/meter lists and resolution remain. |
| `0.1.566` | `70b916813c1d5be86fd66f059a18a0f13789665e` | 0.8 | Same inspected musical timing fields. |
| `0.1.567` | `6d164af6d8b7f7b010969654bef8c7a7e0d32899` | 0.9 | Same inspected musical timing fields. |

These are source-contract checks, not evidence that SEAM opened actual output
from those four app versions. The local C++ regression now uses the historical
five-selector form for 0.6; a previous eight-selector approximation was wrong.

## U30 criteria to current evidence

| Plan criterion | Current evidence | Status |
|---|---|---|
| Bounded 0.6–0.9 import | `decodeUstx` and `UstxDocument::validate` admit exactly 0.6, 0.7, 0.8, 0.9. Focused test checks tempo, meter, notes, UTF-8 lyric, pitch, conversion, canonical 0.9 re-export and rejects 0.5/0.10/1.0. | Implemented; archival inputs absent. |
| Deterministic 0.9 export and explicit losses | `encodeUstx`, `ustx_project_conversion.cpp`, and `USTX_INTERCHANGE_V1.md` declare the subset and losses. Local codec and service tests pass. | Local pass. |
| Hostile YAML and allocation budgets | Codec tests cover aliases, duplicate keys, documents, depth, byte/node limits, invalid scalars and report overflow. U29 separately accepted the local POSIX held-file I/O boundary; its Windows limitation remains. | Local pass, not a full adversarial audit of every old-version field. |
| External OpenUtau interoperability | `seam_voicebank_cli export-score` wrote `original-melody.seam` as USTX 0.9, content hash `69c5195cb5db83a9a063ff27aeae737d6addbe49d4d1e8da6239e2ddcf412282`, with two disclosed conversion losses. The independent oracle, built against pinned OpenUtau `8c0dc40`, called `Ustx.Load` (deserialization, migration, `AfterLoad`, validation) and reported `ORACLE_OK`, one track, one part and 80 notes with UTF-8 lyrics. | OpenUtau core load pass for this 0.9 export; GUI, sound and older-release files not proved. |

## Exact local verification

- `cmake --build build/release --target seam_ustx_interchange_tests -j 8`: pass.
- `ctest --test-dir build/release -R '^seam_ustx_interchange_tests$' --output-on-failure`: 1/1 target pass.
- `/tmp/seam-dotnet/dotnet build tools/openutau_oracle/seam_openutau_oracle.csproj --nologo -v q -p:OpenUtauRoot=/Users/lhs/Downloads/OpenUtau-review`: pass, zero warnings/errors.
- The same oracle's `dotnet run --no-build` on the SEAM-exported score: `ORACLE_OK` after the full OpenUtau core load path.

## Work required before U30 acceptance

1. Capture immutable, independently sourced `.ustx` files actually written by
   OpenUtau versions declaring 0.6, 0.7, 0.8 and 0.9. Record app build, file
   SHA-256 and allowed redistribution. Do not relabel a 0.9 fixture as archival.
2. For every file, compare tempo/meter, track/part offsets, note durations and
   tones, Unicode lyrics, pitch shapes, vibrato and mapped dynamics/styles
   against an independent OpenUtau inspection. Record every intended loss.
3. Re-export imported projects as 0.9, load them through the pinned OpenUtau
   core and exercise the actual desktop open/save workflow. Keep singer
   availability and audio rendering separate from score interoperability.
4. Obtain independent review of those receipts and the hostile-input boundary;
   do not infer whole-product, Windows, or Beta-GO acceptance from this unit.

GitHub CI was intentionally not used or assessed in this audit.
