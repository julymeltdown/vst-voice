# Project SEAM Usable Standalone Product Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: execute this plan with task-by-task TDD, independent review gates, and fresh verification before every completion claim. Checklist items are authoritative and must be checked only after the stated evidence exists.

**Goal:** Convert the current Phase 13B Feature Alpha into a locally usable Apple Silicon standalone application in which a user can create a project, select a legally distributable voicebank, enter and edit notes and lyrics, hear the production sample-concatenative renderer, save and recover the project, and export a verified WAV without using a CLI or a DAW.

**Architecture:** Keep the existing C++20 domain, synthesis, rendering, cache, distribution, native UI, AppKit, and CoreAudio implementations. Extract the authoring logic currently concentrated in `seam-clap-editor` into a shared `seam-authoring-runtime` library, then make both the standalone app and CLAP editor thin adapters over that shared runtime. Do not replace the first-party UI stack with iPlug2, Skia, WebView, or React during this plan.

**Tech Stack:** C++20, CMake 3.25+, Ninja, AppKit, CoreAudio, existing first-party software raster UI, existing system-font CJK renderer, JSON project schema, `.seambank`, OpenSSL 3, current Raw/Classic PSOLA/SpectralClassic/Stretch renderers, CTest, ASan, UBSan, TSan, Python 3 verification scripts.

**Spec:** `docs/PRODUCT_PRINCIPLES.md`, `docs/STATUS.md`, `docs/REMAINING_TASKS.md`, and the Usable Alpha acceptance contract in this document.

## Global Constraints

- Use the `master` branch only; do not create a development branch.
- Preserve the product’s core synthesis model: recorded voice units, deterministic unit selection, editable phoneme boundaries, audible seams, sample pitch-shift artifacts, and editable sustain loops.
- Do not introduce a neural waveform generator or automatic naturalization stage into the core render path.
- Do not let Character 01 assets, display mode, or metadata affect unit selection, PCM cache identity, render output, project audio, or exported WAV files.
- Allow only permissive runtime dependencies already approved by the repository policy; every new dependency requires exact revision, source hash, license file, notices, and SBOM entries before merge.
- The audio callback must not allocate memory, acquire a mutex, access the filesystem, parse JSON, query SQLite, resolve voicebanks, or perform synthesis.
- The same canonical project, exact voicebank content hash, render ABI, render quality, sample rate, and renderer settings must generate deterministic output on the same platform/build.
- Missing, mismatched, modified, or untrusted voicebanks must produce explicit diagnostics and silence; they must never be replaced silently with another bank.
- The first user-facing target is macOS Apple Silicon. Linux must continue to build and run headless/native smoke tests. Windows source contracts must remain buildable in target CI, but Windows completion is not required for the first usable macOS alpha.
- No Beta, Release Candidate, or General Availability claim is allowed until the mandatory validation documents and evidence matrices permit it.
- Production code changes follow red-green-refactor: write a failing behavior test, run it and confirm the expected failure, implement the smallest correct change, run focused tests, then run the required milestone suite.
- Commit frequently. Each task below ends with an independently reviewable commit.

---

# 1. Current-State Baseline

This plan is based on the Phase 13B repository at commit `be9fb80a00fef3ef21ec447fb53167efd604f39e`.

## 1.1 Existing assets that must be reused

- [x] Canonical `Project`, `VocalTrack`, `VocalRegion`, `Note`, `LyricToken`, phoneme override, unit override, seam override, pitch automation, tempo, meter, and routing models.
- [x] Command-based `EditorSession` with Undo/Redo and recovery-required state.
- [x] Project JSON schema 5 with durable atomic save and migration support.
- [x] Japanese phonemizer and deterministic unit-selection/timing pipeline.
- [x] Raw, Classic PSOLA, SpectralClassic, and Stretch renderers.
- [x] `ProductionRegionRenderer` and `ProductionProjectRenderer`.
- [x] Phrase cache, scheduler, stale-result rejection, multichannel routing, SPSC playback, and native audio adapters.
- [x] Voicebank catalog with ID, version, content hash, trust state, installed receipt, and relink roots.
- [x] Signed `.seambank` package and transactional installer foundation.
- [x] First-party native X11, Win32, and AppKit window implementations.
- [x] CJK text rendering and native IME paths.
- [x] CLAP editor runtime with production preview, direct technical-lane editing, voicebank resolution, and multichannel host output.
- [x] Character 01 optional runtime package and separation from synthesis.

## 1.2 Product-critical gaps confirmed in the current code

- [ ] `apps/seam-editor-native/main.cpp` still creates a hard-coded project through `makeDemoProject()`.
- [ ] `apps/seam-editor-native/main.cpp` still creates a synthetic sine-wave playback source through `makeDemoTimeline()` instead of using `ProductionProjectRenderer`.
- [ ] Standalone New/Open/Save/Save As/Recent/Recovery flows are not wired to a user-facing application service.
- [ ] Standalone voicebank discovery, selection, installation, and relink are not exposed as a complete user flow.
- [ ] Standalone WAV/master/stem export is not exposed as a complete user flow.
- [ ] Track and region creation, deletion, duplication, renaming, and backing-audio import are incomplete as user-facing operations.
- [ ] `libs/seam-clap-editor/src/editor_runtime.cpp` is approximately 1,889 lines and owns project session, voicebank discovery, async rendering, technical editing, painting, input, host timeline, microscope state, character state, and state serialization.
- [ ] The public-domain human fixture and eight-unit production fixture are engineering fixtures, not a voicebank capable of singing arbitrary Japanese lyrics.
- [ ] AppKit and CoreAudio code exists, but an Apple Silicon `.app` has not been built and accepted on the user’s actual Mac.
- [ ] Existing Phase 12C/13A/13B gates do not substitute for a complete standalone authoring workflow.

## 1.3 Product priority ruling

Until the Usable Alpha gate is passed:

- [ ] Do not add another plugin format.
- [ ] Do not add another release-policy phase.
- [ ] Do not rewrite the UI framework.
- [ ] Do not expand Character 01 merchandise or legal tooling beyond fixes required to package the alpha.
- [ ] Do not claim progress from pipeline scaffolding unless it advances the user journey defined below.

---

# 2. Usable Alpha Acceptance Contract

The product reaches **Usable Alpha** only when one person can complete the following on an Apple Silicon Mac without a CLI and without a DAW:

- [ ] Launch `Project SEAM.app` from Finder.
- [ ] Create a new project with name, tempo, sample rate, output channels, and voicebank.
- [ ] Add at least one vocal track and one vocal region.
- [ ] Enter at least 30 seconds of notes and Japanese lyrics.
- [ ] See generated phonemes and selected source units.
- [ ] Move a phoneme boundary.
- [ ] choose a different unit variant or renderer.
- [ ] Add, move, and delete a pitch point.
- [ ] Change at least one seam parameter.
- [ ] Hear the production sample-concatenative output corresponding to the visible project.
- [ ] Play, pause, stop, seek, and loop without stale audio from an earlier edit.
- [ ] Save the project to a user-selected path.
- [ ] Quit and reopen the application.
- [ ] Reopen the project and hear materially identical audio.
- [ ] Recover from one forced termination using autosave.
- [ ] Detect a missing voicebank and relink it without changing the selected bank identity.
- [ ] Export a master WAV.
- [ ] Export at least one vocal stem WAV.
- [ ] Open the exported WAV in an external player and verify duration, channel count, and audible content.
- [ ] Work for 30 minutes with zero audio underruns, zero data-loss defects, and no unbounded memory growth.

## 2.1 Quantitative acceptance targets

- [ ] Cold launch to responsive editor: less than 3 seconds on the target M3 Max machine with the demo bank already indexed.
- [ ] Note edit to audible preview for a two-second phrase: median under 150 ms and p95 under 400 ms at 48 kHz Preview quality.
- [ ] Piano-roll interaction: 60 FPS target with 10,000 notes; no frame over 50 ms during ordinary selection, pan, or zoom.
- [ ] Audio callback: zero dynamic allocations and zero locks in instrumented builds.
- [ ] Audio playback: zero underflow frames in a 30-minute Apple Silicon acceptance session at 48 kHz/128 frames.
- [ ] Project save: under 1 second for a five-minute, four-track project excluding embedded backing media copy.
- [ ] UI stall caused by autosave: under 50 ms; full serialization and durable write run off the UI thread.
- [ ] Export: final render must make continuous progress, support cancellation, and never publish a partial file as the requested destination.
- [ ] Memory: no monotonic growth above 100 MiB over baseline during the 30-minute acceptance session after caches have warmed.

---

# 3. Target Architecture

## 3.1 Shared authoring runtime

Create a new library:

```text
libs/seam-authoring-runtime/
├── include/seam/authoring/
│   ├── authoring_runtime.hpp
│   ├── project_document.hpp
│   ├── project_lifecycle.hpp
│   ├── voicebank_session.hpp
│   ├── render_coordinator.hpp
│   ├── transport_controller.hpp
│   ├── export_service.hpp
│   ├── autosave_service.hpp
│   ├── recent_projects.hpp
│   ├── authoring_events.hpp
│   └── authoring_state.hpp
└── src/
    ├── authoring_runtime.cpp
    ├── project_document.cpp
    ├── project_lifecycle.cpp
    ├── voicebank_session.cpp
    ├── render_coordinator.cpp
    ├── transport_controller.cpp
    ├── export_service.cpp
    ├── autosave_service.cpp
    ├── recent_projects.cpp
    └── authoring_events.cpp
```

Responsibilities:

```text
ProjectDocument
  owns EditorSession, ProjectFactory synchronization, path, dirty state,
  last-saved revision, autosave identity, and document health.

VoicebankSession
  owns catalog roots, scan results, exact per-track resolution, installation,
  selection, relink, coverage diagnostics, and trust diagnostics.

AuthoringRenderCoordinator
  owns immutable render submissions, cancellation, progress, error state,
  stale-while-render publication, preview/final quality, and PCM cache.

TransportController
  owns play/pause/stop/seek/loop and publishes a PlaybackTimeline built from
  the latest accepted production render.

ExportService
  performs final-quality master/stem rendering, atomic destination publication,
  progress, cancellation, and export receipts.

AutosaveService
  periodically snapshots a dirty ProjectDocument off the UI thread and creates
  bounded recovery records without modifying the explicit save path.

AuthoringRuntime
  is a thin facade used by Standalone and CLAP adapters; it must not paint UI or
  know AppKit, X11, Win32, CLAP, or Character 01 assets.
```

