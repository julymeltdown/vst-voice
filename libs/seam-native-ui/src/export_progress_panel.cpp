#include "seam/native_ui/export_progress_panel.hpp"

#include <algorithm>

namespace seam::native_ui {

double ExportProgressPanelModel::fraction() const noexcept {
  const auto progress = this->progress();
  if (progress.totalFiles == 0U) return 0.0;
  return std::clamp(static_cast<double>(progress.completedFiles) /
                        static_cast<double>(progress.totalFiles),
                    0.0, 1.0);
}

bool ExportProgressPanelModel::cancellable() const noexcept {
  const auto progress = this->progress();
  return progress.state == authoring::ExportState::Preflight ||
         progress.state == authoring::ExportState::Staging ||
         progress.state == authoring::ExportState::Prepared;
}

}
