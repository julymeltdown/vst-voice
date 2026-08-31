#include "options.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace seam::voicebank_studio_native {
namespace {

bool isSha256(std::string_view value) {
  return value.size() == 64U &&
         std::all_of(value.begin(), value.end(), [](unsigned char item) {
           return std::isxdigit(item) != 0;
         });
}

template <typename Integer>
std::optional<Integer> parseInteger(std::string_view text) {
  Integer value{};
  const auto parsed = std::from_chars(
      text.data(), text.data() + text.size(), value);
  if (text.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<float> parseFloat(std::string_view text) {
  try {
    std::size_t consumed = 0U;
    const auto value = std::stof(std::string{text}, &consumed);
    if (consumed != text.size() || !std::isfinite(value)) {
      return std::nullopt;
    }
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<voicebank_production::OperationKind> parseOperation(
    std::string_view name) {
  using Kind = voicebank_production::OperationKind;
  if (name == "channel-select") return Kind::ChannelSelect;
  if (name == "downmix") return Kind::Downmix;
  if (name == "resample") return Kind::Resample;
  if (name == "remove-dc") return Kind::RemoveDc;
  if (name == "normalize") return Kind::NormalizeGain;
  if (name == "trim") return Kind::Trim;
  if (name == "segment") return Kind::Segment;
  return std::nullopt;
}

}

void printUsage() {
  std::cout << "Usage: seam_voicebank_studio_native [--manifest PATH] [options]\n"
            << "  --production-project PATH  recover a production workspace\n"
            << "  --inventory-sha256 HEX     require the exact inventory digest\n"
            << "  --operator-id ID           bind production journal records\n"
            << "  --production-unit-index N select one required inventory row\n"
            << "  --import-take PATH         inspect and import a PCM WAV for the selected row\n"
            << "  --operation NAME          channel-select, downmix, resample, remove-dc, normalize, trim, or segment\n"
            << "  --channel-index N         channel-select parameter\n"
            << "  --target-sample-rate N    resample parameter\n"
            << "  --target-peak N           normalize parameter from 0 through 1\n"
            << "  --start-frame N           trim or segment first frame\n"
            << "  --end-frame N             trim or segment exclusive end frame\n"
            << "  --export-u57-inputs PATH   export brief and candidate template\n"
            << "  --screenshot PATH          write final PPM screenshot\n"
            << "  --auto-close-ms N          close after N milliseconds\n"
            << "  --record-ms N              record input and close after N milliseconds\n"
            << "  --window-width N           physical window width from 320 to 8192\n"
            << "  --window-height N          physical window height from 240 to 8192\n"
            << "  --force-synthetic-input    skip physical microphone capture\n";
}

std::optional<Options> parseOptions(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg{argv[index]};
    if (arg == "--help") {
      printUsage();
      return std::nullopt;
    }
    if (arg == "--manifest" && index + 1 < argc) {
      options.manifest = std::filesystem::path{argv[++index]};
    } else if (arg == "--production-project" && index + 1 < argc) {
      options.productionProject = std::filesystem::path{argv[++index]};
    } else if (arg == "--inventory-sha256" && index + 1 < argc) {
      options.inventorySha256 = argv[++index];
    } else if (arg == "--operator-id" && index + 1 < argc) {
      options.operatorId = argv[++index];
    } else if (arg == "--production-unit-index" && index + 1 < argc) {
      const auto value = parseInteger<std::uint64_t>(argv[++index]);
      if (!value || *value > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
      }
      options.productionUnitIndex = static_cast<std::size_t>(*value);
    } else if (arg == "--import-take" && index + 1 < argc) {
      options.importTake = std::filesystem::path{argv[++index]};
    } else if (arg == "--operation" && index + 1 < argc) {
      options.operationKind = parseOperation(argv[++index]);
      if (!options.operationKind.has_value()) return std::nullopt;
    } else if ((arg == "--channel-index" || arg == "--target-sample-rate" ||
                arg == "--start-frame" || arg == "--end-frame") &&
               index + 1 < argc) {
      options.operationParameterSpecified = true;
      const auto value = parseInteger<std::uint64_t>(argv[++index]);
      if (!value) return std::nullopt;
      if (arg == "--channel-index") {
        if (*value > std::numeric_limits<std::uint16_t>::max()) {
          return std::nullopt;
        }
        options.channelIndex = static_cast<std::uint16_t>(*value);
      } else if (arg == "--target-sample-rate") {
        if (*value == 0U || *value > 384000U) return std::nullopt;
        options.targetSampleRate = static_cast<std::uint32_t>(*value);
      } else if (*value > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
      } else if (arg == "--start-frame") {
        options.startFrame = static_cast<std::size_t>(*value);
      } else {
        options.endFrame = static_cast<std::size_t>(*value);
      }
    } else if (arg == "--target-peak" && index + 1 < argc) {
      options.operationParameterSpecified = true;
      const auto value = parseFloat(argv[++index]);
      if (!value || !(*value > 0.0F && *value <= 1.0F)) {
        return std::nullopt;
      }
      options.targetPeak = *value;
    } else if (arg == "--export-u57-inputs" && index + 1 < argc) {
      options.exportU57Inputs = std::filesystem::path{argv[++index]};
    } else if (arg == "--screenshot" && index + 1 < argc) {
      options.screenshot = std::filesystem::path{argv[++index]};
    } else if ((arg == "--window-width" || arg == "--window-height") &&
               index + 1 < argc) {
      const auto value = parseInteger<std::uint32_t>(argv[++index]);
      if (!value || *value < (arg == "--window-width" ? 320U : 240U) ||
          *value > 8192U) {
        return std::nullopt;
      }
      if (arg == "--window-width") options.windowWidth = *value;
      else options.windowHeight = *value;
    } else if (arg == "--force-synthetic-input") {
      options.forceSyntheticInput = true;
    } else if ((arg == "--record-ms" || arg == "--auto-close-ms") &&
               index + 1 < argc) {
      const auto value = parseInteger<std::int64_t>(argv[++index]);
      if (!value) return std::nullopt;
      if (arg == "--record-ms") {
        if (*value < 50 || *value > 300000) return std::nullopt;
        options.recordDuration = std::chrono::milliseconds{*value};
      } else {
        if (*value < 0 || *value > 600000) return std::nullopt;
        options.autoClose = std::chrono::milliseconds{*value};
      }
    } else {
      std::cerr << "Unknown argument: " << arg << '\n';
      return std::nullopt;
    }
  }
  if ((options.manifest.empty() && !options.productionProject.has_value()) ||
      (options.recordDuration.count() > 0 && options.autoClose.count() > 0)) {
    return std::nullopt;
  }
  const auto completeProductionInput = options.productionProject.has_value() &&
      isSha256(options.inventorySha256) && !options.operatorId.empty();
  const auto anyProductionInput = options.productionProject.has_value() ||
      !options.inventorySha256.empty() || !options.operatorId.empty() ||
      options.exportU57Inputs.has_value() || options.productionUnitIndex.has_value() ||
      options.importTake.has_value() || options.operationKind.has_value() ||
      options.operationParameterSpecified;
  if (anyProductionInput && !completeProductionInput) return std::nullopt;
  if (options.operationParameterSpecified && !options.operationKind.has_value()) {
    return std::nullopt;
  }
  return options;
}

}
