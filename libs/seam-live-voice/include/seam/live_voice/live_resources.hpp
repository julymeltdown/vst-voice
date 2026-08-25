#pragma once

#include "seam/core/result.hpp"
#include "seam/phase12c/live_voice.hpp"
#include "seam/voicebank/catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace seam::live_voice {

enum class LiveSegmentRole : std::uint8_t {
  Attack,
  Transition,
  Sustain,
  Release,
  Breath,
};

struct LiveUnitAudio final {
  std::string unitId;
  LiveSegmentRole role{LiveSegmentRole::Sustain};
  voicebank::UnitKind kind{voicebank::UnitKind::Sustain};
  std::vector<std::string> phones;
  std::int32_t rootMidi{60};
  std::int32_t priority{0};
  std::int32_t take{1};
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
  std::size_t maximumDecodedBytes{phase12c::kMaxResourceBytes};
  bool requireRelease{false};
};

class LiveResourceBuilder final {
 public:
  [[nodiscard]] core::Result<std::shared_ptr<const LiveVoicebankResources>>
  build(const voicebank::VoicebankCandidate& candidate,
        const LiveResourceBuildOptions& options = {}) const;
};

struct ResourceBuildOptions final {
  std::size_t maximumUnits{4096U};
  std::size_t maximumBytes{phase12c::kMaxResourceBytes};
  std::uint32_t minimumSampleRate{8000U};
  std::uint32_t maximumSampleRate{192000U};
  bool requireSustain{true};
  bool requireRelease{false};
};

[[nodiscard]] core::Result<std::shared_ptr<const phase12c::LiveVoicebankResource>>
buildTrustedResource(const voicebank::VoicebankCandidate& candidate,
                     ResourceBuildOptions options = {});

}
