# Native selected phone-donor snapshots — 2026-10-15

The native production library now reads one selected, unassigned English speech donor together with the exact prepared packet, plan, license and attribution bytes. The read-only CLI is `verify-phone-donor DIRECTORY PACKET_SHA256 DONOR_ID`. It returns unapproved snapshot facts and performs no project or bank writes. This is the byte-consumption prerequisite for the remaining import bridge; it does not complete that bridge or establish Beta readiness.

## Implementation and trust boundary

`prepared_phone_donor.hpp/.cpp` adds `loadPreparedPhoneDonor` and an owned `PreparedPhoneDonorSnapshot`. The caller supplies the packet digest from the trusted retained handoff. Rehashing an unknown replacement and trusting that digest is insufficient. The reader uses existing asset-path validation and bounded held-file reads, including regular-file and link checks. The CLI dispatcher is isolated in `source_intake_commands.cpp`; the existing import and approval commands are unchanged.

The packet and plan have bounded JSON parsing and exact supported record keys, English speech identity and explicitly unapproved/unassigned review fields. Shared snapshots are checked against packet digests. Proposal identities and original source references must agree with the retained plan. The selected canonical mono PCM16 WAV must match its file hash, PCM hash, frame count, rate, duration and nonzero-byte fact. Nonzero PCM does not establish audibility, suitability or listening acceptance. The reader holds bytes in memory; future import must use these held bytes and retain the corresponding evidence, rather than reopening the preparation paths.

Only selected audio is opened. Other proposals' unapproved fields and plan identities are checked, but their WAV bytes are not checked by an individual call. Accordingly, CLI output explicitly reports `wholePacketAudioVerified: false`, `nativeImport: false`, `sourceRightsApproved: false` and `bankAdmission: NOT_APPROVED`. The existing Python whole-packet preflight remains separate. This reader accepts the canonical 44-byte WAV container produced by the preparation writer, rather than arbitrary WAV layouts. Original recording/label hashes are retained references; originals are not reopened and label content is not re-derived here. Pitch, phonetic, performer and legal truth remain independent questions.

## Execution evidence

The integrating source base is `0c4d3e2d48627b79ce80ae1f5ecd15cdb15a13cb`; the exact seven-file implementation/test patch SHA-256 is `153f926cb8d9d252b982d215d2365c888d077753974180f23ad571939ea1c795`. The copied final binaries embed the base commit, with the retained patch supplying the implementation change. The evidence directory is `out/evidence/native-phone-donor-snapshot-2026-10-15/`; its machine-readable index is beside this report.

| Check | Observed result |
| --- | --- |
| Full dependent Release build, `cmake --build build/release -j6` | PASS, 33.812 seconds |
| Focused CTest: source intake, sample-review CLI, style-owned candidate import | 3/3 PASS, 20.493 seconds |
| Python source-intake suite | 41 methods, zero skips, including three new methods invoking the actual native CLI |
| Existing C++ sample-review/candidate cases | 30 + 3 PASS |
| Actual SLT packet trial | 312 native selected-donor commands PASS, 4.522 seconds |

The three new methods cover matching held snapshots without mutation, silent PCM facts, changed/missing/linked retained files, incomplete markers including dangling links, FIFO refusal, invalid/trust-mismatched packet digests, duplicate keys, approval injection, source/plan identity and selected PCM inconsistencies. A missing unselected donor WAV intentionally does not block another selected donor. These are actual native subprocesses, not mocked results. The Python-method count and C++-case count are different units and are not combined into a native-test count.

For the real trial, the trusted packet SHA-256 is `a73a4da7ab25d743c5af45f2fd33c9fae71b9f55ec8d4aa3696e9929e5b49457`, from the previously frozen attributed SLT packet. All 312 proposals were selected once. All 316 files in that preparation output matched the prior manifest before and after the trial. The trial used the retained final CLI, SHA-256 `0b41b7bf07a99f6c3e5513e4658bd521092b29669fc66bc65b2a7b9c53beca66`. No new original recordings, source registration, native import, playback or approval occurred. These are female SLT speech donors, not VocalSet f1 singing or Japanese inventory coverage.

The baseline used a byte-identical retained earlier CLI, embedding `933c81424613fc87265f921a73800580c18114a0`, whose source predates this command. It was not freshly rebuilt at the integrating base. The first copy lacked executable permissions and produced harness errors; that attempt is preserved and is not evidence of a feature defect. After correcting the copy mode and fixture restoration, all three new methods failed against the unsupported old command (35 assertion/subtest failures, not 35 methods). The final full build and tests then passed.

Two subsequent read-only evidence-verifier failures are also preserved: an incorrect assumption about C++ log rendering, then an overly broad replacement of the pinned parent manifest filename. Neither executed native code or changed the verified source. The final verifier and final manifest include both attempts, the earlier manifests, all execution logs/receipts, source snapshots, patches, copied binaries and all 312 command outputs. The final manifest contains 691 artifacts and six preserved bindings. Hash/source verification passed. Environment metadata is an observation, not a hermetic dependency closure.

To recheck retained hashes, logs and current implementation bytes without running native tools:

```sh
python3 -B out/evidence/native-phone-donor-snapshot-2026-10-15/verify-final.py --commit 48647f55
# The later evidence API changes the live source; use the exact earlier commit here.
```

## Remaining work and acceptance

The subsequent [evidence and owned-byte API report](PREPARED_PHONE_DONOR_EVIDENCE_API_2026-10-15.md) records the current `.evidence` snapshot layout and separate metadata-only and in-memory validation entry points. This report and its frozen sources remain the historical first reader implementation.

The next implementation must retain one shared content-addressed packet/plan/license/notice set, bind each imported take to its selected proposal and the actual consumed raw bytes, and enforce that binding through verification, history/recovery, candidate publication and installed redistribution. Duplicating the entire packet in each take would be wasteful. The independent architecture audit of committed base `0c4d3e2d` is still pending; this reader has not yet received an independent implementation verdict. No schema-version decision is asserted by this report.

Actual source admission still needs the pending owner disposition, genuine producer and distinct reviewer, exact rights records, QC policy and musical/phonetic/listening decisions. Synthetic tests may exercise the engineering path while those inputs remain absent. No real recording is registered or imported by this unit. The original no-supplied-recordings voice, full language/resource scope and other product gates remain required.

The subsequent full ASan/UBSan rebuild at exact committed reader `48647f55` passed in 54.227 seconds. The source-intake CTest lane passed in 5.180 seconds: 41 Python methods, zero skips, invoking the instrumented native CLI. The retained reader object references both sanitizer runtimes and the executable links ASan. Explicit binary hashes match before/after execution. This adds bounded sanitizer coverage of the reader; it does not rerun the 312-donor trial under sanitizers. `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:strict_string_checks=1` and `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`; leak detection was disabled and no leak result is claimed. The separate sanitizer manifest and read-only hash/source verification are bound by the index.

Full suite, TSan, AppKit/device/DAW/screen-reader acceptance, Windows/signing and human/external review were not run or established by this unit. The four canonical acceptance/dossier files remain byte-identical: recorded Alpha 0/20, U45 HELD, Beta BLOCKED. No overall progress percentage is inferred from these test counts. `.github` is untouched and Windows support remains TODO.
