#pragma once

#include "seam/application/editor_session.hpp"
#include "seam/application/performance_commands.hpp"
#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/synthesis/automatic_performance.hpp"

#include <cstdint>
#include <stop_token>
#include <string>
#include <vector>

namespace seam::authoring {

// Capture, generate and adopt one automatic performance proposal.
//
// The production backend needs the score, the resolved pronunciation and the
// region's own performance revision to mean anything, so this owner captures all
// three up front: `generate()` then runs from the captured project only and is safe
// to call off the UI thread, and `apply()` refuses a session that moved since the
// capture instead of publishing a proposal computed against other material.
//
// Applying adds a Proposed take through the shared command. It never accepts one:
// acceptance stays an explicit later decision, and manual edits stay authoritative.
class AutomaticPerformanceCapture final {
public:
  [[nodiscard]] static core::Result<AutomaticPerformanceCapture> prepare(
      const application::EditorSession& session,domain::RegionId region,
      domain::PerformanceTimeRange range,
      std::vector<domain::PerformanceChannel> channels,
      domain::SingerResourceIdentity resource,std::uint64_t seed,std::string takeId);

  // True while the live session still holds the material this capture was taken
  // from. Must be checked immediately before adopting a generated proposal.
  [[nodiscard]] bool matches(const application::EditorSession& session) const;

  [[nodiscard]] core::Result<domain::PerformanceTake> generate(
      std::stop_token stop = {}) const;

  [[nodiscard]] core::Result<void> apply(const domain::PerformanceTake& take,
      application::EditorSession& session) const;

  [[nodiscard]] domain::RegionId region() const noexcept {return region_;}
  [[nodiscard]] const domain::PerformanceTimeRange& range() const noexcept {return range_;}
  [[nodiscard]] const std::vector<domain::PerformanceChannel>& channels() const noexcept {
    return channels_;
  }

private:
  AutomaticPerformanceCapture(application::PerformanceJobContext context,
      domain::RegionId region,domain::PerformanceTimeRange range,
      std::vector<domain::PerformanceChannel> channels,
      domain::SingerResourceIdentity resource,std::uint64_t seed,std::string takeId)
      : context_(std::move(context)),region_(region),range_(range),
        channels_(std::move(channels)),resource_(std::move(resource)),seed_(seed),
        takeId_(std::move(takeId)) {}

  application::PerformanceJobContext context_;
  domain::RegionId region_;
  domain::PerformanceTimeRange range_;
  std::vector<domain::PerformanceChannel> channels_;
  // The singer the surface has selected: the proposal records it so a take can
  // never be adopted for a different voice than the one it was computed for.
  domain::SingerResourceIdentity resource_;
  std::uint64_t seed_{0U};
  std::string takeId_;
};

}  // namespace seam::authoring
