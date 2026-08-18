#pragma once

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/authoring/render_coordinator.hpp"
#include "seam/authoring/voicebank_session.hpp"
#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/clap_editor/host_timeline.hpp"
#include "seam/native_ui/character_presentation.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/project_renderer.hpp"
#include "seam/synthesis/unit_selection.hpp"
#include "seam/ui/phoneme_lane_model.hpp"
#include "seam/ui/sample_microscope_model.hpp"
#include "seam/ui/unit_lane_model.hpp"
#include "seam/voicebank/catalog.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace seam::clap_editor {

enum class PreviewStatus {
  Empty,
  Ready,
  VoicebankMissing,
  VoicebankVersionMismatch,
  VoicebankContentHashMissing,
  VoicebankContentMismatch,
  VoicebankUntrusted,
  Failed,
};

struct RenderedPreview final {
  std::uint32_t sampleRate{48000U};
  std::uint64_t revision{0U};
  PreviewStatus status{PreviewStatus::Empty};
  std::vector<float> stereo;
  std::uint8_t channelCount{2U};
  std::vector<float> interleaved;
  std::string diagnostic;
  std::string voicebankId;
  std::string voicebankVersion;
  std::string voicebankContentHash;
  std::vector<std::string> phraseContentHashes;
  std::vector<synthesis::UnitPlanEntry> unitPlan;
  std::size_t phraseCount{0U};
  std::size_t unitCount{0U};
  std::size_t fallbackCount{0U};
  std::size_t cacheHits{0U};
  std::size_t trackCount{0U};
  std::size_t regionCount{0U};
};

[[nodiscard]] std::string_view previewStatusName(PreviewStatus status) noexcept;


class RealtimePreviewPublication final {
public:
  class ReadHandle final {
  public:
    ReadHandle() = default;
    ReadHandle(const ReadHandle&) = delete;
    ReadHandle& operator=(const ReadHandle&) = delete;
    ReadHandle(ReadHandle&& other) noexcept;
    ReadHandle& operator=(ReadHandle&& other) noexcept;
    ~ReadHandle();

    [[nodiscard]] const RenderedPreview* get() const noexcept { return value_; }
    [[nodiscard]] const RenderedPreview* operator->() const noexcept { return value_; }
    [[nodiscard]] const RenderedPreview& operator*() const noexcept { return *value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return value_ != nullptr; }

  private:
    friend class RealtimePreviewPublication;
    ReadHandle(const RealtimePreviewPublication* owner, std::size_t slot,
               const RenderedPreview* value) noexcept;
    void release() noexcept;
    const RealtimePreviewPublication* owner_{nullptr};
    std::size_t slot_{0U};
    const RenderedPreview* value_{nullptr};
  };

  RealtimePreviewPublication();
  [[nodiscard]] ReadHandle acquire() const noexcept;
  [[nodiscard]] bool publish(RenderedPreview preview);

private:
  static constexpr std::size_t kSlotCount = 3U;
  struct Slot final {
    RenderedPreview preview;
    mutable std::atomic<std::uint32_t> readers{0U};
  };
  std::array<Slot, kSlotCount> slots_{};
  std::atomic<std::size_t> published_{0U};
  std::mutex writerMutex_;
};

struct RenderServiceStats final {
  std::uint64_t submitted{0U};
  std::uint64_t completed{0U};
  std::uint64_t cancelled{0U};
  std::uint64_t stale{0U};
};

class AsyncPreviewRenderService;

struct TrackVoicebankResolution final {
  domain::TrackId trackId;
  voicebank::VoicebankResolution resolution;
};

class AsyncPreviewRenderService final {
public:
  AsyncPreviewRenderService();
  ~AsyncPreviewRenderService();

  AsyncPreviewRenderService(const AsyncPreviewRenderService&) = delete;
  AsyncPreviewRenderService& operator=(const AsyncPreviewRenderService&) = delete;

