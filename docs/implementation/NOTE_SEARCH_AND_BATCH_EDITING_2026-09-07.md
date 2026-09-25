# U24 note search and batch editing

Status: bounded lyric/hint/ID search, resolver-derived phone/warning search including bounded 10000-note Japanese fixtures, asynchronous native Find query/results/detail/selection and repeat-match commands, reconciled hints, native hint entry, and native replacement/distribution reviews are implemented locally. Worst-case snapshot/publication latency, full diagnostic aggregation, full IME and cross-host qualification remain open; U24 is not accepted.

## 2026-09-08 native Find integration

### Native active-diagnostic Find

#### Long failure detail preservation

Producer inspection found `recordRenderFailure` appended free-form failure text to `messageKey`, although the registry caps that key at 128 bytes. A long render failure could therefore be discarded by the panel before Find could search it. Render failures now keep the stable `render.<kind>` key and store bounded detail separately. Generic `DiagnosticRegistry::fromError` also retains the original error message rather than reducing it to a generic key alone.

`Diagnostic::setDetail` retains at most 4096 UTF-8 bytes without splitting a code point. Malformed bytes and ASCII controls other than tab/newline/carriage return become visible `\xNN` escapes. Explicit truncated/escaped flags accompany the stored text. A SHA-256 digest of the original input participates in issue identity, so different discarded tails or byte representations do not collapse into one issue merely because their displayed prefixes match. Registry validation rejects oversized, malformed or non-display-safe stored detail and malformed digest syntax. The digest identifies supplied bytes; it is not a provenance attestation. Hashing is proportional to supplied error-text length, while stored detail remains bounded.

Diagnostic snapshots account for detail/digest during capture and search detail as its own field. Identity/staleness checks include detail, flags and source digest. Native full inspection and technical descriptions display detail with truncation/escape markers. The explicit Copy Diagnostic action includes the bounded detail; automatic support-bundle field allowlisting is unchanged, and raw detail/digest are not newly exported.

Regression covers messages longer than the key limit, exact detail cap and multibyte boundary truncation, Unicode/newline retention, invalid/NUL byte escaping, invalid stored fields, searchable detail, distinct failure aggregation including different truncated tails, stale detail changes and reconstruction through native inspection. This repairs a specific producer loss path; full producer inventory and live copy/support/host qualification remain open. Verification is in the execution ledger.

The Edit menu now exposes `Find Active Diagnostics…` through a dedicated application command/callback. The shared native input helper distinguishes replace, note Find and diagnostic Find before opening the field. Diagnostic Find does not require a vocal region and uses the background diagnostic job, not note search. The normal Find field cycle also reaches Active Diagnostics after pronunciation warnings and returns to lyrics only when a current region exists.

The native surface displays preparing/error/empty/result states, counts and result pages. Pointer, keyboard and accessibility row activation opens a complete, paged read-only diagnostic: title/impact, code/message, severity, occurrence count, opaque affected references or an explicit global/unspecified scope label, and available recovery-action names. It never guesses note ownership, changes selection, executes recovery, or creates undo/document notifications. Recovery names are text, not action buttons; the existing diagnostic workflow remains their separate invocation surface. Detail opens with Back to Results focused. Escape backs out, then closes; Refresh explicitly recaptures current sources.

The job/review validates document generation/revision and diagnostic content/counts; native mode/state/index/page/interaction IDs isolate prior callbacks. A changed diagnostic set disables old result inspection even without a song revision change. Switching modes requests cancellation, and the shared paint poller retires each worker without reviving a cancelled panel. All Find workers must retire before another Find starts. Late input from a replaced document rejects.

Controller regression covers a project with no vocal region, query validation, pending/ready states, keyboard and semantic inspection, complete global metadata, long CJK opaque references, result/detail pages, diagnostic-count changes, same-content document replacement, Refresh and cancellation, with no recovery calls or note/song changes. Dispatcher tests cover the menu route. The minimum-size system-font capture is recorded in the execution ledger. Rendering now strips only trailing CR/LF from painted review rows so intentional line terminators do not produce false truncation ellipses; underlying row text and accessibility values remain intact.

This connects the model/job prerequisites below to native inspection and supersedes their earlier unconnected-UI status. It searches active panel diagnostics, not every diagnostic producer through one unified source. Live menu/IME/screen-reader/host qualification, complete producer scope metadata and worst-case responsiveness remain open; U24/Beta GO are not accepted.

### Active-diagnostic source integrity prerequisite

#### Read-only diagnostic search snapshot

##### Background job and document-bound review

`seam/native_ui/diagnostic_search_job.hpp` adds a single-worker job and `DiagnosticSearchReview`. Owner-thread start preflights diagnostic entry/action/ID/field/raw-byte budgets before copying entries, captures the editor's opaque document generation and revision, and sends only plain diagnostic entries/query to the worker. Panel callbacks and session/UI pointers are not captured by the worker. It prepares the existing snapshot and publishes a success/error into one protected slot; owner polling retires the worker, validates both current diagnostic content/counts and document context, then makes the review available once.

Cancellation requests stop without waiting for live work and discards computed results. Another start cannot replace an unretired worker. An invalid new request invalidates an earlier ready result. Exceptions, malformed UTF-8 and source drift produce failure rather than an empty successful search. Destruction stops/joins before worker-visible members are destroyed. Snapshot capture and final source validation remain synchronous and are not a universal responsiveness guarantee.

Review lookup returns a guarded diagnostic-panel index only; it does not select notes or execute recovery actions. Every lookup rechecks document revision/generation, current diagnostic content/counts and review closure. Tests cover ready transfer, missing hit, duplicate start, cancellation, diagnostic count changes without a score edit, same-content document replacement, tempo edits, closed reviews, invalid superseding queries, bounded capture, worker decoding errors and zero recovery callbacks/selection mutation. This is the job/review layer; native Find controls, result presentation and scope-aware navigation are still unconnected.

`seam/native_ui/diagnostic_search.hpp` adds `DiagnosticSearchSnapshot`, separate from note search/navigation. It captures complete diagnostic content and occurrence counts, searches code, presented title/impact, message key, severity, opaque affected IDs and recovery-action labels, and returns one hit per diagnostic in panel order. Hits retain source index, first matching field/field index, exact field text and Unicode-scalar match offset. Fields are searched independently, so a query cannot match across artificial field boundaries. Global issues remain searchable even with no affected ID; no ID is interpreted as a note and no action handler is invoked.

