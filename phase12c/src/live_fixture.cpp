#include "seam/phase12c/live_voice.hpp"
#include "human_fixture.hpp"

#include <algorithm>
#include <memory>

namespace seam::phase12c {

std::shared_ptr<const LiveVoicebankResource> makeEmbeddedHumanResource() {
  auto resource = std::make_shared<LiveVoicebankResource>();
  resource->sampleRate = fixture::kSampleRate;
  resource->trusted = true;
  resource->contentHash = fixture::kDerivedPcmSha256;
  resource->mono.reserve(fixture::kPcm.size());
  for (const auto sample : fixture::kPcm) {
    resource->mono.push_back(static_cast<float>(sample) / 32768.0F);
  }

  const auto frameCount = static_cast<std::uint32_t>(resource->mono.size());
  const auto attackEnd = std::max(2u, frameCount * 15u / 100u);
  const auto sustainStart = std::max(attackEnd + 2u, frameCount * 28u / 100u);
  const auto releaseStart = std::max(sustainStart + 2u, frameCount * 82u / 100u);
  resource->units = {
      {UnitKind::Attack, 0, sustainStart, attackEnd, sustainStart, 60, -1, -1},
      {UnitKind::Sustain, sustainStart, releaseStart, sustainStart,
       releaseStart, 60, -1, -1},
      {UnitKind::Transition, attackEnd, releaseStart, sustainStart,
       releaseStart, 60, 60, 62},
      {UnitKind::Release, releaseStart, frameCount, releaseStart, frameCount,
       60, -1, -1},
  };
  return resource;
}

}