## 3.2 Adapter boundaries

```text
Standalone app ───────────────┐
                              ├── seam-authoring-runtime
CLAP editor adapter ──────────┘

seam-authoring-runtime
├── seam_application
├── seam_formats
├── seam_voicebank
├── seam_rendering
├── seam_platform interfaces
└── seam_core

seam-authoring-runtime ─X→ seam_native_ui
seam-authoring-runtime ─X→ AppKit / Win32 / X11
seam-authoring-runtime ─X→ CLAP ABI
seam-authoring-runtime ─X→ Character image files
```

## 3.3 Refactoring rule

- [ ] Extract behavior behind characterization tests before changing behavior.
- [ ] Keep `EditorRuntime` as a compatibility facade until CLAP tests pass against the shared runtime.
- [ ] Delete duplicated logic only after both CLAP and Standalone use the shared implementation.
- [ ] Do not combine the refactor with visual redesign.
- [ ] Split files by responsibility; no new source file may exceed 600 lines without a written exception in an ADR.

---

# 4. Workstream and Dependency Map

```text
U0 Baseline & contracts
  ↓
U1 Shared authoring runtime refactor
  ├───────────────┐
  ↓               ↓
U2 Project        U3 Voicebank workflow
lifecycle          │
  └───────┬───────┘
          ↓
U4 Production render & playback
          ↓
U5 Complete editing workflow
          ↓
U6 Export workflow
          ↓
U8 macOS app packaging
          ↓
U9 End-to-end acceptance

U7 Usable demo voicebank starts on Day 1 and runs in parallel with U1–U6.
```

Critical-path estimate for one full-time C++ engineer plus part-time voicebank work:

```text
U0         3–5 working days
U1         15–20 working days
U2         8–12 working days
U3         7–10 working days
U4         10–15 working days
U5         10–15 working days
U6         7–10 working days
U7         20–50 working days, parallel
U8         5–10 working days
U9         5–10 working days
```

Expected elapsed time:

- [ ] Full-time focused development with parallel voicebank work: 12–18 weeks.
- [ ] One developer working evenings/weekends: 6–9 months.
- [ ] External Beta readiness after Usable Alpha: an additional 2–4 months.
- [ ] Commercial GA with Official Voicebank 01, VST3/AU, signing, notarization, installers, and DAW certification: an additional 6–12 months or more.

---

# 5. Milestone U0 — Freeze the Baseline and Correct the Product Record

## Task U0.1: Add a Usable Alpha contract to the repository

**Files**

- Create: `docs/product/USABLE_ALPHA_ACCEPTANCE.md`
- Create: `docs/product/USABLE_ALPHA_ACCEPTANCE_KO.md`
- Modify: `docs/STATUS.md`
- Modify: `docs/STATUS_KO.md`
- Modify: `docs/REMAINING_TASKS.md`
- Modify: `docs/REMAINING_TASKS_KO.md`
- Modify: `README.md`

**Checklist**

- [x] Copy the complete acceptance contract from Section 2 into both language documents.
- [x] State that the current standalone is a demo shell because it uses `makeDemoProject()` and `makeDemoTimeline()`.
- [x] State that Phase 12C, 13A, and 13B engineering gates do not prove standalone usability.
- [x] Change the README title from its outdated phase-specific title to `Project SEAM`.
- [x] Make `docs/product/USABLE_ALPHA_ACCEPTANCE.md` the canonical English product gate.
- [x] Add a JSON mirror at `docs/product/usable-alpha-acceptance.json` with stable requirement IDs `UA-001` through `UA-020`.
- [x] Add a verification script that rejects a `PASS` row without an evidence path and SHA-256.

**Tests to write first**

- [x] Add `scripts/test_usable_alpha_contract.py` that fails when the README does not link the contract.
- [x] Add a test that fails when a `PASS` item lacks evidence.
- [x] Add a test that fails when the gate claims `PASSED` while any mandatory item is not `PASS`.

**Verification**

```bash
python3 scripts/test_usable_alpha_contract.py
python3 scripts/verify_master_branch.py --root .
git diff --check
```

**Commit**

```bash
git add README.md docs/product docs/STATUS* docs/REMAINING_TASKS* scripts/test_usable_alpha_contract.py
git commit -m "docs: define the usable standalone alpha gate"
```

## Task U0.2: Add behavior characterization tests before refactoring

**Files**

- Create: `tests/test_authoring_characterization.cpp`
- Modify: `CMakeLists.txt`
- Read: `libs/seam-clap-editor/src/editor_runtime.cpp`
- Read: `apps/seam-editor-native/main.cpp`

**Checklist**

- [x] Capture default-project construction behavior used by CLAP.
- [x] Capture voicebank scan and exact-resolution behavior.
- [x] Capture render submission after Note, lyric, phoneme, unit, pitch, seam, mix, routing, and voicebank commands.
- [x] Capture state encoding/decoding parity.
- [x] Capture preview publication rejection for stale revisions.
- [x] Capture that character display changes do not change PCM or render identity.
- [x] Capture the current `EditorRuntime` public interface used by plugin and UI adapters.

**Tests to write first**

- [x] Write the new characterization tests and run them against the unchanged code.
- [x] Each test must assert user-visible behavior, not private member layout.
- [x] Record the current output hashes only for deterministic fixture inputs; do not use broad snapshot files for UI layout.

**Verification**

```bash
cmake --preset dev
cmake --build --preset dev
./build/dev/seam_tests --filter authoring_characterization
ctest --preset dev -R "seam_phase11|seam_phase12a|seam_phase12b|seam_tests" --output-on-failure
```

**Commit**

```bash
git add tests/test_authoring_characterization.cpp CMakeLists.txt
git commit -m "test: characterize authoring runtime behavior"
```

### U0 exit gate

- [x] Product status documents describe the real standalone limitation.
- [x] Characterization tests pass before any extraction begins.
- [x] `master` is clean.

---

# 6. Milestone U1 — Extract the Shared Authoring Runtime

## Task U1.1: Create `ProjectDocument`

**Files**

- Create: `libs/seam-authoring-runtime/include/seam/authoring/project_document.hpp`
- Create: `libs/seam-authoring-runtime/src/project_document.cpp`
- Create: `tests/test_project_document.cpp`
- Modify: `CMakeLists.txt`

**Interface**

```cpp
namespace seam::authoring {

struct DocumentIdentity final {
  std::optional<std::filesystem::path> projectPath;
  std::optional<std::filesystem::path> autosavePath;
  std::uint64_t lastSavedRevision{0};
  bool dirty{false};
};

class ProjectDocument final {
public:
  ProjectDocument(domain::Project project,
                  application::ProjectFactory factory,
                  core::ILogger* logger = nullptr);

  [[nodiscard]] application::EditorSession& session() noexcept;
  [[nodiscard]] const application::EditorSession& session() const noexcept;
  [[nodiscard]] application::ProjectFactory& factory() noexcept;
  [[nodiscard]] const DocumentIdentity& identity() const noexcept;

  [[nodiscard]] core::Result<void> execute(
      std::unique_ptr<application::ICommand> command);
  [[nodiscard]] core::Result<void> undo();
  [[nodiscard]] core::Result<void> redo();
  [[nodiscard]] core::Result<void> replaceProject(domain::Project project);

  void markSaved(std::filesystem::path path) noexcept;
  void markRecovered(std::filesystem::path autosavePath) noexcept;
  [[nodiscard]] bool dirty() const noexcept;
};

}  // namespace seam::authoring
```

**Checklist**

- [x] Write a failing test that a successful command makes the document dirty.
- [x] Write a failing test that `markSaved()` clears dirty state at the current revision.
- [x] Write a failing test that Undo after save makes the document dirty again.
- [x] Write a failing test that replacing a project synchronizes `ProjectFactory` IDs.
- [x] Write a failing test that a failed command does not change dirty state.
- [x] Implement the smallest wrapper over `EditorSession` that passes the tests.
- [x] Keep project path and autosave path out of the canonical Project JSON.

**Verification**

```bash
cmake --build --preset dev --target seam_tests
./build/dev/seam_tests --filter project_document
```

**Commit**

```bash
git add libs/seam-authoring-runtime tests/test_project_document.cpp CMakeLists.txt
git commit -m "refactor: add shared project document state"
```

## Task U1.2: Extract `VoicebankSession`

**Files**

- Create: `libs/seam-authoring-runtime/include/seam/authoring/voicebank_session.hpp`
- Create: `libs/seam-authoring-runtime/src/voicebank_session.cpp`
- Create: `tests/test_authoring_voicebank_session.cpp`
- Modify: `libs/seam-clap-editor/src/editor_runtime.cpp`
- Modify: `libs/seam-clap-editor/include/seam/clap_editor/editor_runtime.hpp`

**Interface**

```cpp
namespace seam::authoring {

struct TrackVoicebankState final {
  domain::TrackId trackId;
  voicebank::VoicebankResolution resolution;
};

class VoicebankSession final {
public:
  explicit VoicebankSession(std::vector<voicebank::VoicebankSearchRoot> roots);

  [[nodiscard]] core::Result<void> refresh();
  [[nodiscard]] core::Result<void> addSearchRoot(
      voicebank::VoicebankSearchRoot root);
  [[nodiscard]] core::Result<void> bindTrack(
      ProjectDocument& document,
      domain::TrackId trackId,
      const voicebank::VoicebankCandidate& candidate);
  [[nodiscard]] std::vector<voicebank::VoicebankCandidate> candidates() const;
  [[nodiscard]] std::vector<TrackVoicebankState> resolveAll(
      const domain::Project& project) const;
  [[nodiscard]] voicebank::VoicebankResolution resolveTrack(
      const domain::Project& project, domain::TrackId trackId) const;
};

}  // namespace seam::authoring
```

**Checklist**

- [x] Write a failing test for exact ID/version/content-hash resolution.
- [x] Write a failing test for missing, version mismatch, content mismatch, and untrusted states.
- [x] Write a failing test that `bindTrack()` uses `SetTrackVoicebankCommand` and participates in Undo.
- [x] Write a failing test that adding the same canonical search root twice is idempotent.
- [x] Write a failing test that no candidate is selected silently when resolution fails.
- [x] Move catalog roots, candidates, track resolutions, and binding logic out of `EditorRuntime`.
- [x] Leave CLAP public methods as delegating facade methods until U1.6.

**Verification**