Admission limits are 10000 entries, 1–256 query scalars/1024 UTF-8 bytes, 32 actions per entry, 65536 UTF-8 bytes per searchable field and a combined 4M-scalar search budget. Every field is decoded and counted even after an early match. Oversized or malformed later data rejects the whole snapshot rather than returning a deceptively partial successful result. Cancellation is checked per entry/field and during matching. The source remains immutable; `matches` compares ordered full diagnostic content and occurrence counts, catching additions, dismissals and repeated occurrences even when the song revision does not change.

Regressions cover global/scoped results, distinct owners, code/title/action-label matching, CJK/emoji scalar offsets, exact captured text, source-order provenance, zero recovery callbacks, count/dismissal invalidation, malformed UTF-8 after an early match, per-field/aggregate/action/query bounds and cancellation. A dedicated `seam_diagnostic_search_tests` target runs registry/panel/search tests without the complete core suite.

This is not yet connected native Find UI. Snapshot `matches` is diagnostic-content validation only; the job/review layer above adds document binding. The eventual native adapter must also isolate UI interaction identity and appropriate scope before focusing or offering recovery actions. The snapshot API itself is synchronous but now runs in a worker through the job layer; worst-case latency qualification remains open. No note-owner mapping or active-diagnostic UI acceptance is claimed.

Tracing active diagnostics found that both the native panel and authoring runtime coalesced records using only code and message key. Different affected IDs, severity or recovery actions could therefore disappear into the first record. That is unsafe input for diagnostic Find and could also hide a later critical issue behind a warning.

`Diagnostic::sameIssueAs` now defines shared exact-content identity across code, message key, severity, ordered affected IDs and ordered recovery actions. Both runtime record paths and the panel use it. Only occurrence count is excluded from identity; its shared addition saturates at `size_t` maximum instead of wrapping to an invalid zero/small count. No affected IDs are guessed, unioned, sorted away or reassigned. Tests verify separate affected scopes and severity/actions, correct callback targets, blocked-action rejection, duplicate count aggregation, independent dismissal and saturation.

Full active-diagnostic Find integration remains unimplemented. Current runtime diagnostics generally carry no affected IDs, and those IDs are opaque strings rather than typed note references. Render results have track/region/phrase metadata but no general authoritative note binding. The captured model above retains unscoped issues and validates diagnostic changes independently of score revision; it does not infer note ownership from message text or treat an arbitrary opaque ID as a note. This prerequisite repairs evidence loss without claiming that runtime diagnostic Find has been connected. Verification is in the execution ledger.

### Asynchronous preparation follow-up

#### Resolver cancellation and indexing follow-up

##### Bounded 10000-note capacity follow-up

The shared resolver now admits up to 10000 notes and lyric tokens and 65536 resolved phone tokens. Its independent 4096-override, 4096-scalar per-lyric and 65536 aggregate/referenced-text budgets remain enforced. This supports ordinary 10000-note Japanese CV input without splitting the region or discarding continuation context, not arbitrary text expansion. These are pronunciation/search admission limits, not a promise that every renderer accepts one 10000-note phrase.

The cancellable kana adapter accepts a maximum-token budget. The resolver passes its bound, and the adapter rejects before appending a note's output if that would exceed the region total; it no longer constructs the entire oversized region output before rejection. A single note's temporary output remains bounded by the resolver's source text/override admission. Exact-budget, one-under-budget and zero-budget tests cover the append check. The existing single-argument adapter API retains its prior behavior for callers outside the resolver.

A 10000-note fixture resolves 9999 `か` notes plus a final continuation into 19999 phones, finds all 10000 vowel matches, returns no unsupported-character warnings, and retains the final inherited vowel/owner. The async job transfers the same large result and supports selecting the last note without document/history changes. A 10001-note input rejects; a bounded-text 80000-phone expansion rejects with the explicit token-budget error. Existing 10000-note literal fixtures that exceed aggregate pronunciation text still reject derived search, rather than pretending to have zero matches. A native controller fixture searches 10000 Japanese notes, finds a distinct explicit hint on the last note, and exercises result inspection, selection, reveal, accessibility focus and unchanged project state. Verification is recorded in the execution ledger; live latency/language/host and full release qualification remain open.

Derived Find now passes its worker stop token through the shared Japanese resolver into a cancellable kana-adapter overload. The resolver checks cancellation during validation, indexing, hashing by note, override/context processing and token-context grouping, propagates it through recursive base resolution, and checks again before publication. The adapter checks during lyric/note/override indexing, between notes and every 256 source characters. Cancellation returns Conflict without partial output. The existing `IPhonemizer` single-argument API remains available and uses a non-cancelled token; no partial-result fallback was introduced.

The resolver now indexes lyrics and notes once instead of repeatedly scanning whole region vectors during validation, input hashing and token ownership lookup. The adapter indexes overrides by note once while preserving their source order. A regression with append-then-replace overrides verifies the resulting vowel still feeds the following continuation, cancellable/non-cancelled outputs and identities agree, pre-cancellation rejects, and the complete source region is unchanged. Existing pronunciation tests remain the wider semantic regression oracle.

The prerequisite indexing step initially retained all old limits; the bounded capacity follow-up above supersedes its note/lyric/output-token caps. Full copies, sorting, a per-note override batch and final sequence hashing are not interrupted internally; cancellation is cooperative between those bounded phases, not an instant-stop guarantee. Runtime worst-case cancellation latency remains open.

`seam/ui/note_search_job.hpp` introduces one owner-managed `jthread` for Find preparation. Start validates admission and captures an immutable project/source-generation context on the editor thread. Literal/derived search and result indexing then run on that snapshot in the worker, which publishes only into a mutex-protected completion slot and never calls the UI/session. Owner polling joins only after publication and checks source/revision/region/generation before transferring a selectable navigation cursor. Exceptions and resolver errors publish failure, not empty successful results.

Native Find displays Preparing Find Results with only Close enabled until retirement. Both standalone and embedded paint paths already call the shared poll method; it now services Find and lyric jobs independently, scheduling frames until cancelled jobs retire even after their panels close. Close requests stop without joining ongoing search. Cancellation discards even already-computed results, and new searches cannot overwrite an unretired worker. Switching to another creator-review mode also requests cancellation. State/interaction-specific semantic IDs prevent preparing-state callbacks from being reused on published results. Existing native tests now explicitly wait for completion instead of assuming synchronous query submission.

Focused regression uses a 10000-note literal project to verify ready transfer, single-worker admission, cancellation/discard, revision and same-content document replacement rejection, worker-side derived-capacity failure, invalid-new-request invalidation and source/selection preservation. Native regression checks preparing controls, Escape/Close, no restart before retirement, no cancelled-panel revival, stale completion, Refresh recovery and old semantic-ID rejection.

