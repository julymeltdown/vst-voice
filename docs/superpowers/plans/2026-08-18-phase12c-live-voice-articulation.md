# Phase 12C Live Voicebank Articulation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 기존 단일 모음 `LiveSampleInstrument`를 정확한 Voicebank identity와 articulation inventory를 사용하는 32-voice realtime-safe live singing engine으로 교체한다.

**Architecture:** Voicebank 탐색·WAV decode·articulation unit 선택은 main/worker thread에서 수행하고 immutable `LiveVoicebankResources`로 게시한다. Audio thread는 고정 크기 voice array, segment cursor, expression state와 predecoded PCM만 읽는다. CLAP note events, note expression, MIDI 1 events는 sample offset 순서로 `LiveVoiceEngine`에 전달된다.

**Tech Stack:** C++20, existing `seam-voicebank`, `seam-clap-editor`, official CLAP ABI headers, CMake, existing executable-style tests.

**Spec:** `docs/superpowers/specs/2026-08-18-phase12c-runtime-validation-design.md`

## Global Constraints

- `master` 단일 브랜치만 사용한다.
- 최대 decoded live PCM 256 MiB, 최대 live voice 32, 최대 output sample rate 192 kHz다.
- Callback에서 allocation, file I/O, manifest parsing, lock, shared ownership destruction을 금지한다.
- Missing/untrusted Voicebank는 명시적 silence와 diagnostic을 생성한다.
- CLAP과 MIDI 1 dialect만 광고한다. MIDI 2와 MPE는 Phase 12C에서 광고하지 않는다.
- Steal ramp는 `max(64 samples, 1.5 ms)`다.
- Live path는 offline PSOLA/Spectral/Stretch를 호출하지 않고 bounded sample-loop rendering만 사용한다.

---

## File Structure

### New module

```text
libs/seam-live-voice/
├── include/seam/live_voice/
│   ├── articulation.hpp
│   ├── diagnostics.hpp
│   ├── expression.hpp
│   ├── live_resources.hpp
│   ├── midi1_decoder.hpp
│   ├── realtime_publication.hpp
│   └── voice_engine.hpp
└── src/
    ├── articulation.cpp
    ├── diagnostics.cpp
    ├── live_resources.cpp
    ├── midi1_decoder.cpp
    ├── realtime_publication.cpp
    └── voice_engine.cpp
```

### Modified integration files

```text
CMakeLists.txt
libs/seam-clap-editor/include/seam/clap_editor/editor_runtime.hpp
libs/seam-clap-editor/src/editor_runtime.cpp
libs/seam-clap-editor/src/plugin_entry.cpp
tests/test_phase11_clap_editor.cpp
```

### New focused tests

```text
tests/test_phase12c_live_resources.cpp
tests/test_phase12c_articulation.cpp
tests/test_phase12c_live_engine.cpp
tests/test_phase12c_clap_events.cpp
```

---

### Task 1: Add the `seam-live-voice` module and immutable resource types

**Files:**
- Create: `libs/seam-live-voice/include/seam/live_voice/live_resources.hpp`
- Create: `libs/seam-live-voice/include/seam/live_voice/diagnostics.hpp`
- Create: `libs/seam-live-voice/src/live_resources.cpp`
- Create: `libs/seam-live-voice/src/diagnostics.cpp`
- Create: `tests/test_phase12c_live_resources.cpp`
- Modify: `CMakeLists.txt:410-610`

**Interfaces:**
- Consumes: `seam::voicebank::VoicebankCandidate`, `seam::voicebank::Manifest`, `seam::voicebank::AudioBuffer`, `seam::domain::VoicebankReference`.
- Produces:

