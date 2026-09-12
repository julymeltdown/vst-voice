# Style/Coverage sheet model

Status: U25 model, native sheet/controller/menu integration and paged coverage-issue inspection implemented and locally verified. Large-bank latency, long style-ID inspection outside issue details and live host qualification remain open. This is not Beta GO acceptance.

## Coverage issue inspection checkpoint

### Immutable bank snapshot integration

`VoicebankSession::resolveTrackSnapshot` now exposes an immutable owner-created resolution snapshot. Its one-entry cache is keyed by track and full bank reference; catalog refresh attempts and actual development-fixture policy changes invalidate it. A failed refresh makes newly requested snapshots unresolved until a successful refresh, even though the legacy by-value API retains its existing behavior. Retained snapshots remain readable but are not current merely because their saved hash still matches. The API is owner-thread-only; the snapshot payload can be retained as immutable data, but the mutable session cache is not a concurrent resolver.

Standalone and CLAP editor adapters bind the snapshot resolver to the Style/Coverage controller. Model preparation still validates/copies the captured source. Routine view/choice eligibility requires the same owner-created snapshot plus unchanged project/revision/session context, avoiding manifest copies and comparisons. Apply additionally retains the full manifest/trust/style validation before the canonical command. The old by-value resolver remains available with its original full checks for compatibility and tests. No public snapshot constructor accepts a mutable candidate alias.

The large-bank regression now packages, signs and installs its 256-style/16,384-unit manifest and compares both paths. It covers snapshot reuse, no-op policy changes, actual policy invalidation, successful refresh invalidation, unchanged retained payload, exact Apply/undo, changed bank reference, direct unrevisioned project mutation, failed refresh from a deliberately corrupted owned test manifest, recovery and same-content document replacement. The separate installed-bank audio/export regression also uses the snapshot controller path.

Observed focused Release view cost fell from about 6–7 ms to about 0.015 ms in the one-phone capacity fixture; Debug fell from about 47 ms to about 0.089 ms. These are local view-construction observations, not an entire frame/host benchmark. Bank snapshots do not watch filesystem changes automatically: the host must refresh its catalog to observe package changes, as before. One-entry eviction can conservatively require Refresh if another caller switches the cached track/reference. Initial catalog scan, initial snapshot allocation, captured-project copying, weighted coverage queries and metadata-heavy project comparison remain separate latency concerns.

Verification: all 641 Release core cases pass (16.20 s), eight focused style cases pass Release/Debug (2.70/22.64 s), and 23 export cases pass Release/Debug (2.09/11.53 s). The longer focused test includes real large-package installation and intentional corrupt/restore refresh testing, not just the timed redraw loop. Native/core/focused builds and diff checks pass. Changes remain local/uncommitted; no full U25/Beta GO or live host acceptance is claimed.

### Inventory and repeated-choice performance checkpoint

Inventory construction now indexes units once and emits counts in declared style order, replacing the nested style-by-unit scan. The index borrows strings only during construction; no borrowed references survive in the draft. Coverage workload per token is calculated in that same pass. Japanese pronunciation results and failure diagnostics are retained lazily for the immutable captured region, so choosing another style does not rerun pronunciation. Choosing the already selected style still records explicit provenance but preserves its existing coverage result. New drafts/Refresh capture and resolve new source data; stale Apply checks are unchanged.

An admitted-capacity regression creates 256 declared styles and 16,384 units, verifies all enabled/disabled counts, complete one-phone coverage across choices, 1,000 repeated choices, exact source preservation, and 20 native view constructions. Observed focused Release timings: prepare 5.690 ms, different choice 0.314 ms (including inventory assertions), 1,000 same-style choices 0.007 ms, view construction average 6.050 ms. Debug: 49.280/4.588/0.262/47.495 ms respectively. These are local observations for a simple one-phone fixture, not a worst-case performance gate, acoustic test, maximum weighted-query benchmark or frame-rate guarantee. There is no measured pre-change comparison.

At this earlier checkpoint the resolver copied a candidate/manifest and compared the complete captured manifest/project on each view check. The newer immutable-snapshot checkpoint above addresses bank copying/comparison while retaining source validation. Maximum-query analysis and large-document capture latency remain unqualified.

