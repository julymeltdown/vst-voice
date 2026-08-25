#include "seam/core/file_io.hpp"
#include "seam/core/realtime_audit.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace seam::core {
namespace {

std::atomic<std::uint64_t> gTemporarySequence{0U};

Result<void> inject(const AtomicWriteOptions& options, AtomicWriteStage stage) {
  return options.faultInjector ? options.faultInjector(stage) : success();
}

std::filesystem::path temporaryPathFor(const std::filesystem::path& target) {
  const auto sequence =
      gTemporarySequence.fetch_add(1U, std::memory_order_relaxed) + 1U;
#ifdef _WIN32
  const auto process = static_cast<std::uint64_t>(GetCurrentProcessId());
  auto nativeName = target.filename().native();
  nativeName.append(L".tmp.");
  nativeName.append(std::to_wstring(process));
  nativeName.push_back(L'.');
  nativeName.append(std::to_wstring(sequence));
#else
  const auto process = static_cast<std::uint64_t>(::getpid());
  auto nativeName = target.filename().native();
  nativeName.append(".tmp.");
  nativeName.append(std::to_string(process));
  nativeName.push_back('.');
  nativeName.append(std::to_string(sequence));
#endif
  return target.parent_path() / std::filesystem::path{std::move(nativeName)};
}

Result<void> createParent(const std::filesystem::path& path) {
  if (!path.has_parent_path()) return success();
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return failure(ErrorCode::IoError, "Unable to create output directory",
                   error.message());
  }
  return success();
}

Result<void> validateAtomicTarget(const std::filesystem::path& path) {
  if (path.empty()) {
    return failure(ErrorCode::InvalidArgument,
                   "Atomic write target is empty");
  }
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory ||
      status.type() == std::filesystem::file_type::not_found) {
    return success();
  }
  if (error) {
    return failure(ErrorCode::IoError,
                   "Unable to inspect atomic write target", error.message());
  }
  if (status.type() == std::filesystem::file_type::symlink) {
    return failure(ErrorCode::Conflict,
                   "Atomic write target cannot be a symbolic link",
                   path.string());
  }
  if (!std::filesystem::is_regular_file(status)) {
    return failure(ErrorCode::IoError,
                   "Atomic write target is not a regular file",
                   path.string());
  }
  return success();
}

#ifdef _WIN32

Result<void> writeAndSyncTemporary(const std::filesystem::path& path,
                                   std::span<const std::byte> bytes,
                                   const AtomicWriteOptions& options) {
  const auto handle = CreateFileW(
      path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return failure(ErrorCode::IoError, "Unable to create temporary file",
                   std::to_string(GetLastError()));
  }
  auto closeHandle = [&] { CloseHandle(handle); };
  auto injected = inject(options, AtomicWriteStage::TemporaryCreated);
  if (!injected) {
    closeHandle();
    return injected;
  }
  std::size_t offset = 0U;
  while (offset < bytes.size()) {
    const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
        bytes.size() - offset, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
    DWORD written = 0U;
    if (!WriteFile(handle, bytes.data() + offset, chunk, &written, nullptr) ||
        written != chunk) {
      const auto error = GetLastError();
      closeHandle();
      return failure(ErrorCode::IoError, "Unable to write temporary file",
                     std::to_string(error));
    }
    offset += written;
  }
  injected = inject(options, AtomicWriteStage::TemporaryWritten);
  if (!injected) {
    closeHandle();
    return injected;
  }
  if (!FlushFileBuffers(handle)) {
    const auto error = GetLastError();
    closeHandle();
    return failure(ErrorCode::IoError, "Unable to flush temporary file",
                   std::to_string(error));
  }
  closeHandle();
  return inject(options, AtomicWriteStage::TemporarySynced);
}

