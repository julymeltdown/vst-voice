# Installed procedural continuity observation

Date: 2026-10-15, Asia/Seoul. Result: **local engineering observation verified; canonical U45 and Beta remain blocked**. Fetch/fast-forward pull completed before work. The integrating base is `312997181e3af83ed690515e5ca3ffa6f6d682c3`; the tested candidate consists of that base plus the five exact source captures in the [evidence index](INSTALLED_PROCEDURAL_CONTINUITY_2026-10-15.evidence.json). The generated build header reports the base commit, not a clean post-implementation build. Executable hashes and source captures are retained; authenticated build lineage is not claimed.

## Behavior and code boundary

The new native command is:

```text
observe-installed-procedural-continuity PROJECT SHA TRACK REGION LANGUAGES PACKAGE PACKAGE_SHA CANDIDATE_SHA INSTALLED PUBLIC_KEY CHUNK_FRAMES NEW_OUTPUT_DIRECTORY
```

[`candidate_package_commands.cpp`](../../apps/seam-voicebank-cli/candidate_package_commands.cpp) uses the existing trusted-key, signed-package, installed-tree and captured-project verification before invoking [`procedural_continuity_commands.cpp`](../../apps/seam-voicebank-cli/procedural_continuity_commands.cpp). It requires the saved selection and recipe path to match the verified installation and obtains installed procedural admission. It then compiles one immutable performance plan, executes a whole phrase, independently executes each owned window from phrase start, and executes the same windows through `ProceduralSnapshotStream`, retaining its actual checkpoint positions.

Every window must share the compiled-plan pointer and context identity. The observer compares finite native PCM before encoding, reporting both maximum absolute difference and differing float32 words. The predeclared engineering expectation is exact float32-word equality. A mismatch is an observation, not silently converted to acceptance. Whole, joined and per-window outputs are retained as unclamped IEEE float32 WAVs, preserving signed zero and finite values outside the ordinary PCM24 writer's clamp range. `partition.json` retains compiled note spans, whole/per-window phone projections and boundary geometry. Split phone projections are merged by identity and clipping boundaries before comparing them with the whole projection.

This is the selected **procedural snapshot API**, not standalone or CLAP audio-block execution. Classical rendering currently renders a whole phrase and crops each window; the command refuses that family. Installed neural execution is unavailable. The contribution is a native artifact-producing observation path over existing DSP APIs; existing snapshot tests already covered the basic equality property.

## Bounds and output ownership

Input bounds are one selected region with at most 256 notes, integer 8–192 kHz sample rate, at most 1,920,000 context frames, 2–64 forced chunks, at most 64,000,000 restarted-prefix frames and at most 128 whole phone projections. The final observation is limited to 64 KiB. These are operational bounds, not canonical singing-quality thresholds.

The existing parent must be available and the destination must be a new directory outside the installation. A held POSIX directory creates flat owned files exclusively with no-follow access. The writer hashes its own reads and checks file identities, sizes and modification/change timestamps; completion reopens and rehashes every owned file before syncing the directories and printing success. `observation.json` is written last and is externally pinned because it cannot include its own hash. Existing entries are preserved. Cancellation or late failure may leave partial diagnostic files; no successful observation is printed, retries need a new directory, and crash-atomic export/recovery is not claimed. No adversarial privileged-owner or historical-execution authentication claim is made. Windows execution remains TODO.

## Executed evidence

A full untargeted Release build succeeded before the final focused CTest run. The repaired full build took **15.190 seconds**, followed by **93 passing native test cases**: 66 snapshot cases and 27 CLI cases, in **27.095 seconds** of recorded subprocess time. The three observer trials and refusal controls are additions inside existing CLI cases, not three additional CTest cases. They have no Python compile guard. The focused suites also retain their existing checks; the complete repository test suite was not run for this unit.

