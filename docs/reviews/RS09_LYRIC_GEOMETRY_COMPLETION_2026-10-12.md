# RS-09: one lyric presentation geometry boundary

Completed on 2026-10-12, Asia/Seoul. Source base `95d093b15fb5e94729c1c7f68c760f527ce497d6`; exact tested source/log hashes are in the [evidence record](RS09_LYRIC_GEOMETRY_COMPLETION_2026-10-12.evidence.json). This supersedes the earlier deferred disposition after demonstrating a repeated coordinate rule.

## Trigger and bounded change

The controller's short-note request translated to `(300,400,28,16)`, while shell paint expanded/centred it to `(300,396,96,24)` and the embedded native field independently imposed `(300,400,80,24)`. With text width 300, paint widened to 326 while native placement remained 80. These are source/formula reproductions of inconsistent rules, not claims of a previously observed live-host failure.

[`singLyricPresentationBounds`](../../libs/seam-native-ui/src/design/sing_shell.cpp) is now the pure resolver from immutable `SingLayout`, controller note anchor, logical text width and controller content offset. It uses the existing clamping/expansion rule and returns empty for invalid/unavailable geometry. Paint and translated native requests use it. Logical points remain separate from native backing pixels.

[`TextInputRequest::presentedLyricBounds`](../../libs/seam-native-ui/include/seam/native_ui/editor_controller.hpp) is an owner-thread read-only presentation view for copied requests. The shell retains the mutable rectangle and updates only geometry after painting composition/scroll. Retirement clears it. Input start/end, selection, focus and undo remain with their existing lifecycle owners; geometry refresh does not restart composition. Existing ClassicSurface requests keep their compatibility path.

The [embedded AppKit adapter](../../libs/seam-clap-editor/src/embedded_view_appkit.mm) uses the converted bounds without its old conflicting minimum. It updates the native field frame and invalidates IME character coordinates while preserving the editor, marked text and selection. Scrolled-off fields receive a zero frame without hiding the responder; resize/hide cancel editing. The [standalone adapter](../../libs/seam-native-ui/src/native_window_appkit.mm) reads the live rectangle for candidate placement and invalidates coordinates after geometry changes.

Semantic note nodes and hit testing intentionally retain musical note bounds and the same EditText action. There is no separate shell lyric-field semantic node to reroute. Enlarging the text editor therefore does not enlarge the musical selection target.

## Verification

- Full `cmake --build build/release -j6` passed after production/header changes. V06's frame pipeline, shell input and CLAP shell CTest entries passed **3/3, 13.43s**.
- Pure and real shell cases cover short/long/edge/narrow/empty bounds, 1×/2× logical geometry, pre-first-paint opening, scroll off/back, resize/workspace retirement, composition selection, no extra begin/end callbacks, focused node, semantic/direct edit equivalence, commit and undo.
- Supplemental Cocoa test source was rebuilt with `cmake --build build/release --target seam_tests -j6`; transitive production dependencies were already current. `seam_tests 'AppKit'` passed **5/5**.
- Real embedded `NSTextView` markedRange, selectedRange, string, first-responder/editor identity and unchanged document revision survive paint/scroll/offscreen/back frame updates. Resize/hide cancel without committing the original lyric. Standalone live candidate rectangles change without resetting marked/selected ranges. Both native windows reported actual backing scale **2**.
- Independent worker source review approved; lead ran all builds/tests. Staged diff, new-header tracked source closure and evidence identities checked before publication.

Native physical 1× execution was not performed; 1×/2× automated logical geometry must not be confused with two physical display configurations. No VoiceOver, human, DAW, Windows or full-project sanitizer acceptance is claimed. Other native adapters and the classic compatibility contract remain. Local logs under ignored `out/evidence/rs09-lyric-geometry-2026-10-12` are engineering retention, not a release archive.
