#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/routing.hpp"
#include "seam/rendering/playback_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace seam::rendering {

struct RoutedPcm final {
  std::uint32_t sampleRate{48000U};
  time::SampleFrame startFrame{0};
  std::uint8_t channelCount{1U};
  std::vector<float> interleavedSamples;

  [[nodiscard]] time::SampleFrame frameCount() const noexcept {
    return channelCount == 0U
               ? 0
               : static_cast<time::SampleFrame>(interleavedSamples.size() /
                                                channelCount);
  }
  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] static core::Result<RoutedPcm> fromMono(const CachedPcm& pcm);

  friend bool operator==(const RoutedPcm&, const RoutedPcm&) = default;
};

struct RoutedPlaybackClip final {
  std::string id;
  std::shared_ptr<const RoutedPcm> pcm;
  domain::TrackOutputRoute outputRoute;
  float gain{1.0F};
  time::SampleFrame fadeInFrames{0};
  time::SampleFrame fadeOutFrames{0};
  bool enabled{true};
  bool solo{false};
};

class RoutingWorkspace final {
public:
  [[nodiscard]] core::Result<void> prepare(
      const domain::ProjectRouting& routing, std::size_t maximumFrames);
  void clear(std::size_t frames) noexcept;
  [[nodiscard]] std::span<float> busBuffer(domain::BusId bus,
                                            std::size_t frames) noexcept;
  [[nodiscard]] std::span<const float> busBuffer(
      domain::BusId bus, std::size_t frames) const noexcept;
  [[nodiscard]] std::size_t maximumFrames() const noexcept {
    return maximumFrames_;
  }

private:
  struct BusBuffer final {
    std::uint8_t channels{0U};
    std::vector<float> samples;
  };
  std::unordered_map<domain::BusId, BusBuffer> buses_;
  std::size_t maximumFrames_{0U};
};

class RoutedPlaybackTimeline final {
public:
  explicit RoutedPlaybackTimeline(std::uint32_t sampleRate = 48000U)
      : sampleRate_(sampleRate) {}

  [[nodiscard]] core::Result<void> configure(
      domain::ProjectRouting routing,
      std::vector<RoutedPlaybackClip> clips);
  [[nodiscard]] core::Result<void> mix(
      time::SampleFrame startFrame, std::size_t frameCount,
      std::span<float> interleavedOutput,
      RoutingWorkspace& workspace) const noexcept;

  [[nodiscard]] std::uint32_t sampleRate() const noexcept { return sampleRate_; }
  [[nodiscard]] std::uint8_t outputChannels() const noexcept {
    return routing_.deviceOutputChannels;
  }
  [[nodiscard]] time::SampleFrame startFrame() const noexcept;
  [[nodiscard]] time::SampleFrame endFrame() const noexcept;
  [[nodiscard]] const domain::ProjectRouting& routing() const noexcept {
    return routing_;
  }
  [[nodiscard]] const std::vector<RoutedPlaybackClip>& clips() const noexcept {
    return clips_;
  }

private:
  [[nodiscard]] core::Result<void> validateClip(
      const RoutedPlaybackClip& clip) const;

  std::uint32_t sampleRate_{48000U};
  domain::ProjectRouting routing_;
  std::vector<RoutedPlaybackClip> clips_;
  std::vector<domain::BusId> topologicalOrder_;
};

}  // namespace seam::rendering