  void submit(domain::Project project, domain::TrackId activeTrackId,
              domain::RegionId activeRegionId,
              std::vector<TrackVoicebankResolution> resolutions,
              std::uint64_t revision, std::uint32_t sampleRate,
              rendering::RenderQuality quality = rendering::RenderQuality::Preview);
  void setCompletionCallback(std::function<void()> callback);
  [[nodiscard]] RealtimePreviewPublication::ReadHandle acquire() const noexcept {
    return published_.acquire();
  }
  [[nodiscard]] std::shared_ptr<const RenderedPreview> latest() const;
  [[nodiscard]] RenderServiceStats stats() const noexcept;

private:
  struct ResolutionOverride final {
    std::uint64_t revision{0U};
    std::optional<PreviewStatus> status;
    std::string diagnostic;
  };

  void publishFromCoordinator();
  [[nodiscard]] static PreviewStatus statusFor(
      authoring::RenderFailureKind failure) noexcept;

  std::unique_ptr<authoring::AuthoringRenderCoordinator> coordinator_;
  mutable RealtimePreviewPublication published_;
  mutable std::mutex adapterMutex_;
  ResolutionOverride resolutionOverride_;
  mutable std::mutex callbackMutex_;
  std::function<void()> completionCallback_;
};

struct LiveVoice final {
  bool active{false};
  bool releasing{false};
  std::int32_t noteId{-1};
  std::int16_t key{60};
  double samplePosition{0.0};
  double pitchRatio{1.0};
  float envelope{0.0F};
  float velocity{0.0F};
};

class LiveSampleInstrument final {
public:
  static constexpr std::size_t kVoiceCount = 16U;

  void reset() noexcept;
  void setOutputSampleRate(double sampleRate) noexcept;
  void noteOn(std::int32_t noteId, std::int16_t key, float velocity) noexcept;
  void noteOff(std::int32_t noteId, std::int16_t key) noexcept;
  void choke(std::int32_t noteId, std::int16_t key) noexcept;
  [[nodiscard]] float renderSample() noexcept;
  [[nodiscard]] std::size_t activeVoiceCount() const noexcept;

private:
  [[nodiscard]] LiveVoice* allocateVoice() noexcept;
  std::array<LiveVoice, kVoiceCount> voices_{};
  double outputSampleRate_{48000.0};
};

class EditorRuntime final {
public:
  explicit EditorRuntime(
      std::optional<domain::Project> project = std::nullopt,
      const std::filesystem::path& characterPackage =
          std::filesystem::path{"assets/character-01"},
      std::vector<voicebank::VoicebankSearchRoot> voicebankRoots = {});
  ~EditorRuntime() = default;

  EditorRuntime(const EditorRuntime&) = delete;
  EditorRuntime& operator=(const EditorRuntime&) = delete;

  [[nodiscard]] native_ui::NativeEditorController& controller() noexcept {
    return *controller_;
  }
  [[nodiscard]] const native_ui::NativeEditorController& controller() const noexcept {
    return *controller_;
  }

  void setRepaintCallback(std::function<void()> callback);
  void setRenderReadyCallback(std::function<void()> callback);
  void setTextInputCallbacks(
      std::function<void(const native_ui::TextInputRequest&)> begin,
      std::function<void()> end);

  void resize(double logicalWidth, double logicalHeight) noexcept;
  void paint(native_ui::RasterCanvas& canvas) noexcept;
  void pointerDown(const native_ui::PointerEvent& event) noexcept;
  void pointerMove(const native_ui::PointerEvent& event) noexcept;
  void pointerUp(const native_ui::PointerEvent& event) noexcept;
  void scroll(double deltaX, double deltaY, ui::Point anchor,
              native_ui::InputModifiers modifiers) noexcept;
  void keyDown(const native_ui::KeyEvent& event) noexcept;
  void textComposition(std::u32string text,
                       ui::CompositionSelection selection) noexcept;
  void textCommit(std::u32string text) noexcept;
  void textCancel() noexcept;
  void tick() noexcept { requestRepaint(); }
  [[nodiscard]] bool textInputActive() const noexcept {
    return controller_ != nullptr && controller_->textInputActive();
  }

