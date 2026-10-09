#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/distribution/installer.hpp"
#include "seam/distribution/procedural_package.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank/content_identity.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/wav.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stop_token>
#include <string>
#include <thread>

namespace {
using namespace seam;

std::filesystem::path makeBank(const std::filesystem::path& root, std::string_view name, double frequency) {
  const auto source = root / std::string{name};
  std::filesystem::create_directories(source / "audio");
  const auto samples = test::support::sineWave(48000U, frequency, 0.1);
  const auto written = voicebank::writePcm16Wav(source / "audio/a.wav", 48000U, 1U, samples);
  if (!written) throw std::runtime_error(written.error().message);
  const auto manifest = test::support::makeManifest({test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 69, voicebank::UnitKind::Sustain, samples.size())});
  const auto saved = voicebank::ManifestJsonCodec{}.save(manifest, source / "manifest.json");
  if (!saved) throw std::runtime_error(saved.error().message);
  std::ofstream(source / "license.txt") << "Synthetic test bank; engineering fixture only.\n";
  return source;
}

std::map<std::string, std::string> treeDigest(const std::filesystem::path& root) {
  std::map<std::string, std::string> files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    if (entry.is_regular_file())
      files.emplace(entry.path().lexically_relative(root).generic_string(), core::sha256File(entry.path(), 1ULL << 30U).value());
  return files;
}

std::set<std::string> entriesOf(const std::filesystem::path& directory) {
  std::set<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) names.insert(entry.path().filename().string());
  return names;
}

distribution::InstallSeambankOptions trusted(const distribution::SigningKeyPair& key, bool replace = false) {
  distribution::InstallSeambankOptions options;
  options.verification.trustedPublicKeys = {key.publicKey};
  options.verification.requireTrustedSigner = true;
  options.replaceExisting = replace;
  return options;
}

core::Result<void> diskFull() {
  return core::failure(core::ErrorCode::IoError, "No space left on device (simulated disk exhaustion)");
}

struct TwoBanks final {
  std::filesystem::path root;
  distribution::SigningKeyPair key;
  std::filesystem::path first, second, installRoot;
  std::string firstDigest, secondDigest;
  explicit TwoBanks(std::string_view name) : root{test::support::temporaryDirectory(name)} {
    auto generated = distribution::generateSigningKeyPair();
    if (!generated) throw std::runtime_error("key generation failed");
    key = generated.value();
    first = root / "first.seambank";
    second = root / "second.seambank";
    installRoot = root / "installed";
    const auto a = distribution::packSeambank(makeBank(root, "first", 440.0), first, key);
    const auto b = distribution::packSeambank(makeBank(root, "second", 660.0), second, key);
    if (!a || !b) throw std::runtime_error("packing failed");
    firstDigest = a.value().packageDigest;
    secondDigest = b.value().packageDigest;
  }
};
}  // namespace

TEST_CASE("Seambank installation pins the exact package digest and content identity before publishing") {
  TwoBanks banks{"install-pins"};
  auto options = trusted(banks.key);
  options.expectedPackageDigest = banks.secondDigest;  // A trusted package, but not the one captured.
  CHECK(!distribution::installSeambank(banks.first, banks.installRoot, options));
  CHECK(!std::filesystem::exists(banks.installRoot));
  options.expectedPackageDigest = banks.firstDigest;
  options.expectedContentHash = std::string(64U, 'b');
  CHECK(!distribution::installSeambank(banks.first, banks.installRoot, options));
  CHECK(entriesOf(banks.installRoot).empty());
  options.expectedContentHash.reset();
  const auto installed = distribution::installSeambank(banks.first, banks.installRoot, options);
  CHECK(installed);
  if (!installed) return;
  CHECK(installed.value().durabilityConfirmed); CHECK(!installed.value().replacedExisting);
  const auto manifest = voicebank::ManifestJsonCodec{}.load(installed.value().installDirectory / "manifest.json");
  CHECK(manifest);
  CHECK(installed.value().contentHash ==
        voicebank::computeVoicebankContentHash(manifest.value(), installed.value().installDirectory).value());
  const auto receipt = core::readTextFileLimited(installed.value().installDirectory / "install-receipt.json", 1U << 20U);
  CHECK(receipt); CHECK(receipt.value().find(installed.value().contentHash) != std::string::npos);
  CHECK(receipt.value().find(banks.firstDigest) != std::string::npos);
  CHECK((entriesOf(banks.installRoot) == std::set<std::string>{"test.voicebank"}));
}

