#pragma once

#include "seam/application/editor_session.hpp"
#include "seam/application/performance_commands.hpp"
#include <span>
#include <stop_token>

namespace seam::ui {

// On inspection, absent means mixed. In a patch, absent means leave unchanged.
struct VibratoFields final {
  std::optional<bool> enabled;
  std::optional<float> startFraction, fadeInFraction, fadeOutFraction;
  std::optional<float> depthCents, periodMilliseconds, phaseTurns;
};
struct VibratoEdit final {
  domain::NoteId noteId;
  domain::NoteVibrato before, after;
};

// Explicit captured Apply to Selection: only specified fields are replaced.
class VibratoModel final {
public:
  enum class State { Ready, Applied, Cancelled };
  [[nodiscard]] static core::Result<VibratoModel> prepare(const application::EditorSession& session,
      domain::RegionId region, const VibratoFields& patch = {}, std::stop_token stop = {});
  [[nodiscard]] const VibratoFields& values() const noexcept { return values_; }
  [[nodiscard]] std::size_t selectedCount() const noexcept { return selected_.size(); }
  [[nodiscard]] std::span<const VibratoEdit> edits() const noexcept { return edits_; }
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] bool matches(const application::EditorSession& session, domain::RegionId activeRegion) const;
  [[nodiscard]] core::Result<void> apply(application::EditorSession& session, domain::RegionId activeRegion,
      std::stop_token stop = {});
  void cancel() noexcept { if (state_ == State::Ready) state_ = State::Cancelled; }
private:
  VibratoModel(application::PerformanceJobContext context, domain::RegionId region, std::uint64_t revision,
      std::vector<domain::NoteId> selected);
  application::PerformanceJobContext context_;
  domain::RegionId region_;
  std::uint64_t revision_;
  std::vector<domain::NoteId> selected_;
  VibratoFields values_;
  std::vector<VibratoEdit> edits_;
  std::vector<application::NoteExpressionEdit> expressions_;
  State state_{State::Ready};
};
}
