#pragma once
#include <optional>

namespace seam::voicebank_cli {
// No value means the command belongs to another CLI workflow.
[[nodiscard]] std::optional<int> runSampleReviewCommand(int argc, char** argv);
void printSampleReviewUsage();
}