  [[nodiscard]] domain::Project projectCopy() const;
  [[nodiscard]] core::Result<void> replaceProject(domain::Project project);
  [[nodiscard]] domain::RegionId regionId() const noexcept { return regionId_; }
  [[nodiscard]] domain::TrackId trackId() const noexcept { return trackId_; }
  [[nodiscard]] std::uint64_t revision() const noexcept;

  [[nodiscard]] std::vector<domain::TrackId> vocalTrackIds() const;
  [[nodiscard]] std::vector<domain::RegionId> regionIds(
      domain::TrackId trackId) const;
  [[nodiscard]] core::Result<void> selectTrack(domain::TrackId trackId);
  [[nodiscard]] core::Result<void> selectRegion(domain::RegionId regionId);
  [[nodiscard]] core::Result<void> setTrackMix(
      domain::TrackId trackId, float gainDb, float pan, bool muted, bool solo);
  [[nodiscard]] core::Result<void> configureOutputChannels(
      std::uint8_t channels);
  [[nodiscard]] core::Result<void> setHostStartOffset(time::Tick tick);

  [[nodiscard]] core::Result<void> refreshVoicebanks();
  [[nodiscard]] core::Result<void> addVoicebankSearchRoot(
      voicebank::VoicebankSearchRoot root);
  [[nodiscard]] core::Result<void> selectVoicebank(
      std::string_view id, std::string_view version,
      std::optional<std::string_view> contentHash = std::nullopt);
  [[nodiscard]] voicebank::VoicebankResolution voicebankResolution() const;
  [[nodiscard]] std::vector<voicebank::VoicebankCandidate> availableVoicebanks() const;

  void requestRender(std::uint32_t sampleRate);
  void setRenderQuality(rendering::RenderQuality quality);
  [[nodiscard]] rendering::RenderQuality renderQuality() const noexcept;
  [[nodiscard]] std::shared_ptr<const RenderedPreview> renderedPreview() const;
  [[nodiscard]] RealtimePreviewPublication::ReadHandle
  acquireRenderedPreview() const noexcept {
    return renderService_.acquire();
  }
  [[nodiscard]] RenderServiceStats renderStats() const noexcept;

  [[nodiscard]] core::Result<void> movePhonemeBoundary(
      domain::PhonemeKey key, bool startBoundary,
      time::Microseconds offset);
  [[nodiscard]] core::Result<void> selectUnitVariant(
      domain::PhonemeKey key, std::string unitId,
      domain::UnitRendererKind renderer);
  [[nodiscard]] core::Result<void> cycleUnitVariant(domain::PhonemeKey key);
  [[nodiscard]] core::Result<void> cycleUnitRenderer(domain::PhonemeKey key);
  [[nodiscard]] core::Result<void> upsertPitchPoint(
      domain::PitchAutomationPoint point);
  [[nodiscard]] core::Result<void> movePitchPoint(
      time::Tick from, domain::PitchAutomationPoint point);
  [[nodiscard]] core::Result<void> removePitchPoint(time::Tick tick);
  [[nodiscard]] core::Result<void> cyclePitchInterpolation(time::Tick tick);
  [[nodiscard]] core::Result<void> openSampleMicroscope(
      domain::PhonemeKey key);
  void closeSampleMicroscope() noexcept;
  [[nodiscard]] bool sampleMicroscopeOpen() const noexcept;
  [[nodiscard]] const ui::SampleMicroscopeModel* sampleMicroscope() const noexcept;
  [[nodiscard]] std::optional<std::string> selectedUnitId() const;

  void setHostTimelineState(HostTimelineState state) noexcept;
  [[nodiscard]] HostTimelineState hostTimelineState() const noexcept;

  void setLiveSampleRate(double sampleRate) noexcept {
    live_.setOutputSampleRate(sampleRate);
  }
  void noteOn(std::int32_t noteId, std::int16_t key, float velocity) noexcept {
    live_.noteOn(noteId, key, velocity);
  }
  void noteOff(std::int32_t noteId, std::int16_t key) noexcept {
    live_.noteOff(noteId, key);
  }
  void choke(std::int32_t noteId, std::int16_t key) noexcept {
    live_.choke(noteId, key);
  }
  [[nodiscard]] float renderLiveSample() noexcept { return live_.renderSample(); }
  void resetLive() noexcept { live_.reset(); }
  [[nodiscard]] std::size_t activeLiveVoiceCount() const noexcept {
    return live_.activeVoiceCount();
  }

