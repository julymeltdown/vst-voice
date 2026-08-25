#pragma once

#include <string>

namespace seam::native_ui {

enum class UpdatePanelState { Idle, Checking, Available, Blocked, Staged };

struct UpdatePanelView final {
  UpdatePanelState state{UpdatePanelState::Idle};
  std::string targetVersion;
  std::string targetBuild;
  std::string diagnostic;
  bool explicitConfirmationRequired{false};
};

class UpdatePanelModel final {
public:
  void update(UpdatePanelView view) noexcept { view_ = std::move(view); }
  [[nodiscard]] const UpdatePanelView& view() const noexcept { return view_; }

private:
  UpdatePanelView view_;
};

}
