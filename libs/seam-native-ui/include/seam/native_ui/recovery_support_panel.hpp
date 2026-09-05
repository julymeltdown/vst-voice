#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace seam::native_ui {

enum class RecoverySupportMode { Preview, Reports };

struct RecoverySupportItemView final {
  std::string name;
  std::string detail;
  std::uint64_t bytes{0U};
  std::string sha256;
  bool included{false};
  bool selected{false};
};

struct RecoverySupportView final {
  bool visible{false};
  RecoverySupportMode mode{RecoverySupportMode::Preview};
  bool crashMarkerAvailable{false};
  bool recoveryAvailable{false};
  bool safeMode{false};
  std::string candidateId;
  std::uint64_t archiveBytes{0U};
  std::string archiveSha256;
  std::vector<RecoverySupportItemView> items;
  std::size_t firstVisibleItem{0U};
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
