# Prepared donor evidence and owned-byte validation — 2026-10-15

The native reader now separates packet/notice integrity from selected audio verification. It can also revalidate an owned snapshot after the preparation directory disappears. These APIs are prerequisites for a persistent importer and an offline candidate verifier; this unit does not import recordings, write project provenance or complete that bridge.

## Code contract

`prepared_phone_donor.hpp/.cpp` defines:

- `PreparedPhoneDonorEvidence`: exact packet, plan, original license and attribution snapshots; selected donor ID; canonical selected proposal JSON; and the packet's declared derived-audio digest. `loadPreparedPhoneDonorEvidence` verifies those four files without reading a donor WAV. Its result does not establish measured audio facts, annotation truth, rights or review.
- `PreparedPhoneDonorSnapshot`: the evidence object, held selected WAV bytes and measured PCM facts. `loadPreparedPhoneDonor` preserves its CLI behavior and requires the selected WAV to exist and match the declared digest and PCM geometry.
- `validatePreparedPhoneDonorSnapshot`: consumes an owned snapshot plus independently retained expected packet digest and donor ID. It rehashes all five held files, reapplies the shared packet/plan/identity/unapproved-field checks, and derives the selected proposal and audio facts again. Cached proposal text, expected-audio digest and measured fact fields are not trusted. It performs no filesystem reads.

Both file-backed entry points retain the directory/link/`INCOMPLETE` refusals. Cancellation is checked across file, proposal and held-byte validation. The C++ snapshot now contains its shared fields under `.evidence`; the CLI accesses that object but emits the same selected-snapshot JSON. Neither API verifies every donor's audio or reopens the separate original recording/annotation inputs. The packet digest still must come from the trusted retained handoff.

This distinction matters for redistribution: a candidate normally contains processed unit audio, while its provenance refers to the imported raw digest. A future notice-closure check can use the evidence API and compare the selected proposal's declared audio digest with the captured take's raw digest. It must not claim it decoded absent raw WAV bytes. Persistent binding, source-policy association, publication and package enforcement remain separate required work.

## Verification

Source base: `07496e8a52a3251f2118d5d88c7b78321a36d315`. Final five-file implementation/test patch SHA-256: `f45877fd9f49b661fdf78405c5cb46d4b5c85f4d11b9a133e49ac28520d61975`. Retained binaries embed that base; the patch and exact source snapshots identify the executed implementation. Evidence: `out/evidence/prepared-phone-donor-evidence-api-2026-10-15/`, bound by the adjacent machine-readable index.

| Execution | Result |
| --- | --- |
| Full dependent Release rebuild | PASS, 18.364 seconds |
| Release CTest: snapshot, source intake, sample-review CLI | 3/3 PASS, 20.649 seconds; 4 new + 30 existing C++ cases, 41 Python methods, zero skips |
| Full dependent ASan/UBSan rebuild | PASS, 51.673 seconds |
| ASan/UBSan CTest: snapshot and source intake | 2/2 PASS, 5.572 seconds; 4 C++ cases and 41 Python methods, zero skips |
| Existing attributed SLT packet | Four native donor selections PASS, one per distinct source recording; all 316 preparation files unchanged |

The four new C++ cases check evidence integrity when the selected WAV is absent; held-byte validation after deleting the entire preparation folder; canonical reconstruction despite stale cached proposal/fact fields; mutation of each held file even with updated cached digests; renamed notice/audio files; changed caller/snapshot selection and packet pin; and pre-cancellation. Missing attribution still blocks evidence verification. Actual native subprocesses in the Python suite continue checking the unchanged CLI fields and malformed input refusals.

The first Release attempt failed while compiling the new synthetic fixture: integer JSON literals were ambiguous against the repository's explicit constructors. The corrected fixture uses `std::int64_t`; the subsequent full rebuild passed. A generated-header retention path error was corrected before CTest began. Both attempts are retained; neither is presented as a native runtime regression or a baseline feature failure. No old-code failing baseline is claimed for these new API entry points.

The final manifest contains 76 artifacts and eight preserved bindings, including all build attempts, logs, source snapshots, copied executables, before/after executable hashes, actual command outputs, sanitizer object/flags and the architecture advice. The read-only verifier checked exact source blobs and the complete five-file patch. Sanitizer flags are `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:strict_string_checks=1` and `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`; leak detection was disabled. This does not establish full-suite, TSan, human, AppKit, microphone, screen-reader, DAW, Windows or signing acceptance. No sanitizer repeat of all 312 prior donors is claimed. Environment metadata is an observation, not a hermetic build closure.

```sh
python3 -B out/evidence/prepared-phone-donor-evidence-api-2026-10-15/verify.py
# Use --commit COMMIT after integration or later source changes.
```

## Next implementation boundaries

The independent design audit of `0c4d3e2d` recommends schema 5 for required take-to-packet provenance because old project readers otherwise ignore fields or silently recover an earlier generation. It also identifies an existing recovery P2: a missing or changed retained evidence file is treated like a damaged write and recovery can skip newer work. Repair that distinction before persisting new packet-bound takes. The existing per-take candidate license-snapshot requirement is another open finding. The advice is design review, not implementation approval of the reader or this API.

One audit proposal requires correction before implementation: the original corpus license is distinct from the reviewed source-rights declaration used by the approved public-recording admission process. Retain and bind both. A narrow clarification has been requested from the reviewer; no equality assumption or rights disposition is introduced by this unit. The later bridge must preserve the original packet, plan, license and attribution once, capture each selected proposal and actual consumed raw digest atomically, refuse incompatible readers, and enforce the complete notice set through directory, package and installed verification.

Actual registration/import remains held for the pending owner rights/EULA/performer disposition, genuine producer and distinct reviewer, assignment/phonetic decisions, failed-QC policy and listening evidence. These four real trial selections are female SLT speech, not singing coverage or Japanese qualification. No new original audio was downloaded. The canonical four gate/dossier files remain byte-identical: recorded Alpha 0/20, U45 HELD, Beta BLOCKED. `.github` is untouched; Windows remains TODO.