Result<void> replaceFile(const std::filesystem::path& temporary,
                         const std::filesystem::path& target) {
  if (!MoveFileExW(temporary.c_str(), target.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    return failure(ErrorCode::IoError, "Unable to atomically replace file",
                   std::to_string(GetLastError()));
  }
  return success();
}

Result<void> syncParentDirectory(const std::filesystem::path&) {
  // MoveFileExW with MOVEFILE_WRITE_THROUGH provides the strongest portable
  // replacement guarantee available through the Win32 file API used here.
  return success();
}

#else

Result<void> writeAndSyncTemporary(const std::filesystem::path& path,
                                   std::span<const std::byte> bytes,
                                   const AtomicWriteOptions& options) {
  const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (descriptor < 0) {
    return failure(ErrorCode::IoError, "Unable to create temporary file",
                   std::strerror(errno));
  }
  auto closeDescriptor = [&] { static_cast<void>(::close(descriptor)); };
  auto injected = inject(options, AtomicWriteStage::TemporaryCreated);
  if (!injected) {
    closeDescriptor();
    return injected;
  }
  std::size_t offset = 0U;
  while (offset < bytes.size()) {
    const auto remaining = bytes.size() - offset;
    const auto written = ::write(descriptor, bytes.data() + offset, remaining);
    if (written < 0) {
      if (errno == EINTR) continue;
      const auto message = std::string{std::strerror(errno)};
      closeDescriptor();
      return failure(ErrorCode::IoError, "Unable to write temporary file", message);
    }
    if (written == 0) {
      closeDescriptor();
      return failure(ErrorCode::IoError, "Temporary file write made no progress",
                     path.string());
    }
    offset += static_cast<std::size_t>(written);
  }
  injected = inject(options, AtomicWriteStage::TemporaryWritten);
  if (!injected) {
    closeDescriptor();
    return injected;
  }
  if (::fsync(descriptor) != 0) {
    const auto message = std::string{std::strerror(errno)};
    closeDescriptor();
    return failure(ErrorCode::IoError, "Unable to sync temporary file", message);
  }
  closeDescriptor();
  return inject(options, AtomicWriteStage::TemporarySynced);
}

Result<void> replaceFile(const std::filesystem::path& temporary,
                         const std::filesystem::path& target) {
  if (::rename(temporary.c_str(), target.c_str()) != 0) {
    return failure(ErrorCode::IoError, "Unable to atomically replace file",
                   std::strerror(errno));
  }
  return success();
}

Result<void> syncParentDirectory(const std::filesystem::path& path) {
  const auto directory = path.has_parent_path() ? path.parent_path()
                                                : std::filesystem::path{"."};
#ifdef O_DIRECTORY
  const int descriptor = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY);
#else
  const int descriptor = ::open(directory.c_str(), O_RDONLY);
#endif
  if (descriptor < 0) {
    return failure(ErrorCode::IoError, "Unable to open parent directory for sync",
                   std::strerror(errno));
  }
  const auto result = ::fsync(descriptor);
  const auto savedError = errno;
  static_cast<void>(::close(descriptor));
  if (result != 0) {
    return failure(ErrorCode::IoError, "Unable to sync parent directory",
                   std::strerror(savedError));
  }
  return success();
}

#endif

