#pragma once

#include "seam/rendering/render_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace seam::native_ui {

enum class RenderStatusState {
  Idle,
  Queued,
  Rendering,
  Ready,
  Stale,
  Cancelled,
  Failed,
};

struct RenderStatusView final {
  RenderStatusState state{RenderStatusState::Idle};
  std::uint64_t requestedRevision{0U};
  std::uint64_t audibleRevision{0U};
  seam::rendering::RenderQuality requestedQuality{
      seam::rendering::RenderQuality::Preview};
  seam::rendering::RenderQuality audibleQuality{
      seam::rendering::RenderQuality::Preview};
  std::size_t completedPhrases{0U};
  std::size_t totalPhrases{0U};
  double fraction{0.0};
  bool hasAudibleAudio{false};
  bool audibleAudioStale{false};
  std::string diagnostic;
  std::string activeVoicebankId;
  std::string activeVoicebankVersion;
  std::string activeRenderer;
};

class RenderStatusPanelModel final {
public:
  void update(RenderStatusView view) noexcept;

  [[nodiscard]] const RenderStatusView& view() const noexcept { return view_; }
  [[nodiscard]] bool isStale() const noexcept;
  [[nodiscard]] bool canCancel() const noexcept;
  [[nodiscard]] bool canRetry() const noexcept;
  [[nodiscard]] std::string label() const;

private:
  RenderStatusView view_;
};

[[nodiscard]] std::string_view renderStatusStateName(
    RenderStatusState state) noexcept;

}
