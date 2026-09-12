#pragma once
#include "seam/core/result.hpp"
#include <filesystem>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace seam::core {
// Persistent lock inode; never unlink it. The OS releases ownership on exit.
class ExclusiveFileLock final {
public:
  ExclusiveFileLock() = default;
  ExclusiveFileLock(const ExclusiveFileLock&) = delete;
  ExclusiveFileLock& operator=(const ExclusiveFileLock&) = delete;
  ~ExclusiveFileLock() {
#if defined(_WIN32)
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
    if (descriptor_ >= 0) close(descriptor_);
#endif
  }
  Result<void> acquire(const std::filesystem::path& path) {
#if defined(_WIN32)
    if (handle_ != INVALID_HANDLE_VALUE) return failure(ErrorCode::Conflict, "Lock already opened");
    handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if (handle_ == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle_, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
      return failure(ErrorCode::Conflict, "File lock is busy or unsafe");
#else
    if (descriptor_ >= 0) return failure(ErrorCode::Conflict, "Lock already opened");
    descriptor_ = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    struct stat info{};
    if (descriptor_ < 0 || fstat(descriptor_, &info) != 0 || !S_ISREG(info.st_mode) || flock(descriptor_, LOCK_EX | LOCK_NB) != 0)
      return failure(ErrorCode::Conflict, "File lock is busy or unsafe");
#endif
    return success();
  }
private:
#if defined(_WIN32)
  HANDLE handle_{INVALID_HANDLE_VALUE};
#else
  int descriptor_{-1};
#endif
};
}