```bash
cmake --build --preset dev --target seam_tests seam_phase12a_tests
./build/dev/seam_tests --filter authoring_voicebank_session
ctest --preset dev -R "seam_phase12a" --output-on-failure
```

**Commit**

```bash
git add libs/seam-authoring-runtime libs/seam-clap-editor tests/test_authoring_voicebank_session.cpp
git commit -m "refactor: extract shared voicebank session"
```

## Task U1.3: Extract `AuthoringRenderCoordinator`

**Files**

- Create: `libs/seam-authoring-runtime/include/seam/authoring/render_coordinator.hpp`
- Create: `libs/seam-authoring-runtime/src/render_coordinator.cpp`
- Create: `tests/test_authoring_render_coordinator.cpp`
- Modify: `libs/seam-clap-editor/src/editor_runtime.cpp`
- Modify: `libs/seam-clap-editor/include/seam/clap_editor/editor_runtime.hpp`

**Interface**

```cpp
namespace seam::authoring {

enum class RenderState { Idle, Queued, Rendering, Ready, Cancelled, Failed };

struct PublishedProjectAudio final {
  std::uint64_t projectRevision{0};
  rendering::RenderQuality quality{rendering::RenderQuality::Preview};
  rendering::ProjectRenderResult result;
  std::string diagnostic;
};

struct RenderProgress final {
  RenderState state{RenderState::Idle};
  std::uint64_t requestedRevision{0};
  std::uint64_t publishedRevision{0};
  std::size_t completedPhrases{0};
  std::size_t totalPhrases{0};
  double fraction{0.0};
};

class AuthoringRenderCoordinator final {
public:
  explicit AuthoringRenderCoordinator(std::filesystem::path cacheRoot);
  ~AuthoringRenderCoordinator();

  void submit(domain::Project project,
              std::vector<rendering::TrackVoicebankSource> voicebanks,
              domain::TrackId activeTrack,
              domain::RegionId activeRegion,
              std::uint64_t revision,
              std::uint32_t sampleRate,
              rendering::RenderQuality quality);
  void cancel() noexcept;
  [[nodiscard]] RealtimeProjectAudioPublication::ReadHandle acquire() const noexcept;
  [[nodiscard]] RenderProgress progress() const noexcept;
  void setCompletionCallback(std::function<void()> callback);
};

}  // namespace seam::authoring
```

**Checklist**

- [x] Write a failing parity test: shared coordinator PCM equals direct `ProductionProjectRenderer` PCM.
- [x] Write a failing test that a newer revision prevents an older revision from publishing.
- [x] Write a failing test that cancellation produces `Cancelled`, not `Failed`.
- [x] Write a failing test that missing/mismatched voicebanks publish explicit silence and diagnostic state.
- [x] Write a failing test that Preview and Final quality generate distinct cache identities.
- [x] Write a failing test that character display changes do not invalidate audio.
- [x] Reuse the existing content-addressed `PcmCache`; do not add a second cache implementation.
- [x] Move `AsyncPreviewRenderService` behavior into the shared coordinator.
- [x] Keep the existing reader-counted bounded publication contract.

**Verification**

```bash
cmake --build --preset dev --target seam_tests seam_phase12a_tests seam_phase12b_tests
./build/dev/seam_tests --filter authoring_render_coordinator
ctest --preset dev -R "seam_phase12a|seam_phase12b" --output-on-failure
```

**Commit**

```bash
git add libs/seam-authoring-runtime libs/seam-clap-editor tests/test_authoring_render_coordinator.cpp
git commit -m "refactor: extract shared production render coordinator"
```

## Task U1.4: Extract `TechnicalEditController`

**Files**

- Create: `libs/seam-authoring-runtime/include/seam/authoring/technical_edit_controller.hpp`
- Create: `libs/seam-authoring-runtime/src/technical_edit_controller.cpp`
- Create: `tests/test_technical_edit_controller.cpp`
- Modify: `libs/seam-clap-editor/src/editor_runtime.cpp`
- Modify: `libs/seam-clap-editor/include/seam/clap_editor/editor_runtime.hpp`

**Checklist**

- [ ] Move phoneme-boundary commands behind `movePhonemeBoundary()`.
- [ ] Move unit variant and renderer selection behind `selectUnitVariant()`, `cycleUnitVariant()`, and `cycleUnitRenderer()`.
- [ ] Move pitch point add/move/delete/interpolation behavior.
- [ ] Move seam editing behavior.
- [ ] Keep Sample Microscope model construction in inspection/UI code; expose only the selected Unit and immutable sample data from the controller.
- [ ] Write failing tests for each operation, Undo, Redo, invalid key, invalid unit, and renderer fallback diagnostics.
- [ ] Ensure every successful edit increments the document revision exactly once and submits exactly one render request.

**Verification**

```bash
cmake --build --preset dev --target seam_tests seam_phase12b_tests
./build/dev/seam_tests --filter technical_edit_controller
ctest --preset dev -R seam_phase12b --output-on-failure
```

**Commit**

```bash
git add libs/seam-authoring-runtime libs/seam-clap-editor tests/test_technical_edit_controller.cpp
git commit -m "refactor: extract shared technical edit controller"
```

## Task U1.5: Extract `TransportController`

**Files**

- Create: `libs/seam-authoring-runtime/include/seam/authoring/transport_controller.hpp`
- Create: `libs/seam-authoring-runtime/src/transport_controller.cpp`
- Create: `tests/test_transport_controller.cpp`

**Interface**

```cpp
class TransportController final {
public:
  void publishAudio(authoring::RealtimeProjectAudioPublication::ReadHandle audio);
  [[nodiscard]] core::Result<void> play();
  [[nodiscard]] core::Result<void> pause();
  [[nodiscard]] core::Result<void> stop();
  [[nodiscard]] core::Result<void> seek(rendering::SampleFrame frame);
  [[nodiscard]] core::Result<void> setLoop(
      std::optional<rendering::LoopRange> range);
  [[nodiscard]] rendering::PlaybackState state() const noexcept;
};
```

**Checklist**

- [ ] Wrap existing `PlaybackFeederService`, multichannel ring buffer, and callback processor.
- [ ] Write a failing test that Stop resets to project start and clears stale buffered audio through the consumer-owned reset epoch.
- [ ] Write a failing test that Seek cannot replay frames from the previous position.
- [ ] Write a failing test that Loop boundaries are sample accurate within one frame.
- [ ] Write a failing test that publishing a new render switches through a bounded crossfade and never publishes an older revision.
- [ ] Write a failing test that Pause emits silence without queuing a long zero clip.

**Verification**

```bash
cmake --build --preset dev --target seam_tests
./build/dev/seam_tests --filter transport_controller
```

**Commit**

```bash
git add libs/seam-authoring-runtime tests/test_transport_controller.cpp CMakeLists.txt
git commit -m "refactor: add shared authoring transport controller"
```

## Task U1.6: Create the `AuthoringRuntime` facade

**Files**

- Create: `libs/seam-authoring-runtime/include/seam/authoring/authoring_runtime.hpp`
- Create: `libs/seam-authoring-runtime/src/authoring_runtime.cpp`
- Create: `tests/test_authoring_runtime.cpp`

**Interface**

```cpp
struct AuthoringRuntimeConfig final {
  std::filesystem::path cacheRoot;
  std::vector<voicebank::VoicebankSearchRoot> voicebankRoots;
  std::uint32_t previewSampleRate{48000U};
  bool allowDevelopmentVoicebanks{false};
};

class AuthoringRuntime final {
public:
  AuthoringRuntime(ProjectDocument document, AuthoringRuntimeConfig config);

  [[nodiscard]] ProjectDocument& document() noexcept;
  [[nodiscard]] VoicebankSession& voicebanks() noexcept;
  [[nodiscard]] TechnicalEditController& technicalEdits() noexcept;
  [[nodiscard]] AuthoringRenderCoordinator& renderer() noexcept;
  [[nodiscard]] TransportController& transport() noexcept;

  [[nodiscard]] core::Result<void> selectTrack(domain::TrackId id);
  [[nodiscard]] core::Result<void> selectRegion(domain::RegionId id);
  void requestPreview();
  void handleDocumentChanged();
};
```

**Checklist**

- [ ] Write a failing test for a complete Note edit → production render → transport publication cycle.
- [ ] Write a failing test that selecting a different region does not mutate project content.
- [ ] Write a failing test that a track with unresolved voicebank is silent while resolved tracks still render.
- [ ] Write a failing test that one edit produces one document revision and one render submission.
- [ ] Implement the facade without UI painting, native-window types, or CLAP types.

**Verification**

```bash
cmake --build --preset dev --target seam_tests
./build/dev/seam_tests --filter authoring_runtime
```

**Commit**

```bash
git add libs/seam-authoring-runtime tests/test_authoring_runtime.cpp CMakeLists.txt
git commit -m "refactor: add shared authoring runtime facade"
```

## Task U1.7: Convert the CLAP editor into an adapter

**Files**

- Modify: `libs/seam-clap-editor/include/seam/clap_editor/editor_runtime.hpp`
- Split: `libs/seam-clap-editor/src/editor_runtime.cpp`
- Create: `libs/seam-clap-editor/src/editor_runtime_adapter.cpp`
- Create: `libs/seam-clap-editor/src/editor_runtime_paint.cpp`
- Create: `libs/seam-clap-editor/src/editor_runtime_input.cpp`
- Create: `libs/seam-clap-editor/src/editor_runtime_state.cpp`
- Modify: `CMakeLists.txt`

**Checklist**

- [ ] Replace owned project/voicebank/render/technical-edit state with one `authoring::AuthoringRuntime` member.
- [ ] Keep CLAP host timeline mapping and plugin lifecycle in `seam-clap-editor`.
- [ ] Keep painting, hit-testing, text input callbacks, and Character presentation in the adapter.
- [ ] Remove `AsyncPreviewRenderService` after parity tests pass.
- [ ] Remove duplicate voicebank resolution logic after parity tests pass.
- [ ] Keep `LiveSampleInstrument` isolated; do not mix the Phase 12C live-engine work into this milestone.
- [ ] Keep each new `.cpp` under 600 lines.
- [ ] Run the existing dynamic CLAP host test and PCM parity tests.

**Verification**

```bash
cmake --build --preset dev --target seam_clap_editor_plugin seam_clap_editor_host seam_phase12a_tests seam_phase12b_tests
ctest --preset dev -R "seam_phase11|seam_phase12a|seam_phase12b" --output-on-failure
```

**Commit**

