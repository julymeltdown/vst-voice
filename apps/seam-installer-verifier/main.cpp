#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/distribution/trust_policy.hpp"
#include "seam/distribution/update_manifest.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#ifdef _WIN32
#define NOMINMAX
#include <ShlObj.h>
#include <Windows.h>
#endif

#ifndef SEAM_INSTALLER_PLATFORM
#error "SEAM_INSTALLER_PLATFORM must be defined"
#endif
#ifndef SEAM_INSTALLER_ROOT_PUBLIC_KEY_HEX
#error "SEAM_INSTALLER_ROOT_PUBLIC_KEY_HEX must be defined"
#endif

namespace {

struct Arguments final {
  std::filesystem::path handoff;
  std::filesystem::path manifest;
  std::filesystem::path policy;
  std::filesystem::path stagingRoot;
  std::string candidate;
  std::string handoffSha256;
};

struct StageArguments final {
  std::filesystem::path package;
  std::filesystem::path manifest;
  std::filesystem::path stagingRoot;
};

void usage() {
  std::cout
      << "Usage: seam_installer_verifier --handoff FILE --manifest FILE "
         "--policy FILE --staging-root DIR --expected-candidate ID "
         "--expected-handoff-sha256 SHA256\n"
      << "       seam_installer_verifier stage --package FILE --manifest FILE "
         "--staging-root DIR\n";
}

constexpr std::string_view kInstallerPlatform{SEAM_INSTALLER_PLATFORM};
constexpr std::string_view kTrustedRootHex{SEAM_INSTALLER_ROOT_PUBLIC_KEY_HEX};

int hexDigit(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}

seam::core::Result<seam::distribution::Ed25519PublicKey> trustedRoot() {
  seam::distribution::Ed25519PublicKey key{};
  if (kTrustedRootHex.size() != key.size() * 2U) {
    return seam::core::failure<seam::distribution::Ed25519PublicKey>(
        seam::core::ErrorCode::Conflict,
        "Embedded installer update root has an invalid length");
  }
  for (std::size_t index = 0; index < key.size(); ++index) {
    const auto high = hexDigit(kTrustedRootHex[index * 2U]);
    const auto low = hexDigit(kTrustedRootHex[index * 2U + 1U]);
    if (high < 0 || low < 0) {
      return seam::core::failure<seam::distribution::Ed25519PublicKey>(
          seam::core::ErrorCode::Conflict,
          "Embedded installer update root is not lowercase hexadecimal");
    }
    key[index] = static_cast<std::byte>((high << 4) | low);
  }
  return key;
}

seam::core::Result<std::string> utcNow() {
  const auto now = std::chrono::system_clock::now();
  const auto raw = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#ifdef _WIN32
  if (::gmtime_s(&utc, &raw) != 0) {
#else
  if (::gmtime_r(&raw, &utc) == nullptr) {
#endif
    return seam::core::failure<std::string>(
        seam::core::ErrorCode::IoError, "Unable to read the trusted UTC clock");
  }
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

seam::core::Result<std::filesystem::path> replayStateRoot() {
#ifdef _WIN32
  if (kInstallerPlatform != "windows-x64") {
    return seam::core::failure<std::filesystem::path>(
        seam::core::ErrorCode::Conflict,
        "Installer verifier platform is not supported on Windows");
  }
  PWSTR programFiles = nullptr;
  const auto result = ::SHGetKnownFolderPath(
      FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, nullptr, &programFiles);
  if (FAILED(result) || programFiles == nullptr) {
    return seam::core::failure<std::filesystem::path>(
        seam::core::ErrorCode::IoError,
        "Unable to resolve the protected installer replay root");
  }
  const std::filesystem::path root{programFiles};
  ::CoTaskMemFree(programFiles);
  return root / "ProjectSEAM" / "InstallerReplay";
#else
  if (kInstallerPlatform != "macos-arm64") {
    return seam::core::failure<std::filesystem::path>(
        seam::core::ErrorCode::Conflict,
        "Installer verifier platform is not supported on this system");
  }
  return std::filesystem::path{
      "/Library/Application Support/ProjectSEAM/InstallerReplay"};
#endif
}

seam::core::Result<StageArguments> parseStageArguments(int argc, char** argv) {
  StageArguments arguments;
  for (int index = 2; index < argc; ++index) {
    const std::string_view option{argv[index]};
    if (index + 1 >= argc) {
      return seam::core::failure<StageArguments>(
          seam::core::ErrorCode::InvalidArgument,
          "Every handoff staging option requires a value", std::string{option});
    }
    const std::string value{argv[++index]};
    if (option == "--package") arguments.package = value;
    else if (option == "--manifest") arguments.manifest = value;
    else if (option == "--staging-root") arguments.stagingRoot = value;
    else {
      return seam::core::failure<StageArguments>(
          seam::core::ErrorCode::InvalidArgument,
          "Unknown handoff staging option", std::string{option});
    }
  }
  if (arguments.package.empty() || arguments.manifest.empty() ||
      arguments.stagingRoot.empty()) {
    return seam::core::failure<StageArguments>(
        seam::core::ErrorCode::InvalidArgument,
        "Handoff staging requires package, manifest, and staging root");
  }
  return arguments;
}

seam::core::Result<Arguments> parseArguments(int argc, char** argv) {
  Arguments arguments;
  for (int index = 1; index < argc; ++index) {
    const std::string_view option{argv[index]};
    if (index + 1 >= argc) {
      return seam::core::failure<Arguments>(
          seam::core::ErrorCode::InvalidArgument,
          "Every installer verifier option requires a value", std::string{option});
    }
    const std::string value{argv[++index]};
    if (option == "--handoff") arguments.handoff = value;
    else if (option == "--manifest") arguments.manifest = value;
    else if (option == "--policy") arguments.policy = value;
    else if (option == "--staging-root") arguments.stagingRoot = value;
    else if (option == "--expected-candidate") arguments.candidate = value;
    else if (option == "--expected-handoff-sha256") {
      arguments.handoffSha256 = value;
    }
    else {
      return seam::core::failure<Arguments>(
          seam::core::ErrorCode::InvalidArgument,
          "Unknown installer verifier option", std::string{option});
    }
  }
  if (arguments.handoff.empty() || arguments.manifest.empty() ||
      arguments.policy.empty() || arguments.stagingRoot.empty() ||
      arguments.candidate.empty() || arguments.handoffSha256.size() != 64U ||
      std::any_of(arguments.handoffSha256.begin(), arguments.handoffSha256.end(),
                  [](char value) { return hexDigit(value) < 0; })) {
    return seam::core::failure<Arguments>(
        seam::core::ErrorCode::InvalidArgument,
        "Installer verifier requires every trust and identity input");
  }
  return arguments;
}

void printError(const seam::core::Error& error) {
  std::cerr << "INSTALLER_HANDOFF=BLOCKED\nerror=" << error.message;
  if (!error.context.empty()) std::cerr << " (" << error.context << ')';
  std::cerr << '\n';
}

int stageHandoff(int argc, char** argv) {
  auto arguments = parseStageArguments(argc, argv);
  if (!arguments) {
    printError(arguments.error());
    usage();
    return 2;
  }
  auto manifestText = seam::core::readTextFileLimited(
      arguments.value().manifest, 512U * 1024U);
  if (!manifestText) {
    printError(manifestText.error());
    return 3;
  }
  auto manifest = seam::distribution::parseUpdateManifest(manifestText.value());
  if (!manifest) {
    printError(manifest.error());
    return 4;
  }
  auto handoff = seam::distribution::stageVerifiedUpdatePackage(
      arguments.value().package, manifest.value(), arguments.value().stagingRoot);
  if (!handoff) {
    printError(handoff.error());
    return 5;
  }
  const auto handoffPath = arguments.value().stagingRoot /
                           handoff.value().candidateId / "handoff.json";
  std::cout << "INSTALLER_HANDOFF_STAGED=PASS\n"
            << "candidateId=" << handoff.value().candidateId << '\n'
            << "publisherKeyId=" << handoff.value().publisherKeyId << '\n'
            << "handoffPath=" << handoffPath.string() << '\n';
  return 0;
}

}

int main(int argc, char** argv) {
  if (argc >= 2 && std::string_view{argv[1]} == "stage") {
    return stageHandoff(argc, argv);
  }
  if (argc == 2 && std::string_view{argv[1]} == "--help") {
    usage();
    return 0;
  }
  auto arguments = parseArguments(argc, argv);
  if (!arguments) {
    printError(arguments.error());
    usage();
    return 2;
  }
  auto root = trustedRoot();
  auto now = utcNow();
  auto replayRoot = replayStateRoot();
  if (!root || !now || !replayRoot) {
    if (!root) printError(root.error());
    else if (!now) printError(now.error());
    else printError(replayRoot.error());
    return 3;
  }
  auto handoffText = seam::core::readTextFileLimited(
      arguments.value().handoff, 64U * 1024U);
  auto manifestText = seam::core::readTextFileLimited(
      arguments.value().manifest, 512U * 1024U);
  auto policyText = seam::core::readTextFileLimited(
      arguments.value().policy, 512U * 1024U);
  if (!handoffText || !manifestText || !policyText) {
    if (!handoffText) printError(handoffText.error());
    else if (!manifestText) printError(manifestText.error());
    else printError(policyText.error());
    return 3;
  }
  if (seam::core::sha256Hex(handoffText.value()) !=
      arguments.value().handoffSha256) {
    printError(seam::core::Error{seam::core::ErrorCode::Conflict,
                                 "Installer handoff intent digest differs"});
    return 4;
  }
  auto handoff = seam::distribution::parseSealedInstallerHandoff(
      handoffText.value());
  auto manifest = seam::distribution::parseUpdateManifest(manifestText.value());
  auto policy = seam::distribution::parseUpdateTrustPolicy(policyText.value());
  if (!handoff || !manifest || !policy) {
    if (!handoff) printError(handoff.error());
    else if (!manifest) printError(manifest.error());
    else printError(policy.error());
    return 4;
  }
  const seam::distribution::UpdateManifestVerificationOptions manifestOptions{
      .expectedPlatform = kInstallerPlatform,
      .installedVersion = {},
      .highestAcceptedManifestEpoch = std::nullopt,
      .packageBytes = {},
      .now = now.value(),
      .trustedRoot = &root.value()};
  auto verifiedManifest = seam::distribution::verifyUpdateManifest(
      manifest.value(), policy.value(), manifestOptions);
  if (!verifiedManifest) {
    printError(verifiedManifest.error());
    return 5;
  }
  const seam::distribution::InstallerHandoffVerificationOptions handoffOptions{
      .expectedCandidateId = arguments.value().candidate,
      .expectedPlatform = kInstallerPlatform,
      .expectedPublisherKeyId = manifest.value().signature.keyId,
      .now = now.value(),
      .replayStateRoot = replayRoot.value(),
      .consume = true};
  auto verifiedHandoff = seam::distribution::verifySealedInstallerHandoff(
      handoff.value(), manifest.value(), arguments.value().stagingRoot,
      handoffOptions);
  if (!verifiedHandoff) {
    printError(verifiedHandoff.error());
    return 6;
  }
  std::cout << "INSTALLER_HANDOFF=PASS\n"
            << "candidateId=" << handoff.value().candidateId << '\n'
            << "manifestSha256=" << handoff.value().manifestSha256 << '\n'
            << "packageSha256=" << handoff.value().package.sha256 << '\n';
  return 0;
}