This moves search/resolution work off the UI thread, not every cost: snapshot capture, final source validation and full-text wrapping remain synchronous. The resolver now has cooperative stop checks as described above; close itself does not wait for retirement. Destruction requests stop and joins before worker-visible members are destroyed. Worst-case responsiveness and live host shutdown/IME behavior remain unqualified. Verification is in the execution ledger; no U24/Beta GO acceptance.

### Repeat-match follow-up

After explicitly selecting a reviewed result, Find retains its source-bound cursor. The macOS Edit menu exposes Find Next Note (Command-G) and Find Previous Note (Command-Shift-G), routed through dedicated application commands into `NativeEditorController::repeatFind`. Each direction wraps in stable result order, replaces selection with exactly one note, reveals its onset/pitch and focuses its semantic node. The same reveal helper is used by initial result selection. Navigation does not modify musical data, notify documentChanged or add history.

Repeat is unavailable during active text composition, reviews, dialogs or a drag, and before a reviewed result has been selected. Source revision/musical-input/document-generation validation rejects stale results without moving selection; it never silently reruns a changed query against new data. Closing a new Find review without selection cancels that cursor. Completing another creator review replaces the old Find state. The native test traverses two notes across forward/backward wrap, checks viewport visibility and focus on both, rejects repeat during composition, resumes after cancellation, rejects tempo/source replacement drift, and verifies unchanged project/history. Dispatcher tests cover both command routes. Actual menu-shortcut execution, non-Latin keyboard layout behavior and cross-host qualification remain open.

The macOS Edit menu exposes `Find Notes…` with Command-F through a dedicated application command/callback. It opens captured native query input, sharing existing composition/cancel/length validation but not the replacement stage. Query submission searches only; it cannot modify a lyric or select a note. Invalid or replaced-document input rejects. After submission, `Next search field` cycles Lyrics, Hints, Note IDs, Japanese resolved phones and Japanese pronunciation warnings using the same query. Errors remain visible with selection unavailable, distinct from a valid zero-match result; Refresh explicitly recaptures the current region.

The native result list has six rows per page, counts, field labels and explicit inspection. Pointer row activation, accessibility activation or keyboard focus/Enter opens the complete field text, wrapped at 32 display columns and paged six lines at a time. Result detail defaults focus to Back to Results. Escape returns to the list; it never selects a note. The only selecting detail action is `Select and reveal note`, which invokes the guarded navigation model, selects exactly that note, pans timeline/pitch to expose it, closes Find and focuses its note semantic node. It does not call documentChanged or add history. Search mode, field, result/detail index, text page, revision and interaction identity isolate semantic callbacks from prior states.

The existing modal keyboard/pointer/scroll and embedded-input guards apply while Find is open. Native controller regressions cover query validation, complete reconstruction of 400 CJK characters, all five fields, result/detail pages, keyboard inspection, Escape without selection, stale actions, refreshed results, Delete isolation, same-content document replacement, explicit reveal/focus and unchanged whole-project data/history. The standalone dispatcher regression covers the new menu callback. Tests exercise the shared native controller, not live platform input or a screen reader.

Search preparation is now asynchronous as described above; capture/publication retain explicit synchronous boundaries. Repeat-match commands are connected. Windows/embedded menu discovery, actual Command-F/IME/live workflows, full diagnostic aggregation, derived capacity, large native-result latency and release acceptance remain open. Verification and minimum-size render evidence are recorded in the execution ledger; changes remain local/uncommitted.

## 2026-09-08 derived search fields

### Captured result navigation

`seam/ui/note_search_navigation.hpp` adds a captured, selection-only Find cursor for the native adapter to consume. Preparation searches an immutable validated project snapshot and retains its opaque document-generation context. Results expose source field/query/revision and stable musical ordering; no note is selected merely by preparing a search.

Explicit index selection, next and previous validate active region, revision, current musical inputs and document generation before changing selection. Initial next selects the first match; initial previous selects the last. Subsequent navigation wraps in either direction and follows the cursor's last explicitly visited result, independently of unrelated external selection changes. It replaces selection with that one note, returns its ID for native focus/reveal, and creates no document change or undo record. Empty results, invalid indices, cancellation and closed/stale cursors reject without moving either selection or cursor. Closing leaves any already visited selection in place; it is not a musical undo operation.

Regression exercises ordering, both wrap directions, exact selection, no history, region/index rejection, pre-cancellation, close, revision invalidation, explicit recapture, same-content document replacement and empty results. A 10000-note literal fixture captures all matches, selects the last, wraps forward and backward, and verifies exact project preservation. Capture/validation are synchronous and measured locally; native menu/input/result presentation, viewport reveal and asynchronous preparation remain integration work. This is model/navigation evidence, not a completed user-visible Find workflow.

`NoteSearchModel` now supports `NoteId`, `GeneratedPhoneme` and `PronunciationDiagnostic` in addition to separate lyric/hint fields. ID searches use canonical printable IDs. Generated phone searches use the current shared Japanese resolver once for the whole region, preserving neighbor and continuation context and honoring explicit hints and retained overrides. Phone symbols are space-separated per note; note-associated warning messages are newline-separated. Matching remains literal, case-sensitive Unicode-scalar substring matching (for example, `a` also matches `pau`), with exact field text and first-match offsets in stable tick/ID order. No project data or selection is changed.

Derived indexing checks cancellation per record and a shared 4M-scalar output budget before appending. Resolution now runs in the Find worker with cooperative checks; worst-case latency is not qualified. Failure propagates as an error, never a successful empty result or a context-losing per-note fallback. The resolver now supports 10000 notes/lyrics within independent text/override/output budgets as described above. Literal lyric/hint/ID search retains its independent 10000-note/4M-scalar admission.

Diagnostic coverage here means current resolver warnings attached to existing notes, not all active application/resource/render diagnostics or orphan warnings with no focusable note. Resolution failures remain search errors rather than note hits. A unified diagnostic provider and native field selection/result navigation are still needed. This change does not implement English/Korean resolution or claim U24 acceptance.

Regression covers ID identity, explicit-hint phone output distinct from displayed lyric, stable musical order from unsorted storage, exact scalar offsets, unsupported-character warning ownership, whole-project preservation, cancellation, invalid field/hint rejection, independent ID search despite pronunciation failure, 10000-note ID search and explicit derived-capacity rejection. Verification is recorded in the execution ledger; changes remain local/uncommitted.

## Reviewed replacement API

### 2026-09-08 distribution review-before-apply integration

`PianoRollModel::planLyricDistribution` is now a read-only planner returning exact canonical edits and count diagnostics. The direct distribution API and reviewed path use this single implementation for ordering, language preservation, shared-token admission, bounds and no-op detection. Planning checks cancellation during text scanning and before publishing its result; its report never claims a commit.

