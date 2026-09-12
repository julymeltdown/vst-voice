#pragma once
#include "seam/application/editor_session.hpp"
#include "seam/authoring/japanese_reading_stage.hpp"
#include <span>

namespace seam::authoring {
struct JapaneseReadingOwner final {
  domain::NoteId note;
  domain::LyricTokenId lyric;
  std::size_t byteOffset{0U}, byteLength{0U};
  bool explicitHint{false};
};
struct JapaneseReadingBinding final {
  std::vector<domain::NoteId> notes;
  bool touchesExplicitHint{false};
  bool crossesLyrics{false};
  bool crossesUnownedText{false};
};
struct JapaneseReadingReview final {
  phonemizer::JapaneseReadingResult reading;
  std::vector<JapaneseReadingBinding> bindings;
  std::vector<phonemizer::JapaneseReadingPhoneProjection> phoneProjections;
};
class JapaneseReadingCapture final {
public:
  [[nodiscard]] static core::Result<JapaneseReadingCapture> prepare(const application::EditorSession& session,
      domain::RegionId region, std::span<const domain::NoteId> notes, StagedJapaneseReadingResource resource);
  [[nodiscard]] const std::string& source() const noexcept { return source_; }
  [[nodiscard]] const std::vector<JapaneseReadingOwner>& owners() const noexcept { return owners_; }
  [[nodiscard]] bool matches(const application::EditorSession& session, domain::RegionId region,
      const phonemizer::JapaneseReadingIdentity& currentIdentity) const;
  // Worker-safe, immutable analysis. Success is NOT authority to publish/apply:
  // owner must call matches before adopting. No command/hint mutation occurs.
  [[nodiscard]] core::Result<JapaneseReadingReview> read(std::stop_token stop = {}) const;
  [[nodiscard]] core::Result<JapaneseReadingReview> bind(phonemizer::JapaneseReadingResult result,
      std::stop_token stop = {}) const;
  // Pure validation for UI gating. This checks only the immutable review and
  // capture ownership plan; callers must still call matches() immediately
  // before mutating the live session.
  [[nodiscard]] core::Result<void> validateApplyPlan(const JapaneseReadingReview& review) const;
  // Explicit owner action: only single-note, known, hint-free projections are
  // converted to editable phone hints and persisted through one command.
  [[nodiscard]] core::Result<void> apply(JapaneseReadingReview review,
      application::EditorSession& session) const;
private:
  JapaneseReadingCapture(application::PerformanceJobContext context, std::uint64_t revision, domain::RegionId region,
      StagedJapaneseReadingResource resource, std::string source, std::vector<JapaneseReadingOwner> owners)
      : context_(std::move(context)), revision_(revision), region_(region), resource_(std::move(resource)),
        source_(std::move(source)), owners_(std::move(owners)) {}
  application::PerformanceJobContext context_;
  std::uint64_t revision_;
  domain::RegionId region_;
  StagedJapaneseReadingResource resource_;
  std::string source_;
  std::vector<JapaneseReadingOwner> owners_;
};
}
