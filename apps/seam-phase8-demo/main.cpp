#include "seam/platform/platform_capabilities.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>

namespace {
std::optional<std::filesystem::path> outputPath(int argc, char** argv) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (std::string_view{argv[index]} == "--output") {
      return std::filesystem::path{argv[index + 1]};
    }
  }
  return std::nullopt;
}
}

int main(int argc, char** argv) {
  const auto output = outputPath(argc, argv);
  if (!output.has_value()) {
    std::cerr << "Usage: seam_phase8_demo --output DIRECTORY\n";
    return 2;
  }
  std::error_code error;
  std::filesystem::create_directories(*output, error);
  if (error) return 3;
  constexpr auto capabilities = seam::platform::currentPlatformCapabilities();
  std::ofstream summary(*output / "phase8-platform-capabilities.json",
                        std::ios::binary | std::ios::trunc);
  if (!summary) return 4;
  summary << "{\n"
          << "  \"operatingSystem\": \"" << capabilities.operatingSystem << "\",\n"
          << "  \"nativeWindowBackend\": \"" << capabilities.nativeWindowBackend << "\",\n"
          << "  \"textInputBackend\": \"" << capabilities.textInputBackend << "\",\n"
          << "  \"audioOutputBackend\": \"" << capabilities.audioOutputBackend << "\",\n"
          << "  \"audioInputBackend\": \"" << capabilities.audioInputBackend << "\",\n"
          << "  \"maximumOutputChannels\": "
          << static_cast<unsigned int>(capabilities.maximumOutputChannels) << ",\n"
          << "  \"nativeWindowImplemented\": "
          << (capabilities.nativeWindowImplemented ? "true" : "false") << ",\n"
          << "  \"compositionInputImplemented\": "
          << (capabilities.compositionInputImplemented ? "true" : "false") << ",\n"
          << "  \"physicalOutputImplemented\": "
          << (capabilities.physicalOutputImplemented ? "true" : "false") << ",\n"
          << "  \"physicalInputImplemented\": "
          << (capabilities.physicalInputImplemented ? "true" : "false") << "\n"
          << "}\n";
  std::cout << "platform=" << capabilities.operatingSystem << '\n'
            << "window=" << capabilities.nativeWindowBackend << '\n'
            << "text=" << capabilities.textInputBackend << '\n'
            << "output=" << capabilities.audioOutputBackend << '\n'
            << "input=" << capabilities.audioInputBackend << '\n';
  return summary ? 0 : 5;
}
