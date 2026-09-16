#pragma once

#include "seam/domain/project.hpp"
#include "seam/ui/expression_lane.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace seam::native_ui {

// One timbral channel as the inspector shows it beside the track it belongs to. The lane states the
// same thing inside the automation band, where a creator only sees it after opening the band; a
// refusal that a creator has to open a surface to discover is a refusal they will find by editing
// failure instead. The fields mirror the lane's own descriptor so the two cannot disagree about the
// unit or the words.
struct TrackInspectorExpressionRow final {
  ui::ExpressionChannel channel{ui::ExpressionChannel::Formant};
  std::string label;
  std::string unit;
  float valueAtPlayhead{0.0F};
  std::size_t storedPoints{0U};
  // Empty when the selected singer renders this channel; otherwise the product's own refusal, in the
  // same words the lane uses.
  std::string refusal;
};

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
  // At most three rows, chosen as the channels that carry a stored curve or a non-neutral value at the
  // playhead. An inspector that listed all six on every track would spend the window on channels the
  // creator is not working with, and the panel's height is fixed.
  std::vector<TrackInspectorExpressionRow> expressionRows;
};

class TrackInspectorModel final {
public:
  // The playhead is optional so an existing caller that only reads track metadata is unaffected. When it
  // is supplied, a channel's row reports its value there rather than the channel's neutral.
  [[nodiscard]] static TrackInspectorSnapshot snapshot(
      const domain::Project& project, domain::TrackId trackId,
      std::optional<time::Tick> playhead = std::nullopt) noexcept;
};

}
