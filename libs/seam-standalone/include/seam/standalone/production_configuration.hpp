#pragma once

#include "seam/core/result.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/platform/application_paths.hpp"
#include "seam/voicebank/catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace seam::standalone {

enum class ProductionRuntimeMode {
  Release,
  Development,
  DeterministicTest,
};

struct ProductionConfigurationInput final {
  ProductionRuntimeMode mode{ProductionRuntimeMode::Release};
  platform::ApplicationPaths paths;
  std::vector<voicebank::VoicebankSearchRoot> voicebankRoots;
  std::vector<distribution::Ed25519PublicKey> trustedVoicebankKeys;
  std::optional<distribution::Ed25519PublicKey> developmentTrustRoot;
  bool allowDevelopmentVoicebanks{false};
  bool forceThreadedAudio{false};
  bool bindFirstAvailableVoicebank{false};
  bool startPaused{true};
  std::uint32_t sampleRate{48000U};
  std::uint8_t outputChannels{2U};
  std::size_t audioBlockFrames{256U};
  std::filesystem::path characterPackage;
};

struct ProductionConfiguration final {
  ProductionRuntimeMode mode{ProductionRuntimeMode::Release};
  platform::ApplicationPaths paths;
  std::vector<voicebank::VoicebankSearchRoot> voicebankRoots;
  std::vector<distribution::Ed25519PublicKey> trustedVoicebankKeys;
  std::optional<distribution::Ed25519PublicKey> developmentTrustRoot;
  std::filesystem::path applicationSupportRoot;
  std::filesystem::path cacheRoot;
  std::filesystem::path characterPackage;
  bool allowDevelopmentVoicebanks{false};
  bool forceThreadedAudio{false};
  bool bindFirstAvailableVoicebank{false};
  bool startPaused{true};
  bool physicalAudio{true};
  std::uint32_t sampleRate{48000U};
  std::uint8_t outputChannels{2U};
  std::size_t audioBlockFrames{256U};
};

[[nodiscard]] core::Result<ProductionConfiguration>
makeProductionConfiguration(ProductionConfigurationInput input);

}
