#pragma once
#include "seam/authoring/generation_job.hpp"

namespace seam::authoring {
// Which of the producer's own declared classes its recipe can actually prepare,
// and which phones and kinds nothing covers. This is the coverage report the plan
// requires before thousands of jobs run: it compiles every assignment's score and
// snapshot, retains the refusal message for each class that cannot be prepared,
// and never renders audio, collects a take or reserves an assignment.
struct InventoryCoverageLimits final {
  std::size_t maximumAssignments{16384U};
};
struct InventoryCoverageEntry final {
  std::string takeId, coverageKey, style, status, detail;
  std::int64_t pitchLayer{0};
  std::uint64_t frameCount{0U};
};
struct InventoryCoverageReport final {
  std::size_t assignments{0U}, prepared{0U}, refused{0U}, phones{0U}, kinds{0U};
  std::vector<InventoryCoverageEntry> entries;
  std::vector<std::string> missingPhones, missingKinds, refusedClasses;
  bool complete{false};
  // The exact canonical bytes a caller writes to retain this report.
  std::string json;
};
[[nodiscard]] core::Result<InventoryCoverageReport> inspectInventoryCoverage(
    const voicebank_production::VoicebankProductionProject& producer,
    const synthesis::ProceduralSingerResource& recipe,
    InventoryCoverageLimits limits = {}, std::stop_token stop = {});
}

