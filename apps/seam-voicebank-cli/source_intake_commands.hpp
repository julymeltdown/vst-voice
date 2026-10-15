#pragma once
#include <optional>
namespace seam::voicebank_cli {
[[nodiscard]] std::optional<int> runSourceIntakeCommand(int argc, char** argv);
}
