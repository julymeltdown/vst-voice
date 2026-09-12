#pragma once
#include <optional>
namespace seam::voicebank_cli {
[[nodiscard]] std::optional<int> runCampaignCommand(int argc, char** argv);
void printCampaignUsage();
}