  [[nodiscard]] core::Result<void> setPrimarySeamAmount(float value);
  [[nodiscard]] float primarySeamAmount() const noexcept;

private:
  [[nodiscard]] static domain::Project makeDefaultProject(
      application::ProjectFactory& factory, domain::RegionId& regionId);
  [[nodiscard]] static domain::RegionId firstRegionId(
      const domain::Project& project) noexcept;
  [[nodiscard]] static domain::TrackId firstTrackId(
      const domain::Project& project) noexcept;
  void rebuildController();
  void configureControllerCallbacks();
  void requestRepaint() const;
  void requestRenderAfterEdit();
  [[nodiscard]] native_ui::EditorSceneState sceneState() const;
  void refreshVoicebankResolutionLocked();
  void refreshAllVoicebankResolutionsLocked();
  [[nodiscard]] std::vector<TrackVoicebankResolution>
  renderVoicebankResolutionsLocked() const;
  void rebuildTechnicalModelsLocked();
  [[nodiscard]] phonemizer::Result phonemesLocked() const;
  [[nodiscard]] const ui::PhonemeVisual* phonemeVisualAt(
      ui::Point point) const noexcept;
  [[nodiscard]] const ui::UnitLaneVisual* unitVisualAt(
      ui::Point point) const noexcept;
  [[nodiscard]] std::optional<time::Tick> pitchPointAt(
      ui::Point point, double tolerance = 8.0) const noexcept;
  [[nodiscard]] time::Microseconds microsecondOffsetAt(
      domain::NoteId noteId, double x) const noexcept;
  void paintPhase12BOverlay(native_ui::RasterCanvas& canvas) noexcept;
  void paintSampleMicroscope(native_ui::RasterCanvas& canvas) noexcept;
  [[nodiscard]] core::Result<void> bindVoicebankLocked(
      const voicebank::VoicebankCandidate& candidate);

  mutable std::recursive_mutex mutex_;
  bool createdDefault_{false};
  application::ProjectFactory factory_{1000U};
  domain::TrackId trackId_{};
  domain::RegionId regionId_{};
  application::EditorSession session_;
  std::unique_ptr<native_ui::NativeEditorController> controller_;
  native_ui::EditorScenePainter painter_;
  native_ui::CharacterPresentation character_;
  AsyncPreviewRenderService renderService_;
  LiveSampleInstrument live_;
  authoring::VoicebankSession voicebankSession_;
  voicebank::VoicebankResolution voicebankResolution_;
  std::vector<TrackVoicebankResolution> trackVoicebankResolutions_;
  std::function<void()> repaintCallback_;
  std::function<void(const native_ui::TextInputRequest&)> beginTextInput_;
  std::function<void()> endTextInput_;
  double logicalWidth_{1100.0};
  double logicalHeight_{720.0};
  std::uint32_t renderSampleRate_{48000U};
  rendering::RenderQuality renderQuality_{rendering::RenderQuality::Preview};
  bool dirty_{false};
  ui::PhonemeLaneModel phonemeLane_;
  ui::UnitLaneModel unitLane_;
  ui::SampleMicroscopeModel microscope_;
  voicebank::AudioBuffer microscopeAudio_;
  std::optional<std::string> microscopeUnitId_;
  std::optional<domain::PhonemeKey> selectedUnitKey_;
  std::optional<domain::PhonemeKey> draggingPhonemeKey_;
  bool draggingPhonemeStart_{false};
  std::optional<time::Tick> draggingPitchTick_;
  HostTimelineState hostTimelineState_{};
};

[[nodiscard]] core::Result<std::vector<std::byte>> encodeEditorState(
    const domain::Project& project);
[[nodiscard]] core::Result<domain::Project> decodeEditorState(
    std::span<const std::byte> bytes);

}  // namespace seam::clap_editor
