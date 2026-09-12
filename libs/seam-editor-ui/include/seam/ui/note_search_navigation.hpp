#pragma once

#include "seam/ui/note_search_model.hpp"

namespace seam::ui {

// Captured Find results change selection only. The source receipt is validated
// but never consumed as a musical edit; repeated navigation adds no undo entry.
class NoteSearchNavigation final {
public:
  [[nodiscard]] static core::Result<NoteSearchNavigation> prepare(
      const application::EditorSession& session, domain::RegionId region,
      std::string_view query, NoteSearchField field, std::stop_token stop = {}) {
    if (stop.stop_requested()) return core::failure<NoteSearchNavigation>(core::ErrorCode::Conflict, "Find cancelled");
    auto context = session.capturePerformanceJob();
    if (!context) return core::Result<NoteSearchNavigation>{context.error()};
    auto result = NoteSearchModel::search(context.value().sourceProject(), region, session.revision(), query, field, stop);
    if (!result) return core::Result<NoteSearchNavigation>{result.error()};
    return NoteSearchNavigation{std::move(context.value()), std::move(result.value())};
  }
  [[nodiscard]] const NoteSearchResult& result() const noexcept { return result_; }
  [[nodiscard]] std::optional<std::size_t> currentIndex() const noexcept { return current_; }
  [[nodiscard]] bool matches(const application::EditorSession& session, domain::RegionId activeRegion) const {
    if (closed_ || activeRegion != result_.regionId || session.revision() != result_.revision) return false;
    return static_cast<bool>(session.validatePerformanceJob(context_));
  }
  [[nodiscard]] core::Result<domain::NoteId> select(
      application::EditorSession& session, domain::RegionId activeRegion, std::size_t index, std::stop_token stop = {}) {
    if (stop.stop_requested() || !matches(session, activeRegion))
      return core::failure<domain::NoteId>(core::ErrorCode::Conflict, "Find results are cancelled, closed or stale; refresh first");
    if (index >= result_.hits.size())
      return core::failure<domain::NoteId>(core::ErrorCode::NotFound, "Find result is unavailable");
    const auto note = result_.hits[index].noteId;
    // Validate immediately before the only publication: selection, not history.
    if (stop.stop_requested()) return core::failure<domain::NoteId>(core::ErrorCode::Conflict, "Find cancelled");
    session.selection().selectOnly(note); current_ = index; return note;
  }
  [[nodiscard]] core::Result<domain::NoteId> step(
      application::EditorSession& session, domain::RegionId activeRegion, bool backwards = false,
      std::stop_token stop = {}) {
    const auto count = result_.hits.size();
    if (count == 0U) return core::failure<domain::NoteId>(core::ErrorCode::NotFound, "No matching notes");
    const auto next = current_ ? (backwards ? (*current_ == 0U ? count - 1U : *current_ - 1U)
        : (*current_ + 1U == count ? 0U : *current_ + 1U)) : (backwards ? count - 1U : 0U);
    return select(session, activeRegion, next, stop);
  }
  void close() noexcept { closed_ = true; }
private:
  friend class NoteSearchJob;
  NoteSearchNavigation(application::PerformanceJobContext context, NoteSearchResult result)
      : context_(std::move(context)), result_(std::move(result)) {}
  application::PerformanceJobContext context_;
  NoteSearchResult result_;
  std::optional<std::size_t> current_;
  bool closed_{false};
};
}