```bash
git add libs/seam-clap-editor CMakeLists.txt
git commit -m "refactor: make the CLAP editor a shared-runtime adapter"
```

## Task U1.8: Replace Standalone demo state with `AuthoringRuntime`

**Files**

- Replace: `apps/seam-editor-native/main.cpp`
- Create: `apps/seam-editor-native/native_editor_app.hpp`
- Create: `apps/seam-editor-native/native_editor_app.cpp`
- Create: `tests/test_standalone_authoring_integration.cpp`
- Modify: `CMakeLists.txt`

**Checklist**

- [ ] Delete `makeDemoTimeline()`.
- [ ] Move demo-project creation to a test fixture; production startup must create an empty untitled project or open the requested project path.
- [ ] Construct `AuthoringRuntime` in the standalone composition root.
- [ ] Feed the latest production render to `TransportController` and the physical audio adapter.
- [ ] Bind native pointer, key, IME, paint, and text callbacks to the same shared project session used by CLAP.
- [ ] Write a failing integration test that moving a visible Note changes the production PCM hash.
- [ ] Write a failing integration test that no sine-wave helper is referenced by the standalone target.
- [ ] Add a source-contract script that rejects `makeDemoTimeline` and hard-coded `official.voice.01` in `apps/seam-editor-native`.

**Verification**

```bash
cmake --build --preset dev --target seam_editor_native seam_tests
./build/dev/seam_tests --filter standalone_authoring_integration
python3 scripts/verify_standalone_production_path.py
```

**Commit**

```bash
git add apps/seam-editor-native tests/test_standalone_authoring_integration.cpp scripts/verify_standalone_production_path.py CMakeLists.txt
git commit -m "feat: connect standalone editing to production rendering"
```

### U1 exit gate

- [ ] CLAP and Standalone use `seam-authoring-runtime`.
- [ ] CLAP regression suite passes.
- [ ] Standalone Note edits change actual voicebank PCM.
- [ ] `editor_runtime.cpp` no longer owns business logic and no replacement file exceeds 600 lines.
- [ ] No sine-wave playback helper remains in the production standalone path.

---

# 7. Milestone U2 — Complete the Project Lifecycle

## Task U2.1: Implement New Project

**Files**

- Create: `libs/seam-authoring-runtime/include/seam/authoring/project_lifecycle.hpp`
- Create: `libs/seam-authoring-runtime/src/project_lifecycle.cpp`
- Create: `tests/test_project_lifecycle.cpp`
- Modify: `libs/seam-application/include/seam/application/project_factory.hpp`
- Modify: `libs/seam-application/src/project_factory.cpp`

**Request model**

```cpp
struct NewProjectRequest final {
  std::string name;
  double tempoBpm{120.0};
  std::uint32_t sampleRate{48000U};
  std::uint8_t outputChannels{2U};
  std::optional<voicebank::VoicebankCandidate> initialVoicebank;
};
```

**Checklist**

- [ ] Validate project name as non-empty UTF-8 after trimming.
- [ ] Allow tempo from 20.0 through 400.0 BPM.
- [ ] Allow sample rates 44,100, 48,000, and 96,000 in the first alpha UI.
- [ ] Allow 1, 2, 4, or 8 output channels.
- [ ] Create one vocal track and one empty region spanning 16 bars.
- [ ] Bind the chosen exact voicebank identity when provided.
- [ ] Create canonical routing for the requested channel count.
- [ ] Set Character 01 display to `Minimal`, not `Full`, by default.
- [ ] Write tests for every invalid bound and resulting canonical project.

**Commit**

```bash
git add libs/seam-authoring-runtime libs/seam-application tests/test_project_lifecycle.cpp
git commit -m "feat: add validated new-project workflow"
```

## Task U2.2: Implement Open, Save, and Save As

**Files**

- Modify: `libs/seam-authoring-runtime/include/seam/authoring/project_lifecycle.hpp`
- Modify: `libs/seam-authoring-runtime/src/project_lifecycle.cpp`
- Create: `tests/test_project_file_lifecycle.cpp`

**Checklist**

- [ ] `open(path)` uses `ProjectJsonCodec::load()` and synchronizes IDs.
- [ ] `save(document)` fails with `InvalidState` when no path is assigned.
- [ ] `saveAs(document, path)` writes a temporary file, performs durable atomic replacement, then updates the document identity.
- [ ] Save clears dirty state only after durable write succeeds.
- [ ] A failed save leaves the original file and dirty state intact.
- [ ] Open rejects unsupported future schema and preserves the currently open document.
- [ ] Open verifies every exact voicebank reference after load and records resolution diagnostics without rewriting the project.
- [ ] Add round-trip tests with Unicode project names, Japanese lyrics, routing, all technical overrides, and Character mode.

**Commit**

```bash
git add libs/seam-authoring-runtime tests/test_project_file_lifecycle.cpp
git commit -m "feat: add project open save and save-as services"
```

## Task U2.3: Implement Autosave and Recovery

**Files**

- Create: `libs/seam-authoring-runtime/include/seam/authoring/autosave_service.hpp`
- Create: `libs/seam-authoring-runtime/src/autosave_service.cpp`
- Create: `tests/test_autosave_service.cpp`

**Checklist**

- [ ] Generate a stable autosave ID from project ID and explicit project path when available.
- [ ] Store autosaves outside the project file under the platform application-support directory.
- [ ] Default interval: 60 seconds while dirty.
- [ ] Also request autosave after 25 successful commands if 15 seconds elapsed since the last autosave.
- [ ] Snapshot the Project on the UI thread; serialize and write on a worker thread.
- [ ] Bound autosave generations to the newest 5 per project.
- [ ] Never replace the explicit project path with the autosave path.
- [ ] On startup, list autosaves newer than their explicit saved project.
- [ ] Recovery opens a copy marked dirty and preserves the original file.
- [ ] Write fault-injection tests for write failure, process termination between temporary write and rename, and corrupt autosave.

**Commit**

```bash
git add libs/seam-authoring-runtime tests/test_autosave_service.cpp
git commit -m "feat: add bounded autosave and recovery"
```

## Task U2.4: Implement Recent Projects and Unsaved-Close Policy

**Files**

- Create: `libs/seam-authoring-runtime/include/seam/authoring/recent_projects.hpp`
- Create: `libs/seam-authoring-runtime/src/recent_projects.cpp`
- Create: `tests/test_recent_projects.cpp`

**Checklist**

- [ ] Store at most 10 entries.
- [ ] Canonicalize paths before de-duplication.
- [ ] Record last-opened UTC timestamp and display name.
- [ ] Remove entries whose path no longer exists only after explicit refresh; do not remove them during parsing.
- [ ] Define close choices: `Save`, `Discard`, `Cancel`.
- [ ] A failed Save returns to the open document and does not close it.
- [ ] `Discard` never deletes autosave evidence until successful app shutdown.

**Commit**

```bash
git add libs/seam-authoring-runtime tests/test_recent_projects.cpp
git commit -m "feat: add recent projects and unsaved-close policy"
```

## Task U2.5: Add Native File Dialog and Application Menu Ports

**Files**

- Create: `libs/seam-platform/include/seam/platform/file_dialog.hpp`
- Create: `libs/seam-platform/include/seam/platform/application_menu.hpp`
- Create: `libs/seam-platform/src/file_dialog_appkit.mm`
- Create: `libs/seam-platform/src/file_dialog_win32.cpp`
- Create: `libs/seam-platform/src/file_dialog_unavailable.cpp`
- Create: `libs/seam-platform/src/application_menu_appkit.mm`
- Create: `tests/test_file_dialog_contract.cpp`

**Checklist**

- [ ] Define `OpenProject`, `SaveProject`, `ImportAudio`, `InstallVoicebank`, and `ExportAudio` dialog purposes.
- [ ] AppKit uses `NSOpenPanel` and `NSSavePanel` on the main thread.
- [ ] Win32 uses `IFileOpenDialog` and `IFileSaveDialog`.
- [ ] Linux unsupported backend returns a structured error in headless tests; X11 support may use an injected path for alpha tests.
- [ ] Add native macOS File/Edit/Transport/View/Help menus with Command-N/O/S/Shift-S/E/Q/Z/Shift-Z/Space.
- [ ] Route menu commands through an application command dispatcher, not directly into the window implementation.
- [ ] Write contract tests with a fake dialog service.

**Commit**

```bash
git add libs/seam-platform tests/test_file_dialog_contract.cpp CMakeLists.txt
git commit -m "feat: add native project file-dialog and menu ports"
```

### U2 exit gate

- [ ] New, Open, Save, Save As, Recent, Autosave Recovery, and Unsaved Close are executable without a CLI.
- [ ] No data-loss defect remains in fault-injection tests.
- [ ] The active project path is never stored in canonical Project JSON.

---

# 8. Milestone U3 — Voicebank Browser, Installation, Selection, and Relink

## Task U3.1: Create a Voicebank Browser model

**Files**

- Create: `libs/seam-authoring-runtime/include/seam/authoring/voicebank_browser.hpp`
- Create: `libs/seam-authoring-runtime/src/voicebank_browser.cpp`
- Create: `tests/test_voicebank_browser.cpp`

**Model**

```cpp
struct VoicebankCard final {
  std::string id;
  std::string version;
  std::string displayName;
  std::string language;
  std::vector<std::string> styles;
  std::string contentHash;
  voicebank::VoicebankTrust trust;
  bool installed{false};
  bool selectable{false};
  bool characterAvailable{false};
  std::size_t enabledUnitCount{0};
  std::vector<std::string> diagnostics;
};
```

**Checklist**

- [ ] Derive cards from `VoicebankCandidate`, not directly from files in the UI.
- [ ] Sort trusted installed banks before development fixtures, then display name, version, and content hash.
- [ ] Show ID, version, language, style, unit count, signer/trust, and content hash abbreviation.
- [ ] Mark an untrusted installed bank visible but not selectable unless the alpha is launched with an explicit development override.
- [ ] Keep Character availability informational; it must not affect selectability.

**Commit**

```bash
git add libs/seam-authoring-runtime tests/test_voicebank_browser.cpp
git commit -m "feat: add voicebank browser model"
```

## Task U3.2: Integrate `.seambank` installation

**Files**

- Create: `libs/seam-authoring-runtime/include/seam/authoring/voicebank_installer_service.hpp`
- Create: `libs/seam-authoring-runtime/src/voicebank_installer_service.cpp`
- Create: `tests/test_voicebank_installer_service.cpp`

**Checklist**