TEST_CASE("Cancelled or disk-exhausted replacement leaves the installed version and install root intact") {
  TwoBanks banks{"install-interrupted"};
  const auto first = distribution::installSeambank(banks.first, banks.installRoot, trusted(banks.key));
  CHECK(first);
  if (!first) return;
  const auto installed = first.value().installDirectory;
  const auto before = treeDigest(installed);
  const auto rootBefore = entriesOf(banks.installRoot);
  const auto versionsBefore = entriesOf(installed.parent_path());
  const auto intact = [&] {
    return treeDigest(installed) == before && entriesOf(banks.installRoot) == rootBefore &&
           entriesOf(installed.parent_path()) == versionsBefore;
  };
  // Fail at every stage before the commit, including part-way through copying entries.
  for (const auto& [stage, skip] : std::vector<std::pair<distribution::InstallStage, int>>{
           {distribution::InstallStage::Verified, 0}, {distribution::InstallStage::EntryStaged, 0},
           {distribution::InstallStage::EntryStaged, 1}, {distribution::InstallStage::StagedVerified, 0},
           {distribution::InstallStage::BeforeCommit, 0}}) {
    int seen = 0;
    auto options = trusted(banks.key, true);
    options.faultInjector = [&seen, target = stage, remaining = skip](distribution::InstallStage current) -> core::Result<void> {
      if (current != target || seen++ < remaining) return core::success();
      return diskFull();
    };
    CHECK(!distribution::installSeambank(banks.second, banks.installRoot, options));
    CHECK(intact());
  }
  std::stop_source cancel;
  auto cancelling = trusted(banks.key, true);
  cancelling.faultInjector = [&cancel](distribution::InstallStage current) -> core::Result<void> {
    if (current == distribution::InstallStage::EntryStaged) cancel.request_stop();
    return core::success();
  };
  CHECK(!distribution::installSeambank(banks.second, banks.installRoot, cancelling, cancel.get_token()));
  CHECK(intact());
  CHECK(!distribution::installSeambank(banks.second, banks.installRoot, trusted(banks.key, false)));
  CHECK(intact());
  // The same replacement then succeeds and leaves nothing behind but the new version.
  const auto replaced = distribution::installSeambank(banks.second, banks.installRoot, trusted(banks.key, true));
  CHECK(replaced);
  if (!replaced) return;
  CHECK(replaced.value().replacedExisting); CHECK(replaced.value().durabilityConfirmed);
  CHECK(replaced.value().packageDigest == banks.secondDigest);
  CHECK(treeDigest(installed) != before);
  CHECK(entriesOf(banks.installRoot) == rootBefore);
  CHECK(entriesOf(installed.parent_path()) == versionsBefore);
}

TEST_CASE("Installation never removes staging or backup directories it did not create") {
  TwoBanks banks{"install-foreign-leftovers"};
  CHECK(distribution::installSeambank(banks.first, banks.installRoot, trusted(banks.key)));
  // Names an interrupted older installer could have left, including a backup that holds the only
  // copy of a previous version.
  std::map<std::filesystem::path, std::string> leftovers;
  for (int token = 0; token < 32; ++token) {
    for (const auto* prefix : {".staging-", ".backup-"}) {
      const auto directory = banks.installRoot / (std::string{prefix} + "test.voicebank-1.0.0-" + std::to_string(token));
      std::filesystem::create_directories(directory);
      const auto marker = directory / "manifest.json";
      CHECK(core::durableAtomicWriteTextNew(marker, "{\"retained\":" + std::to_string(token) + "}"));
      leftovers.emplace(marker, core::sha256File(marker).value());
    }
  }
  CHECK(distribution::installSeambank(banks.second, banks.installRoot, trusted(banks.key, true)));
  for (const auto& [marker, digest] : leftovers) {
    CHECK(std::filesystem::exists(marker));
    CHECK(core::sha256File(marker).value() == digest);
  }
}

