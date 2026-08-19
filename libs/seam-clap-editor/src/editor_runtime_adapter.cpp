#include "seam/clap_editor/editor_runtime.hpp"
#include "editor_runtime_internal.hpp"

#include "seam/application/render_commands.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/rendering/region_renderer.hpp"
#include "seam/synthesis/timing_solver.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <utility>

namespace seam::clap_editor {
using namespace detail;

domain::Project EditorRuntime::makeDefaultProject(
    application::ProjectFactory& factory, domain::RegionId& regionId) {
  auto project = factory.createProject("SEAM / CLAP EDITOR");
  static_cast<void>(project.tempoMap().addOrReplace(time::Tick{0}, 154.0));
  project.settings().characterDisplay = domain::CharacterDisplayMode::Minimal;
  const auto trackId = factory.addVocalTrack(project, "VOICE 01");
  regionId = factory.addRegion(project, trackId, "DAW PHRASE",
                               time::Tick{0}, time::Tick{15360});
  auto* region = project.findRegion(regionId);
  if (region == nullptr) return project;

  const std::array<std::tuple<std::int64_t, std::int64_t, std::uint8_t,
                              const char32_t*>, 8>
      notes{{
          {0, 720, 64U, U"こ"},
          {720, 480, 67U, U"え"},
          {1200, 960, 69U, U"を"},
          {2400, 480, 67U, U"つ"},
          {2880, 720, 64U, U"な"},
          {3600, 960, 62U, U"ぐ"},
          {4800, 720, 64U, U"ま"},
          {5520, 1440, 67U, U"で"},
      }};
  for (const auto& [start, duration, key, lyricText] : notes) {
    auto [lyric, note] = factory.makeNote(
        time::Tick{start}, time::Tick{duration}, key,
        std::u32string{lyricText}, domain::Language::Japanese);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }
  region->sortNotes();
  const std::array<std::tuple<std::uint16_t, const char*,
                              domain::UnitRendererKind>, 8>
      unitOverrides{{
          {2U, "demo.ja.g4.k-o.01", domain::UnitRendererKind::ClassicPsola},
          {1U, "demo.ja.g4.e.01", domain::UnitRendererKind::Raw},
          {1U, "demo.ja.g4.o.01", domain::UnitRendererKind::SpectralClassic},
          {2U, "demo.ja.g4.ts-u.01", domain::UnitRendererKind::Stretch},
          {2U, "demo.ja.g4.n-a.01", domain::UnitRendererKind::ClassicPsola},
          {2U, "demo.ja.g4.g-u.01", domain::UnitRendererKind::Raw},
          {2U, "demo.ja.g4.m-a.01", domain::UnitRendererKind::SpectralClassic},
          {2U, "demo.ja.g4.d-e.01", domain::UnitRendererKind::Stretch},
      }};
  for (std::size_t index = 0U; index < region->notes.size(); ++index) {
    const auto& [tokenCount, unitId, renderer] = unitOverrides[index];
    region->unitSelectionOverrides.push_back(domain::UnitSelectionOverride{
        .startKey = domain::PhonemeKey{
            .noteId = region->notes[index].id, .ordinal = 0U},
        .tokenCount = tokenCount,
        .unitId = unitId,
        .renderer = renderer,
        .locked = true,
    });
  }
  if (!region->notes.empty()) {
    region->seamOverrides.push_back(domain::SeamOverride{
        .incomingStartKey = domain::PhonemeKey{
            .noteId = region->notes.front().id, .ordinal = 0U},
        .seamAmount = 0.55F,
        .overlap = time::Microseconds{9000},
        .phaseReset = 0.65F,
        .envelopeBlend = 0.20F,
        .curve = domain::SeamCurve::HardCharacter,
        .locked = true,
    });
  }
  static_cast<void>(region->pitchAutomation.upsert(
      domain::PitchAutomationPoint{.tick = time::Tick{0},
                                   .cents = -12.0F,
                                   .interpolation =
                                       domain::CurveInterpolation::Smooth}));
  static_cast<void>(region->pitchAutomation.upsert(
      domain::PitchAutomationPoint{.tick = time::Tick{2400},
                                   .cents = 38.0F,
                                   .interpolation =
                                       domain::CurveInterpolation::Linear}));
  return project;
}

EditorRuntime::~EditorRuntime() {
  if (authoring_) {
    authoring_->setCompletionCallback({});
    authoring_->shutdown();
  }
}

domain::RegionId EditorRuntime::firstRegionId(
    const domain::Project& project) noexcept {
  for (const auto& track : project.vocalTracks()) {
    if (!track.regions.empty()) return track.regions.front().id;
  }
  return {};
}

domain::TrackId EditorRuntime::firstTrackId(
    const domain::Project& project) noexcept {
  return project.vocalTracks().empty() ? domain::TrackId{}
                                       : project.vocalTracks().front().id;
}

EditorRuntime::EditorRuntime(
    std::optional<domain::Project> project,
    const std::filesystem::path& characterPackage,
    std::vector<voicebank::VoicebankSearchRoot> voicebankRoots)
    : createdDefault_(!project.has_value()),
      authoring_([&] {
        application::ProjectFactory seedFactory{1000U};
        domain::Project initial;
        if (project.has_value()) {
          initial = std::move(*project);
          seedFactory.synchronizeWith(initial);
          trackId_ = firstTrackId(initial);
          regionId_ = firstRegionId(initial);
        } else {
          initial = makeDefaultProject(seedFactory, regionId_);
          trackId_ = firstTrackId(initial);
        }
        const auto channels = std::clamp<std::uint8_t>(
            initial.routing().deviceOutputChannels, 1U, 8U);
        auto document = std::unique_ptr<authoring::ProjectDocument>{
            new authoring::ProjectDocument(
                std::move(initial),
                application::ProjectFactory{seedFactory.nextIdValue()})};
        return std::make_unique<authoring::AuthoringRuntime>(
            std::move(document), authoring::AuthoringRuntimeConfig{
                .cacheRoot = previewCacheRoot(),
                .voicebankRoots = runtimeVoicebankRoots(
                    std::move(voicebankRoots)),
                .previewSampleRate = 48000U,
                .outputChannels = channels,
                .allowDevelopmentVoicebanks = true,
                .enableTransport = false,
            });
      }()),
      factory_(authoring_->document().factory()),
      session_(authoring_->document().session()),
      voicebankSession_(authoring_->voicebanks()) {
  authoring_->setCompletionCallback(
      [this] { publishPreviewFromAuthoring(); });
  const auto initialized = authoring_->initialize();
  if (!initialized) {
    voicebankResolution_.status = voicebank::VoicebankResolveStatus::Missing;
    voicebankResolution_.diagnostic = initialized.error().message;
  }
  if (trackId_.valid()) static_cast<void>(authoring_->selectTrack(trackId_));
  if (regionId_.valid()) static_cast<void>(authoring_->selectRegion(regionId_));

  if (createdDefault_) {
    const auto candidates = voicebankSession_.candidates();
    const auto demo = std::find_if(
        candidates.begin(), candidates.end(), [](const auto& candidate) {
          return candidate.manifest.id ==
                     "demo.public-domain.human.production" &&
                 candidate.manifest.version == "0.12.0";
        });
    if (demo != candidates.end()) {
      static_cast<void>(bindVoicebankLocked(*demo));
      authoring_->handleDocumentChanged();
    }
  }
  refreshAllVoicebankResolutionsLocked();
  rebuildController();
  const auto loaded = character_.load(characterPackage);
  if (loaded && controller_) {
    controller_->setCharacterMetadata(character_.displayName(),
                                      character_.styleName());
  }
  controller_->setDirty(authoring_->document().dirty());
  dirty_ = authoring_->document().dirty();
  requestRender(renderSampleRate_);
}

RenderedPreview EditorRuntime::makeRenderedPreview(
    const authoring::PublishedProjectAudio& shared) {
  RenderedPreview output;
  output.sampleRate = shared.result.sampleRate;
  output.revision = shared.projectRevision;
  output.status = shared.state == authoring::RenderState::Ready
                      ? PreviewStatus::Ready
                      : previewStatusFor(shared.failure);
  output.diagnostic = shared.diagnostic;
  output.voicebankId = shared.activeVoicebankId;
  output.voicebankVersion = shared.activeVoicebankVersion;
  output.voicebankContentHash = shared.activeVoicebankContentHash;
  output.phraseCount = shared.result.phraseCount;
  output.unitPlan = shared.result.activeUnitPlan;
  output.unitCount = shared.result.unitCount;
  output.fallbackCount = shared.result.fallbackCount;
  output.cacheHits = shared.result.cacheHits;
  output.trackCount = shared.result.trackCount;
  output.regionCount = shared.result.regionCount;
  output.channelCount = shared.result.channelCount;
  output.phraseContentHashes = shared.result.phraseContentHashes;
  output.interleaved = shared.result.interleaved;
  const auto frames = output.channelCount == 0U
                          ? 0U
                          : output.interleaved.size() / output.channelCount;
  output.stereo.assign(frames * 2U, 0.0F);
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    if (output.channelCount == 1U) {
      const auto value = output.interleaved[frame];
      output.stereo[frame * 2U] = value;
      output.stereo[frame * 2U + 1U] = value;
    } else {
      output.stereo[frame * 2U] =
          output.interleaved[frame * output.channelCount];
      output.stereo[frame * 2U + 1U] =
          output.interleaved[frame * output.channelCount + 1U];
    }
  }
  return output;
}