Result<void> writeImpl(const std::filesystem::path& path,
                       std::span<const std::byte> bytes,
                       const AtomicWriteOptions& options,
                       bool createBackup) {
  if (path.empty()) {
    return failure(ErrorCode::InvalidArgument, "Atomic write target is empty");
  }
  if (createBackup && !options.backupPath.empty() &&
      options.backupPath.lexically_normal() == path.lexically_normal()) {
    return failure(ErrorCode::InvalidArgument,
                   "Atomic write backup path must differ from target");
  }
  const auto parent = createParent(path);
  if (!parent) return parent;
  const auto target = validateAtomicTarget(path);
  if (!target) return target;
  if (createBackup && !options.backupPath.empty()) {
    const auto backupTarget = validateAtomicTarget(options.backupPath);
    if (!backupTarget) return backupTarget;
  }

  if (createBackup && !options.backupPath.empty()) {
    std::error_code existsError;
    const bool exists = std::filesystem::exists(path, existsError);
    if (existsError) {
      return failure(ErrorCode::IoError, "Unable to inspect existing file",
                     existsError.message());
    }
    if (exists) {
      auto oldBytes = readFileBytesLimited(path, options.maximumBackupBytes);
      if (!oldBytes) return Result<void>{oldBytes.error()};
      AtomicWriteOptions backupOptions;
      const auto backup = writeImpl(options.backupPath, oldBytes.value(),
                                    backupOptions, false);
      if (!backup) return backup;
      const auto injected = inject(options, AtomicWriteStage::BackupCommitted);
      if (!injected) return injected;
    }
  }

  const auto temporary = temporaryPathFor(path);
  const auto cleanup = [&] {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
  };
  const auto written = writeAndSyncTemporary(temporary, bytes, options);
  if (!written) {
    cleanup();
    return written;
  }
  auto injected = inject(options, AtomicWriteStage::BeforeReplace);
  if (!injected) {
    cleanup();
    return injected;
  }
  const auto replaced = replaceFile(temporary, path);
  if (!replaced) {
    cleanup();
    return replaced;
  }
  injected = inject(options, AtomicWriteStage::Replaced);
  if (!injected) return injected;
  const auto synced = syncParentDirectory(path);
  if (!synced) return synced;
  return inject(options, AtomicWriteStage::DirectorySynced);
}

}  // namespace

Result<std::vector<std::byte>> readFileBytesLimited(
    const std::filesystem::path& path,
    std::uint64_t maximumBytes) {
  recordRealtimeFileIo();
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (status.type() == std::filesystem::file_type::symlink) {
    return failure<std::vector<std::byte>>(
        ErrorCode::Conflict, "Unable to read a symbolic link", path.string());
  }
  if (error || !std::filesystem::is_regular_file(status)) {
    return failure<std::vector<std::byte>>(
        ErrorCode::IoError, "Unable to read a non-regular file", path.string());
  }
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    return failure<std::vector<std::byte>>(ErrorCode::IoError,
                                           "Unable to inspect input file",
                                           error.message());
  }
  if (size > maximumBytes || size > static_cast<std::uint64_t>(
          std::numeric_limits<std::size_t>::max())) {
    return failure<std::vector<std::byte>>(ErrorCode::Unsupported,
                                           "Input file exceeds configured limit",
                                           path.string());
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return failure<std::vector<std::byte>>(ErrorCode::IoError,
                                           "Unable to open input file",
                                           path.string());
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
  }
  if (!stream || stream.peek() != std::char_traits<char>::eof()) {
    return failure<std::vector<std::byte>>(ErrorCode::IoError,
                                           "Unable to read input file completely",
                                           path.string());
  }
  return bytes;
}

Result<std::string> readTextFileLimited(const std::filesystem::path& path,
                                        std::uint64_t maximumBytes) {
  recordRealtimeFileIo();
  auto bytes = readFileBytesLimited(path, maximumBytes);
  if (!bytes) return Result<std::string>{bytes.error()};
  std::string text(bytes.value().size(), '\0');
  if (!text.empty()) {
    std::memcpy(text.data(), bytes.value().data(), bytes.value().size());
  }
  return text;
}

Result<void> durableAtomicWrite(const std::filesystem::path& path,
                                std::span<const std::byte> bytes,
                                const AtomicWriteOptions& options) {
  recordRealtimeFileIo();
  return writeImpl(path, bytes, options, true);
}

Result<void> durableAtomicWriteText(const std::filesystem::path& path,
                                    std::string_view text,
                                    const AtomicWriteOptions& options) {
  recordRealtimeFileIo();
  return durableAtomicWrite(path,
      std::as_bytes(std::span{text.data(), text.size()}), options);
}

}  // namespace seam::core