`NoteSearchModel::previewLyricDistribution` converts that plan into an immutable review preview with captured selected note IDs. Review publication and Apply verify the selection in addition to project generation/revision/region/source checks. Native batch text submission now closes input and prepares this review asynchronously; it no longer changes lyrics immediately. The shared panel labels the explicit action Apply Distribution and supports dependency outcomes, full Before/After detail, Cancel, Refresh and one canonical undo group. A Refresh explicitly replans using the current selection in the same region; an unchanged stale review cannot apply to new targets.

Mismatched syllable counts become a failed review with Apply disabled. Requested/target counts appear on a separate visible line. Live inspection found that native bridges flatten the panel root into its children, so root-only status text was absent from their exposed tree. The controller now adds a dedicated Review Status and Counts child; regression verifies that child's full mismatch value. That final semantic-child fix is controller-tested, not a completed VoiceOver assessment.

Verification: 12 focused creator-batch cases pass Release/Debug (1.80/1.34 s), and the final rebuilt Release core passes 563 cases (12.02 s). Release native app, strict Debug native-UI builds and diff checks pass. Tests verify pure planning, no pre-Apply mutation/notification, current-selection validation, count diagnostics, cancellation, explicit application, preserved language and exact undo. Existing native batch tests now require the separate Apply action instead of assuming text submission mutates lyrics.

Live exact-Release-app evidence used the historical schema-4 fixture and disposable support root `/tmp/seam-distribution-review.HcAHS0/support`, paused and without physical audio. Selecting its note, pressing Shift+L and submitting `one two` showed `requested=2, target=1` and disabled Apply. After cancelling, submitting `la` opened the `か -> la` review at revision 2 without changing the note. Apply Distribution changed the note at revision 3; Cmd+Z restored `か` at revision 4. The session was discarded and exited 0. The fixture SHA-256 stayed `6fd852a76654c44c5dc9d7251e00bb159a68d39337c93bd4817c799f84a7a6c9`. Increased pronunciation warnings for Latin text in the Japanese fixture were expected; this is workflow/undo evidence, not linguistic or audio quality acceptance.

Remaining: full batch IME/accessibility input qualification, native hint search, normalization/reset workflows, large Japanese/dependency latency and cross-host/release gates. Changes local/uncommitted; U24/Beta GO are not accepted.

### 2026-09-08 distribution language and shared-token semantics

Distribution now accepts an optional language override: omission preserves each target token's language, while an explicit language (including Unspecified) still requests a change. Previously the default Unspecified argument overwrote existing language assignments.

Selected notes are ordered by tick/ID and deduplicated into distinct lyric targets in first-occurrence order. One supplied syllable maps to each distinct token, so a fully selected shared/melisma token produces one canonical edit without detaching its notes or repeatedly addressing the same lyric. Any unselected note sharing a targeted token causes an explicit rejection; distribution does not silently change notes outside the selection. Missing/foreign/duplicate targets reject before mutation.

Reports now distinguish selected note count, distinct lyric targets and changed lyric count. Missing/leftover syllables are calculated against distinct targets. `committed` means the request was accepted; a valid no-op has zero changed lyrics and creates no command/history or native change notification. Explicit language-only changes remain real edits. Native count errors and semantic instructions use the distinct-token policy.

Input is bounded to 10000 region notes/tokens/selected targets and syllables plus 4M Unicode scalars. Invalid scalar values reject. Source note/lyric lookups are indexed instead of repeated linear target searches. The canonical batch lyric command still performs reconciliation and publication.

Regression covers mixed English/Japanese language preservation, explicit reset to Unspecified, shared-token assignment, partial-group rejection, surplus-syllable counts, no-op history/notification neutrality, malformed/oversized input and exact undo/redo. A 10000-note English distribution test verifies all targets, a no-op repeat and one-step exact undo. Its measured operation was 4.24 ms Release / 35.54 ms Debug in focused runs; these are local observations, not universal timing guarantees or large Japanese/audio qualification.

Verification: 11 focused creator-batch cases pass Release/Debug (0.57/1.05 s), and rebuilt Release core passes 561 cases (11.99 s). Release native app and strict Debug native-UI builds passed during this checkpoint; diff checks pass. Native distribution still applies after input submission rather than presenting the full replacement-style review, so reviewed distribution and live IME/host acceptance remain open. Changes local/uncommitted; U24/Beta GO are not accepted.

### 2026-09-08 captured batch-lyric targets

The existing native distribution input used a boolean to remember its mode and read the current selection when committing. A selection change while text entry remained active could therefore retarget the submitted syllables. Batch entry now captures an opaque document-generation context, revision, active region and sorted selected note IDs. Commit validates all of them before calling the existing distribution command. Source metadata changes outside revisioned commands and same-content document replacement are rejected through the captured generation/source checks.

Entry requires a connected native text callback, at most 10000 selected notes, and every selected note belonging to the active region. Region membership is indexed once instead of using a selected-note-by-region linear scan. Active review/drag surfaces reject batch entry. The input painter uses captured target IDs; replacing or cancelling the text workflow releases its context. Non-completion key events stay with native composition; Tab completes distribution without unexpectedly opening another lyric field.

Regression verifies selection retargeting rejection without mutation/history/notification, source-revision drift, same-content document replacement, unrevisioned hint changes, equivalent selection sets in a different insertion order, explicit cancellation, mixed/missing targets, Delete isolation, Tab completion and exact undo. Existing distribution behavior and tests remain in place.

Verification: all 558 Release core cases pass (12.62 s), Release native app and strict Debug native-UI builds pass, and diff checks pass. Live batch IME/error presentation was not requalified. Reviewed distribution preview, shared-lyric/melisma handling, language-preservation policy, normalization/reset and full U24 acceptance still need work. Changes local/uncommitted.

### 2026-09-08 complete lyric detail pages

Lyric result rows are now activatable: click a row or activate its semantic button to open complete Before/After text. The detail surface displays six wrapped lines per page, has separate Previous/Next and Show Before/After actions, and retains the selected lyric identity and page/side in its semantic IDs. Back to Results (or Escape) leaves detail without applying. Apply is only available again on the result list, so a stale list Apply ID cannot be interpreted as a detail action. Cancel closes the entire review; Refresh prepares a new source and retires the old detail.

`wrapUtf8ToDisplayWidth` partitions text into byte ranges without dropping whitespace or newline bytes. It uses the existing display-cluster rules for combining marks, CJK width, regional-indicator pairs and supported emoji join sequences. CRLF is kept together. It rejects invalid UTF-8 and unsupported width/line/text budgets rather than silently returning a partial result. Bounds are 16 MiB input, widths 2–1024 columns, and at most 131072 lines; native detail uses 32 columns. This is a bounded display helper, not a general UAX14 line-breaking or complete grapheme-conformance claim. Detail preparation currently runs synchronously on row activation; worst-case long text and pathological clusters still need responsiveness qualification.