- [ ] Wrap `seam_distribution::Installer` behind an authoring service.
- [ ] Require explicit trusted public key selection or a built-in development trust root for the alpha demo bank.
- [ ] Verify package signature, package digest, file hashes, manifest, content hash, and receipt before exposing the bank as installed.
- [ ] Install into the platform application-support voicebank root.
- [ ] Do not overwrite the same ID/version unless the user confirms Replace and the new content hash differs.
- [ ] Roll back on any failure.
- [ ] Refresh the catalog after successful installation.
- [ ] Add tests for tamper, traversal, symlink, untrusted signer, replace, rollback, and receipt mismatch.

**Commit**

```bash
git add libs/seam-authoring-runtime tests/test_voicebank_installer_service.cpp
git commit -m "feat: integrate signed voicebank installation"
```

## Task U3.3: Implement track selection and relink flows

**Files**

- Modify: `libs/seam-authoring-runtime/include/seam/authoring/voicebank_session.hpp`
- Modify: `libs/seam-authoring-runtime/src/voicebank_session.cpp`
- Create: `tests/test_voicebank_relink.cpp`

**Checklist**

- [ ] Select an exact candidate by ID, version, and content hash.
- [ ] Persist selection through `SetTrackVoicebankCommand`.
- [ ] Relink accepts a new search root but never rewrites the requested identity.
- [ ] Relink succeeds only when the exact identity resolves.
- [ ] Show available versions when the version is missing.
- [ ] Show expected and actual content hashes on mismatch.
- [ ] Add a deliberate `Replace Voicebank` action that changes the canonical identity and is Undoable; do not conflate it with Relink.

**Commit**

```bash
git add libs/seam-authoring-runtime tests/test_voicebank_relink.cpp
git commit -m "feat: add exact voicebank selection and relink"
```

## Task U3.4: Add coverage and missing-unit diagnostics

**Files**

- Create: `libs/seam-voicebank/include/seam/voicebank/coverage.hpp`
- Create: `libs/seam-voicebank/src/coverage.cpp`
- Create: `tests/test_voicebank_coverage.cpp`

**Checklist**

- [ ] Compute inventory by language, style, root-pitch layer, unit kind, phone sequence, release, breath, and sustain.
- [ ] Given a Project/Track/Region, list every phoneme span for which no valid candidate exists.
- [ ] Distinguish missing unit, disabled unit, unsupported pitch range, and unsupported style.
- [ ] Expose summary counts to the Voicebank Browser and render-status panel.
- [ ] Rendering must fail the affected phrase with an explicit diagnostic; unaffected phrases and tracks may continue.

**Commit**

```bash
git add libs/seam-voicebank tests/test_voicebank_coverage.cpp CMakeLists.txt
git commit -m "feat: add voicebank coverage diagnostics"
```

### U3 exit gate

- [ ] A user can install, browse, select, replace, and relink a bank from the app.
- [ ] The exact identity is preserved.
- [ ] Missing units are visible before and during rendering.
- [ ] No silent fallback exists.

---

# 9. Milestone U4 — Production Rendering, Playback, and Audio Settings

## Task U4.1: Bind document changes to production render submissions

**Files**

- Modify: `libs/seam-authoring-runtime/src/authoring_runtime.cpp`
- Modify: `libs/seam-authoring-runtime/src/render_coordinator.cpp`
- Create: `tests/test_standalone_render_binding.cpp`

**Checklist**

- [ ] Every successful audio-affecting command schedules a render for the new revision.
- [ ] Selection-only, zoom, pan, character display, and non-audio view changes do not schedule a render.
- [ ] Dirty phrase calculation includes adjacent phrases affected by preutterance, release, and seam overlap.
- [ ] Multiple edits inside a 20 ms debounce window coalesce to the newest revision.
- [ ] Playhead and visible-region phrases have higher priority than background phrases.
- [ ] A render failure preserves the previous audible PCM and displays the failed revision and diagnostic.

**Commit**

```bash
git add libs/seam-authoring-runtime tests/test_standalone_render_binding.cpp
git commit -m "feat: bind standalone edits to production rendering"
```

## Task U4.2: Add render progress, cancellation, and stale-while-render UI state

**Files**

- Create: `libs/seam-native-ui/include/seam/native_ui/render_status_panel.hpp`
- Create: `libs/seam-native-ui/src/render_status_panel.cpp`
- Create: `tests/test_render_status_panel.cpp`

**Checklist**

- [ ] Display Idle, Queued, Rendering, Ready, Cancelled, and Failed.
- [ ] Display completed phrase count, total phrase count, progress fraction, revision, and diagnostic.
- [ ] Provide Cancel for active renders.
- [ ] Display `Playing previous render` while stale audio remains active.
- [ ] Show voicebank missing/mismatch/trust errors as actionable entries.
- [ ] Do not display Character 01 over the progress or diagnostic text.

**Commit**

```bash
git add libs/seam-native-ui tests/test_render_status_panel.cpp CMakeLists.txt
git commit -m "feat: add standalone render status and cancellation UI"
```

## Task U4.3: Complete transport behavior

**Files**

- Modify: `libs/seam-authoring-runtime/src/transport_controller.cpp`
- Modify: `libs/seam-native-ui/src/editor_controller.cpp`
- Create: `tests/test_standalone_transport.cpp`

**Checklist**

- [ ] Space toggles Play/Pause.
- [ ] Stop returns to project start or loop start according to documented preference.
- [ ] Clicking the ruler seeks.
- [ ] Dragging the playhead seeks continuously without blocking the UI.
- [ ] Loop can be created from the current selection and cleared.
- [ ] New render publication maintains the current musical position.
- [ ] Sample-rate changes restart the audio path and remap position correctly.
- [ ] Transport controls are disabled with a clear diagnostic when no renderable voicebank is available.

**Commit**

```bash
git add libs/seam-authoring-runtime libs/seam-native-ui tests/test_standalone_transport.cpp
git commit -m "feat: complete standalone transport controls"
```

## Task U4.4: Add audio-device settings and controlled restart

**Files**

- Extend: `libs/seam-platform/include/seam/platform/audio_device.hpp`
- Create: `libs/seam-platform/include/seam/platform/audio_device_catalog.hpp`
- Create: `libs/seam-platform/src/audio_device_catalog_appkit.mm`
- Create: `libs/seam-platform/src/audio_device_catalog_unavailable.cpp`
- Create: `libs/seam-native-ui/include/seam/native_ui/audio_settings_panel.hpp`
- Create: `tests/test_audio_settings.cpp`

**Checklist**

- [ ] Enumerate CoreAudio output devices on macOS.
- [ ] Show current device, physical status, sample rate, buffer, channels, and xrun count.
- [ ] Allow 44.1/48/96 kHz and 64/128/256/512 frames for the alpha.
- [ ] Stop feeder and device, clear/reset the ring through the existing epoch contract, reopen, prebuffer, and start.
- [ ] Roll back to the previous working configuration if reopen fails.
- [ ] Persist preferences outside Project JSON.
- [ ] Add a deterministic fake device catalog and restart tests.

**Commit**

```bash
git add libs/seam-platform libs/seam-native-ui tests/test_audio_settings.cpp CMakeLists.txt
git commit -m "feat: add standalone audio-device settings"
```

## Task U4.5: Add backing-audio import and playback

**Files**

- Create: `libs/seam-authoring-runtime/include/seam/authoring/media_import_service.hpp`
- Create: `libs/seam-authoring-runtime/src/media_import_service.cpp`
- Create: `tests/test_media_import_service.cpp`
- Modify: `libs/seam-domain/include/seam/domain/project.hpp`
- Modify: `libs/seam-formats/src/project_json.cpp`

**Checklist**

- [ ] Import PCM WAV in formats already supported by `seam_voicebank::readWav`.
- [ ] Copy media into a project-adjacent media directory only when the user selects `Copy into project`; otherwise store a normalized external path.
- [ ] Add an Undoable AudioTrack creation command.
- [ ] Apply start tick, gain, pan, mute, solo, and output route.
- [ ] Detect missing external media and support Relink separately from Replace.
- [ ] Mix backing media through the existing multichannel routing graph.
- [ ] Do not resample in the audio callback.

**Commit**

```bash
git add libs/seam-authoring-runtime libs/seam-domain libs/seam-formats tests/test_media_import_service.cpp
git commit -m "feat: add backing-audio import and playback"
```

## Task U4.6: Enforce realtime safety

**Files**

- Create: `tests/test_standalone_realtime_contract.cpp`
- Create: `tools/realtime-allocation-probe/main.cpp`
- Modify: `CMakeLists.txt`

**Checklist**

- [ ] Instrument `new`, `delete`, `malloc`, `free`, mutex lock attempts, file-open calls, and logging on the callback thread in a dedicated test build.
- [ ] Run 100,000 callback blocks across 64/128/256/512-frame sizes.
- [ ] Test render publication, seek, loop, pause, and device reset epochs during callback execution.
- [ ] Fail on any callback allocation, lock, file I/O, non-finite sample, or output overrun.
- [ ] Store machine-readable results under `docs/product/evidence/` only when the full command exits zero.

**Commit**

```bash
git add tests/test_standalone_realtime_contract.cpp tools/realtime-allocation-probe CMakeLists.txt
git commit -m "test: enforce standalone realtime audio safety"
```

### U4 exit gate

- [ ] Visible project edits change audible production output.
- [ ] Play/Pause/Stop/Seek/Loop operate on that output.
- [ ] The previous valid render remains audible during a new render.
- [ ] Audio settings can be changed without restarting the app.
- [ ] Callback safety tests report zero violations.

---

# 10. Milestone U5 — Complete the Standalone Editing Workflow

## Task U5.1: Add Track and Region commands

**Files**

- Create: `libs/seam-application/include/seam/application/track_commands.hpp`
- Create: `libs/seam-application/src/track_commands.cpp`
- Create: `libs/seam-application/include/seam/application/region_commands.hpp`
- Create: `libs/seam-application/src/region_commands.cpp`
- Create: `tests/test_track_region_commands.cpp`

**Checklist**

- [ ] Add/Rename/Delete/Duplicate Vocal Track.
- [ ] Add/Rename/Delete/Duplicate/Move/Resize Vocal Region.
- [ ] Add/Delete Audio Track.
- [ ] Preserve nested notes, lyrics, phoneme overrides, unit overrides, seam overrides, pitch automation, voicebank reference, mix, routing, and selection through Undo/Redo.
- [ ] Generate new strong IDs during duplication.
- [ ] Reject deletion of the last remaining vocal track only if the UI contract requires one; otherwise support an empty project and cover it with tests.
- [ ] Validate routing after track deletion and repair only references to the deleted track.

