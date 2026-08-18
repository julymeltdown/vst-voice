#pragma once

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
  InvalidProject,
  RenderFailed,
  PublicationBusy,
};

struct PublishedProjectAudio final {
  std::uint64_t projectRevision{0U};
  rendering::RenderQuality quality{rendering::RenderQuality::Preview};
  RenderState state{RenderState::Idle};
  RenderFailureKind failure{RenderFailureKind::None};
  rendering::ProjectRenderResult result;
  std::string diagnostic;
  std::string activeVoicebankId;
  std::string activeVoicebankVersion;
  std::string activeVoicebankContentHash;
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
  std::size_t completedPhrases{0U};
  std::size_t totalPhrases{0U};
  double fraction{0.0};
  std::string diagnostic;
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
              rendering::RenderQuality quality);
  void cancel() noexcept;
  void shutdown() noexcept;

  [[nodiscard]] RealtimeProjectAudioPublication::ReadHandle acquire()
      const noexcept {
    return publication_.acquire();
  }
  [[nodiscard]] std::shared_ptr<const PublishedProjectAudio> latest() const;
  [[nodiscard]] RenderProgress progress() const noexcept;
  [[nodiscard]] RenderCoordinatorStats stats() const noexcept;
  void setCompletionCallback(std::function<void()> callback);

private:
  struct Request final {
    domain::Project project;
    std::vector<rendering::TrackVoicebankSource> voicebanks;
    domain::TrackId activeTrack;
    domain::RegionId activeRegion;
    std::uint64_t revision{0U};
    std::uint32_t sampleRate{48000U};
    rendering::RenderQuality quality{rendering::RenderQuality::Preview};
  };

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
  [[nodiscard]] static PublishedProjectAudio makeFailureAudio(
      const Request& request, const PreflightResult& preflight);
  [[nodiscard]] static std::size_t countPhrases(
      const domain::Project& project);
  void updateProgress(RenderProgress value) noexcept;
  void notifyCompletion();

  mutable std::mutex mutex_;
  std::condition_variable_any condition_;
  std::optional<Request> pending_;
  std::stop_source activeStopSource_;
  bool active_{false};
  std::atomic<std::uint64_t> latestSubmittedRevision_{0U};
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