```cpp
namespace seam::live_voice {

enum class LiveSegmentRole { Attack, Transition, Sustain, Release, Breath };

struct LiveUnitAudio final {
  std::string unitId;
  LiveSegmentRole role{LiveSegmentRole::Sustain};
  voicebank::UnitKind kind{voicebank::UnitKind::Sustain};
  std::vector<std::string> phones;
  std::int32_t rootMidi{60};
  float gainLinear{1.0F};
  std::uint32_t sourceSampleRate{48000U};
  std::shared_ptr<const std::vector<float>> mono;
  std::uint32_t sourceStart{0U};
  std::uint32_t sourceEnd{0U};
  std::uint32_t stableStart{0U};
  std::uint32_t loopStart{0U};
  std::uint32_t loopEnd{0U};
  std::uint32_t releaseStart{0U};
};

struct LiveVoicebankResources final {
  domain::VoicebankReference identity;
  std::string style;
  std::vector<LiveUnitAudio> units;
  std::size_t decodedBytes{0U};
  std::string diagnosticIdentity;
};

struct LiveResourceBuildOptions final {
  std::string style{"original"};
  std::size_t maximumDecodedBytes{256U * 1024U * 1024U};
};

enum class LiveDiagnosticCode {
  None,
  VoicebankMissing,
  VoicebankUntrusted,
  ArticulationInventoryInvalid,
  LiveResourcePublicationBusy,
  UnsupportedNoteExpression,
  InvalidMidiEvent,
};

[[nodiscard]] std::string_view liveDiagnosticName(
    LiveDiagnosticCode code) noexcept;

class LiveResourceBuilder final {
public:
  [[nodiscard]] core::Result<std::shared_ptr<const LiveVoicebankResources>> build(
      const voicebank::VoicebankCandidate& candidate,
      const LiveResourceBuildOptions& options = {}) const;
};

}  // namespace seam::live_voice
```

- [ ] **Step 1: Write the failing eligibility and resource-limit test**

```cpp
int main() {
  const auto candidate = seam::tests::loadProductionVoicebankCandidate();
  seam::live_voice::LiveResourceBuilder builder;
  auto resources = builder.build(candidate);
  if (!resources) return 1;
  if (resources.value()->identity.id != candidate.manifest.id) return 2;
  if (resources.value()->decodedBytes == 0U ||
      resources.value()->decodedBytes > 256U * 1024U * 1024U) return 3;
  const auto hasSustain = std::ranges::any_of(
      resources.value()->units, [](const auto& unit) {
        return unit.role == seam::live_voice::LiveSegmentRole::Sustain &&
               unit.loopEnd > unit.loopStart;
      });
  if (!hasSustain) return 4;
  return 0;
}
```

- [ ] **Step 2: Add a malformed-marker test**

Copy the fixture manifest into a temporary directory, set `loopEnd <= loopStart`, run `build()`, and assert the error code is `articulation-inventory-invalid` instead of accepting the unit.

- [ ] **Step 3: Register the failing test target**

```cmake
add_library(seam_live_voice
  libs/seam-live-voice/src/live_resources.cpp)
target_include_directories(seam_live_voice PUBLIC
  libs/seam-live-voice/include)
target_link_libraries(seam_live_voice PUBLIC
  seam_voicebank seam_domain seam_core)
seam_apply_compiler_options(seam_live_voice)

add_executable(seam_phase12c_live_resources_tests
  tests/test_phase12c_live_resources.cpp)
target_include_directories(seam_phase12c_live_resources_tests PRIVATE tests)
target_link_libraries(seam_phase12c_live_resources_tests PRIVATE
  seam_live_voice seam_voicebank seam_core)
add_test(NAME seam_phase12c_live_resources_tests
  COMMAND seam_phase12c_live_resources_tests)
```

- [ ] **Step 4: Run the test and verify it fails before implementation**

Run:

```bash
cmake --preset dev
cmake --build --preset dev --target seam_phase12c_live_resources_tests
ctest --test-dir build/dev -R seam_phase12c_live_resources_tests --output-on-failure
```

Expected: build or test failure because `LiveResourceBuilder` does not exist.

