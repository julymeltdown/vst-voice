#include "seam/native_ui/style_coverage_sheet.hpp"
#include "seam/application/performance_commands.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include <algorithm>
#include <unordered_map>

namespace seam::native_ui {
StyleCoverageSheet::StyleCoverageSheet(application::PerformanceJobContext context, std::uint64_t revision,
    domain::TrackId track, domain::RegionId region, voicebank::Manifest manifest, domain::VoiceStyleSelection selection)
    : context_(std::move(context)), revision_(revision), track_(track), region_(region),
      manifest_(std::move(manifest)), selection_(std::move(selection)) {
  std::unordered_map<std::string_view, std::pair<std::size_t, std::size_t>> inventory;
  for (const auto& unit : manifest_.units) {
    auto& counts = inventory[unit.style];
    if (unit.enabled) ++counts.first; else ++counts.second;
    coverageWorkPerToken_ += std::max(std::size_t{1U}, unit.phones.size());
  }
  for (const auto& id : manifest_.styles) {
    const auto counts = inventory.find(id);
    styles_.push_back({id, counts == inventory.end() ? 0U : counts->second.first,
        counts == inventory.end() ? 0U : counts->second.second});
  }
  refreshCoverage();
}
core::Result<StyleCoverageSheet> StyleCoverageSheet::prepare(const application::EditorSession& session,
    domain::TrackId trackId, domain::RegionId regionId, const voicebank::VoicebankResolution& bank) {
  const auto* track = session.project().findVocalTrack(trackId);
  if (!track || !track->findRegion(regionId) || track->findRegion(regionId)->notes.size() > 10000U)
    return core::failure<StyleCoverageSheet>(core::ErrorCode::InvalidArgument, "Choose a vocal region with at most 10000 notes");
  if (track->proceduralRecipe)
    return core::failure<StyleCoverageSheet>(core::ErrorCode::Unsupported, "Procedural styles use recipe controls, not sample-bank style selection");
  if (!bank.resolved() || bank.candidate->manifest.styles.size() > 256U || bank.candidate->manifest.units.size() > 16384U)
    return core::failure<StyleCoverageSheet>(core::ErrorCode::Unsupported, "Resolve a bank within the sheet's 256-style and 16384-unit limits");
  std::size_t phoneBudget = 65536U;
  for (const auto& style : bank.candidate->manifest.styles) if (style.size() > 256U)
    return core::failure<StyleCoverageSheet>(core::ErrorCode::Unsupported, "Style identifier exceeds the sheet text limit");
  for (const auto& unit : bank.candidate->manifest.units) {
    if (unit.phones.size() > phoneBudget) return core::failure<StyleCoverageSheet>(core::ErrorCode::Unsupported, "Unit phone inventory exceeds the sheet limit");
    phoneBudget -= unit.phones.size();
  }
  const auto resolved = voicebank::resolveVoiceStyle(track->voicebank, track->styleSelection, bank);
  if (!resolved) return core::Result<StyleCoverageSheet>{resolved.error()};
  if (resolved.value().status == voicebank::VoiceStyleStatus::BankUnresolved)
    return core::failure<StyleCoverageSheet>(core::ErrorCode::Conflict, "Style editing requires the exact trusted bank version and content hash");
  auto context = session.capturePerformanceJob(); if (!context) return core::Result<StyleCoverageSheet>{context.error()};
  return StyleCoverageSheet{std::move(context.value()), session.revision(), trackId, regionId,
      bank.candidate->manifest, resolved.value().selection};
}
void StyleCoverageSheet::refreshCoverage() {
  coverage_.reset(); diagnostic_.clear();
  if (std::find(manifest_.styles.begin(), manifest_.styles.end(), selection_.styleId) == manifest_.styles.end()) {
    diagnostic_ = "Choose a declared style; no fallback style is applied"; return;
  }
  const auto* region = context_.sourceProject().findRegion(region_);
  // The captured region is immutable for the entire draft. A style choice does
  // not alter pronunciation; retain its result (including failures) once.
  if (!pronunciationPrepared_) {
    const auto pronunciation = phonemizer::resolvePronunciationForLanguage(
        *region, manifest_.language);
    if (!pronunciation) pronunciationDiagnostic_ =
        "Region coverage is unavailable for this language adapter: " +
        pronunciation.error().message;
    else if (!pronunciation.value().pronunciation.warnings.empty()) pronunciationDiagnostic_ =
        "Resolve pronunciation warnings before trusting coverage: " + pronunciation.value().pronunciation.warnings.front().message;
    else if (pronunciation.value().pronunciation.tokens.empty()) pronunciationDiagnostic_ =
        "No phonemes to assess; inventory is not singing-readiness evidence";
    else pronunciationTokens_ = pronunciation.value().pronunciation.tokens;
    pronunciationPrepared_ = true;
  }
  if (!pronunciationDiagnostic_.empty()) { diagnostic_ = pronunciationDiagnostic_; return; }
  if (pronunciationTokens_.size() > 4096U || (coverageWorkPerToken_ > 0U && pronunciationTokens_.size() > 4'194'304U / coverageWorkPerToken_)) {
    diagnostic_ = "Coverage query exceeds the bounded sheet workload; style selection remains available"; return;
  }
  coverage_ = voicebank::VoicebankCoverageAnalyzer::analyzeRegion(manifest_, track_, *region, pronunciationTokens_, selection_.styleId);
  diagnostic_ = "Structural coverage only; source audio, alignment, renderer and listening approval are separate";
}
bool StyleCoverageSheet::hasChanges() const noexcept {
  return selection_ != context_.sourceProject().findVocalTrack(track_)->styleSelection;
}
core::Result<StyleCoverageSheet> StyleCoverageSheet::prepare(const application::EditorSession& session,
    domain::TrackId track, domain::RegionId region, authoring::VoicebankSnapshotPtr bank) {
  if (!bank) return core::failure<StyleCoverageSheet>(core::ErrorCode::Conflict, "Voicebank snapshot is unavailable");
  auto result = prepare(session, track, region, bank->resolution());
  if (result) result.value().bankSnapshot_ = std::move(bank);
  return result;
}
bool StyleCoverageSheet::matches(const application::EditorSession& session, domain::TrackId track,
    domain::RegionId region, const authoring::VoicebankSnapshotPtr& bank) const {
  return bank && bankSnapshot_ == bank && state_ == State::Ready && track == track_ && region == region_ &&
      session.revision() == revision_ && session.project() == context_.sourceProject() &&
      static_cast<bool>(session.validatePerformanceJob(context_));
}
core::Result<void> StyleCoverageSheet::apply(application::EditorSession& session, domain::TrackId track,
    domain::RegionId region, const authoring::VoicebankSnapshotPtr& bank) {
  if (!matches(session, track, region, bank)) return core::failure(core::ErrorCode::Conflict, "Voicebank snapshot changed; Refresh the sheet");
  // Commit still performs the full manifest/trust/source validation. Only
  // routine view/choice eligibility uses the immutable snapshot fast path.
  return apply(session, track, region, bank->resolution());
}
core::Result<void> StyleCoverageSheet::choose(std::string style) {
  if (state_ != State::Ready) return core::failure(core::ErrorCode::Conflict, "Style sheet is closed");
  if (std::find(manifest_.styles.begin(), manifest_.styles.end(), style) == manifest_.styles.end())
    return core::failure(core::ErrorCode::NotFound, "Style is not declared by the captured bank");
  const bool changed = selection_.styleId != style;
  selection_ = {domain::VoiceStyleOrigin::Explicit, std::move(style)};
  if (changed) refreshCoverage();
  return core::success();
}
bool StyleCoverageSheet::matches(const application::EditorSession& session, domain::TrackId track,
    domain::RegionId region, const voicebank::VoicebankResolution& bank) const {
  if (state_ != State::Ready || track != track_ || region != region_ || session.revision() != revision_ ||
      !bank.resolved() || bank.candidate->manifest != manifest_) return false;
  const auto resolved = voicebank::resolveVoiceStyle(context_.sourceProject().findVocalTrack(track_)->voicebank, selection_, bank);
  return resolved && resolved.value().status != voicebank::VoiceStyleStatus::BankUnresolved &&
      session.project() == context_.sourceProject() && static_cast<bool>(session.validatePerformanceJob(context_));
}
core::Result<void> StyleCoverageSheet::apply(application::EditorSession& session, domain::TrackId track,
    domain::RegionId region, const voicebank::VoicebankResolution& bank) {
  if (!matches(session, track, region, bank) || std::find(manifest_.styles.begin(), manifest_.styles.end(), selection_.styleId) == manifest_.styles.end())
    return core::failure(core::ErrorCode::Conflict, "Style source changed or a declared style has not been chosen");
  if (hasChanges()) {
    const auto result = session.executePerformanceResult(context_, std::make_unique<application::EditPerformanceCommand>(
        std::vector<application::NoteExpressionEdit>{}, std::vector<application::RegionDynamicsEdit>{},
        std::vector<application::TrackStyleEdit>{{track_, selection_}}));
    if (!result) return result;
  }
  state_ = State::Applied; return core::success();
}
}
