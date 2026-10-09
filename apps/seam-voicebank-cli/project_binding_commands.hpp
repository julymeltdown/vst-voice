#pragma once
#include <optional>
namespace seam::voicebank_cli {
[[nodiscard]] std::optional<int> runProjectBindingCommand(int argc, char** argv);
void printProjectBindingUsage();
}