Regression verifies exact byte reconstruction across cluster/newline wrapping and explicit bound errors. The native test reconstructs all pages of a 400-character Before lyric and an 800-character Japanese After lyric, checks each line's display-width bound, rejects a stale Apply ID inside detail, returns to results without mutation, then applies and undoes exactly. The system-font 720×520 raster `/tmp/seam-replacement-detail.XE1Twg/detail.png` was inspected: Japanese lines, side/page label and all controls are visible without overlap. This is rendered-fixture evidence, not live screen-reader or complete script/font qualification.

Verification: all 557 Release core cases pass (12.48 s), Release native application and strict Debug native-UI builds pass, and diff checks pass. Changes remain local/uncommitted. Long rows now have a connected visual inspection path; full IME, native hint search, other bulk workflows and cross-host/release gates remain open. U24/Beta GO are not accepted.

### 2026-09-08 native clipboard actions and composed-character navigation

Source inspection confirmed the custom standalone AppKit text client had Select All handling but no paste/copy/cut responder methods. Active text input now dispatches Cmd+A/V/C/X to the corresponding text commands, and the macOS Edit menu supplies standard responder-chain Cut/Copy/Paste/Select All items. They are disabled by the custom view when no text input is active; they do not fabricate score-level cut/copy behavior.

Paste uses the existing selection/marked-range replacement and Unicode composition publication path, with a 4Mi UTF-16-unit resulting-text cap before insertion. Copy writes the selected substring, and Cut removes it only after successful pasteboard publication. These commands are implemented and compile-verified; clipboard content mutation and live paste/copy/cut behavior were not exercised in this checkpoint.

Left/right navigation now moves across composed-character boundaries instead of decrementing/incrementing a UTF-16 code unit. A selection collapses toward the requested end. This prevents moving inside surrogate pairs or combining sequences before insertion/deletion.

Live evidence: the exact rebuilt Release app opened the historical schema-4 fixture with paused/nonphysical audio and temporary support storage under `/tmp/seam-unicode-input.rIhJIS`. Standard clipboard menu entries were visible and disabled outside text input. Direct automation typing of `か🙂é` still inserted only ASCII `e`; adding paste support did not resolve that path, and no IME bypass was introduced. A bounded over-limit semantic submission seeded `255 x characters + 🙂 + é` without modifying the project. Left then Backspace removed the entire emoji while preserving `é`; Right then Backspace removed the whole combining cluster. Escape cancelled input; the project remained at revision 2 with lyric `か`, and close exited 0 without saving or audio frames. Fixture SHA-256 was unchanged.

Verification: Release native app/core and Debug native-UI/platform builds pass; the existing 555-case core suite passes. Diff checks pass. This qualifies composed-character navigation and safe cancellation, not full IME/direct-Unicode typing, clipboard interoperability or embedded-platform editing. Changes local/uncommitted; U24/Beta GO remain open.

### 2026-09-08 native Find/Replace entry and live workflow

The macOS Edit menu now exposes `Find and Replace Lyrics…`. Its application command opens a bounded, isolated query field followed by a separate replacement field. Enter/Tab submit stages; Escape or the visible Cancel button abandons input. Query must be nonempty, and each field is limited to 256 Unicode scalars. Invalid length/empty-query input is reopened with an inline error rather than silently changing notes. Empty replacement is allowed, but canonical preview admission still rejects resulting empty lyrics.

Input captures the document's opaque generation context, revision and region. The second stage revalidates those before starting asynchronous preparation; text submission opens the review and never applies edits directly. Stage-specific semantic IDs reject a query-field callback after advancing to replacement. Cancellation/reopen serials reject previous interactions. Switching into ordinary lyric/rename/batch composition clears the old replacement target.

Live testing found empty AppKit semantic fields returning nil values; they were not settable through the accessibility client until ordinary text had been entered. Both standalone and embedded AppKit bridges now expose an empty string for editable empty fields. The rebuilt standalone app was retested successfully without priming either input.

Evidence: all 555 rebuilt Release core cases pass (11.57 s); strict Release native app plus Debug native-UI/platform/CLAP builds and diff checks pass. Controller tests cover menu dispatch, both text stages, inline empty/length validation, stale stage and revision rejection, cancellation, empty replacement, one reviewed apply/undo, and replacement of the text workflow by ordinary lyric editing.

Two live exact-Release-app sessions used `tests/fixtures/projects/schema-4-historical-writer.seam` with disposable support storage under `/tmp/seam-find-replace.RIKDwf`, paused/nonphysical audio:

- Menu → query → replacement → asynchronous review showed `か -> あ`. The dependency view showed the existing phone record `unresolved -> unresolved`. Explicit Apply changed the visible note to `あ` and revision 2→3; one Cmd+Z restored `か` at revision 4. The session was discarded without saving.
- After the empty-value fix, initially empty query and replacement fields both accepted Japanese semantic value submissions directly. Cancelling the ready review left `か` and revision 2 unchanged; normal close exited 0.

Both sessions exited 0. The fixture SHA-256 remained `6fd852a76654c44c5dc9d7251e00bb159a68d39337c93bd4817c799f84a7a6c9`. No physical audio frames were emitted and BANK_MISSING remained truthful; these are editing/interaction results, not singing-audio qualification. Ordinary ASCII typing after inline validation was observed. Direct Japanese `typeText` automation did not insert text in the first run, so the verified Japanese route is semantic value submission, not full Japanese IME composition.

Remaining: embedded-host and Windows discovery/live interaction, full language/IME qualification, native search of hints, long-row visual expansion, broader bulk normalization/distribution/reset acceptance, and release gates. The menu/input-to-review path is connected, but U24/Beta GO are not accepted. Changes local/uncommitted.

### Native review panel and owner-thread polling

`NativeEditorController::openReplacementReview(query, replacement)` now starts the background job and opens a native review panel. Standalone and embedded paint paths poll the worker on the editor owner thread; preparation/cancellation keeps requesting frames until the worker retires. No worker callback accesses a view. The embedded runtime's technical overlay is omitted while the panel is visible.

The panel shows preparing/error/stale status, changed note/lyric/replacement counts, page position, and six rows of either full before/after lyric data or canonical dependency outcomes. Previous/Next, Lyrics/Dependencies, Apply, Cancel/Close and Refresh have state-dependent availability. Apply is disabled for empty or stale edit sets and calls the guarded job command only after explicit activation; success notifies document change once and closes the panel. Refresh captures a new source in the original selected region. Cancel closes immediately while background retirement continues.