**Commit**

```bash
git add libs/seam-application tests/test_track_region_commands.cpp CMakeLists.txt
git commit -m "feat: add undoable track and region commands"
```

## Task U5.2: Add Arrangement and Track controls

**Files**

- Create: `libs/seam-native-ui/include/seam/native_ui/arrangement_panel.hpp`
- Create: `libs/seam-native-ui/src/arrangement_panel.cpp`
- Create: `libs/seam-native-ui/include/seam/native_ui/track_inspector.hpp`
- Create: `libs/seam-native-ui/src/track_inspector.cpp`
- Create: `tests/test_arrangement_panel.cpp`

**Checklist**

- [ ] Track add/delete/rename controls.
- [ ] Track Mute/Solo/Gain/Pan controls.
- [ ] Voicebank selection control.
- [ ] Region add/move/resize/duplicate/delete.
- [ ] Backing-audio import.
- [ ] Selection updates active Track/Region in `AuthoringRuntime`.
- [ ] All destructive actions support Undo and show a confirmation only when media or unsaved work would become unreachable.
- [ ] Keep Piano Roll and technical lanes scoped to the selected region.

**Commit**

```bash
git add libs/seam-native-ui tests/test_arrangement_panel.cpp CMakeLists.txt
git commit -m "feat: add standalone arrangement and track controls"
```

## Task U5.3: Finish Lyrics, Phoneme, Unit, Pitch, and Seam ergonomics

**Files**

- Modify: `libs/seam-native-ui/src/editor_controller.cpp`
- Modify: `libs/seam-native-ui/src/editor_scene.cpp`
- Modify: `libs/seam-editor-ui/src/phoneme_lane_model.cpp`
- Modify: `libs/seam-editor-inspection/src/unit_lane_model.cpp`
- Create: `tests/test_technical_lane_workflow.cpp`

**Checklist**

- [ ] Batch lyric entry distributes whitespace-separated syllables from the first selected Note.
- [ ] Tab/Shift-Tab moves lyric editing to next/previous Note.
- [ ] Return commits; Escape cancels composition.
- [ ] Phoneme boundary drag shows microsecond offset and prevents invalid order.
- [ ] Unit context menu lists exact candidates, root pitch, take, style, renderer, and expected pitch shift.
- [ ] Pitch lane supports add, move, delete, interpolation selection, and Reset Selected.
- [ ] Seam lane supports amount, overlap, phase reset, envelope blend, curve, and Reset Selected.
- [ ] Sample Microscope shows waveform, spectrogram, source markers, pitch marks, and current destination context.
- [ ] Signed Voicebank source markers remain read-only in the authoring app.
- [ ] Every edit has a keyboard-accessible action and Undo/Redo test.

**Commit**

```bash
git add libs/seam-native-ui libs/seam-editor-ui libs/seam-editor-inspection tests/test_technical_lane_workflow.cpp
git commit -m "feat: complete standalone technical editing workflow"
```

## Task U5.4: Add diagnostics and actionable error presentation

**Files**

- Create: `libs/seam-authoring-runtime/include/seam/authoring/diagnostic.hpp`
- Create: `libs/seam-authoring-runtime/src/diagnostic.cpp`
- Create: `libs/seam-native-ui/include/seam/native_ui/diagnostic_panel.hpp`
- Create: `libs/seam-native-ui/src/diagnostic_panel.cpp`
- Create: `tests/test_diagnostic_panel.cpp`

**Checklist**

- [ ] Normalize project, voicebank, render, cache, media, audio-device, save, autosave, and export errors into stable diagnostic codes.
- [ ] Every diagnostic includes severity, short message, detailed context, affected object IDs, and zero or more actions.
- [ ] Supported actions include Relink Voicebank, Install Voicebank, Relink Media, Retry Render, Open Audio Settings, Save As, Open Recovery Folder, and Copy Diagnostic.
- [ ] Prevent duplicate identical diagnostics from flooding the panel.
- [ ] Character 01 warning portrait is optional and never substitutes for the text.

**Commit**

```bash
git add libs/seam-authoring-runtime libs/seam-native-ui tests/test_diagnostic_panel.cpp
git commit -m "feat: add actionable product diagnostics"
```

### U5 exit gate

- [ ] A user can construct a multi-track, multi-region song without editing source JSON.
- [ ] All core technical parameters are editable from the standalone UI.
- [ ] Every failure in the Usable Alpha flow produces an actionable diagnostic.

---

# 11. Milestone U6 — Master and Stem Export

## Task U6.1: Extend WAV output formats

**Files**

- Modify: `libs/seam-voicebank/include/seam/voicebank/wav.hpp`
- Modify: `libs/seam-voicebank/src/wav.cpp`
- Create: `tests/test_wav_export_formats.cpp`

**Checklist**

- [ ] Support interleaved PCM16, PCM24, and IEEE Float32 output.
- [ ] Support 1–8 channels.
- [ ] Validate RIFF sizes and prevent integer overflow.
- [ ] Clamp integer formats and reject non-finite float samples.
- [ ] Round-trip test each bit depth and channel count.
- [ ] Preserve current APIs through delegating overloads until all callers migrate.

**Commit**

```bash
git add libs/seam-voicebank tests/test_wav_export_formats.cpp
git commit -m "feat: add production WAV export formats"
```

## Task U6.2: Implement `ExportService`

**Files**

- Create: `libs/seam-authoring-runtime/include/seam/authoring/export_service.hpp`
- Create: `libs/seam-authoring-runtime/src/export_service.cpp`
- Create: `tests/test_export_service.cpp`

**Interface**

```cpp
enum class ExportBitDepth { Pcm16, Pcm24, Float32 };
enum class ExportScope { EntireProject, LoopRange, SelectionRange };

struct ExportRequest final {
  std::filesystem::path destination;
  ExportScope scope{ExportScope::EntireProject};
  ExportBitDepth bitDepth{ExportBitDepth::Pcm24};
  std::uint32_t sampleRate{48000U};
  bool exportMaster{true};
  bool exportVocalStems{false};
  bool exportAudioStems{false};
  bool normalize{false};
};
```

**Checklist**

- [ ] Use `RenderQuality::Final`.
- [ ] Resolve exact voicebanks before rendering.
- [ ] Render master through canonical routing.
- [ ] Render vocal stems pre-master while retaining each track’s gain/pan and requested output route according to a documented policy.
- [ ] Write to temporary files and atomically publish only after successful render and file flush.
- [ ] Cancellation deletes temporary outputs and leaves an existing destination untouched.
- [ ] Generate an export receipt containing project ID, project revision, voicebank identities, sample rate, bit depth, channels, duration, render ABI, and SHA-256.
- [ ] Do not normalize by default; when enabled, use peak normalization with a documented -1.0 dBFS target and no limiter.

**Commit**

```bash
git add libs/seam-authoring-runtime tests/test_export_service.cpp
git commit -m "feat: add final-quality master and stem export service"
```

## Task U6.3: Add Export UI

**Files**

- Create: `libs/seam-native-ui/include/seam/native_ui/export_dialog.hpp`
- Create: `libs/seam-native-ui/src/export_dialog.cpp`
- Create: `libs/seam-native-ui/include/seam/native_ui/export_progress_panel.hpp`
- Create: `libs/seam-native-ui/src/export_progress_panel.cpp`
- Create: `tests/test_export_dialog.cpp`

**Checklist**

- [ ] Master/stem checkboxes.
- [ ] Entire Project/Loop Range/Selection Range scope.
- [ ] 44.1/48/96 kHz.
- [ ] PCM16/PCM24/Float32.
- [ ] Destination selection through native Save Panel.
- [ ] Preflight summary of missing banks, missing units, destination conflicts, estimated duration, and channel count.
- [ ] Progress and Cancel.
- [ ] Success panel with Reveal in Finder and receipt path.
- [ ] Failure panel with retry and diagnostic copy.

**Commit**

```bash
git add libs/seam-native-ui tests/test_export_dialog.cpp CMakeLists.txt
git commit -m "feat: add standalone export workflow"
```

### U6 exit gate

- [ ] A user can export master and vocal stems without a CLI.
- [ ] Exported file metadata and SHA-256 receipt match actual output.
- [ ] Interrupted export never leaves a partial requested destination.

---

# 12. Milestone U7 — Build a Legally Usable Demo Voicebank

This work starts immediately and runs in parallel with U1–U6. The existing public-domain fixture remains a renderer test asset and cannot satisfy this milestone.

## Task U7.1: Choose the rights path

**Recommended path:** record a consenting voice provider specifically for the demo bank. This is faster and safer than trying to infer redistribution and derivative-voicebank rights from unrelated public recordings.

**Checklist**

- [ ] Use the repository’s voice-provider contract requirements as the minimum rights checklist.
- [ ] Obtain written permission for recording, phoneme segmentation, pitch/time/formant transformation, local distribution with the alpha, user-created commercial audio, and association or non-association with Character 01.
- [ ] State whether the provider’s real name is public or anonymous.
- [ ] State that the demo bank is not Official Voicebank 01 unless every official gate is met.
- [ ] Store signed evidence outside the public repository and store only a redacted evidence hash in release metadata.
- [ ] If an internet source is used instead, require an explicit license that permits commercial use, modification, creation and redistribution of a derived voicebank, and the relevant performer/publicity rights. Reject “royalty-free,” “free download,” and ambiguous Creative Commons descriptions that do not cover the derived voicebank.

## Task U7.2: Define the minimum Japanese alpha inventory

Use a CVVC-oriented inventory because the existing Japanese phonemizer and unit-selection model can represent transition units while keeping the recording burden bounded.

**Required phoneme inventory**

```text
Vowels: a i u e o
Special: N cl pau br
Consonant families: k g s z sh j t d ch ts n h f b p m y r w
```

**Required unit groups**

- [ ] Start CV for every supported consonant-vowel combination.
- [ ] VC transitions from each vowel into every supported consonant family.
- [ ] VV transitions for all 25 ordered vowel pairs.
- [ ] Sustain for `a i u e o`.
- [ ] Release for `a i u e o N`.
- [ ] At least 6 breaths: short/medium/long inhale and exhale.
- [ ] Glottal attack for each vowel.
- [ ] Standalone `N`, `cl`, and pause units.
- [ ] At least two alternate takes for high-frequency consonants `k s t n m r`.