- [ ] **Step 5: Implement exact unit decoding and validation**

Implementation rules:

```text
candidate.trust must be TrustedInstalled or DevelopmentFixture
candidate.contentHash must equal identity.contentHash
unit.enabled must be true
style must match requested style
WAV path must resolve inside bankRoot
WAV must be mono-mixable and finite
attack/transition/release markers must be ordered
sustain requires loopStart < loopEnd <= releaseStart/audioEnd
sum(decoded mono sample count * sizeof(float)) <= maximumDecodedBytes
```

Decode each unique audio path once, share it through `shared_ptr<const vector<float>>`, and store unit-local frame windows without copying the PCM per role.

- [ ] **Step 6: Run the focused test**

Expected: `seam_phase12c_live_resources_tests` passes and invalid loop markers return the expected diagnostic.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt libs/seam-live-voice tests/test_phase12c_live_resources.cpp
git commit -m "feat: add immutable live voicebank resources"
```

---

### Task 2: Implement articulation inventory and deterministic plan selection

**Files:**
- Create: `libs/seam-live-voice/include/seam/live_voice/articulation.hpp`
- Create: `libs/seam-live-voice/src/articulation.cpp`
- Create: `tests/test_phase12c_articulation.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `LiveVoicebankResources` from Task 1.
- Produces:

```cpp
struct ArticulationRequest final {
  std::optional<std::string_view> previousVowel;
  std::string_view targetVowel{"a"};
  std::int16_t key{60};
  std::int16_t previousKey{60};
  bool legato{false};
};

struct SegmentSelection final {
  const LiveUnitAudio* unit{nullptr};
  std::uint32_t startFrame{0U};
  std::uint32_t endFrame{0U};
};

struct ArticulationPlan final {
  SegmentSelection attack;
  std::optional<SegmentSelection> transition;
  SegmentSelection sustain;
  std::optional<SegmentSelection> release;
  bool usedTransitionFallback{false};
  std::string diagnostic;
};

class ArticulationPlanner final {
public:
  [[nodiscard]] core::Result<ArticulationPlan> plan(
      const LiveVoicebankResources& resources,
      const ArticulationRequest& request) const;
};
```

- [ ] **Step 1: Write the fallback-order test**

Cover these exact cases:

```text
Attack: Cv/Vcv → Glottal/Special → sustain onset
Transition: Vcv/Vc/Vv/Cc → equal-power fallback
Sustain: Sustain → vowel-bearing Cv/Vcv with valid loop
Release: Release/Vc/Special → no unit, envelope only
```

Assert the same input produces identical unit IDs across 1,000 calls.

- [ ] **Step 2: Run and confirm failure**

Run the focused CTest target. Expected: compilation failure because `ArticulationPlanner` is absent.

- [ ] **Step 3: Implement deterministic ranking**

Ranking tuple:

```cpp
(rolePriority, phoneMatchPenalty, rootPitchDistance,
 -unit.priority, unit.take, unit.id)
```

Never use random selection or spectral-boundary similarity.

- [ ] **Step 4: Implement legato transition selection**

When `request.legato` is true, search exact `previousVowel,targetVowel` transitions first. If none exists, set `usedTransitionFallback = true`, leave `transition` empty, and return a successful plan so the renderer can perform bounded equal-power crossfade.

- [ ] **Step 5: Run tests**

Expected: exact unit selection, fallback flag, missing-sustain rejection, and deterministic 1,000-run checks pass.

- [ ] **Step 6: Commit**

```bash
git add libs/seam-live-voice tests/test_phase12c_articulation.cpp CMakeLists.txt
git commit -m "feat: add deterministic live articulation planning"
```

---

### Task 3: Add expression state and MIDI 1 decoding

