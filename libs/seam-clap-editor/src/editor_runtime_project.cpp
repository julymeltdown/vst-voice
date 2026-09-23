#include "seam/clap_editor/editor_runtime.hpp"
#include "editor_runtime_internal.hpp"

#include "seam/build/version.hpp"
#include "seam/core/sha256.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/rendering/region_renderer.hpp"
#include "seam/synthesis/timing_solver.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <thread>
#include <utility>

namespace seam::clap_editor {
using namespace detail;

namespace {

// How far an acquired host tempo map must reach to time the whole score: the region
// extent in musical beats, which does not depend on the tempo map itself.
double projectEndBeats(const domain::Project& project) {
  const auto ppq = static_cast<double>(project.tempoMap().ppq());
  if (!(ppq > 0.0)) return 0.0;
  double end = 0.0;
  for (const auto& track : project.vocalTracks()) {
    for (const auto& region : track.regions) {
      const auto ticks = static_cast<double>((region.startTick + region.durationTick).value());
      end = std::max(end, ticks / ppq);
    }
  }
  return end;
}

OfflineRenderIdentity makeOfflineIdentity(
    const domain::Project& project, std::uint64_t revision,
    std::uint32_t sampleRate, OfflineTimingAuthority authority,
    const HostTimelineState& hostTimeline,
    const time::TempoMap* tempoOverride = nullptr,
    const PreparedHostTimeline* prepared = nullptr) {
  OfflineRenderIdentity identity{
      .projectRevision = revision,
      .sampleRate = sampleRate,
      .quality = rendering::RenderQuality::Final,
      .authority = authority,
      .projectContentHash = {},
      .timingMapHash = {},
      .rendererIdentity = std::string{build::kRenderAbiId} + "/offline/v1",
  };
  auto rendered = project;
  if (tempoOverride != nullptr) rendered.tempoMap() = *tempoOverride;
  // A recorded renderer says which build made a sound, not what the sound is, so it stays out of the
  // offline identity. Otherwise recording provenance would invalidate the very bounce it describes.
  const auto encoded = formats::ProjectJsonCodec{}.encode(
      rendered, formats::ProjectJsonEncodeOptions{.includeRendererProvenance = false});
  if (!encoded) return identity;
  identity.projectContentHash = core::sha256Hex(encoded.value());
  core::Sha256 timing;
  timing.update(identity.projectContentHash);
  timing.update(OfflineRenderSession::authorityName(authority));
  if (authority == OfflineTimingAuthority::FollowHost) {
    timing.update(hostTimeline.playing ? "playing" : "stopped");
    timing.update(hostTimeline.hasSeconds ? "seconds" : "no-seconds");
    timing.update(std::to_string(hostTimeline.seconds));
    timing.update(hostTimeline.hasBeats ? "beats" : "no-beats");
    timing.update(std::to_string(hostTimeline.beats));
    timing.update(hostTimeline.hasTempo ? "tempo" : "no-tempo");
    timing.update(std::to_string(hostTimeline.tempo));
    timing.update(hostTimeline.loopActive ? "loop" : "no-loop");
    timing.update(std::to_string(hostTimeline.loopStartSeconds));
    timing.update(std::to_string(hostTimeline.loopEndSeconds));
    timing.update(std::to_string(hostTimeline.loopStartBeats));
    timing.update(std::to_string(hostTimeline.loopEndBeats));
    timing.update(tempoOverride != nullptr ? "host-map" : "no-map");
    if (tempoOverride != nullptr) {
      // The acquired events are hashed directly, so two renders cannot share a timing
      // identity while following different host tempo histories.
      for (const auto& event : tempoOverride->events()) {
        timing.update(std::to_string(event.tick.value()));
        timing.update(std::to_string(event.bpm));
      }
    }
    // The frozen authority carries what the tempo events alone cannot: the covered range,
    // the host's meter segments, loop semantics and the capture it was frozen from.
    timing.update(prepared != nullptr ? prepared->contentHash()
                                      : std::string{"no-prepared-timeline"});
  }
  identity.timingMapHash = timing.hexDigest();
  return identity;
}

}  // namespace

domain::Project EditorRuntime::projectCopy() const {
  std::lock_guard lock(mutex_);
  return session_.project();
}

core::Result<void> EditorRuntime::replaceProject(domain::Project project) {
  std::lock_guard lock(mutex_);
  offlineRender_.invalidate("Project replacement changed the offline audio identity");
  offlineAudioReady_.store(false, std::memory_order_release);
  const auto replacementTrack = firstTrackId(project);
  const auto replacementRegion = firstRegionId(project);
  if (!replacementTrack.valid() || !replacementRegion.valid()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "CLAP editor state contains no vocal track and region");
  }
  const auto refreshed = voicebankSession_.refresh();
  if (!refreshed) return refreshed;
  const auto migrated = voicebankSession_.migrateLegacyStyles(project);
  if (!migrated) return core::Result<void>{migrated.error()};
  // A reopened project bounces the way it was saved to bounce. This is the only place the
  // persisted choice becomes the live one, so an imported document cannot silently inherit
  // the previous session's authority.
  offlineTimingAuthority_ =
      project.settings().bounceTimingAuthority == domain::BounceTimingAuthority::FollowHost
          ? OfflineTimingAuthority::FollowHost
          : OfflineTimingAuthority::FixedAudio;
  preparedHostTimeline_.reset();
  const auto replaced = authoring_->document().replaceProject(std::move(project));
  if (!replaced) return replaced;
  selectedUnitKey_.reset();
  draggingPhonemeKey_.reset();
  draggingPitchTick_.reset();
  trackId_ = replacementTrack;
  regionId_ = replacementRegion;
  static_cast<void>(authoring_->selectTrack(trackId_));
  static_cast<void>(authoring_->selectRegion(regionId_));
  refreshAllVoicebankResolutionsLocked();
  rebuildController();
  controller_->setCharacterMetadata(character_.displayName(),
                                    character_.styleName());
  controller_->setCharacterPortrait(
      character_.portrait(character::State::Neutral));
  dirty_ = authoring_->document().dirty();
  controller_->setDirty(dirty_);
  authoring_->handleDocumentChanged();
  requestRepaint();
  return core::success();
}