void EditorRuntime::publishPreviewFromAuthoring() {
  const auto shared = authoring_->renderer().latest();
  if (shared == nullptr || shared->state == authoring::RenderState::Idle) return;
  static_cast<void>(previewPublication_.publish(makeRenderedPreview(*shared)));
  std::function<void()> callback;
  {
    std::lock_guard lock(mutex_);
    callback = renderReadyCallback_;
  }
  if (callback) callback();
}


core::Result<void> EditorRuntime::bindVoicebankLocked(
    const voicebank::VoicebankCandidate& candidate) {
  if (!trackId_.valid()) {
    return core::failure(core::ErrorCode::NotFound,
                         "CLAP editor contains no vocal track for Voicebank binding");
  }
  return voicebankSession_.bindTrack(authoring_->document(), trackId_, candidate);
}

void EditorRuntime::refreshVoicebankResolutionLocked() {
  const auto* track = session_.project().findVocalTrack(trackId_);
  if (track == nullptr) {
    voicebankResolution_ = {};
    voicebankResolution_.status = voicebank::VoicebankResolveStatus::InvalidReference;
    voicebankResolution_.diagnostic = "CLAP editor contains no vocal track";
    return;
  }
  voicebankResolution_ =
      voicebankSession_.resolveTrack(session_.project(), trackId_);
}