All 641 Release core cases pass (14.14 s); eight focused cases pass Release/Debug (0.55/1.72 s). Release native/core/focused and Debug focused builds pass, as does `git diff --check`. Changes remain local/uncommitted; this is a bounded preparation/repeated-choice improvement, not full UI latency or U25 acceptance.

### Installed-bank audio and persistence verification

`tests/test_export_service.cpp` now includes `native style selection changes installed bank audio and survives reload export undo and redo`. It creates original/soft synthetic sine WAVs with different amplitudes, packages and signs a two-style manifest, installs through `VoicebankInstallerService`, and requires real `TrustedInstalled` resolution with development fixtures disabled. The native sheet uses that session's resolver rather than an invented trusted candidate.

The regression proves draft-only PCM remains unchanged and actually exercises a cache hit. Apply emits one document-change notification/revision, modifies only explicit track style, selects `soft-a` instead of `original-a`, changes phrase hashes and produces finite nonzero Final PCM with less than one tenth the original energy. Preview PCM also changes. This amplitude fixture demonstrates source selection, not a realistic expressive timbre or a universal style gain rule.

Project JSON reload preserves the entire score. A newly created bank session rescans installed storage, retains trusted resolution, and a cold render without a cache exactly reproduces the edited samples. Cached replay, Float32 master/stem exports and undo/redo also reproduce their expected PCM exactly. The fixture does not constitute a shipping voicebank, human listening approval, physical playback or real DAW state-stream verification; procedural/neural style routes are outside this sample-bank test.

Release core passes 640 cases (13.98 s), and all 23 export cases pass Release/Debug (2.58/10.47 s). Scoped builds and `git diff --check` pass. An initial test constructor mismatch was corrected before the successful build/run. No production rendering fix was necessary for this tested path. Work remains local/uncommitted; full U25/Beta GO acceptance is still open.

Coverage details switches from declared styles to a six-row issue list. Activating an issue opens read-only text wrapped to 32 display columns and paged in groups of six lines. It includes issue kind, phone, requested style, target MIDI, note ID, zero-based phone ordinal, diagnostic and related witness unit IDs. Related units are analyzer witnesses, not an exhaustive alternative list. Back to issues restores the originating issue-list page; Back to styles preserves the staged choice. Explicit Apply is still the only score mutation. Escape cancels the entire sheet, consistently with other inspector drafts.

When no report is available, a dedicated row opens the complete unavailable explanation rather than presenting zero issues as success. A valid report with no issues explicitly retains the audio-QA qualification warning. Both inspection modes reuse stale source/semantic guards and do not select notes, invoke recovery actions, or change history. Full source strings are reconstructed exactly across display lines; wrapping failure rejects opening rather than partially replacing the detail view.

Two additional regressions cover an eight-issue/two-page fixture, complete detail reconstruction, stale callbacks and source changes, original-list-page restoration, preserved draft choice, empty-issue qualification, unavailable language diagnostics and Escape. All 639 Release core cases pass (14.10 s); seven focused cases pass Release/Debug (0.42/0.54 s). Release native/core and Debug focused builds and diff checks pass. The 480×320 detail raster `/tmp/seam-coverage-details.TvsgHr/detail.png` was inspected; six readable rows and action buttons do not overlap. This is in-process controller and raster verification, not live VoiceOver, OS input, host or acoustic qualification. No commit/push was performed.

## Native integration checkpoint

The native controller now owns the draft within the existing single-review surface. Declared styles appear six per page, with current draft selection and enabled/disabled unit counts. Navigation and row choice leave the project untouched. Apply track style creates one canonical undoable edit; Cancel discards; Refresh recaptures the current source. Source changes disable selection and Apply, and stale accessibility interaction identifiers reject. Missing/untrusted banks and procedural tracks retain the model's explicit errors.

Standalone and embedded adapters supply current resolutions through their existing `VoicebankSession`, with no fabricated trust or new filesystem scan. The standalone Edit menu exposes **Track Style and Coverage…**. The shared inspector adds a third STYLE button using matching painter, pointer and accessibility geometry. This retains the existing character-Off/available-dock visibility policy; the standalone menu is independent of that policy.

