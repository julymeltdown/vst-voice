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

enum class HeldReadStage {
  Opened,       // Descriptor opened and fstat-validated, before allocation/read.
  ContentRead,  // Content read, before the post-read identity verification.
};

using HeldReadFaultInjector =
    std::function<Result<void>(HeldReadStage stage)>;

struct AtomicWriteOptions final {
  std::filesystem::path backupPath;
  std::uint64_t maximumBackupBytes{128ULL * 1024ULL * 1024ULL};
  AtomicWriteFaultInjector faultInjector;
};

// Held-input read: on POSIX the file is opened once with O_NOFOLLOW and
// O_NONBLOCK (a FIFO cannot block the admission before the regular-file
// check), the descriptor is fstat-validated, the SAME descriptor supplies
// every byte, and a post-read fstat compares device, inode, size, mtime and
// ctime. Parent-directory replacement cannot redirect the read because the
// descriptor pins the originally opened inode; the metadata comparison is a
// best-effort snapshot check that rejects observed change (ctime is
// kernel-maintained and cannot be restored by user-space timestamp calls),
// not a proof that the content is immutable. The injector exists for
// deterministic adversarial tests that intervene inside this single
// admission; production callers pass none. Windows retains the prior
// stat-then-read sequence pending platform evidence.
[[nodiscard]] Result<std::vector<std::byte>> readFileBytesLimited(
    const std::filesystem::path& path,
    std::uint64_t maximumBytes,
    const HeldReadFaultInjector& faultInjector = {});

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

[[nodiscard]] Result<void> durableAtomicWriteNew(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes);

[[nodiscard]] Result<void> durableAtomicWriteTextNew(
    const std::filesystem::path& path,
    std::string_view text);

}  // namespace seam::core