**Pitch-layer algorithm**

After a recorded range test:

```text
comfortableLow  = lowest note held for 4 seconds, repeated three times without strain
comfortableHigh = highest note held for 4 seconds, repeated three times without strain
P_low  = comfortableLow + 3 semitones
P_high = comfortableHigh - 3 semitones
P_mid  = nearest semitone to the midpoint between P_low and P_high
```

- [ ] Record three layers when `P_high - P_low >= 7 semitones`.
- [ ] Otherwise record two layers at `P_low` and `P_high` and document the reduced range.
- [ ] Do not select exact notes before the range test.

## Task U7.3: Generate and version the recording script

**Files**

- Create: `tools/voicebank-script-generator/main.py`
- Create: `docs/voicebank/ALPHA_JAPANESE_CVVC_INVENTORY.json`
- Create: `tests/test_voicebank_script_generator.py`

**Checklist**

- [ ] Generate deterministic prompt IDs, file names, target pitch, unit coverage, pronunciation hints, and retake group.
- [ ] Ensure every required phone sequence appears at least once per required layer.
- [ ] Add coverage tests that fail on duplicates, missing sequences, or unsafe file names.
- [ ] Emit a printable CSV and machine-readable JSON.
- [ ] Pin the script hash in the recording-session metadata.

**Commit**

```bash
git add tools/voicebank-script-generator docs/voicebank/ALPHA_JAPANESE_CVVC_INVENTORY.json tests/test_voicebank_script_generator.py
git commit -m "feat: add deterministic Japanese alpha recording script"
```

## Task U7.4: Record and ingest the bank

**Recording specification**

- [ ] 48 kHz, 24-bit, mono, dry WAV.
- [ ] Fixed microphone, preamp, distance, angle, room, and gain.
- [ ] Peak between -12 and -6 dBFS.
- [ ] No clipping.
- [ ] Record 30 seconds of room tone at the start and end.
- [ ] Record calibration tone or spoken calibration phrase at every session.
- [ ] Maximum 45 minutes per block and minimum 10-minute break.
- [ ] Mark each take Accepted, Needs Review, or Retake immediately.
- [ ] Keep raw recordings immutable; perform editing into a derived working tree.

**Ingestion checklist**

- [ ] Import through Voicebank Studio.
- [ ] Verify sample rate, channel count, bit depth, clipping, DC offset, and silence.
- [ ] Set root pitch from the prompt and compare against F0 analysis.
- [ ] Mark audio offset, consonant end, vowel onset, stable start, loop start/end, release start, and audio end.
- [ ] Review and correct pitch marks for voiced stable regions.
- [ ] Reject a sample rather than hiding an invalid marker with renderer fallback.

## Task U7.5: Validate and package the Demo Voicebank

**Checklist**

- [ ] Validator errors: 0.
- [ ] Unsupported required inventory entries: 0.
- [ ] Duplicate active aliases: 0 unless explicitly prioritized by take.
- [ ] Loop discontinuity warnings: 0 for five core sustain units after manual review.
- [ ] Root-pitch octave errors: 0.
- [ ] All source and derived audio hashes recorded.
- [ ] Package signed with the development trust key used by the alpha app.
- [ ] `official=false` and `contractedSinger` reflect the actual rights basis.
- [ ] Install through the same UI path used by users.
- [ ] Add a 30-second reference project that uses every consonant family and all four renderers.

## Task U7.6: Listening acceptance

Conduct a blind A/B listening review with at least three listeners who understand the target aesthetic.

- [ ] Confirm every lyric is intelligible enough to identify the intended Japanese mora in the reference project.
- [ ] Confirm audible seams are stylistic rather than digital clicks.
- [ ] Confirm Raw, PSOLA, Spectral, and Stretch produce distinct usable options.
- [ ] Confirm no sample contains unauthorized background speech or identifiable private information.
- [ ] Record listener, date, build SHA, bank content hash, project hash, output hashes, and findings.
- [ ] Block Usable Alpha on any Critical pronunciation, clipping, corrupt loop, or rights finding.

### U7 exit gate

- [ ] A legally distributable demo bank can sing the reference project and common Japanese lyrics.
- [ ] The app installs and resolves it as an exact trusted bank.
- [ ] The bank is explicitly not marketed as Official Voicebank 01 unless the official gate is genuinely satisfied.

---

# 13. Milestone U8 — Build the Apple Silicon Standalone Application

## Task U8.1: Create a real macOS `.app` target

**Files**

- Create: `packaging/macos/ProjectSEAM-App-Info.plist.in`
- Create: `packaging/macos/ProjectSEAM.entitlements`
- Modify: `CMakeLists.txt`
- Create: `scripts/package_macos_standalone.sh`

**Checklist**

- [ ] Build `Project SEAM.app/Contents/MacOS/Project SEAM` as arm64 Release.
- [ ] Set bundle ID `com.project-seam.editor`.
- [ ] Set minimum supported macOS version to 13.0 for the first alpha.
- [ ] Include Character 01 runtime resources, notices, demo bank installer package, and default preferences.
- [ ] Do not embed private signing keys or development absolute paths.
- [ ] Place writable projects, cache, autosaves, recent-projects metadata, installed voicebanks, and logs under appropriate user directories, never inside the app bundle.
- [ ] Use ad-hoc signing for the private alpha package.
- [ ] Keep Developer ID/notarization scripts separate and still fail closed without credentials.

**Commit**

```bash
git add packaging/macos scripts/package_macos_standalone.sh CMakeLists.txt
git commit -m "feat: add Apple Silicon standalone app bundle"
```

## Task U8.2: Complete AppKit application lifecycle

**Files**

- Modify: `libs/seam-native-ui/src/native_window_appkit.mm`
- Create: `apps/seam-editor-native/macos_application_delegate.mm`
- Create: `tests/test_macos_source_contract.py`

**Checklist**

- [ ] Application delegate handles open-file events from Finder.
- [ ] App menu routes New/Open/Save/Save As/Export/Quit.
- [ ] Quit invokes the unsaved-close policy.
- [ ] Reopen with no visible window restores or creates a window.
- [ ] Window restoration stores only document path and frame, not project content.
- [ ] App Nap does not suspend active audio playback or export.
- [ ] Native file panels start from the last relevant directory.
- [ ] `NSTextInputClient` candidate rectangle tracks the lyric cell under Retina scaling.

**Commit**

```bash
git add libs/seam-native-ui/src/native_window_appkit.mm apps/seam-editor-native/macos_application_delegate.mm tests/test_macos_source_contract.py
git commit -m "feat: complete AppKit standalone lifecycle"
```

## Task U8.3: Validate CoreAudio and Apple Silicon runtime

**Required real-device matrix**

```text
Sample rates: 44.1, 48, 96 kHz
Buffers:      64, 128, 256, 512 frames
Channels:     1, 2, 4, 8 when the selected device supports them
Devices:      Built-in output and one external/interface device when available
```

**Checklist**

- [ ] Open, start, stop, and reopen each supported matrix entry.
- [ ] Change device while project is open.
- [ ] Unplug the external device and verify a structured failure and fallback choice.
- [ ] Confirm zero callback allocation and lock violations.
- [ ] Confirm transport position survives controlled device restart.
- [ ] Store actual macOS version, hardware model, build SHA, device name, matrix entry, result, and log hash.

## Task U8.4: Verify CJK input, paths, and Retina

**Checklist**

- [ ] Enter and edit Japanese Hiragana, Katakana, Kanji, and Korean Hangul lyrics.
- [ ] Verify composition, candidate window, commit, cancel, next/previous Note, and Undo.
- [ ] Open/save/export paths containing Korean, Japanese, spaces, emoji, and combining marks.
- [ ] Verify 1× logical geometry and 2× Retina backing without hit-test drift.
- [ ] Verify 100%, 125%, 150%, and 200% application scale settings.

## Task U8.5: Produce the private alpha package

**Artifacts**

```text
Project-SEAM-Usable-Alpha-arm64.zip
Project-SEAM-Usable-Alpha-arm64.zip.sha256
THIRD_PARTY_NOTICES.md
SBOM.spdx.json
DemoVoicebank.seambank
DemoVoicebank public key
Usable Alpha test project
Known limitations
```

**Checklist**

- [ ] Build from a clean checkout with network disabled after dependency setup.
- [ ] Verify bundle contains no build directories, source-only fixtures, private keys, or absolute build paths.
- [ ] Ad-hoc sign the app.
- [ ] Verify with `codesign --verify --deep --strict`.
- [ ] Launch from a clean macOS user account.
- [ ] Install the demo bank through the UI.
- [ ] Record package and contained artifact SHA-256 values.

### U8 exit gate

- [ ] The M3 Max machine launches the app from Finder.
- [ ] AppKit, IME, CoreAudio, project dialogs, save/recovery, voicebank install, and export work on that machine.
- [ ] A private alpha ZIP is reproducible and verified.

---

# 14. Milestone U9 — Real-Song End-to-End Acceptance

## Task U9.1: Create the canonical acceptance song

**Project requirements**

- [ ] Minimum 30 seconds, target 45–60 seconds.
- [ ] Minimum two vocal tracks.
- [ ] Minimum three regions.
- [ ] At least one backing-audio track.
- [ ] At least 50 Notes.
- [ ] Japanese lyrics covering every supported consonant family and five vowels.
- [ ] At least two phoneme-boundary edits.
- [ ] At least two unit variant changes.
- [ ] Use all four renderers.
- [ ] At least eight pitch points across Step, Linear, and Smooth interpolation.
- [ ] At least four seam edits with different overlap/phase/blend settings.
- [ ] At least one loop range.
- [ ] Stereo master and two vocal stems.

**Evidence**

- [ ] Project file SHA-256.
- [ ] Exact voicebank ID/version/content hash.
- [ ] App build SHA.
- [ ] Master and stem SHA-256.
- [ ] Screenshot of arrangement, Piano Roll, Phoneme Lane, Unit Lane, Pitch Lane, Seam Inspector, and export completion.

## Task U9.2: Run the complete user journey

- [ ] Launch from Finder.
- [ ] Create a new project.
- [ ] Install or select the demo voicebank.
- [ ] Build the acceptance song without using JSON or CLI tools.
- [ ] Save.
- [ ] Quit normally.
- [ ] Reopen from Recent Projects.
- [ ] Compare production PCM hash before and after reopen.
- [ ] Export master and stems.
- [ ] Open outputs in QuickTime Player or another external player.
- [ ] Verify channel count, duration, and audible content.

