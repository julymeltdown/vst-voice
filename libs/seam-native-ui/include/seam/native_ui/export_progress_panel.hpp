#pragma once

#include "seam/authoring/export_service.hpp"

#include <mutex>
#include <string>

namespace seam::native_ui {

class ExportProgressPanelModel final {
public:
  void update(const authoring::ExportProgress& progress) noexcept {
    std::lock_guard lock(mutex_);
    progress_ = progress;
  }
  void requestCancel() noexcept {
    std::lock_guard lock(mutex_);
    cancelRequested_ = true;
  }
  void reset() noexcept {
    std::lock_guard lock(mutex_);
    progress_ = {};
    cancelRequested_ = false;
  }
  [[nodiscard]] authoring::ExportProgress progress() const noexcept {
    std::lock_guard lock(mutex_);
    return progress_;
  }
  [[nodiscard]] bool cancelRequested() const noexcept {
    std::lock_guard lock(mutex_);
    return cancelRequested_;
  }
  [[nodiscard]] double fraction() const noexcept;
  [[nodiscard]] bool cancellable() const noexcept;

private:
  mutable std::mutex mutex_;
  authoring::ExportProgress progress_;
  bool cancelRequested_{false};
};

}