| Synthetic selected region | Forced chunk frames | Chunks | Difference versus whole, both paths | Derived boundary |
| --- | ---: | ---: | --- | --- |
| Vibrato with quarter-turn starting phase | 11,003 | 3 | 0 differing words; max absolute difference 0 | Cuts inside planned `a` |
| Two contiguous legato notes sharing a lyric | 12,100 | 2 | 0 differing words; max absolute difference 0 | 100 frames inside the second note |
| Japanese `な` nasal articulation | 1,001 | 24 | 0 differing words; max absolute difference 0 | Initial cuts inside planned `n` |

The three C++ trials are retained, together with three direct native trials using a copied signed synthetic installation and saved projects whose sole semantic relocation changes the recipe path. The public key, package, four installed files, exact producer/test binaries and command receipts are retained; no private key is copied. Original CTest projects still refer to their historical temporary paths. The relocated trials explicitly disclose their different saved-project hashes. They do not create a production singer or clear any internet recording.

The independent read-only verifier checks **208 retained artifacts and 10 preserved bindings**, decodes float32 RIFF bytes, recomputes every whole/window/join comparison, verifies actual checkpoint geometry, merges split projections and checks the vibrato/legato/nasal plan geometry. It verifies hashes and observations; it does not authenticate past execution, listen, or run the native producer. Its optional `--source-commit` compares final source captures with immutable Git blobs after integration.

The initial focused run failed **26/27 CLI cases** passing and **66/66 snapshot cases** passing. The legato fixture's hard-coded cut was 24,101 frames, beyond its 24,000-frame phrase, so the native command correctly refused fewer than two chunks. A retained direct probe confirms exit 2 and the refusal diagnostic. The project uses 960 PPQ; the fixture now calculates the second-note frame from its tempo map and adds 100 frames. Failed sources, binaries and logs remain frozen. No DSP repair is claimed for that fixture error. The first successful build's duration was not retained; subsequent build receipts preserve their real durations. The final runner form is retained, not every historical runner edit.

## Acceptance boundary and next work

The native record is `seam.u45.installed-procedural-continuity.v1`, with `ENGINEERING_ONLY`, `NOT_QUALIFIED`, `authorizesRelease: false` and `releaseEligible: false`. Acoustic F0/phase, neighboring-context invalidation, scheduler/cache execution, standalone blocks, Windows and human acceptance remain explicitly NOT_RUN. Marker timing and note modulation parameters are projections of the shared compiled plan, not independent acoustic measurements. The canonical reproducibility tolerance remains UNRESOLVED; this unit does not freeze its null value or mark any release case MET.

Among the eight continuity cases in the full contract, only the procedural dimensions of `R2.vibrato` and `R2.legato-melisma` can use this API prerequisite. These short synthetic JA fixtures do not accept either case. Released resources, case/resource combinations, host/platform execution, independent quality review and the original contract remain required. The canonical report and U45 reconciliation hold are unchanged; the new record has no closed historical/fresh native replay reader yet.

Next engineering work should add that closed, caller-pinned continuity replay reader, rejecting altered metrics, projections and coordinated PCM/record forgeries through fresh native execution. Then retain phrase-scoped neighboring-context invalidation and actual scheduler execution before reviewed semantic integration. Source-bank admission remains separate: the internet route has 25 clean VocalSet f1 WAVs and 272 female Karoryfer growl WAVs, but genuine producer/reviewer, source/performer and public-licence disposition, useful phonetic/pitch coverage and listening remain unresolved. The [internet-route handoff](INTERNET_FEMALE_ROUTE_HANDOFF_2026-10-13.md) preserves those requirements and the original no-supplied-recording voice requirement.

Usable Alpha remains **0/20 recorded passes**, External Beta **BLOCKED**, and the full development goal **ACTIVE**. `.github`, README Windows TODO, canonical acceptance files, original female recordings, blank review worksheets, frozen observation kit and the existing stash are preserved. Independent technical review of this new unit is pending; prior review approval applies only to its expressly named commits.
