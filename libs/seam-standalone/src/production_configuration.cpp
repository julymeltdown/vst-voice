#include "seam/standalone/production_configuration.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

namespace seam::standalone {
namespace {

std::filesystem::path absolutePath(std::filesystem::path path) {
  if (path.empty()) return {};
  std::error_code error;
  auto absolute = std::filesystem::absolute(std::move(path), error);
  return error ? std::filesystem::path{} : absolute.lexically_normal();
}

bool containsRoot(const std::vector<voicebank::VoicebankSearchRoot>& roots,
                 const voicebank::VoicebankSearchRoot& candidate) {
  return std::find_if(roots.begin(), roots.end(), [&candidate](const auto& root) {
           return root.path == candidate.path && root.kind == candidate.kind;
         }) != roots.end();
}

std::vector<voicebank::VoicebankSearchRoot> productionVoicebankRoots(
    const ProductionConfigurationInput& input) {
  std::vector<voicebank::VoicebankSearchRoot> roots;
  if (!input.paths.voicebankRoot.empty()) {
    roots.push_back(voicebank::VoicebankSearchRoot{
        .path = input.paths.voicebankRoot,
        .kind = voicebank::VoicebankRootKind::Installed,
    });
  }
  for (auto root : input.voicebankRoots) {
    if (root.path.empty()) continue;
    root.path = absolutePath(std::move(root.path));
    if (root.path.empty()) continue;
    if (input.mode == ProductionRuntimeMode::Release &&
        root.kind == voicebank::VoicebankRootKind::Development) {
      continue;
    }
    if (!containsRoot(roots, root)) roots.push_back(std::move(root));
  }
  return roots;
}

bool validUserPaths(const platform::ApplicationPaths& paths) {
  return !paths.userDataRoot.empty() && paths.userDataRoot.is_absolute() &&
         !paths.cacheRoot.empty() && paths.cacheRoot.is_absolute() &&
         !paths.voicebankRoot.empty() && paths.voicebankRoot.is_absolute();
}

}

core::Result<ProductionConfiguration> makeProductionConfiguration(
    ProductionConfigurationInput input) {
  if (!validUserPaths(input.paths)) {
    return core::failure<ProductionConfiguration>(
        core::ErrorCode::InvalidArgument,
        "Production runtime requires absolute user data, cache, and voicebank roots");
  }
  if (input.sampleRate < 8000U || input.sampleRate > 192000U) {
    return core::failure<ProductionConfiguration>(
        core::ErrorCode::InvalidArgument,
        "Production sample rate must be between 8000 and 192000 Hz");
  }
  if (input.outputChannels < 1U || input.outputChannels > 8U) {
    return core::failure<ProductionConfiguration>(
        core::ErrorCode::InvalidArgument,
        "Production output channel count must be between one and eight");
  }
  if (input.audioBlockFrames == 0U) {
    return core::failure<ProductionConfiguration>(
        core::ErrorCode::InvalidArgument,
        "Production audio block size must be greater than zero");
  }

  ProductionConfiguration result{
      .mode = input.mode,
      .paths = input.paths,
      .voicebankRoots = productionVoicebankRoots(input),
      .trustedVoicebankKeys = std::move(input.trustedVoicebankKeys),
      .developmentTrustRoot = std::move(input.developmentTrustRoot),
      .applicationSupportRoot = input.paths.userDataRoot,
      .cacheRoot = input.paths.cacheRoot,
      .characterPackage = std::move(input.characterPackage),
      .allowDevelopmentVoicebanks = input.allowDevelopmentVoicebanks,
      .forceThreadedAudio = input.forceThreadedAudio,
      .bindFirstAvailableVoicebank = input.bindFirstAvailableVoicebank,
      .startPaused = input.startPaused,
      .physicalAudio = !input.forceThreadedAudio,
      .sampleRate = input.sampleRate,
      .outputChannels = input.outputChannels,
      .audioBlockFrames = input.audioBlockFrames,
  };
  if (result.characterPackage.empty() && !result.paths.resourcesRoot.empty()) {
    result.characterPackage = result.paths.resourcesRoot / "character-01";
  }

  if (input.mode == ProductionRuntimeMode::Release) {
    result.allowDevelopmentVoicebanks = false;
    result.forceThreadedAudio = false;
    result.bindFirstAvailableVoicebank = false;
    result.startPaused = true;
    result.physicalAudio = true;
    result.developmentTrustRoot.reset();
  } else if (input.mode == ProductionRuntimeMode::DeterministicTest) {
    result.bindFirstAvailableVoicebank = input.allowDevelopmentVoicebanks;
    result.forceThreadedAudio = true;
    result.startPaused = true;
    result.physicalAudio = false;
  } else {
    result.bindFirstAvailableVoicebank = input.allowDevelopmentVoicebanks;
    result.forceThreadedAudio = false;
    result.physicalAudio = true;
  }
  return result;
}

}
