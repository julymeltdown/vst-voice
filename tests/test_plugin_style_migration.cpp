#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/authoring/voicebank_installer_service.hpp"
#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/core/file_io.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

seam::voicebank::VoicebankCandidate installPluginStyleBank(
    const std::filesystem::path& root) {
  const auto source = root / "source";
  std::filesystem::create_directories(source / "audio");
  const auto samples = seam::test::support::sineWave(48000U, 261.6256, 0.12);
  CHECK(seam::voicebank::writePcm16Wav(source / "audio/a.wav", 48000U, 1U, samples));
  auto soft = seam::test::support::makeUnit(
      "a.soft", {"a"}, "audio/a.wav", 60,
      seam::voicebank::UnitKind::Sustain, samples.size());
  soft.style = "soft";
  auto original = soft;
  original.id = "a.original";
  original.style = "original";
  auto manifest = seam::test::support::makeManifest({soft, original});
  manifest.id = "plugin.style.migration.fixture";
  manifest.styles = {"soft", "original"};
  CHECK(seam::voicebank::ManifestJsonCodec{}.save(manifest, source / "manifest.json"));
  CHECK(seam::core::durableAtomicWriteText(
      source / "license.txt", "Self-generated test audio; integration fixture only\n"));
  const auto key = seam::distribution::generateSigningKeyPair();
  CHECK(key);
  const auto package = root / "plugin-style.seambank";
  CHECK(seam::distribution::packSeambank(source, package, key.value()));
  seam::authoring::VoicebankSession banks(
      {{root / "installed", seam::voicebank::VoicebankRootKind::Installed}}, false);
  seam::authoring::VoicebankInstallerService installer(banks, root / "installed");
  const auto installed = installer.install(seam::authoring::VoicebankInstallRequest{
      .packagePath = package,
      .trustedPublicKeys = {key.value().publicKey},
  });
  CHECK(installed);
  CHECK(installed.value().candidate.trust ==
        seam::voicebank::VoicebankTrust::TrustedInstalled);
  return installed.value().candidate;
}

seam::domain::Project initialPluginProject() {
  seam::application::ProjectFactory factory{500U};
  auto project = factory.createProject("Current plugin document");
  const auto track = factory.addVocalTrack(project, "Current singer");
  static_cast<void>(factory.addRegion(project, track, "Current phrase",
                                      seam::time::Tick{0}, seam::time::Tick{1920}));
  CHECK(project.validate());
  return project;
}

seam::domain::Project decodedLegacyPluginProject(
    const seam::voicebank::VoicebankCandidate& candidate) {
  const auto bytes = seam::core::readTextFileLimited(
      std::filesystem::path{SEAM_SCHEMA8_SOURCE_ROOT} /
          "tests/singing_quality/corpus/unequal-rests.seam", 1U << 20U);
  CHECK(bytes);
  auto tree = seam::formats::parseJson(bytes.value());
  CHECK(tree);
  CHECK(tree.value().find("schemaVersion")->asInt64() == 7);
  auto& bank = tree.value().find("vocalTracks")->asArray().front().asObject().at("voicebank");
  bank.asObject().at("id") = seam::formats::JsonValue{candidate.manifest.id};
  bank.asObject().at("version") = seam::formats::JsonValue{candidate.manifest.version};
  bank.asObject().at("contentHash") = seam::formats::JsonValue{candidate.contentHash};
  const auto decoded = seam::formats::ProjectJsonCodec{}.decode(
      seam::formats::stringifyJson(tree.value()));
  CHECK(decoded);
  CHECK(decoded.value().vocalTracks().front().styleSelection.origin ==
        seam::domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution);
  return decoded.value();
}

std::vector<seam::voicebank::VoicebankSearchRoot> installedPluginRoots(
    const std::filesystem::path& root) {
  return {{root / "installed", seam::voicebank::VoicebankRootKind::Installed}};
}

}