The sheet displays covered/total phonemes with an explicit not-audio-QA label, or the model's unavailable diagnostic. Full row and summary strings are available to accessibility; long visible text uses existing bounded text rendering. Issue navigation was added in the newer checkpoint above; full visual inspection of long style IDs without an associated issue remains open. Structural analysis remains bounded synchronous work on open/choose, not an asynchronous coverage job; large-resource UI latency is not qualified. Current-bank matching resolves/copies catalog data and compares the captured source on owner-thread view/action checks, so those costs also need profiling before release.

Five focused cases now cover the original model contracts plus native pointer entry, staged row changes, stale accessibility callbacks, changed trust, source replacement, Refresh/Cancel, exact Apply/undo/redo and multi-page style selection. The standalone command-dispatch regression includes the new command. Release native app/core and Release/Debug focused builds pass. The 480×320 raster at `/tmp/seam-style-ui.zoH1Oe/style.png` was inspected: status, two style rows and six action slots fit without overlap. This is controller/raster evidence, not actual AppKit/CLAP host, VoiceOver or musician acceptance. Changes remain local and uncommitted.

## Contract

The planned `style_coverage_sheet.hpp/.cpp` now captures a vocal track/region, immutable project context and sample-bank manifest. It reuses `resolveVoiceStyle` for exact reference/version/content-hash and trusted-installed checks. Declared style order is preserved. Each row counts enabled and disabled units independently; inventory presence is not advertised as renderer support.

The current style can remain missing or unchosen without a fallback. A current unresolved choice can be inspected and explicitly changed, but Apply requires a declared style. Sole-style/legacy resolution uses the existing canonical resolver's provenance and remains draft-only until Apply. Choosing an undeclared style rejects without replacing the last valid choice.

Apply revalidates active track/region, session revision/generation, complete project contents and the current resolved bank's manifest/trust/identity. It publishes only `TrackStyleEdit` through `EditPerformanceCommand`, with one undo group; no-op Apply does not increment revision. Cancellation and applied-state reuse reject. The caller must supply its current bank resolution; the model does not rescan files or independently certify package trust.

## Coverage and bounds

Coverage uses the existing Japanese pronunciation resolver and corrected structural coverage analyzer. Pronunciation warnings/errors, no phonemes, another bank language or excess query work produce explicit unavailable diagnostics rather than a successful empty report. Style choice remains available when the optional coverage query is unavailable or incomplete. The result is not proof of source audio, forced-edit compatibility, alignment, renderer support or listening quality.

Admission bounds: 10,000 region notes, 256 declared styles, 256 bytes per style ID, 16,384 units and 65,536 total unit-phone entries. Coverage additionally limits the token query to 4,096 tokens and a weighted token×unit-phone estimate of 4,194,304. These are model/query admission limits, not expanded renderer capacities or a universal latency guarantee. Compilation/resolution here is synchronous and is not performed by a painter.

Procedural tracks explicitly reject this sample-bank editing route because their active styles belong to recipe controls. Procedural/neural style presentation still needs the appropriate resource-specific UI integration; an inactive sample-bank field must not appear to control a procedural singer.

## Original model verification (superseded by native checkpoint above)

Three cases in `tests/test_style_coverage_sheet.cpp` cover declared inventory, disabled-style coverage, draft isolation, exact style-only Apply/undo/redo, no-op Apply, missing choice, bad choice, changed manifest/hash/trust, document replacement, cancellation, non-Japanese/empty coverage, size admission and procedural routing rejection. The trusted candidate in these model tests is a supplied fixture, not evidence of a new package installation or signature verification.

Release native app/core/style-model and Debug style-model builds pass. All 635 Release core cases pass (13.77 s); three focused cases pass Release/Debug (0.50/0.42 s). `git diff --check` passes. An initial missing namespace terminator was repaired before the successful builds; the earlier missing-executable test attempt is not acceptance evidence.

Initial native verification: all 637 Release core cases passed (20.83 s); five focused cases passed Release/Debug (6.01/6.31 s) including pointer entry. The newer coverage-issue checkpoint above supersedes these counts. Next: long style-ID inspection, large-bank latency profiling, and audio/persistence/live host qualification. Changes remain local/uncommitted; U25/Beta GO remain incomplete.
