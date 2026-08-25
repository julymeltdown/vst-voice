#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace seam::native_ui {

struct RecoverySupportView final {
  bool crashMarkerAvailable{false};
  bool recoveryAvailable{false};
  bool safeMode{false};
  std::uint32_t reportCount{0U};
  std::string status;
};

class RecoverySupportPanelModel final {
public:
  void update(RecoverySupportView view) noexcept { view_ = std::move(view); }
  [[nodiscard]] const RecoverySupportView& view() const noexcept { return view_; }

private:
  RecoverySupportView view_;
};

}
