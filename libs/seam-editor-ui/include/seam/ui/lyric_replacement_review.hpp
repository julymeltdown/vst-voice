#pragma once

#include "seam/ui/note_search_model.hpp"
#include "seam/ui/dependent_edit_review.hpp"
#include <span>
#include <utility>

namespace seam::ui {

using ReplacementDependencyKind = DependentEditKind;
using ReplacementDependencyOutcome = DependentEditOutcome;

// Owns full text and canonical dry-run outcomes. Pagination limits materialized
// UI rows, never the edit set. Off-thread callers must supply a privately owned
// session snapshot, never race the live editor. Canonical staging is not
// mid-call cancellable; preparation checks cancellation before publication.
class LyricReplacementReview final {
public:
  static constexpr std::size_t pageSize = 6U;
  static constexpr std::size_t maximumDependencyRecords = maximumDependencyReviewRecords;
  enum class State { Ready, Applied, Cancelled };

  [[nodiscard]] static core::Result<LyricReplacementReview> prepare(
      const application::EditorSession& session, domain::RegionId regionId,
      std::string_view query, std::string_view replacement, std::stop_token stop = {}) {
    return prepare(session.project(), regionId, session.revision(), query, replacement, stop);
  }
  [[nodiscard]] static core::Result<LyricReplacementReview> prepare(
      const domain::Project& project, domain::RegionId regionId, std::uint64_t revision,
      std::string_view query, std::string_view replacement, std::stop_token stop = {},
      const std::optional<std::vector<domain::NoteId>>& distributionTargets = std::nullopt) {
    auto preview = distributionTargets ? NoteSearchModel::previewLyricDistribution(project, regionId, revision, *distributionTargets, replacement, stop) :
        NoteSearchModel::previewLyricReplacement(project, regionId, revision, query, replacement, stop);
    if (!preview) return core::Result<LyricReplacementReview>{preview.error()};
    const auto& source = *project.findRegion(regionId);
    if (preview.value().retainedDependencyRecords() > maximumDependencyRecords)
      return core::failure<LyricReplacementReview>(core::ErrorCode::InvalidArgument, "Replacement review exceeds 30000 dependency records");
    const auto valid = source.validate();
    if (!valid) return core::Result<LyricReplacementReview>{valid.error()};
    if (stop.stop_requested()) return core::failure<LyricReplacementReview>(core::ErrorCode::Conflict, "Replacement review cancelled");
    LyricReplacementReview review{project.id(), revision, source, std::move(preview.value())};
    auto staged = project;
    if (!review.preview_.edits().empty()) {
      application::BatchSetLyricsCommand command{review.preview_.edits()};
      const auto result = command.apply(staged);
      if (!result) return core::Result<LyricReplacementReview>{result.error()};
    }
    if (stop.stop_requested()) return core::failure<LyricReplacementReview>(core::ErrorCode::Conflict, "Replacement review cancelled");
    const auto& after = *staged.findRegion(regionId);
    auto outcomes = compareDependentEdits(source, after, stop);
    if (!outcomes) return core::Result<LyricReplacementReview>{outcomes.error()};
    review.outcomes_ = std::move(outcomes.value());
    if (stop.stop_requested()) return core::failure<LyricReplacementReview>(core::ErrorCode::Conflict, "Replacement review cancelled");
    return core::success(std::move(review));
  }

  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] const LyricReplacementPreview& preview() const noexcept { return preview_; }
  [[nodiscard]] std::span<const application::BatchLyricEdit> editsPage(std::size_t page) const noexcept {
    return pageOf<application::BatchLyricEdit>(preview_.edits(), page);
  }
  [[nodiscard]] std::span<const ReplacementDependencyOutcome> dependenciesPage(std::size_t page) const noexcept {
    return pageOf<ReplacementDependencyOutcome>(outcomes_, page);
  }
  [[nodiscard]] std::size_t dependencyCount() const noexcept { return outcomes_.size(); }
  [[nodiscard]] bool matches(const application::EditorSession& session, domain::RegionId selectedRegion) const {
    const auto* region = session.project().findRegion(source_.id);
    return state_ == State::Ready && session.project().id() == projectId_ &&
        session.revision() == revision_ && selectedRegion == source_.id && region && *region == source_ && preview_.matchesSelection(session);
  }
  [[nodiscard]] core::Result<void> apply(application::EditorSession& session,
      domain::RegionId selectedRegion, std::stop_token stop = {}) {
    if (!matches(session, selectedRegion))
      return core::failure(core::ErrorCode::Conflict, "Refresh the changed replacement review");
    const auto result = preview_.apply(session, stop);
    if (result) state_ = State::Applied;
    return result;
  }
  void cancel() noexcept { if (state_ == State::Ready) state_ = State::Cancelled; }

private:
  LyricReplacementReview(domain::ProjectId projectId, std::uint64_t revision, domain::VocalRegion source,
      LyricReplacementPreview preview)
      : projectId_(projectId), revision_(revision),
        source_(std::move(source)), preview_(std::move(preview)) {}
  template<class T>
  static std::span<const T> pageOf(const std::vector<T>& rows, std::size_t page) noexcept {
    if (rows.empty() || page > (rows.size() - 1U) / pageSize) return {};
    const auto offset = page * pageSize; // Multiply only after bounding the page.
    return std::span<const T>{rows}.subspan(offset, std::min(pageSize, rows.size() - offset));
  }
  domain::ProjectId projectId_;
  std::uint64_t revision_;
  domain::VocalRegion source_;
  LyricReplacementPreview preview_;
  std::vector<ReplacementDependencyOutcome> outcomes_;
  State state_{State::Ready};
};
}
