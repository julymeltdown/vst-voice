#pragma once

#include "seam/live_voice/live_resources.hpp"
#include "seam/phase12c/live_voice.hpp"

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <memory>
#include <span>

namespace seam::live_voice {

using EventType = phase12c::EventType;
using LiveEvent = phase12c::LiveEvent;
using LiveStats = phase12c::LiveStats;
using LiveVoicebankResource = phase12c::LiveVoicebankResource;

class VoiceEngine final {
 public:
  VoiceEngine();

  static void resetCallPathEvidence() noexcept;
  [[nodiscard]] static std::uint64_t callPathEvidence() noexcept;

  void configure(std::uint32_t sampleRate,
                 std::uint32_t outputChannels) noexcept;
  void setOutputSampleRate(double sampleRate) noexcept;
  bool publishResource(
      std::shared_ptr<const LiveVoicebankResource> resource) noexcept;
  bool publishVoicebankResource(
      std::shared_ptr<const LiveVoicebankResource> resource) noexcept;
  bool publishResources(
      std::shared_ptr<const LiveVoicebankResources> resources) noexcept;
  void clearResource() noexcept;
  void clearVoicebankResource() noexcept;
  void process(std::span<const LiveEvent> events,
               float* const* outputs,
               std::uint32_t channels,
               std::uint32_t frames) noexcept;
  void dispatch(const LiveEvent& event) noexcept;
  void dispatchLiveEvent(const LiveEvent& event) noexcept;
  void renderLiveRange(float* const* outputs, std::uint32_t channels,
                       std::uint32_t beginFrame,
                       std::uint32_t endFrame) noexcept;
  void reset() noexcept;
  void noteOn(std::int32_t noteId,
              std::int32_t key,
              float velocity) noexcept;
  void noteOff(std::int32_t noteId, std::int32_t key) noexcept;
  void choke(std::int32_t noteId, std::int32_t key) noexcept;
  float renderSample() noexcept;
  [[nodiscard]] std::size_t activeVoiceCount() const noexcept;
  [[nodiscard]] LiveStats stats() const noexcept;

 private:
  phase12c::LiveVoiceEngine engine_;
};

}
