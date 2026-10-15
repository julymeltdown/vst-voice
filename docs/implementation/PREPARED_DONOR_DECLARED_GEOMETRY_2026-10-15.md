# Prepared donor declared geometry — 2026-10-15

The evidence-only reader now refuses internally inconsistent declared PCM geometry without requiring a WAV. This addresses the forward-looking candidate-verification note in the independent `bd35ddac` review. It checks metadata consistency, not measured audio, annotation truth, source rights or musical suitability.

After checking original JSON types, the shared parser requires a declared rate of 8,000–192,000 Hz, a positive frame count within the existing 64 MiB canonical mono PCM16 WAV cap (including its 44-byte header), a nonnegative start, an increasing interval whose length equals frames, and duration exactly equal to frames/rate. The condition short-circuits before division or interval subtraction on invalid inputs. Full and owned snapshots still measure and compare actual audio separately; their measurement contract is unchanged. Silent audio remains valid input when its declared nonzero flag matches measured bytes.

Source base: `7e141c0fdd997df6b98b46db8b7d1e7fe4f7c5f3`; exact two-file final patch SHA-256: `e465afd3dbd9e08da11a9697269625d3918c98cd201d29d0d79cc5b49df420b9`. The adjacent JSON index binds `out/evidence/prepared-donor-declared-geometry-2026-10-15/`.

| Executed check | Result |
| --- | --- |
| Test-only baseline build | PASS, 4.199 seconds |
| Baseline regression | 0 PASS / 1 FAIL at `CHECK(!evidence)`, 0.350 seconds: declared rate zero was accepted without a WAV |
| Full dependent Release rebuild | PASS, 33.201 seconds |
| Snapshot and source-intake CTest lanes | 2/2 PASS, 2.296 seconds; 9 C++ cases and 41 Python methods, zero skips |

The one new case tests nine malformed forms across evidence-only and owned validation: zero/out-of-range rate, zero/negative frames, negative/shifted start, extended/empty interval and wrong numeric duration. Every evidence-only refusal is checked after deleting the selected WAV and must report an invalid-argument declared-PCM diagnostic. Owned validation must report the same diagnostic. Existing positive readers, held-byte mutation checks, silent native CLI behavior, schema-5 persistence and packet damage cases also pass in these lanes. No old-code failure is claimed beyond the first baseline assertion, which ends that case.

The immutable manifest contains 28 artifacts and eight preserved bindings: exact baseline/final source blobs/patches, complete logs/receipts, copied executables, version headers, explicit before/after executable digests, parent manifests, the exact originating review and the unchanged four gate/dossier files. The read-only verifier passes and runs no retained native tools. No additional real-donor run, source registration/import, sanitizer, full-suite or human acceptance is claimed. Independent review of this metadata consistency unit is pending.

```sh
python3 -B out/evidence/prepared-donor-declared-geometry-2026-10-15/verify.py
# Use --commit COMMIT after integration or later source changes.
```

Next is the inspected prepared-donor importer, followed by complete candidate-4 declaration/packet notice closure in directory/package/install checks. Schema-5 review is in progress; certified-asset repair review remains pending. Actual source admission still awaits the pending owner rights/performer decision, genuine producer/distinct reviewer, assignment/review inputs and failed-QC policy. Recorded Alpha 0/20, U45 HELD, Beta BLOCKED; `.github` untouched, Windows TODO retained.


## Independent disposition

The exact combined read-only review of `7e141c0f` and `24cf12d5` is retained under `out/evidence/asset-and-geometry-independent-2026-10-15/` (verdict 13,074 bytes, SHA-256 `3d85c506c3f83af9a4a1d881a667dbff73bf3d95529c89ea6bb618b83c5da92f`) and bound by the adjacent index. The asset repair received APPROVE with no findings and its P2 is independently closed. Declared geometry received APPROVE and its concern is closed, with one open P3: add internally consistent out-of-range test inputs so each range clause is independently protected. The reviewer inspected exact committed source and retained evidence without builds, native execution or running retained verifiers. No later importer/cache, source, human or Beta acceptance is implied. Original manifests remain immutable.

The [PCM range test follow-up](PREPARED_DONOR_PCM_RANGE_NEGATIVES_2026-10-15.md) now locally repairs that coverage gap with exact interval/duration equalities, two valid boundary forms and five out-of-range forms. All 22 snapshot cases pass; each negative form requires the exact range diagnostic through five reader paths, including owned whole-packet facts. Zero frames and an empty interval remain logically coupled; no independent-conjunct or guard-deletion mutation claim is made. This new repair awaits its own exact review. The original runtime review and frozen manifest remain unchanged; genuine admission and Beta are still blocked.
