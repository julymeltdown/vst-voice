#pragma once

#include "seam/voicebank_production/operations.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace seam::voicebank_studio_native {

struct Options final {
  std::filesystem::path manifest;
  std::optional<std::filesystem::path> productionProject;
  std::optional<std::filesystem::path> exportU57Inputs;
  std::string inventorySha256;
  std::string operatorId;
  std::optional<std::size_t> productionUnitIndex;
  std::optional<std::filesystem::path> importTake;
  std::optional<voicebank_production::OperationKind> operationKind;
  std::uint16_t channelIndex{0U};
  std::uint32_t targetSampleRate{48000U};
  float targetPeak{0.9F};
  std::size_t startFrame{0U};
  std::size_t endFrame{0U};
  bool operationParameterSpecified{false};
  std::optional<std::filesystem::path> screenshot;
  std::chrono::milliseconds autoClose{0};
  std::chrono::milliseconds recordDuration{0};
  std::uint32_t windowWidth{1440U};
  std::uint32_t windowHeight{900U};
  bool forceSyntheticInput{false};
};

void printUsage();
[[nodiscard]] std::optional<Options> parseOptions(int argc, char** argv);

}