A dedicated semantic tree exposes the current rows and actions without virtual score notes. Action IDs include interaction, project, revision, selected region, worker state, page and view mode, so callbacks from preparing or prior pages/reviews cannot target the current panel. Full row text remains in the semantic value even when bounded visual text cannot fit. Keyboard/pointer/scroll handlers isolate the panel; the embedded adapter forwards panel actions before technical-lane shortcuts. Global standalone document lifecycle commands remain able to replace/close the document, with generation/revision checks preventing stale review application.

Controller regression prepares a 13-note review asynchronously, verifies disabled/preparing and stale-page actions, blocked background value/lyric/tempo/Delete input, all three lyric pages, dependency view, stale Apply after a tempo edit, explicit refresh, one change notification and exact single undo, then cancellation without mutation. A 720×520 raster at `/tmp/seam-replacement-panel.AnCqxc/review.png` was inspected: six rows and all six buttons are separated and visible. This capture uses software text rendering, not live AppKit/embedded text or VoiceOver evidence.

Remaining: native query/replacement entry and menu/shortcut opener are not connected yet, so this panel is currently invoked through the controller API/test harness. Long-row visual expansion, native worker-error/cancellation interaction, live host qualification and multilingual workflows remain open. U24 is not accepted.

Verification: rebuilt Release core passes all 554 cases in 11.80 s. Release native application and strict Debug native-UI builds pass, as do diff checks. The embedded input-routing changes compile in the Release app/core dependency build, but interactive embedded-host qualification has not been performed. Changes remain local/uncommitted.

### Immutable-snapshot background preparation

`seam/ui/lyric_replacement_job.hpp` adds an owner-thread-controlled background preparation lifecycle. Start captures a validated immutable project and opaque document-generation context through the existing `EditorSession::capturePerformanceJob`, along with the exact editor revision and selected region. Read-only preview/review overloads now accept a project plus revision directly, so a worker does not manufacture a revision-zero editor session or read mutable live state.

One `std::jthread` prepares the private review. A mutex-protected completed-result slot is polled by the owner; the worker never calls a native view callback. Poll validates document generation, revision, selected region and full region source before exposing Ready state. Apply revalidates generation and uses the review's guarded canonical command path. Replacing a document with identical content still expires the result. A successful apply cannot be repeated.

Cancellation requests stop without joining a still-running worker. Poll retires the completed worker and discards cancelled results even if computation finished before the cancellation request. Another preparation is rejected until that worker retires; this avoids an unbounded worker queue. Destruction requests stop/joins before worker-visible members are destroyed. A new request retires an old displayed review even if new input is invalid, preventing accidental application of an earlier query's edits. Worker errors are returned on retirement and retained for UI presentation.

Responsiveness boundary: immutable snapshot validation/copy still runs on the owner thread, and canonical dry-run staging remains non-interruptible within its call. Cancellation is publication-safe, not a hard CPU-time bound. The destructor may wait for that staging to finish. No native polling/input/dialog adapter is connected yet; this is the background service needed by that integration, not a completed visible workflow.

Regression covers successful preparation at nonzero revision, no pre-review apply, rejection of concurrent starts, apply/undo, live edits while a worker uses its snapshot, cancelled result retirement, same-content document replacement before/after review publication, invalid replacement error reporting, invalid new input superseding an old review, and destruction of an active worker without document mutation.

Verification: 11 focused creator-batch cases pass in Release/Debug (0.77/1.07 s); rebuilt Release core passes 553 cases (12.30 s). Strict builds and diff checks pass. UI integration and U24 acceptance remain open; changes local/uncommitted.

### Paged replacement review and canonical dependency outcomes

`seam/ui/lyric_replacement_review.hpp` now provides the shared review model intended for native presentation. Preparation owns an immutable replacement preview plus the original region, validates the region and applies the canonical `BatchSetLyricsCommand` to a private project copy. The live project and undo history are untouched. This dry run uses the same reconciliation logic as publication, rather than estimating losses from lyric differences.

The model exposes full before/after lyric records in six-row pages and separate paged phoneme/unit/seam outcome records. Each dependency records optional before/after unresolved status: absence means the record is absent on that side, not resolved. Outcomes are keyed and ordered by kind and phoneme key. Existing unresolved records are distinguishable from newly unresolved ones; these statuses are not a claim about audio quality or resource coverage. Review admits at most 30000 combined source dependency records in addition to the existing 10000-note/text limits. Invalid duplicate dependency identities fail region validation rather than being silently coalesced by outcome indexing.

Apply checks captured project identity, revision, selected region and the full source-region value before executing the immutable preview. This guards hints/dependencies changed outside ordinary revisioned commands as well as normal stale edits. Applied/cancelled reviews cannot be reused, including after undo; no-op application is history-neutral but consumes the review. Failed/cancelled publication leaves a Ready review retryable. Page indexes are bounded before multiplication, including maximum-size inputs, and do not truncate the stored batch.

Threading boundary: callers preparing reviews asynchronously must provide a privately owned session snapshot, never read a concurrently mutated live editor. Preparation checks cancellation before and after canonical staging and before result publication; the synchronous canonical command itself is not mid-call cancellable. The native worker/dialog/input flow has not yet been connected, so this model does not make the user-facing search/replacement workflow complete.

Verification: all 10 creator-batch cases pass in Release/Debug (1.25/1.35 s). Japanese regression verifies a retained lock and unit become unresolved in the dry run and actual commit, with no pre-apply mutation and exact undo. Other tests cover paging, source drift, wrong selected region, cancellation, terminal states, no-op history and every page of a 10000-note batch. The latter's review preparation measured 4.76 ms Release / 42.23 ms Debug in this local English fixture, including canonical staging. These are observations, not universal latency guarantees or Japanese dense-dependency qualification.

The review-model checkpoint also passes the rebuilt Release core: 552 cases, 11.94 s, with clean diff checks. Native review integration remains open.

### macOS pronunciation menu entry

The standalone macOS Edit menu exposes `Edit Pronunciation Hint…`. A new application command delegates through the standalone configuration to `NativeEditorController::beginSelectedHintEdit`, which is also the Alt+Enter entry point. Exactly one selected note and the existing language/composition/region checks remain mandatory. Japanese, English, and Korean hints use their registered language-specific validators before a non-empty edit can be committed; empty input clears the hint. Invalid text remains in the active editor for correction, and opening the field does not mutate the document. Missing callback returns Unsupported, and the dispatcher propagates callback failures.

Menu failures display a native alert sheet with the returned reason (or a modal alert when no key window exists). Live testing first found that the existing error-recording path alone only logged the empty-selection rejection; the explicit alert fixes that menu usability gap. This does not change all other application menu error handling.

