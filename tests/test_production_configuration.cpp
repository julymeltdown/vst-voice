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

// The procedural singer root and its review store are what make the installed-singer lifecycle reachable
// at all: without them the shipped application catalogs nothing and the commands fail. They are checked
// here with the other user paths rather than only in the controller tests, because a build that never
// resolves them would pass every library test and still ship a picker that can never offer a singer.
TEST_CASE("the procedural singer root and review store resolve under the user data root") {
  const auto paths = testPaths();
  CHECK(paths.proceduralSingerRoot.is_absolute());
  CHECK(paths.proceduralReviewStorePath.is_absolute());
  // The singer root is user-owned, like the banks, so it must sit under user data and not beside the
  // installation, which may be read-only.
  CHECK(paths.proceduralSingerRoot.string().starts_with(paths.userDataRoot.string()));
  CHECK(paths.proceduralReviewStorePath.string().starts_with(paths.userDataRoot.string()));
  // The review store is a file inside the singer root, so removing the singers also removes their
  // decisions rather than leaving approvals behind for resources that no longer exist.
  CHECK(paths.proceduralReviewStorePath.parent_path() == paths.proceduralSingerRoot);
  CHECK(paths.proceduralSingerRoot != paths.voicebankRoot);
  CHECK(paths.proceduralSingerRoot != paths.userDataRoot);
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
  CHECK(value.characterPackage.empty());
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
