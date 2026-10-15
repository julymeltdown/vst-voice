# Python validation of retained prepared-donor provenance — 2026-10-15

The external Python draft validator now accepts schema-5 producer workspaces and verifies each prepared-donor binding against retained metadata. Previously it rejected every schema-5 generation before examining the binding. The native importer can therefore be followed by a schema-aware external validation path. This unit tests deliberately constructed synthetic saved fixtures against both readers; it does not claim a new end-to-end native importer execution, real recording admission, source approval or Beta readiness.

## Code contract

`tools/external_beta/_prepared_donor_evidence.py` validates four owned byte strings without filesystem access or WAV measurements. It mirrors the native packet-facts contract: exact packet/plan shapes and unapproved fields, source/span/derived identities, original-source links, retained hashes, all-row integer/boolean wire types and consistent bounded PCM declarations. It refuses duplicate JSON keys, nonfinite values, int64 overflow, non-UTF-8 encodings and excessive nesting/collections/nodes/strings. File limits remain 16/1/1/2 MiB. These are declared facts, not measured WAV, phonetic or rights decisions.

`_production_draft_validation.py` admits schema 5 while retaining schemas 1–4 and refusing future schema 6. A present `preparedDonor` must have exactly five typed fields, a safe donor ID, lowercase digests and an exact v2 binding ID. It requires an English human-recording binding. The original license is independent of the separately reviewed source declaration. Retained notices are checked under the canonical real-directory chain `source-evidence/packets/<packetSha256>/`; missing, changed, linked or incomplete storage refuses. Bounded descriptor reads supply the owned bytes that are hashed and parsed.

Bindings are grouped by packet for each historical generation checked. One bounded donor map is used for that group; every binding must match donor/audio, plan, original-license and attribution identities. Nothing is cached across generations or operations. Existing immutable language/style/take/source history checks extend to schema 5. Style-owned take pitches use the declared layers, consistent with assignments and the native model. Ordinary v1 binding identities retain historical behavior; this unit adds strict v2 identity checking only for prepared bindings. This is focused schema-5 interoperability, not a claim that all existing Python/native validation behavior is identical.

## Tests and retained evidence

Source base: `a42e808675d6a49c8c4af30128eb1c3a6f310765`. Exact three-file source/test patch SHA-256: `09303924892a041eeab2b4a8a65d896befdcc55323103d21921c27d7085906e1`.

Eight new test methods exercise two donor IDs, two styles and one shared synthetic PCM blob. The fixture uses actual native workspace creation and synthetic source registration, then constructs schema-5 project/journal fixtures with failed current inspection receipts. Both Python validation and the actual native read-only source-quality reader accept the intact fixture before and after deleting all external preparation files. No review is added, native release eligibility remains false and no current passing quality assessment is reported. Every dual-reader check snapshots and compares workspace bytes/links to detect writes.

Refusal subcases cover 11 malformed typed bindings, four correctly re-identified wrong donor/notice bindings, 15 notice/parent/incomplete-storage forms, four schema/language/source-kind bypass forms, seven internally consistent geometry/wire-type forms, seven packet parser forms and three immutable-history changes. The seven geometry variants directly exercise the pure Python metadata API; the three history changes are Python history checks. Neither group is presented as a new native parity execution or closure of the separate native range-test P3. The parser variants run against both workspace readers, including a freshly hashed packet and v2 binding.

| Recorded execution | Result |
| --- | --- |
| Full dependent Release build | PASS, 32.187 seconds; all C++ sources unchanged from the exact base |
| Initial baseline attempt | FAILED before validation because the first test profile supplied one pitch layer; retained as a fixture failure, not regression proof |
| Corrected old-validator baseline | Expected failure, 0.172 seconds; valid schema-5 generations rejected with `producer schema is invalid` |
| First focused Python/native run | 54/54 PASS, 7.339 seconds; before the parser follow-up |
| Final focused Python/native run | 55/55 PASS, zero skips, 7.339 seconds; eight new methods plus 47 existing tests |
| Read-only retained verifier | PASS; 55 artifacts and eight preserved bindings |

The five final modules are prepared-donor workspace, production-draft parity, draft producer workspace, generation-request mirror and review-transition mirror. Full build happened before Python-only fixture/parser corrections; native source snapshots and CMake are proven byte-identical to the source base. Every test command records and retains the exact CLI digest before/after, an unchanged copied executable, source patch/copies, generated version header, logs and receipt. The final CLI is 9,967,408 bytes, SHA-256 `dc54f74e316af6de92d809055c81f5da159bf2eb442325879bc68f4fd9c47289`. The first failed fixture and superseded passing run remain frozen. CMake's existing test glob includes the new module without configuration edits.

```sh
python3 -B out/evidence/prepared-donor-python-parity-2026-10-15/verify.py
# Use --commit COMMIT after integration or later source changes.
```

The adjacent evidence index binds the manifest, logs, prior exact independent review and four unchanged gate/dossier files. No full suite, new sanitizer run, Windows, DAW, device, signing, playback, source quality or human acceptance is claimed. Independent review of this Python unit is pending.

## Remaining production work

The latest independent review separately approves importer `dd0b515d` with one P3 for new directory durability and packet verification `a42e8086` with no findings, closing its scaling P3. The exact combined answer is bound by this unit's manifest and adjacent index. Directory parent-sync ordering, candidate-4 complete notice closure through directory/package/install verification and Studio/CLI notice/phone display remain engineering work. Packet-bound candidate publication still refuses explicitly.

No downloaded WAV or real donor was imported here. Internet holdings remain 1,469 distinct originals: 65 VocalSet f1 singing, 272 growls and 1,132 speech, with their separate identities. Original license terms, attribution/change notices, owner source/EULA/performer disposition, a real producer and distinct reviewer, explicit phone/pitch/style policy, failed-QC policy and listening remain necessary for real admission. The original recording-free voice and full-product scope are unchanged. Alpha recorded **0/20**; U45 **HELD**; Beta **BLOCKED**; full goal **ACTIVE**. `.github`, root README/Windows TODO and existing stash are preserved.