TEST_CASE("plugin replacement migrates a legacy style from a newly installed exact bank") {
  const auto root = seam::test::support::temporaryDirectory("plugin-style-late-install");
  seam::clap_editor::EditorRuntime runtime(initialPluginProject(), {}, installedPluginRoots(root));
  const auto installed = installPluginStyleBank(root);
  const auto legacy = decodedLegacyPluginProject(installed);
  auto expected = legacy;
  expected.vocalTracks().front().styleSelection = {
      seam::domain::VoiceStyleOrigin::LegacyManifestFirst, "soft"};
  const auto revision = runtime.revision();

  CHECK(runtime.replaceProject(legacy));
  CHECK(runtime.projectCopy() == expected);
  CHECK(runtime.revision() == revision + 1U);
  CHECK(runtime.voicebankResolution().resolved());
  CHECK(runtime.voicebankResolution().candidate->trust ==
        seam::voicebank::VoicebankTrust::TrustedInstalled);
  CHECK(runtime.replaceProject(runtime.projectCopy()));
  CHECK(runtime.projectCopy() == expected);
}

TEST_CASE("plugin state replacement preserves schema eight expressions during style migration") {
  const auto root = seam::test::support::temporaryDirectory("plugin-style-expressions");
  const auto installed = installPluginStyleBank(root);
  auto project = decodedLegacyPluginProject(installed);
  auto& region = project.vocalTracks().front().regions.front();
  CHECK(!region.notes.empty());
  region.notes.front().vibrato = {
      .enabled = true,
      .startFraction = 0.35F,
      .fadeInFraction = 0.2F,
      .fadeOutFraction = 0.25F,
      .depthCents = 72.0F,
      .periodMilliseconds = 170.0F,
      .phaseTurns = 0.4F,
  };
  region.notes.front().phoneticHint = "a";
  CHECK(region.dynamicsAutomation.replacePoints(
      {{seam::time::Tick{0}, 0.3F}, {seam::time::Tick{480}, 1.4F}}));
  CHECK(project.validate());
  const auto encoded = seam::clap_editor::encodeEditorState(project);
  CHECK(encoded);
  const auto decoded = seam::clap_editor::decodeEditorState(encoded.value());
  CHECK(decoded);
  CHECK(decoded.value() == project);
  auto expected = project;
  expected.vocalTracks().front().styleSelection = {
      seam::domain::VoiceStyleOrigin::LegacyManifestFirst, "soft"};

  seam::clap_editor::EditorRuntime runtime(initialPluginProject(), {}, installedPluginRoots(root));
  CHECK(runtime.replaceProject(decoded.value()));
  CHECK(runtime.projectCopy() == expected);
  const auto savedState = seam::clap_editor::encodeEditorState(runtime.projectCopy());
  CHECK(savedState);
  const auto reopened = seam::clap_editor::decodeEditorState(savedState.value());
  CHECK(reopened);
  CHECK(reopened.value() == expected);
}

TEST_CASE("plugin replacement leaves unavailable exact styles unresolved and respects explicit choice") {
  const auto root = seam::test::support::temporaryDirectory("plugin-style-unresolved");
  const auto installed = installPluginStyleBank(root);
  const auto legacy = decodedLegacyPluginProject(installed);
  seam::clap_editor::EditorRuntime runtime(initialPluginProject(), {}, installedPluginRoots(root));

  auto wrongHash = legacy;
  wrongHash.vocalTracks().front().voicebank.contentHash = std::string(64U, '0');
  CHECK(runtime.replaceProject(wrongHash));
  CHECK(runtime.projectCopy() == wrongHash);
  CHECK(!runtime.voicebankResolution().resolved());

  auto absent = legacy;
  absent.vocalTracks().front().voicebank.id = "plugin.style.missing.fixture";
  CHECK(runtime.replaceProject(absent));
  CHECK(runtime.projectCopy() == absent);
  CHECK(!runtime.voicebankResolution().resolved());

  auto deliberate = legacy;
  deliberate.vocalTracks().front().styleSelection = {
      seam::domain::VoiceStyleOrigin::Explicit, "original"};
  CHECK(runtime.replaceProject(deliberate));
  CHECK(runtime.projectCopy() == deliberate);

  const auto revision = runtime.revision();
  const auto trackId = runtime.trackId();
  const auto regionId = runtime.regionId();
  auto invalid = legacy;
  invalid.vocalTracks().front().styleSelection.styleId = "invalid-pending-style";
  CHECK(!runtime.replaceProject(invalid));
  CHECK(runtime.projectCopy() == deliberate);
  CHECK(runtime.revision() == revision);
  CHECK(runtime.trackId() == trackId);
  CHECK(runtime.regionId() == regionId);
}
