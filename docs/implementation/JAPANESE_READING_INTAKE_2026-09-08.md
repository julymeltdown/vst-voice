# U26 Japanese reading dependency intake

Status: real dependency built and dictionary reading demonstrated; development-only, not integrated into the shared resolver or distributed with SEAM. U26 remains incomplete.

## Bounded response decoder checkpoint

`authoring::decodeJapaneseReadingResponse` now converts the probe's version-1 JSON into the typed reading model using the existing bounded JSON parser. Request admission checks valid nonempty UTF-8, no embedded NUL and at most 4096 source bytes before parsing output. Response limits are 1048576 bytes, depth 8, 32780 nodes, 4096 bytes per decoded string and 4096 entries per collection. The decoder accepts exactly the three root fields and six token fields defined by this protocol; wrong/missing/extra fields, wrong types, non-integer or out-of-range offsets/lengths, unsupported status, version drift and mismatched echoed source reject. The shared typed validator then checks exact source coverage, encoding boundaries, reading semantics, resource identity format and aggregate expansion. No partial output escapes on error.

The caller supplies a resource identity already established by trusted loading/execution; it is attached to the result, not authenticated by JSON. A matching echoed source is not helper attestation or a request nonce. Verified executable/dictionary selection, process provenance and stale-request binding remain responsibilities of the upcoming runner. Cancellation is checked before/after bounded parsing and during conversion/validation; the generic JSON parser itself is not preemptible midway through parsing.

Two regression cases exercise the matching wire shape, 12 schema/type/span mutations, duplicate keys, concatenated JSON, excess bytes/depth/string/collection size and pre-cancellation. A fresh live probe invocation for `私, XYZ` produced the same source/token fields as the decoder fixture; the C++ fixture uses synthetic identity metadata and does not claim real helper attestation. All 647 Release core cases pass (16.76 s); 11 focused Japanese cases pass Release/Debug (0.52/0.55 s). Release native/core/focused and Debug focused builds and diff checks pass.

Source inspection found no reusable production subprocess runner in the existing libraries; generation jobs are not such a runner. Next implement verified helper launch, bounded pipe reads, cancellation/deadline/termination/reaping and immutable-request adoption, then connect typed readings to note ownership and editable resolver/UI intent. The Open JTalk dependency remains development-only and not distributed. Changes remain local/uncommitted; full U26/Beta GO remains incomplete.

## Typed span and validation checkpoint

New `japanese_reading.hpp/.cpp` defines resource identity, original UTF-8 byte spans/surface, lexical reading, contextual pronunciation and distinct Known/Unknown/MissingReading states. `validateJapaneseReading` checks an expected 40-hex engine revision and 64-hex dictionary identity, exact source SHA-256, valid nonempty UTF-8 input (no NUL, at most 4096 bytes), at most 4096 tokens, ordered non-overlapping UTF-8-aligned spans and exact surface reconstruction. Only ASCII space/tab/newline/CR and ideographic space may be omitted between spans. Known readings require both nonempty/non-star UTF-8 fields; unknown/missing readings cannot carry invented text. Individual fields are limited to 4096 bytes and aggregate reading expansion to 65536 bytes. Overflow-safe bounds and stop checks are included.

This is validation of an already decoded result, **not** a bounded JSON decoder, subprocess runner, binary authentication, dictionary loader, note-owner mapper or lyric mutation. A caller must establish trusted resource identity; the data structure is not self-attesting. It must also bound allocations before constructing these objects. The existing kana resolver remains unchanged by this new, not-yet-connected API.

The development probe now calls MeCab's native lattice/node API and emits JSON with exact byte offsets/lengths, surface, status and optional readings. It no longer concatenates surface with feature CSV. Feature fields are decoded separately with quoted/escaped-quote handling; JSON string escaping preserves punctuation/control bytes. It validates bounded node counts, feature sizes, ordered address spans and output size, but upstream lattice allocation still precedes those checks. Process isolation and admission before upstream work remain required for a production helper.

