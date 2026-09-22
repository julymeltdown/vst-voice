#pragma once

#include "seam/authoring/interchange_service.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace seam::native_ui {

// A read-only view over an admitted import draft. The draft must outlive this
// model and remain unchanged while it is displayed. Rows borrow their strings:
// displaying a large report never constructs a second copy of every issue.
class ConversionReviewModel final {
public:
  explicit ConversionReviewModel(
      const authoring::InterchangeImportDraft& draft) noexcept;
  ConversionReviewModel(authoring::InterchangeImportDraft&&) = delete;
  ConversionReviewModel(const authoring::InterchangeImportDraft&&) = delete;

  [[nodiscard]] std::string_view formatName() const noexcept;
  [[nodiscard]] std::size_t issueCount() const noexcept;
  [[nodiscard]] const authoring::InterchangeIssue* issue(
      std::size_t index) const noexcept;
  [[nodiscard]] std::size_t warningCount() const noexcept { return warnings_; }
  [[nodiscard]] std::size_t lossCount() const noexcept { return losses_; }
  [[nodiscard]] bool hasLosses() const noexcept { return losses_ != 0U; }
  [[nodiscard]] std::size_t trackCount() const noexcept { return tracks_; }
  [[nodiscard]] std::size_t regionCount() const noexcept { return regions_; }
  [[nodiscard]] std::size_t noteCount() const noexcept { return notes_; }
  // Incomplete identity, not an installed-resource lookup. Even a complete
  // reference still needs resolution by the regular singer selection path.
  [[nodiscard]] std::size_t unresolvedSingerCount() const noexcept {
    return unresolvedSingers_;
  }
  [[nodiscard]] const std::filesystem::path& sourcePath() const noexcept;
  [[nodiscard]] std::string_view sourceHash() const noexcept;
  [[nodiscard]] std::string summary() const;
  [[nodiscard]] std::string singerDisclosure() const;
  [[nodiscard]] std::string sourceDetails() const;
  // No ellipsis or byte clipping: the selected row is the full loss record,
  // including arbitrarily long (within import budgets) Unicode path/message.
  [[nodiscard]] std::string issueDetails(std::size_t index) const;

private:
  const authoring::InterchangeImportDraft& draft_;
  std::size_t warnings_{0U};
  std::size_t losses_{0U};
  std::size_t tracks_{0U};
  std::size_t regions_{0U};
  std::size_t notes_{0U};
  std::size_t unresolvedSingers_{0U};
};

}  // namespace seam::native_ui