## Task U9.3: Run failure and recovery scenarios

- [ ] Force terminate while dirty; recover autosave without overwriting the explicit project.
- [ ] Rename the installed voicebank directory; verify Missing and Relink.
- [ ] Modify one voicebank WAV; verify ContentMismatch and silence.
- [ ] Restore the exact bank and verify rendering resumes.
- [ ] Make the project destination temporarily unwritable; verify Save failure preserves the original.
- [ ] Cancel export halfway; verify no partial destination is published.
- [ ] Unplug or invalidate the active audio device; verify controlled recovery.
- [ ] Corrupt one cache entry; verify quarantine and regeneration.

## Task U9.4: Run 30-minute product soak

The soak must use the real standalone app, real acceptance project, physical CoreAudio output, active UI, and production renderer.

During 30 minutes:

- [ ] Play and loop continuously for at least 10 minutes.
- [ ] Perform at least 100 Note edits.
- [ ] Perform at least 20 phoneme/unit/pitch/seam edits.
- [ ] Save at least 5 times.
- [ ] Trigger at least 3 autosaves.
- [ ] Seek at least 50 times.
- [ ] Open and close Sample Microscope at least 20 times.
- [ ] Change render quality twice.
- [ ] Cancel at least 10 renders.
- [ ] Export once while the project remains open.

Pass conditions:

- [ ] Audio underflow frames: 0.
- [ ] Data-loss defects: 0.
- [ ] Crash/hang: 0.
- [ ] Non-finite PCM: 0.
- [ ] Stale render published as current: 0.
- [ ] Memory growth after warm-up: less than 100 MiB.
- [ ] File descriptor growth: 0 after transient files close.
- [ ] Thread count returns to baseline after export and render cancellation.

## Task U9.5: Burn down defects

Severity policy:

```text
Blocker  Data loss, corrupt project, security boundary failure, no audio, cannot export
Critical Crash, persistent hang, wrong bank audio, severe timing corruption
Major    Broken core edit, unusable IME, repeatable click/underrun, broken relink
Minor    Cosmetic issue or workaround that does not block the acceptance song
```

- [ ] Blocker: 0.
- [ ] Critical: 0.
- [ ] Major: 0 for the canonical acceptance workflow.
- [ ] Every deferred Minor has an issue ID, workaround, and target milestone.

## Task U9.6: Declare Usable Alpha only with evidence

- [ ] Update every `UA-001` through `UA-020` row with `PASS`, evidence path, SHA-256, date, tester, OS, hardware, and build SHA.
- [ ] Run the Usable Alpha gate script.
- [ ] Run Release build, CTest, ASan, UBSan, focused TSan, license audit, master-only policy, and `git fsck --full`.
- [ ] Package the exact tested build, not a later rebuild.
- [ ] Mark the product `USABLE_ALPHA` only when the gate exits zero.

### U9 exit gate

The product may be called **Usable Alpha**. It may not yet be called Beta, Release Candidate, or General Availability.

---

# 15. Post-Usable-Alpha Work

These tasks are intentionally deferred until U9 passes.

## External Beta

- [ ] Replace the alpha voicebank with a substantially complete, rights-cleared Beta bank.
- [ ] Run exact 7,200-second full soak.
- [ ] Execute official `clap-validator` and store the unmodified log.
- [ ] Execute actual Windows and macOS runtime matrices.
- [ ] Build and validate VST3 and AU artifacts.
- [ ] Test REAPER, Bitwig, and Logic Pro as minimum Beta hosts.
- [ ] Add crash reporting, privacy controls, update mechanism, user manual, and support workflow.
- [ ] Sign/notarize installers.

## Commercial GA

- [ ] Complete Official Voicebank 01 contract, recording, retakes, labeling, QA, signed `.seambank`, EULA, and legal approval.
- [ ] Finalize Character 01 public name, trademark clearance, IP assignment, production model, LODs, expressions, animation, and key art.
- [ ] Complete all mandatory DAW, OS, signing, notarization, and installer gates.
- [ ] Resolve every mandatory Phase 12C/13A/13B row.

---

# 16. Continuous Verification Matrix

Run after every task affecting the relevant subsystem.

## Every C++ task

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
git diff --check
```

## Before each milestone commit series is declared complete

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure

cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize --output-on-failure

cmake --preset thread-sanitize
cmake --build --preset thread-sanitize
ctest --preset thread-sanitize --output-on-failure

python3 scripts/verify_master_branch.py --root .
python3 tools/license-auditor/audit.py --root .
git fsck --full
git status --short --branch
```

## Additional milestone tests

```text
U1  CLAP parity, standalone production-path contract, stale revision tests
U2  save fault injection, autosave recovery, recent-projects tests
U3  bank tamper/install/relink/coverage tests
U4  callback allocation probe, transport/reset tests, physical CoreAudio tests
U5  complete edit/Undo/Redo workflow tests
U6  format round-trip, atomic export, cancellation tests
U7  inventory coverage, validator, listening evidence, package verification
U8  Apple Silicon bundle, IME, CoreAudio, file-dialog and package tests
U9  canonical song, failure recovery, 30-minute product soak, gate script
```

---

# 17. Risk Register

## Risk R1: Voicebank rights or data delay

**Likelihood:** High  
**Impact:** Blocks the first meaningful song.

Mitigation checklist:

- [ ] Begin U7 on Day 1.
- [ ] Prefer dedicated consent recording.
- [ ] Do not rely on ambiguous internet audio.
- [ ] Keep engine work and bank work parallel.
- [ ] Use the current fixture only for regression tests.

## Risk R2: Shared-runtime extraction breaks CLAP

**Likelihood:** Medium  
**Impact:** Regresses the most complete authoring surface.

Mitigation checklist:

- [ ] Complete U0 characterization tests first.
- [ ] Keep a delegating CLAP facade until parity passes.
- [ ] Compare direct/CLAP/Standalone PCM for the same project and bank.
- [ ] Split extraction into small commits.

## Risk R3: macOS source exists but real runtime fails

**Likelihood:** Medium  
**Impact:** Blocks the user’s primary machine.

Mitigation checklist:

- [ ] Build and launch a minimal arm64 app during U1, not at the end.
- [ ] Run AppKit, IME, and CoreAudio smoke tests weekly on the M3 Max.
- [ ] Do not mark source presence as runtime PASS.

## Risk R4: UI framework limitations

**Likelihood:** Medium  
**Impact:** Slower polish and accessibility.

Mitigation checklist:

- [ ] Do not rewrite the framework before Usable Alpha.
- [ ] Add only the widgets required by U2–U6.
- [ ] Reassess after the acceptance song is complete.
- [ ] Record an ADR only if profiling or accessibility evidence proves a rewrite necessary.

## Risk R5: render latency makes editing unpleasant

**Likelihood:** Medium  
**Impact:** Usable Alpha fails even when features exist.

Mitigation checklist:

- [ ] Measure edit-to-preview latency from U1 onward.
- [ ] Prioritize playhead/visible phrases.
- [ ] Debounce burst edits by 20 ms.
- [ ] Preserve stale audio during rendering.
- [ ] Profile cache misses and per-renderer cost before optimizing algorithms.

## Risk R6: documentation phase inflation hides missing product flow

**Likelihood:** High based on project history  
**Impact:** Phase count rises while the app remains unusable.

Mitigation checklist:

- [ ] Use milestone names U0–U9 only until Usable Alpha.
- [ ] Every completed milestone must unlock a user-observable capability.
- [ ] Reject new policy or packaging tasks that do not unblock the acceptance contract.
- [ ] Keep the acceptance matrix in the repository root documentation.

---

# 18. Recommended Execution Order and Staffing

## One engineer

```text
Week 1        U0, start U7 rights/recording preparation, macOS smoke build
Weeks 2–5    U1 shared runtime refactor
Weeks 6–7    U2 project lifecycle
Weeks 8–9    U3 voicebank workflow
Weeks 10–12  U4 production playback and audio settings
Weeks 13–15  U5 editing completeness
Weeks 16–17  U6 export
Parallel      U7 recording, labeling, validation, listening
Weeks 18–19  U8 macOS bundle and actual-device matrix
Weeks 20–21  U9 acceptance song and defect burn-down
```

This schedule assumes the demo voicebank is ready by Week 17. If it is not, the acceptance date moves with it.

## Two engineers plus voicebank operator

```text
Engineer A   U1 runtime, U4 rendering/playback, U6 export
Engineer B   U2 lifecycle, U3 browser/install, U5 UI, U8 macOS
Operator     U7 recording, labeling, retakes, QA
Shared       U0 contracts and U9 acceptance
```

Expected elapsed time: approximately 10–14 weeks when work is genuinely parallel.

---

# 19. Commit and Review Strategy

- [ ] Use one commit for each Task listed in this plan.
- [ ] Never combine refactoring and user-visible behavior unless the task explicitly requires both.
- [ ] Require a task-scoped code review after every U1 extraction task and every task touching save, render publication, audio callback, bank trust, or export.
- [ ] Run the milestone gate before starting the next milestone.
- [ ] Keep release-policy and evidence-only commits separate from production-code commits.
- [ ] Do not rewrite previous commit authorship.
- [ ] Keep the repository on `master` only, as explicitly required by the project owner.

---

# 20. Final Definition of Done

Project SEAM is **actually usable** when all of the following are true:

- [ ] The standalone app no longer contains or calls `makeDemoTimeline()`.
- [ ] The visible project is the project that the renderer and audio device use.
- [ ] A legally distributable, intelligible demo voicebank is installed and selectable.
- [ ] New/Open/Save/Save As/Autosave Recovery work through the UI.
- [ ] Track/Region/Note/Lyric/Phoneme/Unit/Pitch/Seam editing works through the UI.
- [ ] Voicebank install/select/relink works through the UI.
- [ ] Play/Pause/Stop/Seek/Loop use production audio.
- [ ] Master and stem export work through the UI.
- [ ] An Apple Silicon `.app` runs on the target M3 Max.
- [ ] The canonical acceptance song survives save, quit, reopen, recovery, relink, and export.
- [ ] All Usable Alpha mandatory items have real evidence and hashes.
- [ ] Blocker, Critical, and core-workflow Major defects are zero.

Only after this checklist passes should development priority return to full soak, VST3/AU, broad DAW certification, signing/notarization, Official Voicebank 01, and Character 01 commercial release work.