The checker verifies exact source/span reconstruction as well as dictionary hashes and observed readings. A third fixture includes commas, quotes, ideographic whitespace and unknown text. It reveals that this dictionary chooses カ for quoted isolated 歌 in the tested context; this is preserved as machine-observed behavior, not asserted to be the user's intended singing reading. Editable reading choices and native-language review remain necessary. Unknown punctuation is retained as unknown instead of silently converted to a guessed reading.

Two core validator cases cover malformed/overlapping/missing spans, split UTF-8, mismatched text/resource/source identity, invalid statuses, fabricated unknown readings, missing known readings, overflow sizes, count/aggregate expansion bounds, omitted whitespace and cancellation. All 645 Release core cases pass (16.62 s); nine focused Japanese cases pass Release/Debug (0.37/0.40 s). The probe builds with its strict warnings and passes four dictionary hashes, three reading/span cases and two input-admission cases. Scoped builds and diff checks pass. Work remains local/uncommitted; no connected kanji editor workflow or full U26 acceptance yet.

## Selected integration candidate and identity

Use the MeCab/dictionary front end from the [r9y9 Open JTalk fork](https://github.com/r9y9/open_jtalk/tree/462fc38e7520aa89e4d32b2611749208528c901e), revision `462fc38e7520aa89e4d32b2611749208528c901e`, as the current integration candidate. The checkout identifies Open JTalk 1.11 and contains a CMake static-library build. It fits SEAM's C/C++ toolchain and provides lexical reading plus contextual pronunciation without requiring a voice model or acoustic synthesis. This is a specific fork, not a claim that its HEAD is an unmodified official release.

Exact source hash: `git archive --format=tar HEAD` SHA-256 `dac8303a55d808e58211953835a324644114e3189313449c9a77eab7f5cb59d3`. Checkout used: `/tmp/seam-japanese-intake.nAGk13/open_jtalk`. Source was cloned outside the project; no dependency source/binary/dictionary was vendored or linked into shipping targets. The intake is recorded under `plannedOrReferenceOnly` in `third_party/manifest.yml`.

Inspected licenses: `src/COPYING`, `src/mecab/COPYING`, and `src/mecab-naist-jdic/COPYING`. They include BSD-style three-condition notices for HTS/Nagoya, MeCab's authors, NAIST, and the UniDic Consortium. The dictionary is not NAIST-only: its build consumes both `naist-jdic.csv` (486757 entries) and `unidic-csj.csv` (302157 entries). All relevant notices must accompany any eventual source/binary redistribution; this intake is not legal counsel or a completed distribution audit. [Pinned dictionary notices](https://github.com/r9y9/open_jtalk/blob/462fc38e7520aa89e4d32b2611749208528c901e/src/mecab-naist-jdic/COPYING)

Source data SHA-256:

- `naist-jdic.csv`: `ab4463fb4aa953986b4e4be13d332e5ca4bb0451615f34fdefc46c971ec8016e`
- `unidic-csj.csv`: `8595729c9f4b21821edddb81d251ad9115d726370ac138452c6ad47e691e3a91`
- Dictionary `COPYING`: `f4eca42ebd930e2c6e57fca58319d989bebcd1510cb7714b149c50f5425135ea`
- MeCab `COPYING`: `05e94c185a3e31f0c658f7011132be952b6d1a4d588b682f92da380e0a290650`
- Root `COPYING`: `14f380a0db8dce139fdcfb731d21e993a31a00b68907515d1c54a5f356241343`

## Reproduced build and reading evidence

Configured `src` with `CMAKE_BUILD_TYPE=Release` and `BUILD_PROGRAMS=OFF`, then built `openjtalk`. This builds the front-end library without the command-line TTS program's HTS engine dependency. Upstream CMake generates its config header inside the checkout. The upstream build passed with warnings, including deprecated `sprintf` and the suspicious string-plus-character expression in `mecab/src/writer.cpp:253`; no SEAM warning policy was changed.

Compiled `tools/dependency-probes/openjtalk_reading_probe.cpp` separately with C++20, `-Wall -Wextra -Werror`, the checkout's `src/mecab/src` include directory and the built `libopenjtalk.a`. Output: `/tmp/seam-japanese-intake.nAGk13/reading-probe`.

Dictionary build preparation follows upstream `mecab-naist-jdic/Makefile.am`: copy `_left-id.def`, `_right-id.def`, `_pos-id.def`, `_rewrite.def` to their unprefixed names in that temporary source directory. Then run:

```sh
/tmp/seam-japanese-intake.nAGk13/reading-probe --compile-dictionary \
  -d /tmp/seam-japanese-intake.nAGk13/open_jtalk/src/mecab-naist-jdic \
  -o /tmp/seam-japanese-intake.nAGk13/dictionary -f UTF-8 -t UTF-8
python3 tools/dependency-probes/check_openjtalk_reading.py \
  /tmp/seam-japanese-intake.nAGk13/reading-probe \
  /tmp/seam-japanese-intake.nAGk13/dictionary
```

An initial exploratory dictionary build without the unprefixed definitions emitted a minimum-POS-setting warning; it was replaced with the correctly prepared build before acceptance checks. The four compiled runtime files total 107134224 bytes. Their exact hashes are pinned in `tests/fixtures/pronunciation/openjtalk-intake.json`; this is one locally compiled artifact set, not proven cross-platform reproducibility.

The checker passes four file hashes, two reading cases and empty/over-budget input rejection. The first case resolves `私は今日学校へ行く。`, distinguishing dictionary reading from contextual pronunciation: は has ハ/ワ, へ has ヘ/エ, 今日 has キョウ/キョー, and 学校 has ガッコウ/ガッコー. The second resolves 東京/歌う and leaves XYZ/𠮷 without usable reading fields. Fixtures are explicitly machine-observed and **not native-language approved**.

## Required implementation boundary

1. Add a typed reading service with frozen engine/dictionary/vocabulary identity, surface byte/scalar spans, lexical reading, contextual pronunciation and explicit unknown status. Do not concatenate a reconstructed sentence and lose note/lyric ownership.
2. Use lower-level MeCab node spans/status rather than the convenience API's `surface + comma + feature` strings. The typed probe now demonstrates this (checkpoint above); a bounded production decoder/execution path remains to be implemented.
3. Keep stored lyric text and explicit user phone hints authoritative. Reading selection must be separately editable and versioned, with integration through the shared resolver/context hash and existing override reconciliation.
4. Admit bounded UTF-8 text before parsing and validate all returned spans/features against the source. Current probe admits 1..4096 bytes and checks returned counts/aggregate output, but output checks happen **after** upstream allocation. It is not a memory-safety or worst-case-resource guarantee.
5. Run parsing outside the audio/UI thread. `Mecab_analysis` constructs a lattice and has no stop-token/deadline interface. Prefer an isolated helper with bounded request/response, deadline, memory limits and retirement/stale-result rules, or first prove equivalent cooperative limits in a reviewed adapted front end. Do not treat post-hoc cancellation as preemption.
6. Pin and verify all dictionary/runtime files, control configuration/search paths, and register complete redistributed notices before shipping. Do not load arbitrary project-provided dictionaries or infer trust from a filename.
7. Test punctuation, unknowns, ambiguous readings, cross-note words, particles, chunk boundaries, edits after dictionary revision changes, and resource-vocabulary compatibility. Native-language and musician review remain required.

## Verification boundary

The probe builds and its checker passes; `git diff --check` passes. No core production code changed in this intake turn, and no claim of connected kanji singing is made. The repository license auditor was run and returned a single branch-policy error: current branch `codex/production-readiness-completion`, expected `master`. That gate was not bypassed and the branch was not changed. The license audit is therefore not a PASS. No commit/push or shipping dependency promotion occurred.
