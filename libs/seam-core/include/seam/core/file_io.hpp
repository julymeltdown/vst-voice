#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace seam::core {

enum class AtomicWriteStage {
  TemporaryCreated,
  TemporaryWritten,
  TemporarySynced,
  BackupCommitted,
  BeforeReplace,
  Replaced,
  DirectorySynced,
};

using AtomicWriteFaultInjector =
    std::function<Result<void>(AtomicWriteStage stage)>;

struct AtomicWriteOptions final {
  std::filesystem::path backupPath;
  std::uint64_t maximumBackupBytes{128ULL * 1024ULL * 1024ULL};
  AtomicWriteFaultInjector faultInjector;
};

[[nodiscard]] Result<std::vector<std::byte>> readFileBytesLimited(
    const std::filesystem::path& path,
    std::uint64_t maximumBytes);

[[nodiscard]] Result<std::string> readTextFileLimited(
    const std::filesystem::path& path,
    std::uint64_t maximumBytes);

[[nodiscard]] Result<void> durableAtomicWrite(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes,
    const AtomicWriteOptions& options = {});

[[nodiscard]] Result<void> durableAtomicWriteText(
    const std::filesystem::path& path,
    std::string_view text,
    const AtomicWriteOptions& options = {});

}  // namespace seam::core