Verification: all 550 Release core cases pass (12.44 s), including command routing, callback failure propagation and unchanged document revision. Release app and Debug native-UI/platform builds pass. The exact rebuilt Release app was launched with disposable support roots under `/tmp/seam-hint-menu.5SXXek`, paused threaded test audio and no user project. Its Edit entry was observed, activated with zero selected notes, and the resulting sheet exposed `Cannot edit pronunciation hint` plus `Select exactly one note to edit its phone hint` and OK. Dismissing it restored the editor; closing exited 0 with revision 0 and no audio frames. No user file was opened or saved.

Scope boundary: this qualifies macOS standalone menu discovery and rejection feedback, not a live successful selected-note hint commit, full VoiceOver/IME use, Windows menu integration or embedded-host menu discovery. Those and native search/replacement preview remain open; U24 and Beta GO are not accepted. Changes local/uncommitted.

### Active hint accessibility and interaction isolation

Active pronunciation-hint input now has a dedicated native semantic tree containing the editable field and a visible Cancel button. The field exposes its current text, identifies empty input as clearing, and sends value changes through the same canonical commit path. The hint field and Cancel button use separate bounds inside the existing auxiliary input area. Score controls and virtual note pages are not exposed by this active tree.

Each opened hint edit gets a monotonic interaction identity in addition to its captured project/note/revision context and current editor revision. Old field/cancel IDs fail after cancellation, reopen, revision change or completion. Oversized UTF-8 input and malformed encoding reject before decoding/publication. Background semantic actions/value changes and pointer clicks cannot retarget active hint input; non-completion keyboard events stay with native text input rather than score shortcuts. Enter/Tab still commit and Escape cancels.

Regression covers the two-node tree and non-overlapping field/cancel bounds, blocked background actions, byte/encoding admission, canceled/reopened identities, hint set/clear, stale revision handling, Delete isolation and pointer cancellation. This is active-field accessibility support, not a permanent inspector/menu opener or live VoiceOver/platform qualification. Native search/replacement review and U24 acceptance remain open.

Verification: all 550 Release core cases pass (12.10 s; capture-enabled repeat 24.51 s). Strict Release native app and Debug native-UI builds pass. A 720×520 raster capture at `/tmp/seam-hint-ui.S2zdq1/hint.png` was inspected: the hint label/value and Cancel button occupy separate, visible bounds. The fixture's note is outside the current viewport, so this capture qualifies the input layout only, not score interaction or live native text rendering. Diff checks pass; changes remain local/uncommitted.

### Native keyboard phone-hint entry

Alt+Enter opens a separate bounded phone-hint field for exactly one selected Japanese/Unspecified-language note. Enter commits, Escape cancels, and Tab commits without navigating to another lyric. The field displays `PHONE HINT (EMPTY = CLEAR)` and starts with the existing hint; displayed lyric text is not used as hint input. Note accessibility descriptions advertise the shortcut, but there is not yet a dedicated semantic hint action or inspector control.

The controller captures project/region/note identity, editor revision and original hint. Commit rechecks that context, including for no-op submissions, before using `SetNoteHintsCommand`. Actual changes notify the authoring runtime through the existing document-changed callback; no-ops add neither history nor notification. Invalid phones and stale edits fail without project mutation. Replacing hint input with lyric, track/region rename or batch lyric input clears the old target.

Empty hint input explicitly opts into clearing in `TextCompositionModel::commit`; ordinary lyric commits still reject empty text by default. Regression initially caught the shared model's unconditional empty-text rejection, which would otherwise have made the advertised clear action fail. Native tests cover shortcut entry, distinct field state, hint/lyric separation, clear/undo/redo, no-op behavior, invalid and unsupported input, revision/source drift, cancel, replacement by lyric input and Tab completion. The composition-model test verifies the default nonempty policy remains intact.

Limits: explicit built-in Japanese phone symbols only; no English/Korean hint editor, kana-reading conversion or multilingual IME qualification. Hint-specific accessibility controls, discoverable inspector/menu integration, visual/live platform qualification and native search/replacement review remain unfinished. These changes do not accept U24 or Beta GO.

Verification: rebuilt Release core passes all 549 cases in 11.99 seconds; Release native application and strict Debug native-UI library builds pass. `git diff --check` passes. No full CTest inventory rerun or live AppKit/embedded hint interaction qualification was performed in this checkpoint. Changes remain local/uncommitted.

### Mixed-expression hint route consolidation

The older `EditPerformanceCommand` now detects actual hint changes and composes them through `SetNoteHintsCommand`, so it no longer bypasses pronunciation reconciliation. Changes involving hints use a private project and command copy; failed validation cannot publish other expressions or partially capture undo state. The no-hint path retains its existing implementation/cost.

Apply stages the ordinary expressions, restores source hint values for guarded reconciliation, and then runs the dedicated hint command. Undo reverses that ordering. Newly changed ownership records receive the combined final revision, with snapshots retained for exact redo. Existing unchanged ownership is not reauthored merely because pronunciation changed.

The combined-expression test now expects reconciled pronunciation state rather than metadata-only hint mutation. A new test combines hint and manual ownership, verifies both revision counters and owner revision, checks exact undo/redo, and confirms an invalid hint leaves project and command history uncaptured. This supersedes the earlier mixed-writer gap; native hint controls and broader UI qualification remain open.

Verification: all 18 performance-command cases pass in Release/Debug (0.60/0.45 s); rebuilt Release core passes 547 cases (12.63 s). Strict builds and diff checks pass.

### Dedicated hint command and dependency reconciliation

`SetNoteHintsCommand` adds a bounded batch of captured before/after optional hints, with note-scoped phrase-audio impact. It rejects missing/repeated/stale targets and validates new Japanese phone sequences before mutation. Languages without a registered hint validator reject explicitly; clearing a legacy hint remains possible without interpreting it as Japanese. Undo restores captured prior metadata, not a newly inferred reading.

The shared staged-note path now treats hint changes as pronunciation-input changes. It reconciles retained phoneme/unit/seam records, advances pronunciation revision and captures exact prior/result dependency state for undo/redo. Displayed lyric tokens are untouched. Tests change `き` to explicit `sh a`, retain the old locked phoneme/unit edits as unresolved, verify the new resolved phones and exact undo/redo, then clear/undo the hint. Invalid, stale, repeated and unsupported-language requests reject without partial mutation.

Verification: all eight creator-batch cases pass in Release/Debug (2.32/1.72 s), and the Release core passes 547 cases (12.74 s). Strict builds and diff checks pass. Native hint UI and consolidation of the older mixed `EditPerformanceCommand` hint-writing route remain unfinished; the dedicated command is the reconciled route introduced here. This checkpoint does not claim every legacy writer or 10000-note Japanese hint batch is qualified.

