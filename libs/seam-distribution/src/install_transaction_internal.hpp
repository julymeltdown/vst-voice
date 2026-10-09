#pragma once

#include "seam/distribution/installer.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>

namespace seam::distribution::install_internal {

inline constexpr std::string_view kInstallReceiptEntry = "install-receipt.json";

// One family-neutral installation of an already verified signed container. Every signed entry is
// staged in a directory created for this call and re-read against its signed digest; the family
// then validates the staged tree and writes its receipt. Only then is the tree published
// atomically: create-new, or an atomic swap when replacement was requested. Until that commit, any
// failure, cancellation or injected fault leaves the previous installation and every path this call
// did not create exactly as they were. The replaced version is removed only after the new one is
// durably visible; otherwise it is kept and reported.
struct ContainerInstallRequest final {
  const SignedContainerInfo* container{nullptr};
  std::filesystem::path installRoot;
  std::string id;
  std::string version;
  bool replaceExisting{false};
  std::uint64_t maximumEntryBytes{0U};
  std::uint64_t maximumArchiveBytes{0U};
  std::function<core::Result<void>(InstallStage)> faultInjector;
  std::function<core::Result<void>(const std::filesystem::path& staging)> finalizeStaged;
};

struct ContainerInstallOutcome final {
  std::filesystem::path installDirectory;
  bool replacedExisting{false};
  bool durabilityConfirmed{true};
  std::string diagnostic;
};

[[nodiscard]] core::Result<ContainerInstallOutcome> installContainerTree(
    const ContainerInstallRequest& request, std::stop_token stop);

}  // namespace seam::distribution::install_internal
