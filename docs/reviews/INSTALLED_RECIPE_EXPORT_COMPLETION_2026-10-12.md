# Installed-recipe export: failure diagnosis and engineering completion

Date: 2026-10-12, Asia/Seoul. Source base: `c1a17861ef7df83a3d5a707fea6de85bdb4a785f`, with exact executed source hashes in the [evidence record](INSTALLED_RECIPE_EXPORT_COMPLETION_2026-10-12.evidence.json). The user authorized finishing the two previously preserved pending files. Fetch and fast-forward pull completed first; local, tracking and live remote master were equal and already current. This closes an engineering operation without qualifying a singer or release.

## Root cause and retained contrary evidence

The original fixture used Japanese language with Latin surface `a`, no reading hint and no phonetic override. Fresh binaries produced 24,000 stereo frames at 48 kHz with every PCM24 data byte zero. The captured installed recipe hash was `299690aab9c5f0cfe8894a98c57e7e5c86a2ccf66b85752457148a0b28666ba1`.

[Production snapshot resolution](../../libs/seam-rendering/src/render_snapshot.cpp) calls the Japanese phonemizer: unsupported Latin spelling produces `UnsupportedCharacter` and `pau`. Pause rendering is intentionally silent. Changing the audible fixture to Japanese `あ` resolves to `a` without pronunciation warnings. Regression assertions exercise both production snapshots, retaining the original unsupported-spelling case.

The original `peak > 1e-4F` assertion is unchanged. Corrected PCM24 peak: **0.0075130462646484375**. Receipt pre-encoder peak: **0.007513029966503382**, with the difference explained by quantization. Initial peak and later zero-metadata failures are retained as failures.

## Additional defects repaired

| Boundary | Resulting behavior and regression |
|---|---|
| Incorrect set-export peak/clipping defaults | One pre-encoder master observer serves both writers. Set receipts carry master measurements; quieter stems cannot overwrite them. Controlled PCM sums cover quiet and clipped masters. |
| Receipt-only provenance mutation | `ExportResult::receiptSha256` binds exact writer text before callbacks. CLI compares the observed digest before success. A deterministic Committed callback changes only provenance: ownership inspection passes, but the held digest detects mutation. Recovery does not manufacture original-request attestation. |
| Stereo default and rate mismatch | CLI uses project `deviceOutputChannels` and integer 8–192 kHz. Tests export mono/four-channel audio and refuse 192001/384000 Hz before creating output parents. |
| Recovery bypass of no-replacement policy | `recoverInterruptedSet` defaults true for existing callers; the evidence CLI sets false. Pending journals remain untouched. Ordinary recovery still works, with destination policy rechecked after restoration. |
| Destination/journal races | macOS/Linux exclusive rename uses the locked parent inode; initial journal writes are create-new. Late destinations, empty directories, dangling symlinks and foreign journals survive refusal. Live recovery and cooperating sibling exports conflict while that parent is locked. Windows concurrency remains TODO. |
| Shell refusal workaround and overstated declarations | Tests inspect typed exit/signal/streams, exact exit 2, expected stderr and sanitizer markers. `projectLanguages` accurately labels whole-project note declarations. Second-track pin mismatches refuse even for muted or solo-excluded tracks. This is not acoustic language coverage. |

The operation binds the decoded project hash, signed installed candidate, all saved installation pins and actual canonical recipe path. Admitted frozen bytes feed [packaging and rendering](../../libs/seam-authoring-runtime/src/export_service.cpp). Sample/model operation capture and backing-media attestation remain explicit refusals. Failed observation after commit preserves output for investigation and emits no success record.

## Executed verification

| Check | Result |
|---|---|
| Final full dependent Release build, `cmake --build build/release -j6` | PASS; no stale dependent binaries used |
| Release CTest: helper, neural worker protocol, neural phrase runner, Japanese reading, export, process-crash recovery, sample-review CLI | **7/7 PASS, 21.71s**; export 37/37 and CLI 25/25 native cases |
| ASan/UBSan target and transitive dependencies, `cmake --build build/sanitize --target seam_export_tests -j6` | PASS |
| Instrumented `seam_export_tests 'set export'`, absolute macOS symbolizer | **8/8 PASS**, no visible sanitizer reports; scoped cases only |
| Independent final worker review | APPROVED on source review; runtime checks belong to lead |
| Staged diff, tracked source closure, evidence identities | Checked before publication |

The evidence JSON records commands, executed source/executable digests and 29 retained artifact digests. Ignored `out/evidence/installed-recipe-export-2026-10-12` retains starting file bytes/patch, failing export, intermediate failures and final export. This is local engineering retention, not an immutable release archive. The brand-fonts/tooltips stash is preserved. `.github`, Windows TODO and U45 holds remain. No human listening, playback, DAW, signing, Windows runtime or release acceptance is claimed.