void EditorRuntime::refreshAllVoicebankResolutionsLocked() {
  refreshVoicebankResolutionLocked();
}

core::Result<void> EditorRuntime::refreshVoicebanks() {
  std::lock_guard lock(mutex_);
  const auto refreshed = voicebankSession_.refresh();
  if (!refreshed) return refreshed;
  refreshAllVoicebankResolutionsLocked();
  if (controller_) {
    const auto ready = voicebankResolution_.resolved();
    controller_->setAudioState(ready, voicebankStatusLabel(voicebankResolution_));
  }
  requestRender(renderSampleRate_);
  requestRepaint();
  return core::success();
}

core::Result<void> EditorRuntime::addVoicebankSearchRoot(
    voicebank::VoicebankSearchRoot root) {
  std::lock_guard lock(mutex_);
  const auto added = voicebankSession_.addSearchRoot(std::move(root));
  if (!added) return added;
  refreshAllVoicebankResolutionsLocked();
  if (controller_) {
    const auto ready = voicebankResolution_.resolved();
    controller_->setAudioState(ready, voicebankStatusLabel(voicebankResolution_));
  }
  requestRender(renderSampleRate_);
  requestRepaint();
  return core::success();
}

core::Result<void> EditorRuntime::selectVoicebank(
    std::string_view id, std::string_view version,
    std::optional<std::string_view> contentHash) {
  std::lock_guard lock(mutex_);
  const auto candidates = voicebankSession_.candidates();
  const auto candidate = std::find_if(
      candidates.begin(), candidates.end(),
      [id, version, contentHash](const auto& value) {
        return value.manifest.id == id && value.manifest.version == version &&
               (!contentHash.has_value() || value.contentHash == *contentHash);
      });
  if (candidate == candidates.end()) {
    return core::failure(
        contentHash.has_value() ? core::ErrorCode::Conflict : core::ErrorCode::NotFound,
        contentHash.has_value()
            ? "Requested Voicebank content hash is unavailable for that ID and version"
            : "Requested Voicebank ID and version are unavailable",
        std::string{id} + " " + std::string{version});
  }
  if (contentHash.has_value() && *contentHash != candidate->contentHash) {
    return core::failure(core::ErrorCode::Conflict,
                         "Requested Voicebank content hash does not match",
                         candidate->contentHash);
  }
  const auto bound = bindVoicebankLocked(*candidate);
  if (!bound) return bound;
  refreshAllVoicebankResolutionsLocked();
  if (controller_) {
    controller_->setAudioState(
        true, "BANK " + candidate->manifest.displayName + " [" +
                  std::string{voicebank::voicebankTrustName(candidate->trust)} + "]");
  }
  requestRenderAfterEdit();
  return core::success();
}