std::uint64_t EditorRuntime::revision() const noexcept {
  std::lock_guard lock(mutex_);
  return session_.revision();
}

void EditorRuntime::requestRender(std::uint32_t sampleRate) {
  std::lock_guard lock(mutex_);
  offlineRender_.invalidate("Render request changed the offline audio identity");
  offlineAudioReady_.store(false, std::memory_order_release);
  renderSampleRate_ = std::clamp(sampleRate, 8000U, 192000U);
  static_cast<void>(authoring_->setPreviewSampleRate(renderSampleRate_));
  authoring_->setRenderQuality(renderQuality_);
  authoring_->requestPreview();
}

void EditorRuntime::setRenderQuality(rendering::RenderQuality quality) {
  std::lock_guard lock(mutex_);
  if (renderQuality_ == quality) return;
  offlineRender_.invalidate("Render quality changed the offline audio identity");
  offlineAudioReady_.store(false, std::memory_order_release);
  renderQuality_ = quality;
  authoring_->setRenderQuality(quality);
}

rendering::RenderQuality EditorRuntime::renderQuality() const noexcept {
  std::lock_guard lock(mutex_);
  return renderQuality_;
}

void EditorRuntime::setOfflineTimingAuthority(
    OfflineTimingAuthority authority) noexcept {
  std::lock_guard lock(mutex_);
  if (offlineTimingAuthority_ == authority) return;
  offlineTimingAuthority_ = authority;
  // The choice is part of the project, not a session preference: a host that reopens this
  // state must bounce the way the creator chose, and a project saved by an older build keeps
  // the document's own map because that is what it meant.
  session_.project().settings().bounceTimingAuthority =
      authority == OfflineTimingAuthority::FollowHost
          ? domain::BounceTimingAuthority::FollowHost
          : domain::BounceTimingAuthority::FixedAudio;
  if (controller_) {
    controller_->setBounceFollowHost(authority == OfflineTimingAuthority::FollowHost);
  }
  offlineRender_.invalidate("Offline timing authority changed");
  offlineAudioReady_.store(false, std::memory_order_release);
  preparedHostTimeline_.reset();
  refreshRenderStatusView();
  if (persistentStateChangeCallback_) persistentStateChangeCallback_();
}