**Files:**
- Create: `libs/seam-live-voice/include/seam/live_voice/expression.hpp`
- Create: `libs/seam-live-voice/include/seam/live_voice/midi1_decoder.hpp`
- Create: `libs/seam-live-voice/src/midi1_decoder.cpp`
- Create: `tests/test_phase12c_midi1.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
struct NoteExpressionState final {
  float volume{1.0F};
  float pan{0.0F};
  float tuningSemitones{0.0F};
  float vibrato{0.0F};
  float expression{1.0F};
  float brightness{0.0F};
  float pressure{0.0F};
};

enum class Midi1ActionType {
  None, NoteOn, NoteOff, PitchBend, Pressure, ControlChange,
  SustainPedal, AllNotesOff, AllSoundOff
};

struct Midi1Action final {
  Midi1ActionType type{Midi1ActionType::None};
  std::uint8_t channel{0U};
  std::uint8_t data1{0U};
  std::uint8_t data2{0U};
  float normalized{0.0F};
};

class Midi1Decoder final {
public:
  [[nodiscard]] static Midi1Action decode(
      std::span<const std::uint8_t, 3> bytes) noexcept;
};
```

- [ ] **Step 1: Write table-driven MIDI tests**

Test exact mappings:

```text
0x90 velocity>0 → NoteOn
0x90 velocity=0 → NoteOff
0x80 → NoteOff
0xE0 → PitchBend, centered 8192
0xD0 → Pressure
CC1 → vibrato
CC7 → channel gain
CC10 → pan
CC11 → expression
CC64 → sustain pedal
CC74 → brightness
CC120 → AllSoundOff
CC123 → AllNotesOff
invalid status/data byte → None and invalid counter input
```

- [ ] **Step 2: Run and verify failure**

Expected: missing decoder types.

- [ ] **Step 3: Implement fixed-cost decoding**

Use bit masks only. Do not allocate strings or containers. Clamp MIDI data bytes to `0..127`; reject malformed status values instead of interpreting them.

- [ ] **Step 4: Add expression clamp helpers**

```cpp
void applyVolume(float linear) noexcept;       // 0..4
void applyPan(float pan) noexcept;              // -1..1
void applyTuning(float semitones) noexcept;     // -48..48 defensive
void applyVibrato(float value) noexcept;        // 0..1
void applyExpression(float value) noexcept;     // 0..4
void applyBrightness(float value) noexcept;     // 0..1
void applyPressure(float value) noexcept;       // 0..1
```

- [ ] **Step 5: Run tests and commit**

```bash
git add libs/seam-live-voice tests/test_phase12c_midi1.cpp CMakeLists.txt
git commit -m "feat: add live note expression and MIDI 1 decoding"
```

---

### Task 4: Implement bounded realtime resource publication

**Files:**
- Create: `libs/seam-live-voice/include/seam/live_voice/realtime_publication.hpp`
- Create: `libs/seam-live-voice/src/realtime_publication.cpp`
- Create: `tests/test_phase12c_live_publication.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
class LiveResourcePublication final {
public:
  class ReadHandle final {
  public:
    ReadHandle(ReadHandle&&) noexcept;
    ReadHandle& operator=(ReadHandle&&) noexcept;
    ~ReadHandle();
    [[nodiscard]] const LiveVoicebankResources* get() const noexcept;
  };

  [[nodiscard]] ReadHandle acquire() const noexcept;
  [[nodiscard]] bool publish(
      std::shared_ptr<const LiveVoicebankResources> resources);

private:
  static constexpr std::size_t kSlotCount = 3U;
};
```

- [ ] **Step 1: Write concurrent reader/writer tests**

One writer publishes 10,000 monotonically numbered resource identities while two readers continuously acquire handles and validate that each identity is internally consistent. The test fails on null after initial publication, use-after-free, partial identity, or unbounded publication queue.

- [ ] **Step 2: Run under TSan and verify the test initially fails to build**

```bash
cmake --preset thread-sanitize
cmake --build --preset thread-sanitize --target seam_phase12c_live_publication_tests
ctest --preset thread-sanitize -R seam_phase12c_live_publication_tests --output-on-failure
```