### Explicit Japanese phone-hint semantics

Inspection found that existing notes could store hints such as `k a` but the Japanese adapter ignored them. The adapter now treats a present hint as an explicit, space-separated Japanese phone sequence, independent of displayed lyric text. The parser accepts only symbols from the built-in mora inventory plus N/cl/pau, at most 256 phones and 4096 bytes. This is phone entry, not unrestricted romaji or automatic kanji-reading inference.

The canonical resolver validates hints before generation and includes their presence/content in its input hash. Aggregate expanded hint bytes share its existing bounded input budget. Unsupported hints fail canonical resolution; direct inspection emits a visible warning rather than silently falling back to the displayed lyric. Removing a hint restores lyric-driven pronunciation. Resolver identity advances to version 2 and phonemizer algorithm revision 2→3 invalidates old cache provenance.

Regression verifies `漢` displayed unchanged while `k a N` resolves to the requested phones, changed input/sequence identities, explicit unsupported-hint failure, inspection warnings, and exact identity restoration after hint removal. The Release core plus separate performance-command/preservation suites pass; command-side persistent dependency reconciliation and native hint controls still need implementation. Merely storing metadata is no longer confused with having a functioning hint control.

Verification: 545 Release core cases pass (12.65 s), as do the 17-case performance-command and five-case preservation suites. Debug phonemizer build and diff checks pass. This is pronunciation/identity evidence, not subjective audio or multilingual hint qualification.

### 10000-note transaction and indexed canonical lookup

A new test previews and applies 10000 distinct English lyric-token edits, then verifies a single undo group, exact project restoration and exact redo. The first Release measurement was preview 3.39 ms / apply 71.63 ms / undo 66.97 ms / redo 67.71 ms. Inspection found repeated linear project-wide lyric lookup for every edit during admission, staging and publication.

BatchSetLyricsCommand now indexes requested lyric IDs once for the live and staged projects. It keeps the staged project validation, dependency reconciliation and swap publication. Targeted IDs occurring in multiple regions reject as ambiguous rather than mutating whichever region a linear scan finds first; unrelated reused lyric IDs remain permitted. Tests explicitly cover both cases. The indexes do not outlive a command call or survive vector mutation.

After the change, the same Release fixture measured preview 1.87 ms / apply 3.80 ms / undo 2.94 ms / redo 3.31 ms. Debug measured 20.45/25.60/22.66/23.59 ms. These are single-run local observations, not timing thresholds or a general latency guarantee. The fixture covers English lyric storage; supported-language phonemization, dense retained dependencies and much larger surrounding projects need separate cost qualification. Cancellation remains before the synchronous command publication, not mid-commit interruption.

All six focused cases pass in Release and Debug. Native review UI is still unimplemented; this does not accept U24 or complete its language/IME requirements.

Rebuilt Release core verification passes 544 cases (12.55 s); diff checks pass.

`previewLyricReplacement` searches displayed lyrics only, then prepares an immutable preview with exact before/after text, preserved languages, distinct lyric edits, matched/changed note counts and replacement-occurrence counts. Shared melisma tokens are changed once while every affected note is counted. Literal replacement is non-overlapping and uses a prefix-table scan; empty replacement is allowed only when the resulting lyric stays nonempty. Replacement text is capped at 256 scalars and aggregate generated output at 4M scalars.

The preview exposes conservative region-wide retained phoneme/unit/seam record counts, not an asserted number of lost or unresolved records. Exact dependent-edit diagnostics still need the native review integration. Hints are not modified. Identical replacements and no-match results produce no command/history entry.

Applying the preview requires its project identity and editor revision, checks each original lyric text/language again, and delegates to one canonical BatchSetLyricsCommand. Existing override reconciliation and session-level rollback remain authoritative. A stale preview cannot be reused after apply or undo. Cancellation is checked during preview and immediately before command execution; the synchronous canonical commit itself is not newly interruptible in this checkpoint.

Regressions verify shared-token deduplication, non-overlapping occurrences, accurate counts, hint preservation, source immutability before apply, one undo/redo group, pre-cancel rejection, stale original values/revisions, no-op history neutrality, empty-output rejection and bounded expansion. These tests do not yet qualify 10000-note commit latency or live native review.

Current verification: all four focused cases pass in Release/Debug (2.60/0.78 s); rebuilt Release core passes 542 cases (12.11 s). Strict builds and diff checks pass.

## Search contract

`NoteSearchModel::search` operates on one identified region and returns project/region/revision metadata plus note hits. The caller chooses either displayed lyric text or pronunciation hints; the model never phonemizes, normalizes, rewrites lyrics or conflates a reading hint with visible text. Matching is literal and case-sensitive over Unicode scalar values. A hit records the first occurrence's scalar offset, original matched text, lyric identity and note identity/start. Multiple notes sharing a melisma lyric produce separate note hits, so later replacement must explicitly deduplicate lyric targets.

Empty or malformed queries reject. Limits are 10000 notes and lyric tokens, 256 query scalars, 4096-byte hints and a cumulative 4M-scalar searchable-text budget. Oversized/invalid sources fail the whole search; no partial-success result is returned. Missing/duplicate identities and invalid notes reject. Invalid lyric Unicode is checked even after an early match.

The query is compiled once into a prefix table and matched linearly rather than repeatedly rescanning long prefixes. Results sort by tick and stable note ID without changing source order. Cancellation is checked before work, while indexing/scanning each note, every 4096 text scalars and before publication. These checks are implemented; current regression verifies pre-cancellation, not a timing-sensitive concurrent-cancellation experiment.

## Verification

Tests verify Japanese text with a supplementary-plane prefix (scalar rather than byte offsets), shared-lyric ordering, independent hint search, no-match success, invalid/empty/oversized queries, source preservation, 10000-note admission, over-limit rejection, repeated-prefix matching, aggregate budget and invalid Unicode after a match. The new `seam_creator_batch_tests` target is also part of `seam_tests` from its introduction.

Strict Debug/Release focused builds and both two-case suites pass (0.42/0.48 s); the rebuilt Release core suite passes 540 cases (12.13 s), and diff checks pass. This read-only API does not authorize or implement a replacement transaction merely because a query found hits.

## Next implementation

Connect the preview to native query/result/review/navigation and IME-safe confirmation. Qualify 10000-note batch latency and cancellation boundaries, and expose meaningful dependent-edit diagnostics. Add separate pronunciation-hint editing without rewriting visible lyrics. Case folding/normalization must be explicit options with defined language behavior, not hidden transformations of saved lyrics.