OfflineTimingAuthority EditorRuntime::offlineTimingAuthority() const noexcept {
  std::lock_guard lock(mutex_);
  return offlineTimingAuthority_;
}

core::Result<void> EditorRuntime::prepareOfflineRender(
    std::chrono::milliseconds timeout) {
  // Every exit path, including the refusals below, leaves the editor's render status panel
  // describing what actually happened -- which is where a creator sees that a Follow Host
  // bounce needs a range the host has not reported yet.
  struct RefreshRenderStatus final {
    EditorRuntime* runtime{nullptr};
    ~RefreshRenderStatus() {
      if (runtime != nullptr) runtime->refreshRenderStatusView();
    }
  };
  const RefreshRenderStatus statusRefresh{this};
  offlineAudioReady_.store(false, std::memory_order_release);
  if (timeout <= std::chrono::milliseconds::zero() ||
      timeout > std::chrono::minutes{5}) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Offline render timeout is outside supported bounds");
  }

  OfflineRenderIdentity identity;
  std::uint64_t revision = 0U;
  domain::ProjectId projectId{};
  // Cleared on every exit path, including the failures below, so a later Fixed Audio
  // render can never inherit host timing from an earlier attempt.
  struct ClearTempoOverride final {
    authoring::AuthoringRuntime* runtime{nullptr};
    ClearTempoOverride() = default;
    explicit ClearTempoOverride(authoring::AuthoringRuntime* value) : runtime{value} {}
    // The guard exists to clear the override exactly once, when its own scope ends. A
    // copy or move would let a temporary clear the override during construction, which
    // is the opposite of what the render needs.
    ClearTempoOverride(const ClearTempoOverride&) = delete;
    ClearTempoOverride& operator=(const ClearTempoOverride&) = delete;
    ClearTempoOverride(ClearTempoOverride&&) = delete;
    ClearTempoOverride& operator=(ClearTempoOverride&&) = delete;
    ~ClearTempoOverride() {
      if (runtime != nullptr) runtime->setTempoMapOverride(std::nullopt);
    }
  };
  std::optional<ClearTempoOverride> hostTempoGuard;
  {
    std::lock_guard lock(mutex_);
    if (!authoring_) {
      return core::failure(core::ErrorCode::InvalidState,
                           "CLAP editor authoring runtime is unavailable");
    }
    revision = session_.revision();
    projectId = session_.project().id();
    identity = makeOfflineIdentity(session_.project(), revision,
                                   renderSampleRate_, offlineTimingAuthority_,
                                   hostTimelineState_);
    const auto valid = identity.validate();
    if (!valid) return valid;
    offlineAudioReady_.store(false, std::memory_order_release);
    const auto started = offlineRender_.begin(identity);
    if (!started) return started;
    std::optional<PreparedHostTimeline> frozenHostTimeline;
    if (offlineTimingAuthority_ == OfflineTimingAuthority::FollowHost) {
      // One instantaneous BPM cannot certify later tempo events, so the render is
      // authorized only by map coverage. The tolerance is an engineering default for how
      // far apart two host reports may be during playback, not a measured threshold.
      constexpr double kMaximumUnobservedBeats{1.0};
      const auto endBeats = projectEndBeats(session_.project());
      const auto projectStartBeats =
          static_cast<double>(session_.project().settings().hostStartOffsetTick.value()) /
          static_cast<double>(session_.project().tempoMap().ppq());
      const HostTimelineCaptureRequest captureRequest{
          .projectId = projectId,
          .projectRevision = revision,
          .sampleRate = renderSampleRate_,
          .ppq = session_.project().tempoMap().ppq(),
          .projectStartBeats = projectStartBeats,
          .requestedStartBeats = 0.0,
          .requestedEndBeats = projectStartBeats + endBeats,
          .maximumGapBeats = kMaximumUnobservedBeats,
          .hostId = {},
      };
      auto frozen = hostTimelineCapture_.freeze(captureRequest);
      if (!frozen) {
        const std::string diagnostic =
            "Follow Host final rendering cannot be prepared: " + frozen.error().message;
        static_cast<void>(offlineRender_.fail(identity, diagnostic));
        return core::failure(frozen.error().code, diagnostic);
      }
      frozenHostTimeline = frozen.value();
      identity = makeOfflineIdentity(session_.project(), revision, renderSampleRate_,
                                     offlineTimingAuthority_, hostTimelineState_,
                                     &frozenHostTimeline->tempoMap(),
                                     &*frozenHostTimeline);
      const auto hostIdentityValid = identity.validate();
      if (!hostIdentityValid) return hostIdentityValid;
      // The session must hold the identity that will actually be rendered. Otherwise a
      // later failure or publication for the host-timed render is rejected as stale and
      // the host is left looking at a preparation that never resolves.
      const auto rebound = offlineRender_.begin(identity);
      if (!rebound) return rebound;
    }
    if (frozenHostTimeline.has_value()) {
      preparedHostTimeline_ = *frozenHostTimeline;
    }
    const auto anySolo = std::any_of(
        session_.project().vocalTracks().begin(),
        session_.project().vocalTracks().end(),
        [](const auto& track) { return track.solo && !track.muted; });
    const auto resolvedTracks = voicebankSession_.resolveAll(session_.project());
    for (const auto& track : session_.project().vocalTracks()) {
      if (track.muted || (anySolo && !track.solo)) continue;
      if (std::none_of(track.regions.begin(), track.regions.end(),
                       [](const auto& region) { return !region.notes.empty(); })) {
        continue;
      }
      if (track.proceduralRecipe.has_value()) continue;
      const auto state = std::find_if(
          resolvedTracks.begin(), resolvedTracks.end(),
          [&track](const auto& value) { return value.trackId == track.id; });
      if (state == resolvedTracks.end() || !state->resolution.resolved()) {
        const auto diagnostic = state == resolvedTracks.end()
                                    ? "Final offline render has no voicebank resolution"
                                    : state->resolution.diagnostic;
        static_cast<void>(offlineRender_.fail(
            identity,
            diagnostic.empty()
                ? "Final offline render voicebank is unavailable"
                : diagnostic));
        return core::failure(core::ErrorCode::NotFound,
                             diagnostic.empty()
                                 ? "Final offline render voicebank is unavailable"
                                 : diagnostic,
                             track.id.toString());
      }
    }
    renderQuality_ = rendering::RenderQuality::Final;
    static_cast<void>(authoring_->setPreviewSampleRate(renderSampleRate_));
    authoring_->setRenderQuality(rendering::RenderQuality::Final);
    // The render follows the frozen host authority when Follow Host is authorized, and the
    // document's own map otherwise. The override is cleared after the render completes so
    // a later Fixed Audio render cannot inherit host timing.
    if (frozenHostTimeline.has_value()) {
      authoring_->setTempoMapOverride(frozenHostTimeline->tempoMap());
      hostTempoGuard.emplace(authoring_.get());
    } else {
      authoring_->setTempoMapOverride(std::nullopt);
    }
    // Immediate here means enqueue now; the worker still owns all rendering
    // and the calling host thread only waits for the publication gate below.
    authoring_->requestPreview(true);
  }

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  for (;;) {
    {
      std::lock_guard lock(mutex_);
      if (session_.revision() != revision || session_.project().id() != projectId) {
        offlineRender_.invalidate("Project changed during offline preparation");
        offlineAudioReady_.store(false, std::memory_order_release);
        return core::failure(core::ErrorCode::Conflict,
                             "Project changed during offline render preparation");
      }
    }
    const auto progress = authoring_->renderer().progress();
    if (progress.state == authoring::RenderState::Failed) {
      const auto diagnostic = progress.diagnostic.empty()
                                  ? "Final offline render failed"
                                  : progress.diagnostic;
      static_cast<void>(offlineRender_.fail(identity, diagnostic));
      offlineAudioReady_.store(false, std::memory_order_release);
      return core::failure(core::ErrorCode::Conflict, diagnostic);
    }
    if (progress.state == authoring::RenderState::Ready &&
        progress.requestedRevision == revision &&
        progress.publishedRevision == revision &&
        progress.publishedQuality == rendering::RenderQuality::Final) {
      const auto latest = authoring_->renderer().latest();
      // Preview may deliberately retain the successful phrases of a partial
      // render. Final bounce must not accept that same partial project as GO.
      if (latest && !latest->result.diagnostics.empty()) {
        const auto diagnostic = "Final render is incomplete: " + latest->result.diagnostics.front().message;
        static_cast<void>(offlineRender_.fail(identity, diagnostic));
        offlineAudioReady_.store(false, std::memory_order_release);
        return core::failure(core::ErrorCode::Conflict, diagnostic);
      }
      bool sourceDigestMatches = false;
      if (latest && latest->sourceProject != nullptr) {
        const auto encoded = formats::ProjectJsonCodec{}.encode(
            *latest->sourceProject);
        sourceDigestMatches = encoded &&
                              core::sha256Hex(encoded.value()) ==
                                  identity.projectContentHash;
      }
      if (latest && latest->state == authoring::RenderState::Ready &&
          latest->projectRevision == revision && latest->projectId == projectId &&
          latest->quality == rendering::RenderQuality::Final &&
          latest->sourceProject != nullptr &&
          sourceDigestMatches &&
          latest->result.sampleRate == identity.sampleRate &&
          latest->result.channelCount > 0U &&
          !latest->result.interleaved.empty() &&
          authoring_->renderer().matchesCurrent(*latest)) {
        // Publish the actual captured Final PCM, not whichever Preview happens
        // to be visible when process() next runs. The coordinator request token
        // invalidates this snapshot on every edit/resource/rate resubmission.
        auto finalAudio = makeRenderedPreview(*latest);
        finalAudio.offlineSource = latest;
        if (!offlinePublication_.publish(std::move(finalAudio))) {
          const std::string diagnostic = "Final audio publication is busy; bounce was not prepared";
          static_cast<void>(offlineRender_.fail(identity, diagnostic));
          return core::failure(core::ErrorCode::Conflict, diagnostic);
        }
        const auto published = offlineRender_.publish(
            identity, true,
            latest->diagnostic.empty() ? "Final offline render is ready"
                                       : latest->diagnostic);
        if (!published) return published;
        offlineAudioReady_.store(true, std::memory_order_release);
        if (!offlineRenderReady()) {
          offlineRender_.invalidate("Final source changed during publication");
          offlineAudioReady_.store(false, std::memory_order_release);
          return core::failure(core::ErrorCode::Conflict, "Final source changed during publication");
        }
        return core::success();
      }
      // Name the failing condition: "no publishable PCM" alone cannot distinguish a
      // stale publication from one rendered with a different timing map.
      std::string reason;
      if (latest == nullptr) reason = "the renderer published nothing";
      else if (latest->state != authoring::RenderState::Ready) reason = "the publication is not ready";
      else if (latest->projectRevision != revision) reason = "the publication is for another revision";
      else if (latest->projectId != projectId) reason = "the publication is for another project";
      else if (latest->quality != rendering::RenderQuality::Final) reason = "the publication is not Final quality";
      else if (latest->sourceProject == nullptr) reason = "the publication has no source project";
      else if (!sourceDigestMatches) reason = "the published source differs from the prepared timing identity";
      else if (latest->result.sampleRate != identity.sampleRate) reason = "the publication sample rate differs";
      else if (latest->result.channelCount == 0U) reason = "the publication has no channels";
      else if (latest->result.interleaved.empty()) reason = "the publication has no audio";
      else reason = "the publication is no longer current";
      const auto diagnostic = "Final offline render produced no publishable PCM: " + reason;
      static_cast<void>(offlineRender_.fail(identity, diagnostic));
      offlineAudioReady_.store(false, std::memory_order_release);
      return core::failure(core::ErrorCode::Conflict, diagnostic);
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      const auto diagnostic = "Timed out waiting for Final offline render";
      static_cast<void>(offlineRender_.fail(identity, diagnostic));
      offlineAudioReady_.store(false, std::memory_order_release);
      return core::failure(core::ErrorCode::Conflict, diagnostic);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
}

OfflineRenderView EditorRuntime::offlineRenderView() const {
  auto view = offlineRender_.view();
  if (view.state == OfflineRenderState::Ready && !offlineRenderReady()) {
    view.state = OfflineRenderState::Stale;
    view.hasAudio = false;
    view.diagnostic = "The captured Final source is no longer current; prepare the bounce again";
  }
  return view;
}

std::shared_ptr<const RenderedPreview> EditorRuntime::renderedPreview() const {
  auto handle = previewPublication_.acquire();
  if (!handle) return std::make_shared<RenderedPreview>();
  return std::make_shared<RenderedPreview>(*handle);
}

RenderServiceStats EditorRuntime::renderStats() const noexcept {
  const auto stats = authoring_->renderer().stats();
  return RenderServiceStats{
      .submitted = stats.submitted,
      .completed = stats.completed,
      .cancelled = stats.cancelled,
      .stale = stats.stale,
  };
}

std::vector<domain::TrackId> EditorRuntime::vocalTrackIds() const {
  std::lock_guard lock(mutex_);
  std::vector<domain::TrackId> ids;
  ids.reserve(session_.project().vocalTracks().size());
  for (const auto& track : session_.project().vocalTracks()) ids.push_back(track.id);
  return ids;
}

std::vector<domain::RegionId> EditorRuntime::regionIds(
    domain::TrackId trackId) const {
  std::lock_guard lock(mutex_);
  std::vector<domain::RegionId> ids;
  const auto* track = session_.project().findVocalTrack(trackId);
  if (track == nullptr) return ids;
  ids.reserve(track->regions.size());
  for (const auto& region : track->regions) ids.push_back(region.id);
  return ids;
}

core::Result<void> EditorRuntime::selectTrack(domain::TrackId trackId) {
  std::lock_guard lock(mutex_);
  const auto* track = session_.project().findVocalTrack(trackId);
  if (track == nullptr || track->regions.empty()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Selected vocal track has no editable region");
  }
  const auto selected = authoring_->selectTrack(trackId);
  if (!selected) return selected;
  trackId_ = authoring_->selectedTrack();
  regionId_ = authoring_->selectedRegion();
  refreshAllVoicebankResolutionsLocked();
  rebuildController();
  controller_->setCharacterMetadata(character_.displayName(),
                                    character_.styleName());
  rebuildTechnicalModelsLocked();
  requestRender(renderSampleRate_);
  requestRepaint();
  return core::success();
}

core::Result<void> EditorRuntime::selectRegion(domain::RegionId regionId) {
  std::lock_guard lock(mutex_);
  const auto* track = session_.project().findVocalTrack(trackId_);
  if (track == nullptr || track->findRegion(regionId) == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Selected region does not belong to the active track");
  }
  const auto selected = authoring_->selectRegion(regionId);
  if (!selected) return selected;
  trackId_ = authoring_->selectedTrack();
  regionId_ = authoring_->selectedRegion();
  rebuildController();
  controller_->setCharacterMetadata(character_.displayName(),
                                    character_.styleName());
  rebuildTechnicalModelsLocked();
  requestRender(renderSampleRate_);
  requestRepaint();
  return core::success();
}

core::Result<void> EditorRuntime::setTrackMix(
    domain::TrackId trackId, float gainDb, float pan, bool muted, bool solo) {
  std::lock_guard lock(mutex_);
  auto result = authoring_->execute(
      std::make_unique<application::SetVocalTrackMixCommand>(
          trackId, gainDb, pan, muted, solo));
  if (result) {
    dirty_ = authoring_->document().dirty();
    controller_->setDirty(dirty_);
    requestRepaint();
  }
  return result;
}

core::Result<void> EditorRuntime::configureOutputChannels(
    std::uint8_t channels) {
  std::lock_guard lock(mutex_);
  auto result = authoring_->execute(
      std::make_unique<application::ConfigureProjectOutputCommand>(channels));
  if (result) {
    dirty_ = authoring_->document().dirty();
    controller_->setDirty(dirty_);
    requestRepaint();
  }
  return result;
}

core::Result<void> EditorRuntime::setHostStartOffset(time::Tick tick) {
  std::lock_guard lock(mutex_);
  auto result = authoring_->execute(
      std::make_unique<application::SetHostStartOffsetCommand>(tick));
  if (result) {
    dirty_ = authoring_->document().dirty();
    controller_->setDirty(dirty_);
    requestRepaint();
  }
  return result;
}

}  // namespace seam::clap_editor