voicebank::VoicebankResolution EditorRuntime::voicebankResolution() const {
  std::lock_guard lock(mutex_);
  return voicebankResolution_;
}

std::vector<voicebank::VoicebankCandidate>
EditorRuntime::availableVoicebanks() const {
  std::lock_guard lock(mutex_);
  return voicebankSession_.candidates();
}

void EditorRuntime::configureControllerCallbacks() {
  native_ui::EditorHostCallbacks callbacks{
      .requestRepaint = [this] { requestRepaint(); },
      .beginTextInput = [this](const native_ui::TextInputRequest& request) {
        if (beginTextInput_) beginTextInput_(request);
      },
      .endTextInput = [this] {
        if (endTextInput_) endTextInput_();
      },
      .setPlaying = [this](bool) { requestRepaint(); },
  };
  controller_ = std::make_unique<native_ui::NativeEditorController>(
      session_, factory_, regionId_, std::move(callbacks));
  controller_->resize(logicalWidth_, logicalHeight_);
  const auto ready = voicebankResolution_.resolved();
  controller_->setAudioState(ready, voicebankStatusLabel(voicebankResolution_));
}

void EditorRuntime::rebuildController() {
  configureControllerCallbacks();
}

void EditorRuntime::setRepaintCallback(std::function<void()> callback) {
  std::lock_guard lock(mutex_);
  repaintCallback_ = std::move(callback);
}

void EditorRuntime::setRenderReadyCallback(std::function<void()> callback) {
  std::lock_guard lock(mutex_);
  renderReadyCallback_ = std::move(callback);
}

void EditorRuntime::setTextInputCallbacks(
    std::function<void(const native_ui::TextInputRequest&)> begin,
    std::function<void()> end) {
  std::lock_guard lock(mutex_);
  beginTextInput_ = std::move(begin);
  endTextInput_ = std::move(end);
}

void EditorRuntime::resize(double logicalWidth, double logicalHeight) noexcept {
  std::lock_guard lock(mutex_);
  logicalWidth_ = std::max(480.0, logicalWidth);
  logicalHeight_ = std::max(320.0, logicalHeight);
  controller_->resize(logicalWidth_, logicalHeight_);
}

}  // namespace seam::clap_editor