- [ ] **Step 3: Implement three-slot publication**

Use the same reader-counted slot contract as `RealtimePreviewPublication`, but store one `shared_ptr` per non-audio writer slot and expose only the stable raw pointer through `ReadHandle`. Replacing a slot is allowed only when its reader count is zero.

- [ ] **Step 4: Verify TSan and bounded-busy behavior**

When all two non-current slots are busy, `publish()` returns `false` and increments no queue. It must not block.

- [ ] **Step 5: Commit**

```bash
git add libs/seam-live-voice tests/test_phase12c_live_publication.cpp CMakeLists.txt
git commit -m "feat: add bounded live resource publication"
```

---

### Task 5: Implement the 32-voice articulation renderer and de-click stealing

**Files:**
- Create: `libs/seam-live-voice/include/seam/live_voice/voice_engine.hpp`
- Create: `libs/seam-live-voice/src/voice_engine.cpp`
- Create: `tests/test_phase12c_live_engine.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

```cpp
struct LiveEventAddress final {
  std::int32_t noteId{-1};
  std::int16_t port{0};
  std::int16_t channel{0};
  std::int16_t key{60};
};

struct LiveEngineDiagnostics final {
  std::uint64_t noteOnCount{0U};
  std::uint64_t transitionFallbackCount{0U};
  std::uint64_t voiceStealCount{0U};
  std::uint64_t invalidEventCount{0U};
  std::uint64_t resourceUnavailableCount{0U};
};

class LiveVoiceEngine final {
public:
  static constexpr std::size_t kMaximumVoices = 32U;

  void setSampleRate(double sampleRate) noexcept;
  void setResources(const LiveVoicebankResources* resources) noexcept;
  void reset() noexcept;
  void noteOn(LiveEventAddress address, float velocity) noexcept;
  void noteOff(LiveEventAddress address) noexcept;
  void choke(LiveEventAddress address) noexcept;
  void setNoteExpression(LiveEventAddress address,
                         const NoteExpressionState& expression) noexcept;
  void applyMidi1(const Midi1Action& action) noexcept;
  void render(float* const* output, std::uint32_t channels,
              std::uint32_t frames) noexcept;
  [[nodiscard]] LiveEngineDiagnostics diagnostics() const noexcept;
};
```

- [ ] **Step 1: Write voice lifecycle tests**

Test attack → sustain → release transitions, release fallback, sustain pedal, note choke, and zero-velocity Note On. Assert every rendered sample is finite.

- [ ] **Step 2: Write legato tests**

Send overlapping notes on one channel. Assert exact transition unit usage when present; remove that unit and assert `transitionFallbackCount == 1` with finite equal-power crossfade output.

- [ ] **Step 3: Write 33-note stealing test**

Trigger 33 simultaneous notes. Assert:

```text
active voices == 32
voiceStealCount == 1
maximum adjacent-sample jump around steal boundary < 0.25
```

The threshold applies to normalized float PCM and prevents hard discontinuity.

- [ ] **Step 4: Implement fixed-size voice storage**

Each voice stores only POD-like segment cursors, envelope values, expression state, note address, age counter and pointers into immutable resources. No `std::vector`, `std::string`, `shared_ptr`, or allocation is permitted in the voice object.

- [ ] **Step 5: Implement deterministic allocation order**

```text
free voice
→ released voice with smallest envelope
→ oldest voice among the quietest envelope bucket
```

For a steal, render the old voice through a dedicated tail slot for `max(64, ceil(sampleRate * 0.0015))` samples while the incoming voice attacks. The total tail slots are fixed at 4; when all are busy, choose the oldest tail and restart its ramp from its current value.

- [ ] **Step 6: Implement expression rendering**

- `TUNING`: pitch ratio multiplier.
- `VIBRATO`: bounded sinusoidal modulation up to 50 cents.
- `PAN`: equal-power stereo pan.
- `VOLUME × EXPRESSION`: linear gain.
- `BRIGHTNESS`: one-pole high-frequency emphasis with fixed state per voice.
- `PRESSURE`: bounded gain/noise blend using deterministic source noise seeded from note ID.

- [ ] **Step 7: Run Debug, ASan/UBSan and TSan focused tests**

Expected: all focused tests pass, no sanitizer report, finite output, bounded voice count.

- [ ] **Step 8: Commit**

```bash
git add libs/seam-live-voice tests/test_phase12c_live_engine.cpp CMakeLists.txt
git commit -m "feat: implement realtime voicebank articulation engine"
```

---

### Task 6: Replace `LiveSampleInstrument` in `EditorRuntime`

**Files:**
- Modify: `libs/seam-clap-editor/include/seam/clap_editor/editor_runtime.hpp:180-378`
- Modify: `libs/seam-clap-editor/src/editor_runtime.cpp:442-528`
- Modify: `tests/test_phase11_clap_editor.cpp`
- Create: `tests/test_phase12c_editor_live_resources.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `LiveResourceBuilder`, `LiveResourcePublication`, `LiveVoiceEngine`.
- Produces new runtime methods:

