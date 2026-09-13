#pragma once
#include <optional>
namespace seam::voicebank_cli {
[[nodiscard]] std::optional<int> runCoverageCommand(int argc, char** argv);
void printCoverageUsage();
}

