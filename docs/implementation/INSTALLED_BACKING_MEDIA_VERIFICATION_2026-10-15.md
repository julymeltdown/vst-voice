# Installed backing-media export: independent review and sanitizer evidence

Date: 2026-10-15, Asia/Seoul. Exact implementation **`67e3e68bc00f7bfcff8e07d167302724ccebc438`** is independently **APPROVED for engineering, zero findings/no required changes**. No production/test code changes are made in this follow-up.

The **Review in English** session (`01a11bad-88f2-7521-849e-c38111dc2197`) independently inspected Git/source/patch identities, staging and replay logic, frozen artifact hashes, native case counts, baseline refusals and actual trial outputs. It executed no build, test, native binary or retained script. Its exact final answer is retained at `out/evidence/installed-backing-media-independent-2026-10-15/review-67e3e68b.md`: **17,709 bytes**, SHA-256 `1076ca22f899fa99d2c598af77529b0375baa800eeb9eef1da590db8e10c6fb0`. Turn `01a13d7d-184e-7610-aa97-a12c67061656`, message `msg_bc90124627c94ebca53bc85dd077b147`, completed Unix `1792034245`. This approval excludes later sanitizer execution and source/human/release decisions.

The lead separately executed the existing `sanitize` preset at clean **`6b0acfb589e7ce0b559b4a382a1b62b9e56639f1`**. Its only differences from the implementation commit are eight documentation files; production, test and configuration Git blobs are identical. Configuration, full dependent build and both test runs retain their exact source identity, environment overrides and clean/unchanged tracked state.

| Execution | Result |
| --- | --- |
| `cmake --preset sanitize` | PASS; Debug, ASan+UBSan enabled, TSan disabled |
| `cmake --build build/sanitize -j6` | PASS, 218.445 seconds; incremental full dependent rebuild, 914 build steps |
| First focused CTest launch, `detect_leaks=1` | Exit 8; both processes abort before recorded cases: this Mac's ASan runtime reports leak detection unsupported |
| Supported focused CTest, `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1` | **2/2 PASS**, 180.436 seconds: **38 export + 30 CLI native cases**; no retained ASan/UBSan diagnostics |

The supported run uses the unchanged binaries from the successful full build. Their hashes are recorded before/after execution and frozen copies are retained. Read-only symbol/library inspection confirms all three executables reference ASan and UBSan instrumentation; captured CLI test compile flags include the Python replay integration. The generated header embeds `6b0acfb5`. Leak detection is **UNAVAILABLE**, not passed. An evidence-copy helper initially used the wrong generated-header path and failed before the supported test launch; its initial script/error and corrected path are retained. It required no code change or rebuild.

The existing [evidence index](INSTALLED_BACKING_MEDIA_EXPORT_2026-10-15.evidence.json) now binds the independent verdict and `out/evidence/installed-backing-media-sanitizers-2026-10-15/manifest.json`: **41 artifacts/six pins**, including all attempts, raw logs, receipts, source snapshots, configuration/flags, version header, three executables and a read-only verifier. The earlier 124-artifact/five-pin root remains unchanged. Environment/dependency closure and a clean-from-scratch build are not claimed.

Two optional review wording notes are clarified in the original dossier: relative/absolute path resolution does not imply containment; missing audible media and changed muted-alias bytes are different controls, and only the sample helper explicitly rehashes installation immediately afterwards. Other optional notes remain in the exact verdict: empty-path coverage, the conservative replay aggregate limit, test-fixture cleanup on failure, a direct v1-with-media negative case and initial-trial retention gaps. None is a required change in the reviewed implementation.

This closes the focused ASan/UBSan verification gap for the backing-media unit. Full-suite/TSan and original source, producer/reviewer, failed-QC, musical coverage/listening, installed Alpha, platform/DAW/signing obligations remain. **Alpha0/20; U45 HELD; Beta BLOCKED; full goal ACTIVE.** `.github`, root README and stash are preserved.
