#pragma once

#include "seam/domain/project.hpp"

#include <optional>
#include <string>

namespace seam::native_ui {

struct TrackInspectorSnapshot final {
  bool valid{false};
  bool vocal{false};
  domain::TrackId trackId;
  std::string name;
  float gainDb{0.0F};
  float pan{0.0F};
  bool muted{false};
  bool solo{false};
  domain::VoicebankReference voicebank;
  std::string mediaPath;
  std::string mediaHash;
  domain::MediaOwnership mediaOwnership{domain::MediaOwnership::ExternalReference};
  std::string originalFilename;
  std::uint32_t sourceSampleRate{0U};
  std::uint16_t sourceChannels{0U};
  std::uint64_t sourceFrameCount{0U};
  domain::TrackOutputRoute outputRoute;
};

class TrackInspectorModel final {
public:
  [[nodiscard]] static TrackInspectorSnapshot snapshot(
      const domain::Project& project, domain::TrackId trackId) noexcept;
};

}
