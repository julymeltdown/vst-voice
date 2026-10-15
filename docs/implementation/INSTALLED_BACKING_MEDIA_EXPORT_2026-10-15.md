# Bind backing media in installed singer export and replay

Date: 2026-10-15, Asia/Seoul. Base: `6f6380ce883a3ee60570133477a2fd007417dc8f`. **Implemented and locally verified; independent review pending.** Git fetch/fast-forward pull and local/tracking/live master equality were confirmed before the unit. This advances U45 native operation evidence; the canonical reconciliation and release holds remain.

## Defect and resulting behavior

Installed sample/recipe export rejected every project containing an audio track, even though `ExportService` already verifies backing WAV identities, copies them to a private staging snapshot and renders from that snapshot. A singer over backing audio could not use the installed-operation observation/replay path. The new native regressions reproduce the exact refusal in both families against unchanged production.

`candidate_package_commands.cpp::exportInstalledProject` now uses that existing export service instead of the blanket refusal. Source paths resolve under the captured project directory; the service verifies original content hash, rate, channels and complete frame count, including muted tracks, then copies the exact bytes before rendering. Shared content is copied once. The wrapper checks the committed media artifact against every captured track's declared identity and geometry before emitting success. A failure after commit preserves outputs for investigation.

Projects with consumed backing media emit **schemaVersion 2** and `seam.u45.installed-{sample|recipe}-export.v2`. The closed record adds `backingMediaCapture: PROJECT_HASH_MATCHED_STAGED_WAV_BYTES` and one `backingMedia` row per source-bearing audio track: track ID, canonical media artifact path, SHA-256, original frame count/rate/channels. Different tracks may share one file. These are project-bound inputs; the installed singer's signature does **not** sign or grant rights to the backing recording. No source approval or human acceptance is inferred. Projects without backing inputs retain the version-1 shape; source/build/executable identity matching still applies to replay.

`tools/external_beta/installed_export_record.py` admits both exact versions. Version 2 requires nonempty bounded media bindings, valid distinct track IDs, canonical hash-named owned WAVs, complete geometry matching the artifact, consistent geometry for shared bytes and no unbound media artifact. Version 1 cannot claim backing artifacts. Both versions retain the same no-authorization flags and external CLI/key pins. Replay compares every record field except the receipt digest, re-renders through the caller's pinned native tool and compares receipt semantics except the existing transaction/time fields. A self-consistent declared rate forgery still fails against actual native output.

## Execution and contrary evidence

| Check | Result |
| --- | --- |
| Initial test-harness build | **FAIL**, one C++ mixed-auto declaration; retained separately and corrected before runtime baseline |
| First compiled baseline | Native 28 PASS/2 FAIL; new Python cases fail; native diagnostic initially generic |
| Refined unchanged-production baseline | Native **28 PASS/2 FAIL**, both exact `Operation export cannot attest backing-media inputs yet`; Python **7 PASS/1 failure/2 errors** across 10 methods |
| Final full Release build | **PASS**, 2.297 seconds after the preceding complete baseline rebuilds |
| Focused CTest | **2/2 PASS**, 21.855 seconds; **38 export + 30 sample-review CLI native cases** |
| Direct Python policy tests | **10/10 PASS**; these mock native orchestration and are not ten native renders |
| Real native regression coverage | Both installed sample and recipe: audible trimmed/gained/panned backing, relative/absolute aliases, one copied artifact for two tracks, muted-output equality, replay and coordinated-output forgery controls; missing/changed audible and muted-alias media refuse; original project/output/installation preserved |
| Separate retained sample trial | Actual version-2 export and Python/native replay pass against an existing synthetic signed sample installation. Changed original bytes refuse direct export (2) and replay (3); a coherent rate forgery in both rows refuses replay (3). Earlier **87-artifact/seven-pin** root and retained output unchanged |

The separate trial uses a newly generated 0.5-second mono 48 kHz tone and an existing **SYNTHETIC_ENGINEERING_ONLY** sample fixture. The initial trial remains retained; `actual-final` adds the coherent geometry-forgery control. Neither imports internet recordings nor supplies a production singer, real producer/reviewer or listening verdict. The prepared internet pool remains a separate, unapproved 1,469-original acquisition.

## Retention and remaining work

The [evidence index](INSTALLED_BACKING_MEDIA_EXPORT_2026-10-15.evidence.json) binds **124 artifacts and five preserved files**: initial harness failure, both baseline attempts, final source snapshots/patch, build/test receipts and raw logs, configured CTest file, copied executables/version header, separate trials and a read-only verifier. The final CLI is **9,842,512 bytes**, SHA-256 `19aba7fe6f72fa6ffd34e4e007ce834f03f9f60385ec1597b91186f71239b3a8`. Its embedded commit is the base `6f6380ce`; the exact uncommitted implementation/test patch and eight selected source snapshots bind the executed unit. This is not a claim that the base Git tree alone contains the new code or a hermetic build. The verifier rehashes those bytes, checks exact baseline/final outcomes and actual records, and optionally matches the four changed code/test blobs to an integrated commit. It does not execute tests, builds or native tools.

No full CTest suite, sanitizer, Windows, DAW or playback/listening acceptance was run for this unit. Native operation evidence still does not prove acoustic usefulness, source rights or external human authority. Learned resources, full continuity/measurement semantics, genuine source admission/qualification, installed unaided Alpha and all original platform/host/signing/release obligations remain. **U45 HELD; Alpha0/20; Beta BLOCKED; full goal ACTIVE.** `.github`, root README and existing stash are preserved.