```cpp
void refreshLiveResources();
[[nodiscard]] live_voice::LiveResourcePublication::ReadHandle
    acquireLiveResources() const noexcept;
void setLiveNoteExpression(live_voice::LiveEventAddress address,
                           const live_voice::NoteExpressionState& value) noexcept;
void applyLiveMidi1(const live_voice::Midi1Action& action) noexcept;
void renderLive(float* const* output, std::uint32_t channels,
                std::uint32_t frames) noexcept;
[[nodiscard]] live_voice::LiveEngineDiagnostics liveDiagnostics() const noexcept;
```

- [ ] **Step 1: Update the regression test first**

Remove direct construction of `LiveSampleInstrument`. Build exact live resources from `SEAM_SOURCE_PRODUCTION_VOICEBANK`, wait for publication, trigger Note On/Off through `EditorRuntime`, render a stereo block, and assert energy > 1.0 and `resourceUnavailableCount == 0`.

- [ ] **Step 2: Add missing/untrusted Voicebank tests**

Construct an `EditorRuntime` without a resolved exact Voicebank. Assert live rendering writes silence and increments `resourceUnavailableCount`; it must not fall back to `human_vowel_data.hpp`.

- [ ] **Step 3: Run tests and observe current single-vowel behavior failure**

The new diagnostic and resource identity assertions must fail before integration.

- [ ] **Step 4: Remove generated sample dependency from runtime live path**

Keep the public-domain fixture in `assets/` for tests, but stop including `libs/seam-clap-editor/generated/human_vowel_data.hpp` from the live instrument implementation. Delete the generated header only after no target references it.

- [ ] **Step 5: Build resources on Voicebank refresh and project replacement**

On successful exact Voicebank resolution:

```text
build immutable resources on non-audio thread
→ publish to three-slot live publication
→ expose exact ID/version/content hash in diagnostics
```

On missing, mismatch or untrusted resolution, publish no replacement and set an explicit unavailable diagnostic that causes silence.

- [ ] **Step 6: Run regression and new tests**

Expected: Phase 11 and 12A/12B preview tests remain green; live path now depends on resolved Voicebank content.

- [ ] **Step 7: Commit**

```bash
git add libs/seam-clap-editor tests CMakeLists.txt
git commit -m "feat: bind live singing to exact voicebank resources"
```

---

### Task 7: Integrate CLAP note expression and MIDI 1 events

