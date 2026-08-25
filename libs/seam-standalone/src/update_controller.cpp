#include "seam/standalone/update_controller.hpp"

#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace seam::standalone {
namespace {

std::string timestampNow() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

core::Result<std::string> readMetadata(const std::filesystem::path& path) {
  auto text = core::readTextFileLimited(path, 256U * 1024U);
  if (!text) return text;
  return text.value();
}

core::Result<UpdateCheckResult> blocked(std::string message) {
  return UpdateCheckResult{.status = UpdateCheckStatus::Blocked,
                           .manifest = std::nullopt,
                           .diagnostic = std::move(message)};
}

}

core::Result<std::unique_ptr<UpdateController>> UpdateController::create(
    UpdateControllerConfig config) {
  if (config.statePath.empty() || config.stagingRoot.empty() ||
      config.expectedPlatform.empty() || config.installedVersion.empty() ||
      !config.trustedRoot.has_value()) {
    return core::failure<std::unique_ptr<UpdateController>>(
        core::ErrorCode::InvalidArgument,
        "Update controller requires state, staging, platform, version, and trusted-root inputs");
  }
  return std::unique_ptr<UpdateController>{new UpdateController(std::move(config))};
}

core::Result<std::optional<std::uint64_t>> UpdateController::highestEpoch() const {
  std::error_code error;
  if (!std::filesystem::exists(config_.statePath, error)) {
    if (error) {
      return core::failure<std::optional<std::uint64_t>>(
          core::ErrorCode::IoError, "Unable to inspect update state", error.message());
    }
    return std::optional<std::uint64_t>{};
  }
  auto text = core::readTextFileLimited(config_.statePath, 64U * 1024U);
  if (!text) return core::Result<std::optional<std::uint64_t>>{text.error()};
  auto parsed = formats::parseJson(text.value(), formats::JsonParseLimits{
      .maximumInputBytes = 64U * 1024U,
      .maximumDepth = 8U,
      .maximumNodes = 64U,
      .maximumStringBytes = 4096U,
      .maximumCollectionEntries = 32U});
  if (!parsed || !parsed.value().isObject()) {
    return core::failure<std::optional<std::uint64_t>>(
        core::ErrorCode::ParseError, "Update state is malformed");
  }
  const auto* epoch = parsed.value().find("highestManifestEpoch");
  if (epoch == nullptr || !epoch->isInteger() || epoch->asInt64() < 0) {
    return core::failure<std::optional<std::uint64_t>>(
        core::ErrorCode::ParseError, "Update state epoch is invalid");
  }
  return std::optional<std::uint64_t>{static_cast<std::uint64_t>(epoch->asInt64())};
}

core::Result<void> UpdateController::accept(
    const distribution::UpdateManifest& manifest) const {
  if (config_.statePath.parent_path().empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Update state path must have a parent directory");
  }
  formats::JsonValue::Object state;
  state.emplace("schemaVersion", static_cast<std::int64_t>(1));
  state.emplace("highestManifestEpoch",
                static_cast<std::int64_t>(manifest.manifestEpoch));
  state.emplace("acceptedManifestId", manifest.manifestId);
  state.emplace("acceptedTargetBuild", manifest.targetBuild);
  state.emplace("acceptedManifestSha256",
                distribution::updateManifestIdentity(manifest));
  return core::durableAtomicWriteText(
      config_.statePath,
      formats::stringifyJson(formats::JsonValue{std::move(state)}, true));
}

core::Result<UpdateCheckResult> UpdateController::check(
    const std::filesystem::path& policyPath,
    const std::filesystem::path& manifestPath) const {
  auto policyText = readMetadata(policyPath);
  if (!policyText) return blocked(policyText.error().message);
  auto manifestText = readMetadata(manifestPath);
  if (!manifestText) return blocked(manifestText.error().message);
  auto policy = distribution::parseUpdateTrustPolicy(policyText.value());
  if (!policy) return blocked(policy.error().message);
  auto manifest = distribution::parseUpdateManifest(manifestText.value());
  if (!manifest) return blocked(manifest.error().message);
  auto epoch = highestEpoch();
  if (!epoch) return blocked(epoch.error().message);
  const auto verificationTime = config_.verificationTime.empty()
                                    ? timestampNow()
                                    : config_.verificationTime;
  const distribution::UpdateManifestVerificationOptions options{
      .expectedPlatform = config_.expectedPlatform,
      .installedVersion = config_.installedVersion,
      .highestAcceptedManifestEpoch = epoch.value(),
      .packageBytes = {},
      .now = verificationTime,
      .trustedRoot = &config_.trustedRoot.value()};
  auto verified = distribution::verifyUpdateManifest(
      manifest.value(), policy.value(), options);
  if (!verified) {
    return UpdateCheckResult{.status = UpdateCheckStatus::Blocked,
                             .manifest = manifest.value(),
                             .diagnostic = verified.error().message};
  }
  return UpdateCheckResult{.status = UpdateCheckStatus::Available,
                           .manifest = manifest.value(),
                           .diagnostic = {}};
}

core::Result<distribution::SealedInstallerHandoff> UpdateController::stage(
    const std::filesystem::path& policyPath,
    const std::filesystem::path& manifestPath,
    const std::filesystem::path& packagePath) {
  auto result = check(policyPath, manifestPath);
  if (!result) return core::Result<distribution::SealedInstallerHandoff>{result.error()};
  if (result.value().status != UpdateCheckStatus::Available ||
      !result.value().manifest.has_value()) {
    return core::failure<distribution::SealedInstallerHandoff>(
        core::ErrorCode::Conflict,
        result.value().diagnostic.empty() ? "Update is not available"
                                          : result.value().diagnostic);
  }
  auto handoff = distribution::stageVerifiedUpdatePackage(
      packagePath, *result.value().manifest, config_.stagingRoot);
  if (!handoff) return handoff;
  auto accepted = accept(*result.value().manifest);
  if (!accepted) return core::Result<distribution::SealedInstallerHandoff>{accepted.error()};
  return handoff;
}

}
