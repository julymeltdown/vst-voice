#include "test_framework.hpp"

#include "seam/platform/application_paths.hpp"
#include "seam/standalone/production_configuration.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace {

seam::platform::ApplicationPaths testPaths() {
  return seam::platform::ApplicationPaths::forTestRoot(
      std::filesystem::path{"/tmp/project-seam-u3-profile"});
}

}

TEST_CASE("application paths keep mutable categories in distinct user roots") {
  const auto paths = testPaths();
  CHECK(paths.userDataRoot.is_absolute());
  CHECK(paths.cacheRoot.is_absolute());
  CHECK(paths.settingsRoot.is_absolute());
  CHECK(paths.voicebankRoot.is_absolute());
  CHECK(paths.autosaveRoot.is_absolute());
  CHECK(paths.recoveryRoot.is_absolute());
  CHECK(paths.logsRoot.is_absolute());
  CHECK(paths.crashReportsRoot.is_absolute());
  CHECK(paths.updateStagingRoot.is_absolute());
  CHECK(paths.cacheRoot != paths.userDataRoot);
  CHECK(paths.settingsRoot != paths.cacheRoot);
  CHECK(paths.autosaveRoot != paths.recoveryRoot);
  CHECK(paths.logsRoot != paths.crashReportsRoot);
  CHECK(paths.updateStagingRoot != paths.cacheRoot);
  CHECK(paths.manualsRoot != paths.userDataRoot);
  CHECK(paths.resourcesRoot != paths.userDataRoot);
}

TEST_CASE("release configuration removes development and fallback defaults") {
  const auto paths = testPaths();
  const auto configuration = seam::standalone::makeProductionConfiguration(
      seam::standalone::ProductionConfigurationInput{
          .mode = seam::standalone::ProductionRuntimeMode::Release,
          .paths = paths,
          .voicebankRoots = {
              seam::voicebank::VoicebankSearchRoot{
                  .path = paths.userDataRoot / "fixture-bank",
                  .kind = seam::voicebank::VoicebankRootKind::Development,
              },
              seam::voicebank::VoicebankSearchRoot{
                  .path = paths.voicebankRoot / "installed-bank",
                  .kind = seam::voicebank::VoicebankRootKind::Installed,
              },
          },
          .allowDevelopmentVoicebanks = true,
          .forceThreadedAudio = true,
          .bindFirstAvailableVoicebank = true,
          .startPaused = false,
      });
  CHECK(configuration);
  const auto& value = configuration.value();
  CHECK(value.mode == seam::standalone::ProductionRuntimeMode::Release);
  CHECK(!value.allowDevelopmentVoicebanks);
  CHECK(!value.forceThreadedAudio);
  CHECK(!value.bindFirstAvailableVoicebank);
  CHECK(value.startPaused);
  CHECK(value.cacheRoot == paths.cacheRoot);
  CHECK(value.applicationSupportRoot == paths.userDataRoot);
  CHECK(value.voicebankRoots.size() == 2U);
  CHECK(value.voicebankRoots.front().path == paths.voicebankRoot);
  CHECK(value.voicebankRoots.front().kind ==
        seam::voicebank::VoicebankRootKind::Installed);
  CHECK(value.voicebankRoots.back().kind ==
        seam::voicebank::VoicebankRootKind::Installed);
}

TEST_CASE("deterministic test configuration is an explicit nonphysical opt in") {
  const auto paths = testPaths();
  const auto configuration = seam::standalone::makeProductionConfiguration(
      seam::standalone::ProductionConfigurationInput{
          .mode = seam::standalone::ProductionRuntimeMode::DeterministicTest,
          .paths = paths,
          .voicebankRoots = {seam::voicebank::VoicebankSearchRoot{
              .path = paths.userDataRoot / "fixture-bank",
              .kind = seam::voicebank::VoicebankRootKind::Development,
          }},
          .allowDevelopmentVoicebanks = true,
          .forceThreadedAudio = true,
          .bindFirstAvailableVoicebank = true,
          .startPaused = true,
      });
  CHECK(configuration);
  const auto& value = configuration.value();
  CHECK(value.mode ==
        seam::standalone::ProductionRuntimeMode::DeterministicTest);
  CHECK(value.allowDevelopmentVoicebanks);
  CHECK(value.forceThreadedAudio);
  CHECK(value.bindFirstAvailableVoicebank);
  CHECK(value.startPaused);
  CHECK(!value.physicalAudio);
  CHECK(value.voicebankRoots.size() == 2U);
  CHECK(value.voicebankRoots.back().kind ==
        seam::voicebank::VoicebankRootKind::Development);
}

TEST_CASE("development configuration binds an explicit fixture by default") {
  const auto paths = testPaths();
  const auto configuration = seam::standalone::makeProductionConfiguration(
      seam::standalone::ProductionConfigurationInput{
          .mode = seam::standalone::ProductionRuntimeMode::Development,
          .paths = paths,
          .voicebankRoots = {seam::voicebank::VoicebankSearchRoot{
              .path = paths.userDataRoot / "fixture-bank",
              .kind = seam::voicebank::VoicebankRootKind::Development,
          }},
          .allowDevelopmentVoicebanks = true,
          .forceThreadedAudio = false,
          .bindFirstAvailableVoicebank = false,
          .startPaused = true,
      });
  CHECK(configuration);
  const auto& value = configuration.value();
  CHECK(value.mode == seam::standalone::ProductionRuntimeMode::Development);
  CHECK(value.allowDevelopmentVoicebanks);
  CHECK(value.bindFirstAvailableVoicebank);
  CHECK(value.voicebankRoots.size() == 2U);
  CHECK(value.voicebankRoots.back().kind ==
        seam::voicebank::VoicebankRootKind::Development);
}