**Files:**
- Modify: `libs/seam-clap-editor/src/plugin_entry.cpp:400-610`
- Modify: `apps/seam-clap-editor-host/main.cpp`
- Create: `tests/test_phase12c_clap_events.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `EditorRuntime` live methods from Task 6.
- Produces: CLAP input port advertising `CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI`.

- [ ] **Step 1: Add failing port-dialect test**

Assert `supported_dialects` contains exactly CLAP and MIDI 1, and `preferred_dialect == CLAP_NOTE_DIALECT_CLAP`.

- [ ] **Step 2: Add sample-offset event test to the dynamic host**

Create a process block containing:

```text
frame 0    CLAP Note On C4
frame 32   CLAP Tuning +1 semitone
frame 64   CLAP Pan right
frame 96   MIDI 1 pitch bend maximum
frame 128  MIDI CC1 vibrato
frame 160  CLAP Note Off
```

Assert the pre-event and post-event output checksums differ at the expected frame ranges and remain finite.

- [ ] **Step 3: Implement CLAP note expression dispatch**

Handle only `CLAP_EVENT_NOTE_EXPRESSION`. Map event addresses using note ID/port/channel/key and ignore unsupported IDs without failing the process block.

- [ ] **Step 4: Implement MIDI event dispatch**

Handle only `CLAP_EVENT_MIDI` on input port 0. Decode its three bytes with `Midi1Decoder`; reject invalid data by incrementing diagnostics and continue rendering.

- [ ] **Step 5: Render live audio blockwise**

Replace per-sample `renderLiveSample()` calls with one fixed-cost call:

```cpp
runtime_->renderLive(liveScratchPointers.data(), output.channel_count,
                     process->frames_count);
```

Use preallocated activation-time scratch buffers sized to `maximumFrames_ × maximumChannels` and add preview PCM after live rendering. Do not allocate inside `pluginProcess()`.

- [ ] **Step 6: Run dynamic host and sanitizer tests**

Expected: CLAP/MIDI events apply at exact offsets, no hidden allocation, no invalid output.

- [ ] **Step 7: Commit**

```bash
git add libs/seam-clap-editor/src/plugin_entry.cpp apps/seam-clap-editor-host tests/test_phase12c_clap_events.cpp CMakeLists.txt
git commit -m "feat: integrate CLAP expression and MIDI live singing"
```

---

### Task 8: Add Phase 12C live-articulation evidence and contracts

**Files:**
- Create: `apps/seam-phase12c-live-demo/main.cpp`
- Create: `scripts/verify_phase12c_live_contracts.py`
- Create: `docs/phase12c/LIVE_ARTICULATION.md`
- Create: `docs/phase12c/ACCEPTANCE.md` with the live-articulation acceptance rows; the Linux validation plan extends this same file.
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: full live engine and CLAP event integration.
- Produces evidence:

```text
phase12c-live-articulation.wav
phase12c-live-articulation-summary.json
phase12c-live-articulation-spectrogram.png
```

- [ ] **Step 1: Implement the deterministic demo sequence**

Sequence:

```text
C4 attack/sustain
legato E4
pitch bend +2 semitones
brightness and pressure sweep
33-note burst to force one steal
release and choke
```

Write 48 kHz stereo PCM16 WAV and JSON diagnostics containing exact Voicebank ID/version/content hash, selected attack/transition/sustain/release unit IDs, fallback count and voice-steal count.

- [ ] **Step 2: Add the source contract script**

The script must fail if:

```text
LiveSampleInstrument remains in public headers
human_vowel_data.hpp is referenced by the CLAP live path
MIDI dialect is not advertised
maximum voices differs from 32
resource byte limit differs from 256 MiB
```

- [ ] **Step 3: Run the demo and validate finite audio**

Use the existing spectrogram evidence generator or `ffmpeg` only for presentation. The functional test must inspect PCM directly and fail on NaN/Inf, silence, or frame-count mismatch.

- [ ] **Step 4: Commit**

```bash
git add apps/seam-phase12c-live-demo scripts/verify_phase12c_live_contracts.py docs/phase12c CMakeLists.txt
git commit -m "test: add Phase 12C live articulation evidence"
```
