#pragma once
#include "seam/domain/project.hpp"
#include "seam/core/result.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/application/lyric_commands.hpp"
#include <stop_token>
#include <algorithm>

namespace seam::ui {
enum class NoteSearchField { Lyric, PronunciationHint, NoteId, GeneratedPhoneme, PronunciationDiagnostic };
struct NoteSearchHit final {
  domain::NoteId noteId;
  domain::LyricTokenId lyricId;
  time::Tick start;
  std::u32string matchedText;
  std::size_t firstMatch{0U}; // Unicode scalar offset, not UTF-8 bytes.
};
struct NoteSearchResult final {
  domain::ProjectId projectId;
  domain::RegionId regionId;
  std::uint64_t revision{0U};
  NoteSearchField field{NoteSearchField::Lyric};
  std::u32string query;
  std::size_t scannedNotes{0U};
  std::vector<NoteSearchHit> hits;
};
class LyricReplacementPreview final {
public:
  [[nodiscard]] const std::vector<application::BatchLyricEdit>& edits() const noexcept { return edits_; }
  [[nodiscard]] std::size_t matchedNotes() const noexcept { return matchedNotes_; }
  [[nodiscard]] std::size_t changedNotes() const noexcept { return changedNotes_; }
  [[nodiscard]] std::size_t replacements() const noexcept { return replacements_; }
  // Conservative region-wide dependency counts, not a claim that every record
  // will become unresolved. Canonical lyric commands perform reconciliation.
  [[nodiscard]] std::size_t retainedDependencyRecords() const noexcept { return dependencyRecords_; }
  [[nodiscard]] core::Result<void> apply(application::EditorSession& session, std::stop_token stop = {}) const;
  [[nodiscard]] bool matchesSelection(const application::EditorSession& session) const {
    if (!selectedNotes_) return true;
    auto selected = session.selection().noteIds(); std::sort(selected.begin(), selected.end());
    return selected == *selectedNotes_;
  }
private:
  friend class NoteSearchModel;
  LyricReplacementPreview() = default;
  domain::ProjectId projectId_;
  domain::RegionId regionId_;
  std::uint64_t revision_{0U};
  std::size_t matchedNotes_{0U}, changedNotes_{0U}, replacements_{0U}, dependencyRecords_{0U};
  std::vector<application::BatchLyricEdit> edits_;
  std::optional<std::vector<domain::NoteId>> selectedNotes_;
};
class NoteSearchModel final {
public:
  static constexpr std::size_t maximumNotes = 10000U;
  static constexpr std::size_t maximumQueryScalars = 256U;
  static constexpr std::size_t maximumTextScalars = 4U * 1024U * 1024U;
  // Literal, case-sensitive scalar matching. No mutation or normalization.
  // Derived fields use the region's language resolver and its stricter admission
  // limits; resolver failure rejects the search, never returns a false empty set.
  // Diagnostic search covers resolver warnings, not all application diagnostics.
  [[nodiscard]] static core::Result<NoteSearchResult> search(const domain::Project& project,
      domain::RegionId regionId, std::uint64_t revision, std::string_view utf8Query,
      NoteSearchField field = NoteSearchField::Lyric, std::stop_token stop = {});
  [[nodiscard]] static core::Result<LyricReplacementPreview> previewLyricReplacement(
      const application::EditorSession& session, domain::RegionId regionId,
      std::string_view utf8Query, std::string_view utf8Replacement, std::stop_token stop = {});
  [[nodiscard]] static core::Result<LyricReplacementPreview> previewLyricReplacement(
      const domain::Project& project, domain::RegionId regionId, std::uint64_t revision,
      std::string_view utf8Query, std::string_view utf8Replacement, std::stop_token stop = {});
  [[nodiscard]] static core::Result<LyricReplacementPreview> previewLyricDistribution(
      const domain::Project& project, domain::RegionId regionId, std::uint64_t revision,
      const std::vector<domain::NoteId>& selectedNotes, std::string_view utf8Text, std::stop_token stop = {});
};
}
