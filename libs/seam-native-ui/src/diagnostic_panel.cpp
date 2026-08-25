#include "seam/native_ui/diagnostic_panel.hpp"

#include <algorithm>
#include <utility>

namespace seam::native_ui {

void DiagnosticPanelModel::add(authoring::Diagnostic diagnostic) {
  if (!authoring::DiagnosticRegistry::validate(diagnostic)) return;
  const auto existing = std::find_if(
      entries_.begin(), entries_.end(), [&diagnostic](const auto& entry) {
        return entry.diagnostic.code == diagnostic.code &&
               entry.diagnostic.messageKey == diagnostic.messageKey;
      });
  if (existing != entries_.end()) {
    existing->diagnostic.occurrenceCount += diagnostic.occurrenceCount;
    return;
  }
  entries_.push_back(DiagnosticPanelEntry{.diagnostic = std::move(diagnostic)});
}

std::size_t DiagnosticPanelModel::errorCount() const noexcept {
  return static_cast<std::size_t>(std::count_if(
      entries_.begin(), entries_.end(), [](const auto& entry) {
        return entry.diagnostic.severity == authoring::DiagnosticSeverity::Error ||
               entry.diagnostic.severity == authoring::DiagnosticSeverity::Critical;
      }));
}

bool DiagnosticPanelModel::hasBlockingIssue() const noexcept {
  return std::any_of(entries_.begin(), entries_.end(), [](const auto& entry) {
    return entry.diagnostic.severity == authoring::DiagnosticSeverity::Critical;
  });
}

core::Result<void> DiagnosticPanelModel::activate(
    std::size_t index, authoring::DiagnosticAction action) const {
  if (index >= entries_.size()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Diagnostic panel entry does not exist");
  }
  const auto& actions = entries_[index].diagnostic.actions;
  if (std::find(actions.begin(), actions.end(), action) == actions.end()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Diagnostic action is not available for this entry");
  }
  if (actionHandler_) return actionHandler_(entries_[index].diagnostic, action);
  return core::success();
}

void DiagnosticPanelModel::dismiss(std::size_t index) {
  if (index < entries_.size()) entries_.erase(entries_.begin() +
                                              static_cast<std::ptrdiff_t>(index));
}

}
