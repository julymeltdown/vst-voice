#pragma once
#include "seam/application/editor_session.hpp"
#include "seam/voicebank/coverage.hpp"
#include "seam/voicebank/style_resolution.hpp"
#include "seam/authoring/voicebank_snapshot.hpp"

namespace seam::native_ui {
class StyleCoverageSheet final {
public:
  struct StyleRow { std::string id; std::size_t enabled{0U}, disabled{0U}; };
  enum class State { Ready, Applied, Cancelled };
  [[nodiscard]] static core::Result<StyleCoverageSheet> prepare(const application::EditorSession& session,
      domain::TrackId track, domain::RegionId region, const voicebank::VoicebankResolution& bank);
  [[nodiscard]] static core::Result<StyleCoverageSheet> prepare(const application::EditorSession& session,
      domain::TrackId track, domain::RegionId region, authoring::VoicebankSnapshotPtr bank);
  [[nodiscard]] bool matches(const application::EditorSession& session, domain::TrackId track,
      domain::RegionId region, const authoring::VoicebankSnapshotPtr& bank) const;
  [[nodiscard]] core::Result<void> apply(application::EditorSession& session, domain::TrackId track,
      domain::RegionId region, const authoring::VoicebankSnapshotPtr& bank);
  [[nodiscard]] const std::vector<StyleRow>& styles() const noexcept { return styles_; }
  [[nodiscard]] const domain::VoiceStyleSelection& selection() const noexcept { return selection_; }
  [[nodiscard]] const std::optional<voicebank::VoicebankCoverageReport>& coverage() const noexcept { return coverage_; }
  [[nodiscard]] const std::optional<voicebank::VoicebankCoverageReport>& secondaryCoverage() const noexcept { return secondaryCoverage_; }
  [[nodiscard]] const std::string& diagnostic() const noexcept { return diagnostic_; }
  [[nodiscard]] bool hasChanges() const noexcept;
  [[nodiscard]] core::Result<void> choose(std::string style);
  [[nodiscard]] core::Result<void> chooseBlend(std::string secondary, float amount);
  [[nodiscard]] core::Result<void> clearBlend();
  [[nodiscard]] bool matches(const application::EditorSession& session, domain::TrackId track,
      domain::RegionId region, const voicebank::VoicebankResolution& bank) const;
  [[nodiscard]] core::Result<void> apply(application::EditorSession& session, domain::TrackId track,
      domain::RegionId region, const voicebank::VoicebankResolution& bank);
  void cancel() noexcept { if (state_ == State::Ready) state_ = State::Cancelled; }
private:
  StyleCoverageSheet(application::PerformanceJobContext context, std::uint64_t revision, domain::TrackId track,
      domain::RegionId region, voicebank::Manifest manifest, domain::VoiceStyleSelection selection);
  void refreshCoverage();
  application::PerformanceJobContext context_;
  std::uint64_t revision_;
  domain::TrackId track_;
  domain::RegionId region_;
  voicebank::Manifest manifest_;
  domain::VoiceStyleSelection selection_;
  std::vector<StyleRow> styles_;
  std::optional<voicebank::VoicebankCoverageReport> coverage_;
  std::optional<voicebank::VoicebankCoverageReport> secondaryCoverage_;
  std::string diagnostic_;
  std::size_t coverageWorkPerToken_{0U};
  bool pronunciationPrepared_{false};
  std::vector<domain::PhonemeToken> pronunciationTokens_;
  std::string pronunciationDiagnostic_;
  State state_{State::Ready};
  authoring::VoicebankSnapshotPtr bankSnapshot_;
};
}