TEST_CASE("A committed replacement whose durability is uncertain keeps the replaced version") {
  TwoBanks banks{"install-uncertain"};
  const auto first = distribution::installSeambank(banks.first, banks.installRoot, trusted(banks.key));
  CHECK(first);
  if (!first) return;
  const auto before = treeDigest(first.value().installDirectory);
  auto options = trusted(banks.key, true);
  options.faultInjector = [](distribution::InstallStage current) -> core::Result<void> {
    return current == distribution::InstallStage::AfterCommitBeforeSync ? diskFull() : core::success();
  };
  const auto replaced = distribution::installSeambank(banks.second, banks.installRoot, options);
  CHECK(replaced);
  if (!replaced) return;
  CHECK(!replaced.value().durabilityConfirmed); CHECK(replaced.value().replacedExisting);
  CHECK(replaced.value().diagnostic.find("retained at") != std::string::npos);
  CHECK(treeDigest(replaced.value().installDirectory) != before);
  std::filesystem::path retained;
  for (const auto& name : entriesOf(banks.installRoot))
    if (name.starts_with(".staging-")) retained = banks.installRoot / name;
  CHECK(!retained.empty());
  if (!retained.empty()) CHECK(treeDigest(retained) == before);
}

TEST_CASE("Procedural singer installation shares the transaction, its pins and its intact-on-failure rule") {
  const auto root = test::support::temporaryDirectory("install-procedural-transaction");
  auto key = distribution::generateSigningKeyPair();
  CHECK(key);
  if (!key) return;
  const auto recipe = voice_design::loadVoiceRecipeResource(
      std::filesystem::path{SEAM_TEST_SOURCE_DIR} / "assets/pilots/seam-song-01/recipe.json");
  CHECK(recipe);
  if (!recipe) return;
  distribution::PublishProceduralSingerOptions options;
  options.version = "1.0.0";
  options.language = "ja";
  const auto first = distribution::publishProceduralSingerFromRecipe(recipe.value(), root / "first-source",
      root / "first.seamsinger", key.value(), options);
  options.displayName = "Changed presentation";
  const auto second = distribution::publishProceduralSingerFromRecipe(recipe.value(), root / "second-source",
      root / "second.seamsinger", key.value(), options);
  CHECK(first); CHECK(second);
  if (!first || !second) return;
  distribution::InstallProceduralOptions install;
  install.verification.trustedPublicKeys = {key.value().publicKey};
  install.verification.requireTrustedSigner = true;
  const auto installed = distribution::installProceduralPackage(root / "first.seamsinger", root / "installed", install);
  CHECK(installed);
  if (!installed) return;
  const auto before = treeDigest(installed.value().installDirectory);
  const auto rootBefore = entriesOf(root / "installed");
  install.replaceExisting = true;
  install.faultInjector = [](distribution::InstallStage current) -> core::Result<void> {
    return current == distribution::InstallStage::BeforeCommit ? diskFull() : core::success();
  };
  CHECK(!distribution::installProceduralPackage(root / "second.seamsinger", root / "installed", install));
  CHECK(treeDigest(installed.value().installDirectory) == before);
  CHECK(entriesOf(root / "installed") == rootBefore);
  install.faultInjector = nullptr;
  install.expectedContentHash = std::string(64U, 'c');
  CHECK(!distribution::installProceduralPackage(root / "second.seamsinger", root / "installed", install));
  CHECK(treeDigest(installed.value().installDirectory) == before);
  install.expectedContentHash.reset();
  install.expectedPackageDigest = second.value().container.packageDigest;
  const auto replaced = distribution::installProceduralPackage(root / "second.seamsinger", root / "installed", install);
  CHECK(replaced);
  if (!replaced) return;
  CHECK(replaced.value().replacedExisting); CHECK(replaced.value().durabilityConfirmed);
  CHECK(treeDigest(replaced.value().installDirectory) != before);
  CHECK(entriesOf(root / "installed") == rootBefore);
}


TEST_CASE("Concurrent installers cannot replace a version while its transaction owns the root") {
  TwoBanks banks{"install-concurrent-writer"};
  CHECK(distribution::installSeambank(banks.first, banks.installRoot, trusted(banks.key)));
  auto options = trusted(banks.key, true);
  bool refused = false;
  options.faultInjector = [&](distribution::InstallStage stage) {
    if (stage == distribution::InstallStage::BeforeCommit) {
      std::jthread competing{[&] {
        const auto other = distribution::installSeambank(banks.first, banks.installRoot, trusted(banks.key, true));
        refused = !other && other.error().message.find("busy") != std::string::npos;
      }};
    }
    return core::success();
  };
  const auto installed = distribution::installSeambank(banks.second, banks.installRoot, options);
  CHECK(installed); CHECK(refused);
  CHECK(installed.value().packageDigest == banks.secondDigest);
  // The lease is released when the transaction returns.
  CHECK(distribution::installSeambank(banks.first, banks.installRoot, trusted(banks.key, true)));
}

