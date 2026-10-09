#pragma once
#include <optional>

namespace seam::voicebank_cli {
// Candidate inspection, packaging and installation as distinct steps. No value means another workflow.
[[nodiscard]] std::optional<int> runCandidatePackageCommand(int argc, char** argv);
void printCandidatePackageUsage();
}
