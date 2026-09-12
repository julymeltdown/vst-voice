#pragma once

#include "seam/native_ui/diagnostic_panel.hpp"
#include "seam/native_ui/diagnostic_presentation.hpp"
#include "seam/domain/note.hpp"
#include <optional>
#include <stop_token>
#include <initializer_list>
#include <span>

namespace seam::native_ui {

enum class DiagnosticSearchField { Code, Title, Impact, MessageKey, AffectedId, Severity, Action, Detail };
struct DiagnosticSearchHit final {
  std::size_t sourceIndex;
  DiagnosticSearchField field;
  std::size_t fieldIndex;
  std::u32string matchedText;
  std::size_t firstMatch;
};

// Read-only diagnostic results, deliberately not NoteSearchHits. Opaque IDs and
// global issues never become guessed note targets or automatic recovery actions.
class DiagnosticSearchSnapshot final {
public:
  static constexpr std::size_t maximumDiagnostics = 10000U;
  static constexpr std::size_t maximumScalars = 4U * 1024U * 1024U;
  // Owner-thread preflight/copy: no action handler crosses to a search worker.
  [[nodiscard]] static core::Result<std::vector<DiagnosticPanelEntry>> capture(const DiagnosticPanelModel& panel) {
    if (panel.entries().size() > maximumDiagnostics)
      return core::failure<std::vector<DiagnosticPanelEntry>>(core::ErrorCode::InvalidArgument, "Diagnostic entry capture exceeds bounds");
    std::size_t bytes = 0U;
    const auto account = [&](std::string_view value) {
      if (value.size() > 65536U || value.size() > maximumScalars * 4U - bytes) return false;
      bytes += value.size(); return true;
    };
    for (const auto& entry : panel.entries()) {
      const auto& diagnostic = entry.diagnostic;
      if (diagnostic.actions.size() > 32U || diagnostic.affectedIds.size() > 32U ||
          !account(diagnostic.code) || !account(diagnostic.messageKey) || !account(diagnostic.detail) || !account(diagnostic.detailSourceHash))
        return core::failure<std::vector<DiagnosticPanelEntry>>(core::ErrorCode::InvalidArgument, "Diagnostic capture exceeds field bounds");
      for (const auto& id : diagnostic.affectedIds) if (!account(id))
        return core::failure<std::vector<DiagnosticPanelEntry>>(core::ErrorCode::InvalidArgument, "Diagnostic capture exceeds text bounds");
      const auto valid = authoring::DiagnosticRegistry::validate(diagnostic);
      if (!valid) return core::Result<std::vector<DiagnosticPanelEntry>>{valid.error()};
    }
    return panel.entries();
  }
  [[nodiscard]] static core::Result<DiagnosticSearchSnapshot> prepare(
      const DiagnosticPanelModel& panel, std::string_view query, std::stop_token stop = {}) {
    return prepare(std::span<const DiagnosticPanelEntry>{panel.entries()}, query, stop);
  }
  [[nodiscard]] static core::Result<DiagnosticSearchSnapshot> prepare(
      std::span<const DiagnosticPanelEntry> entries, std::string_view query, std::stop_token stop = {}) {
    const auto cancelled = [] { return core::failure<DiagnosticSearchSnapshot>(core::ErrorCode::Conflict, "Diagnostic search cancelled"); };
    if (stop.stop_requested()) return cancelled();
    if (entries.size() > maximumDiagnostics || query.empty() || query.size() > 1024U)
      return core::failure<DiagnosticSearchSnapshot>(core::ErrorCode::InvalidArgument, "Diagnostic search exceeds query or entry bounds");
    auto decodedQuery = domain::fromUtf8(std::string{query});
    if (!decodedQuery) return core::Result<DiagnosticSearchSnapshot>{decodedQuery.error()};
    if (decodedQuery.value().empty() || decodedQuery.value().size() > 256U)
      return core::failure<DiagnosticSearchSnapshot>(core::ErrorCode::InvalidArgument, "Diagnostic query must contain 1 to 256 characters");
    DiagnosticSearchSnapshot snapshot; snapshot.query_ = std::move(decodedQuery.value());
    const auto& needle = snapshot.query_; std::vector<std::size_t> prefix(needle.size(), 0U);
    for (std::size_t i = 1U, j = 0U; i < needle.size(); ++i) {
      while (j && needle[i] != needle[j]) j = prefix[j - 1U];
      if (needle[i] == needle[j]) ++j;
      prefix[i] = j;
    }
    std::size_t scalars = 0U;
    for (const auto& entry : entries) {
      if (stop.stop_requested()) return cancelled();
      const auto& diagnostic = entry.diagnostic;
      if (diagnostic.actions.size() > 32U)
        return core::failure<DiagnosticSearchSnapshot>(core::ErrorCode::InvalidArgument, "Diagnostic actions exceed search bounds");
      const auto valid = authoring::DiagnosticRegistry::validate(diagnostic);
      if (!valid) return core::Result<DiagnosticSearchSnapshot>{valid.error()};
      std::optional<DiagnosticSearchHit> hit;
      const auto scan = [&](std::string_view value, DiagnosticSearchField field, std::size_t fieldIndex = 0U) -> core::Result<void> {
        if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Diagnostic search cancelled");
        if (value.size() > 65536U)
          return core::failure(core::ErrorCode::InvalidArgument, "Diagnostic field exceeds search bounds");
        auto decoded = domain::fromUtf8(std::string{value}); if (!decoded) return core::Result<void>{decoded.error()};
        if (decoded.value().size() > maximumScalars - scalars)
          return core::failure(core::ErrorCode::InvalidArgument, "Diagnostic text exceeds the 4M-character search budget");
        scalars += decoded.value().size();
        for (std::size_t i = 0U, j = 0U; i < decoded.value().size(); ++i) {
          if ((i & 255U) == 0U && stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Diagnostic search cancelled");
          if (hit) continue; // Still decode/budget/check cancellation for every later field.
          while (j && decoded.value()[i] != needle[j]) j = prefix[j - 1U];
          if (decoded.value()[i] == needle[j]) ++j;
          if (j == needle.size()) hit.emplace(DiagnosticSearchHit{snapshot.source_.size(), field, fieldIndex, decoded.value(), i + 1U - j});
        }
        return core::success();
      };
      const auto presentation = presentDiagnostic(diagnostic);
      for (const auto& [value, field] : std::initializer_list<std::pair<std::string_view, DiagnosticSearchField>>{
          {diagnostic.code, DiagnosticSearchField::Code}, {presentation.title, DiagnosticSearchField::Title},
          {presentation.impact, DiagnosticSearchField::Impact}, {diagnostic.messageKey, DiagnosticSearchField::MessageKey},
          {diagnostic.detail, DiagnosticSearchField::Detail},
          {authoring::toString(diagnostic.severity), DiagnosticSearchField::Severity}}) {
        const auto result = scan(value, field); if (!result) return core::Result<DiagnosticSearchSnapshot>{result.error()};
      }
      for (std::size_t i = 0U; i < diagnostic.affectedIds.size(); ++i) {
        const auto result = scan(diagnostic.affectedIds[i], DiagnosticSearchField::AffectedId, i);
        if (!result) return core::Result<DiagnosticSearchSnapshot>{result.error()};
      }
      for (std::size_t i = 0U; i < diagnostic.actions.size(); ++i) {
        const auto result = scan(diagnosticActionLabel(diagnostic.actions[i]), DiagnosticSearchField::Action, i);
        if (!result) return core::Result<DiagnosticSearchSnapshot>{result.error()};
      }
      snapshot.source_.push_back(diagnostic);
      if (hit) snapshot.hits_.push_back(std::move(*hit));
    }
    if (stop.stop_requested()) return cancelled();
    return snapshot;
  }
  [[nodiscard]] const std::vector<DiagnosticSearchHit>& hits() const noexcept { return hits_; }
  [[nodiscard]] const std::vector<authoring::Diagnostic>& source() const noexcept { return source_; }
  [[nodiscard]] bool matches(const DiagnosticPanelModel& panel) const noexcept {
    if (panel.entries().size() != source_.size()) return false;
    for (std::size_t i = 0U; i < source_.size(); ++i)
      if (!source_[i].sameIssueAs(panel.entries()[i].diagnostic) || source_[i].occurrenceCount != panel.entries()[i].diagnostic.occurrenceCount) return false;
    return true;
  }
private:
  DiagnosticSearchSnapshot() = default;
  std::u32string query_;
  std::vector<authoring::Diagnostic> source_;
  std::vector<DiagnosticSearchHit> hits_;
};
}
