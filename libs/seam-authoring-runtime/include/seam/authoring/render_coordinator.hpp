#pragma once

#include "seam/application/command.hpp"
#include "seam/domain/project.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/project_renderer.hpp"

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
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace seam::authoring {

enum class RenderState {
  Idle,
  Queued,
  Rendering,
  Ready,
  Stale,
  Cancelled,
  Failed,
};

enum class RenderFailureKind {
  None,
  VoicebankMissing,
  VoicebankVersionMismatch,
  VoicebankContentHashMissing,
  VoicebankContentMismatch,
  VoicebankUntrusted,
  // An audible track resolved to a neural source without an admitted bundle or
  // worker runner. This is not a voicebank problem and must not be reported as
  // one, because the track never asked for a sample bank.
  NeuralSourceMissing,
  InvalidProject,
  RenderFailed,
  PublicationBusy,
};

struct RenderPublicationIdentity final {};

// Which admitted model and helper produced one published neural track.
// A neural render has no sample unit plan, so the renderer identity cannot be
// inferred from units; the coordinator reports the admitted execution instead.
struct PublishedNeuralIdentity final {
  domain::TrackId trackId{};
  std::string modelId;
  std::string modelVersion;
  std::string bundleContentHash;
  std::uint32_t configurationVersion{0U};
  std::int64_t inferenceSteps{0};
  std::string workerVersion;
  std::string runtimeVersion;
  std::string provider;
};

struct PublishedProjectAudio final {
  std::uint64_t projectRevision{0U};
  domain::ProjectId projectId{};
  std::uint64_t requestId{0U};
  std::shared_ptr<const RenderPublicationIdentity> sourceIdentity;
  std::shared_ptr<const domain::Project> sourceProject;
  application::CommandImpact impact;
  rendering::RenderQuality quality{rendering::RenderQuality::Preview};
  RenderState state{RenderState::Idle};
  RenderFailureKind failure{RenderFailureKind::None};
  rendering::ProjectRenderResult result;
  std::string diagnostic;
  std::string activeVoicebankId;
  std::string activeVoicebankVersion;
  std::string activeVoicebankContentHash;
  std::string activeRenderer;
  // Every audible neural track in this render, in submission order. Empty for a
  // render that used no neural source, which is not the same as a failed one.
  std::vector<PublishedNeuralIdentity> neuralIdentities;
};

class RealtimeProjectAudioPublication final {
public:
  class ReadHandle final {
  public:
    ReadHandle() = default;
    ReadHandle(const ReadHandle&) = delete;
    ReadHandle& operator=(const ReadHandle&) = delete;
    ReadHandle(ReadHandle&& other) noexcept;
    ReadHandle& operator=(ReadHandle&& other) noexcept;
    ~ReadHandle();

    [[nodiscard]] const PublishedProjectAudio* get() const noexcept {
      return value_;
    }
    [[nodiscard]] const PublishedProjectAudio* operator->() const noexcept {
      return value_;
    }
    [[nodiscard]] const PublishedProjectAudio& operator*() const noexcept {
      return *value_;
    }
    [[nodiscard]] explicit operator bool() const noexcept {
      return value_ != nullptr;
    }

  private:
    friend class RealtimeProjectAudioPublication;
    ReadHandle(const RealtimeProjectAudioPublication* owner,
               std::size_t slot,
               const PublishedProjectAudio* value) noexcept;
    void release() noexcept;

    const RealtimeProjectAudioPublication* owner_{nullptr};
    std::size_t slot_{0U};
    const PublishedProjectAudio* value_{nullptr};
  };

  RealtimeProjectAudioPublication();
  [[nodiscard]] ReadHandle acquire() const noexcept;
  [[nodiscard]] bool publish(PublishedProjectAudio audio);

private:
  static constexpr std::size_t kSlotCount = 3U;
  struct Slot final {
    PublishedProjectAudio audio;
    mutable std::atomic<std::uint32_t> readers{0U};
  };

  std::array<Slot, kSlotCount> slots_{};
  std::atomic<std::size_t> published_{0U};
  std::mutex writerMutex_;
};

struct RenderProgress final {
  RenderState state{RenderState::Idle};
  std::uint64_t requestedRevision{0U};
  std::uint64_t publishedRevision{0U};
  rendering::RenderQuality requestedQuality{rendering::RenderQuality::Preview};
  rendering::RenderQuality publishedQuality{rendering::RenderQuality::Preview};
  std::size_t completedPhrases{0U};
  std::size_t totalPhrases{0U};
  double fraction{0.0};
  bool audibleAudioStale{false};
  std::string diagnostic;
  std::string activeVoicebankId;
  std::string activeVoicebankVersion;
  std::string activeRenderer;
  RenderFailureKind failure{RenderFailureKind::None};
};

struct RenderCoordinatorStats final {
  std::uint64_t submitted{0U};
  std::uint64_t completed{0U};
  std::uint64_t cancelled{0U};
  std::uint64_t stale{0U};
  std::uint64_t failed{0U};
};

struct RenderCoordinatorHooks final {
  std::function<void(std::uint64_t, std::stop_token)> beforeRender;
  std::function<void(std::uint64_t, std::stop_token)> beforePublication;
};

class AuthoringRenderCoordinator final {
public:
  explicit AuthoringRenderCoordinator(
      std::filesystem::path cacheRoot,
      RenderCoordinatorHooks hooks = {});
  ~AuthoringRenderCoordinator();

  AuthoringRenderCoordinator(const AuthoringRenderCoordinator&) = delete;
  AuthoringRenderCoordinator& operator=(
      const AuthoringRenderCoordinator&) = delete;

  void submit(domain::Project project,
              std::vector<rendering::TrackVoicebankSource> voicebanks,
              domain::TrackId activeTrack,
              domain::RegionId activeRegion,
              std::uint64_t revision,
              std::uint32_t sampleRate,
              rendering::RenderQuality quality,
              bool immediate = false,
              application::CommandImpact impact = application::CommandImpact{
                  .scope = application::CommandAudioImpact::ProjectAudio,
                  .projectWide = true});
  void cancel() noexcept;
  // Reject captured audio immediately when new document intent is queued,
  // including the interval before a debounced render is submitted.
  void invalidateCurrent() noexcept;
  // Explicit resolved-resource submission; does not persist a track selection.
  void submitWithSources(domain::Project project,
              std::vector<rendering::TrackSingerSource> sources,
              domain::TrackId activeTrack, domain::RegionId activeRegion,
              std::uint64_t revision, std::uint32_t sampleRate,
              rendering::RenderQuality quality, bool immediate = false,
              application::CommandImpact impact = application::CommandImpact{
                  .scope = application::CommandAudioImpact::ProjectAudio, .projectWide = true});
  void shutdown() noexcept;

  [[nodiscard]] RealtimeProjectAudioPublication::ReadHandle acquire()
      const noexcept {
    return publication_.acquire();
  }
  [[nodiscard]] std::shared_ptr<const PublishedProjectAudio> latest() const;
  // Current ready audio only; unlike acquire(), this rejects retained stale
  // playback after replacement, cancellation or a same-revision resubmission.
  [[nodiscard]] RealtimeProjectAudioPublication::ReadHandle acquireCurrent() const noexcept;
  [[nodiscard]] bool matchesCurrent(const PublishedProjectAudio& source) const noexcept;
  [[nodiscard]] RenderProgress progress() const noexcept;
  [[nodiscard]] RenderCoordinatorStats stats() const noexcept;
  void setCompletionCallback(std::function<void()> callback);

private:
  struct Request final {
    std::uint64_t requestId{0U};
    domain::Project project;
    std::vector<rendering::TrackSingerSource> voicebanks;
    domain::TrackId activeTrack;
    domain::RegionId activeRegion;
    std::uint64_t revision{0U};
    std::uint32_t sampleRate{48000U};
    rendering::RenderQuality quality{rendering::RenderQuality::Preview};
    bool immediate{false};
    application::CommandImpact impact;
  };
  const std::shared_ptr<const RenderPublicationIdentity> sourceIdentity_{std::make_shared<const RenderPublicationIdentity>()};

  struct PreflightResult final {
    RenderFailureKind failure{RenderFailureKind::None};
    std::string diagnostic;
    std::string activeVoicebankId;
    std::string activeVoicebankVersion;
    std::string activeVoicebankContentHash;

    [[nodiscard]] bool ok() const noexcept {
      return failure == RenderFailureKind::None;
    }
  };

  void workerLoop(std::stop_token stopToken);
  [[nodiscard]] std::optional<PublishedProjectAudio> render(
      const Request& request, std::stop_token stopToken);
  [[nodiscard]] static PreflightResult preflight(const Request& request);
  [[nodiscard]] PublishedProjectAudio makeFailureAudio(
      const Request& request, const PreflightResult& preflight);
  [[nodiscard]] static std::size_t countPhrases(
      const domain::Project& project, std::uint32_t sampleRate,
      std::span<const rendering::TrackSingerSource> sources);
  void updateProgress(RenderProgress value) noexcept;
  void notifyCompletion();

  mutable std::mutex mutex_;
  std::condition_variable_any condition_;
  std::optional<Request> pending_;
  std::stop_source activeStopSource_;
  bool active_{false};
  std::atomic<std::uint64_t> latestSubmittedRevision_{0U};
  std::atomic<std::uint64_t> latestSubmittedRequestId_{0U};
  std::uint64_t nextRequestId_{0U};
  std::unique_ptr<rendering::PcmCache> cache_;
  RenderCoordinatorHooks hooks_;
  mutable RealtimeProjectAudioPublication publication_;

  mutable std::mutex progressMutex_;
  RenderProgress progress_;
  std::atomic<std::uint64_t> submitted_{0U};
  std::atomic<std::uint64_t> completed_{0U};
  std::atomic<std::uint64_t> cancelled_{0U};
  std::atomic<std::uint64_t> stale_{0U};
  std::atomic<std::uint64_t> failed_{0U};

  mutable std::mutex callbackMutex_;
  std::function<void()> completionCallback_;
  std::atomic<bool> shutdown_{false};
  std::jthread worker_;
};

[[nodiscard]] std::string_view renderStateName(RenderState state) noexcept;
[[nodiscard]] std::string_view renderFailureName(
    RenderFailureKind failure) noexcept;

}  // namespace seam::authoring
