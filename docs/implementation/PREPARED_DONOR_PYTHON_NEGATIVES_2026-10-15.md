# Prepared donor Python negative coverage — 2026-10-15

The persistence parity tests now exercise the two gaps identified in the exact `c9ef0d21` review: a present donor whose audio differs from its captured take, and histories that isolate the schema/English/human-recording guard. The Python validator and native runtime are unchanged. Both gaps have locally verified test repairs; independent closure requires review of this exact new commit.

## Distinct audio and captured identity

`tests/production/test_prepared_donor_workspace.py::fixture()` now writes 64 varying PCM16 samples, rather than a constant value. Its two 32-frame spans must have different PCM and WAV digests. Both content-addressed raw assets are recorded, with deduplication still keyed by digest. The positive persistence test expects two assets and two bindings, and still succeeds after deleting the external preparation and declaration paths.

`test_present_donor_with_different_pcm_cannot_replace_captured_audio()` first passes both readers on that complete fixture. It confirms the first donor exists in the retained packet and that its raw digest differs from the second take's. It changes only the second captured donor ID to the first present donor, recomputes the v2 binding ID and owning take reference, and rewrites the last synthetic generation/journal with the existing fixture helper. The second raw asset and technical receipt remain unchanged.

Python must refuse specifically with `retained prepared donor differs from captured take <take-id>`. Every returned Python error must end with that diagnostic; an unrelated history or identity failure cannot satisfy the test. The native quality inspector must also fail. Its exact reason is not asserted here, so this is a cross-reader refusal check rather than isolated proof of a particular native clause.

## Histories that isolate each guard

The existing four latest-generation corruption tests remain unchanged. Their history failures are useful, but cannot prove the prepared-donor guard itself. The new `test_consistent_histories_isolate_schema_language_and_source_kind_guards()` constructs three distinct fixtures:

| Case | Creation and history | Target clause |
|---|---|---|
| Schema four | Native creation/registration at schema 4; both synthetic take generations remain schema 4 | Prepared bindings require schema 5 |
| Japanese | Native Japanese profile with `a` vowel; language stays `ja` through all four generations | Prepared English speech donors cannot bypass project-language ownership |
| Procedural source | Source registered as procedural before either synthetic take; captured strategy equals that registered source in every generation | Prepared donors require a human-recording source |

The test requires generation numbers 1–4, constant language, nondecreasing schemas, schema 4 throughout the legacy case, unchanged source strategy after registration, and exact captured strategy equality. It does not relabel only the last generation. The public Python workspace validator must return only `prepared donor requires schema 5 English human-recording identity and five exact fields` errors. Unrelated downgrade, immutable-language or capture-time-history errors cannot hide a missing guard. Native must also refuse each fixture. Japanese is the supported non-English profile tested here; no Korean inventory implementation or acceptance is claimed.

These are deliberately constructed persistence models for reader validation. Native CLI workspace creation and source registration are real executions with synthetic actors/declarations; the saved take generations and receipts in this parity fixture are constructed by test helpers, not actual importer output. The six separate CLI integration methods run alongside them and do use actual native capture/import/retake persistence. Those methods also preserve legitimate donor/raw-asset reuse. Neither category is a real recording admission or factual rights decision.

## Focused execution and evidence

Base commit is `3ffe2125ca767722d8d900fcb842da153353f48b`. The implementation/test diff changes only this Python test file, SHA-256 `7177947ec58b5c22b6e9cabfcb8123a2139083dc3c75ba865213aeead0931c63`. Runtime validator/helper pins equal their base Git blobs.

- The Release CLI target/dependency check passed in **0.073 s**, with the graph already up to date and no objects compiled in that frozen stage. Actual compilation is retained only in the preliminary build log, without the final source/binary inventory.
- The final source-bound Python run passed **16/16 methods, zero skips**, in **4.356 s**: ten persistence parity methods, including two new ones, plus six actual CLI integration methods. The new methods contain four negative forms and one positive control, not five additional unittest methods.
- The actual CLI is copied and hashed before/after the run, with identical identities. Its version header embeds the base commit. HEAD and the exact test-source diff remain stable. Five explicit outcome traces are retained.
- A preliminary ten-method pass is retained separately and is not promoted to equivalent source/binary-inventoried evidence. This unit ran no CTest, full build/suite, ASan/UBSan, TSan, leak, OS fault, power-loss, device, DAW, Windows, acoustic or human checks. No mutation test that deletes validator clauses was run; the isolation claim rests on consistent histories and strict diagnostic assertions.

The adjacent index binds frozen `out/evidence/prepared-donor-python-negatives-2026-10-15`: **19 artifacts, 12 pins**, manifest **5,909 bytes**, SHA-256 `00ed62efb46a174c8855fc9263c30a82185a2c41c9998a1209fd7645d2e83b52`. The read-only verifier checks every retained artifact/pin, source diff/snapshot, result and trace, actual CLI identity and build version without running retained native/test tools. File/link inventories and bytes are compared around reader calls; the parity helper does not inventory empty directories.

## Independent verdicts and remaining admission work

Exact native repair `874c5fa2` has independently received **APPROVE, zero findings**, closing the originating `01e78a0c` deeper-negative-coverage P3. Its exact final answer is retained at `out/evidence/prepared-donor-semantic-negatives-independent-2026-10-15/review-874c5fa2.md`, **10,708 bytes**, SHA-256 `46310bb97a6871ab9d22e2b37f8090df32abc7bb0f2fea585718d39c73dddbf8`, with metadata. Both are pinned by this unit. The reviewer read committed source and rehashed retained evidence; it ran no build, test, native binary, retained script or playback. This closes the native candidate finding only. The new Python repair remains pending its own exact review.

The native PCM-range isolation P3, wrapper cancellation/commit-fault coverage, style-migration follow-up, optional parser/reader and CLI display work remain separate. Studio presentation and human review of notices are unestablished. No downloaded recording or prepared real donor was modified, registered, imported, played or approved. Genuine source/performer disposition, producer and distinct reviewer, phone/pitch/style coverage, failed-QC policy and listening remain required. Alpha **0/20**, U45 **HELD**, Beta **BLOCKED**, full goal **ACTIVE**. `.github` and README are untouched; Windows remains TODO.


Exact Python test repair `3df28db4` has now independently received **APPROVE, zero findings**, closing both originating `c9ef0d21` Python coverage P3s. Its exact 15,553-byte final answer (SHA-256 `9ade76b5c4342a14a35834c6b78330e1ea5d4e58615d87676a0a8ae4da48c2c3`) and metadata are retained under `out/evidence/prepared-donor-python-negatives-independent-2026-10-15` and bound by this index after the original freeze. No frozen manifest or execution claim changes. The reviewer independently inspected committed source and retained identities; it ran no build, test, native tool, retained script or playback. Remaining optional assertions/parser work and genuine admission/Beta requirements stay separate. Native range `090d1e09` and migration durability `dc433d80` are now under their own exact read-only review. Alpha 0/20, U45 HELD, Beta BLOCKED.
