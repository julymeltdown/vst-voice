#pragma once

#include "seam/authoring/diagnostic.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace seam::native_ui {

struct DiagnosticPanelEntry final {
  authoring::Diagnostic diagnostic;
  bool expanded{false};
};

class DiagnosticPanelModel final {
public:
  using ActionHandler = std::function<core::Result<void>(
      const authoring::Diagnostic&, authoring::DiagnosticAction)>;

  void add(authoring::Diagnostic diagnostic);
  void clear() noexcept { entries_.clear(); }
  void setActionHandler(ActionHandler handler) { actionHandler_ = std::move(handler); }
  void dismiss(std::size_t index);
  [[nodiscard]] const std::vector<DiagnosticPanelEntry>& entries() const noexcept {
    return entries_;
  }
  [[nodiscard]] std::size_t errorCount() const noexcept;
  [[nodiscard]] bool hasBlockingIssue() const noexcept;
  [[nodiscard]] core::Result<void> activate(
      std::size_t index, authoring::DiagnosticAction action) const;

private:
  std::vector<DiagnosticPanelEntry> entries_;
  ActionHandler actionHandler_;
};

}
