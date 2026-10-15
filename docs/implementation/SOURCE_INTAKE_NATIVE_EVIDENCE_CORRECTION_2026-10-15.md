# Correct native execution attribution in the source-intake evidence

Date: 2026-10-15, Asia/Seoul. Base: `bc29b1d0c1f8df72948dd503031baa48c820f0a8`. **Evidence wording corrected; no production/test code changes.** Exact packet-integrity commit `21c04d3a94d75b047fc7769570f1793d3d69168a` has independent engineering APPROVE with one P3 and no required code changes. That P3 is independently **CLOSED by exact `6f6380ce`**, zero new findings; [terminal verdict retained](SOURCE_INTAKE_REVIEW_CLOSURE_2026-10-15.md).

The reviewer found that both directly invoked source-intake suites include `test_native_analysis_binds_decoded_source_and_current_cut_contract`. It runs the actual native CLI when available. The historical 37-method packet-integrity and 38-method early-encoding stderr logs both mark it **ok**, not skipped. The reports' no-native statements were wrong. Those runs did execute native integration; their executable hashes/versions were not captured, so the historical binary identities remain **UNKNOWN**. No CTest run is claimed. Historical manifests, logs and verifier source remain unchanged; their erroneous printed no-native message is explicitly superseded here.

Two new controlled repeats use separate exact Git source copies and an explicit `SEAM_SOURCE_INTAKE_CLI` pointing at a retained executable:

| Python source snapshot | Result | Native integration |
| --- | --- | --- |
| `21c04d3a` packet-integrity sources | **37/37 methods PASS** | Included, actual native analyze invocation |
| `1a0cfceb` early-encoding sources | **38/38 methods PASS** | Included, actual native analyze invocation |

These are separate mixed Python/native runs, not proof of the old executable's identity or a full native suite. Both use the copied 9,841,392-byte CLI, SHA-256 `812abe00aba817de9df1078efc0cdf1193f1c27995f8cf044805716828f7f2a9`, from the fresh full Release build at `1a0cfceb` recorded in the [f1 acquisition unit](FEMALE_F1_SLOW_SOURCE_EXTENSION_2026-10-15.md). The retained generated version header and the executable's embedded literal identify `1a0cfcebf8435020002dc91b96f25b008907454a`; this is build attribution, not cryptographic attestation. Neither repeat rewrites the earlier donor outputs or creates bank/source approval. Interpreter/dependency closure is not claimed.

The [correction index](SOURCE_INTAKE_NATIVE_EVIDENCE_CORRECTION_2026-10-15.evidence.json) binds **24 artifacts and five preserved files**: the exact review final answer, metadata, both five-file Git snapshots, test stdout/stderr/receipts, copied executable/header and local runner/read-only verifier. It pins both historical manifests and the new build/acquisition binding. The new verifier rehashes both historical roots and their pins, checks the native integration log lines, exact snapshot Git blobs and fresh native identity, and reports the execution scope correctly. It does not run tests, builds or native commands.

Reviewer thread: **Review in English**, `01a11bad-88f2-7521-849e-c38111dc2197`; final turn `01a13d57-3838-72b1-9428-75fccd294972`, completed Unix `1792031145`. The reviewer executed read-only Git/hash/JSON/RIFF/Decimal checks, independently reconstructing all 312 donor PCM intervals from their originals and labels; no retained scripts, tests or native binaries were executed by the reviewer. Exact final answer SHA-256 `7fe6d25c451aa993b78966734fa9415a893c7b86ce6110e2d456b79642d96255` (10,994 bytes).

Other optional review notes remain in that verdict; durable bank admission stays **OPEN**. Exact encoding unit `1a0cfceb` is independently APPROVED with zero findings; the new f1 acquisition remains independently unreviewed. No rights, actors, listening, stored-audio QC, source admission or Beta acceptance is supplied. Alpha **0/20**, U45 **HELD**, Beta **BLOCKED**, full goal **ACTIVE**.