TEST_CASE("A target replaced outside the transaction is retained and the stale install refuses") {
  TwoBanks banks{"install-target-replaced"};
  const auto original = distribution::installSeambank(banks.first, banks.installRoot, trusted(banks.key));
  CHECK(original);
  const auto target = original.value().installDirectory;
  const auto saved = banks.root / "original-retained";
  const auto originalBytes = treeDigest(target);
  auto options = trusted(banks.key, true);
  options.faultInjector = [&](distribution::InstallStage stage) {
    if (stage == distribution::InstallStage::BeforeCommit) {
      std::filesystem::rename(target, saved);
      std::filesystem::create_directory(target);
      CHECK(core::durableAtomicWriteTextNew(target / "foreign.txt", "new owner"));
    }
    return core::success();
  };
  const auto result = distribution::installSeambank(banks.second, banks.installRoot, options);
  CHECK(!result);
  CHECK(result.error().message.find("Installed version changed") != std::string::npos);
  CHECK(core::readTextFileLimited(target / "foreign.txt", 100U).value() == "new owner");
  CHECK(treeDigest(saved) == originalBytes);
}

TEST_CASE("Redirected product parents cannot receive a staged installation") {
  TwoBanks banks{"install-parent-redirected"};
  const auto original = distribution::installSeambank(banks.first, banks.installRoot, trusted(banks.key));
  CHECK(original);
  const auto target = original.value().installDirectory;
  const auto saved = banks.root / "original-product";
  const auto foreign = banks.root / "foreign-product";
  std::filesystem::create_directory(foreign);
  auto options = trusted(banks.key, true);
  options.faultInjector = [&](distribution::InstallStage stage) {
    if (stage == distribution::InstallStage::BeforeCommit) {
      std::filesystem::rename(target.parent_path(), saved);
      std::filesystem::create_directory_symlink(foreign, target.parent_path());
    }
    return core::success();
  };
  const auto result = distribution::installSeambank(banks.second, banks.installRoot, options);
  CHECK(!result);
  CHECK(result.error().message.find("parent directory changed") != std::string::npos);
  CHECK(entriesOf(foreign).empty());
  CHECK(std::filesystem::is_directory(saved / target.filename()));
}

TEST_CASE("Every newly created ancestor has a sync checkpoint and interrupted roots can retry") {
  TwoBanks banks{"install-fresh-ancestors"};
  for (int failAt = 0; failAt < 4; ++failAt) {
    const auto root = banks.root / ("nested-" + std::to_string(failAt)) / "level" / "installed";
    int seen = 0;
    auto options = trusted(banks.key);
    options.faultInjector = [&](distribution::InstallStage stage) {
      if (stage == distribution::InstallStage::DirectoryCreatedBeforeSync && seen++ == failAt) return diskFull();
      return core::success();
    };
    const auto failed = distribution::installSeambank(banks.first, root, options);
    CHECK(!failed); CHECK(seen == failAt + 1);
    CHECK(!std::filesystem::exists(root / "test.voicebank/1.0.0"));
    const auto retried = distribution::installSeambank(banks.first, root, trusted(banks.key));
    CHECK(retried); CHECK(retried.value().durabilityConfirmed);
  }
  int synced = 0;
  auto options = trusted(banks.key);
  options.faultInjector = [&](distribution::InstallStage stage) {
    if (stage == distribution::InstallStage::DirectoryCreatedBeforeSync) ++synced;
    return core::success();
  };
  const auto fresh = distribution::installSeambank(banks.first, banks.root / "new/level/installed", options);
  CHECK(fresh); CHECK(fresh.value().durabilityConfirmed); CHECK(synced == 4);
}

TEST_CASE("Cancellation requested at the final precommit checkpoint never publishes") {
  TwoBanks banks{"install-final-cancel"};
  const auto first = distribution::installSeambank(banks.first, banks.installRoot, trusted(banks.key));
  CHECK(first);
  const auto before = treeDigest(first.value().installDirectory);
  std::stop_source cancel;
  auto options = trusted(banks.key, true);
  options.faultInjector = [&](distribution::InstallStage stage) {
    if (stage == distribution::InstallStage::BeforeCommit) cancel.request_stop();
    return core::success();
  };
  CHECK(!distribution::installSeambank(banks.second, banks.installRoot, options, cancel.get_token()));
  CHECK(treeDigest(first.value().installDirectory) == before);
}
