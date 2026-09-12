#pragma once

#include "seam/ui/vibrato_model.hpp"
#include <array>

namespace seam::native_ui {
enum class VibratoField : std::size_t { Enabled, Start, FadeIn, FadeOut, Depth, Period, Phase, Count };

// Draft form state for the existing native inspector. Text input never commits
// a musical command; explicit Apply publishes one validated selection patch.
class VibratoInspectorDraft final {
public:
  [[nodiscard]] static core::Result<VibratoInspectorDraft> prepare(
      const application::EditorSession& session, domain::RegionId region);
  [[nodiscard]] std::string text(VibratoField field) const;
  [[nodiscard]] bool mixed(VibratoField field) const noexcept;
  [[nodiscard]] bool current(const application::EditorSession& session, domain::RegionId activeRegion) const { return !closed_ && source_.matches(session, activeRegion); }
  [[nodiscard]] static std::string_view label(VibratoField field) noexcept;
  [[nodiscard]] const std::string& error() const noexcept { return error_; }
  [[nodiscard]] std::size_t selectedCount() const noexcept { return source_.selectedCount(); }
  [[nodiscard]] std::size_t changedCount() const noexcept { return preview_ ? preview_->edits().size() : 0U; }
  [[nodiscard]] bool canApply(const application::EditorSession& session, domain::RegionId activeRegion) const;
  [[nodiscard]] core::Result<void> set(const application::EditorSession& session, domain::RegionId activeRegion, VibratoField field, std::string text);
  [[nodiscard]] core::Result<void> reset(const application::EditorSession& session, domain::RegionId activeRegion, VibratoField field);
  [[nodiscard]] core::Result<void> apply(application::EditorSession& session, domain::RegionId activeRegion);
  void cancel() noexcept;
private:
  VibratoInspectorDraft(ui::VibratoModel source, domain::RegionId region);
  [[nodiscard]] core::Result<void> rebuild(const application::EditorSession& session);
  ui::VibratoModel source_;
  domain::RegionId region_;
  std::optional<ui::VibratoModel> preview_;
  std::array<std::optional<std::string>, static_cast<std::size_t>(VibratoField::Count)> input_;
  std::string error_;
  bool closed_{false};
};
}
