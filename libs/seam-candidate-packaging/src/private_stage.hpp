#pragma once

#include "seam/core/result.hpp"
#include <filesystem>
#include <memory>
#include <system_error>
#include <string_view>
#include <cerrno>
#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <cstdlib>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

namespace seam::candidate_packaging::detail {
// A random 0700 child with held parent/stage identities. Cleanup refuses redirected paths.
// Publication is create-new and anchored to both held directory descriptors.
class PrivateStage final {
public:
  std::filesystem::path path;
  PrivateStage() = default;
  PrivateStage(const PrivateStage&) = delete;
  PrivateStage& operator=(const PrivateStage&) = delete;
  ~PrivateStage() {
#if defined(__APPLE__) || defined(__linux__)
    if (unchanged()) { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    if (stage_ >= 0) ::close(stage_);
    if (parent_ >= 0) ::close(parent_);
#endif
  }
  static core::Result<std::unique_ptr<PrivateStage>> create(const std::filesystem::path& parent) {
    using Output = std::unique_ptr<PrivateStage>;
#if defined(__APPLE__) || defined(__linux__)
    auto value = std::make_unique<PrivateStage>();
    std::error_code error;
    const auto status = std::filesystem::symlink_status(parent, error);
    if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status))
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate staging parent must be a real directory");
    value->parentPath_ = std::filesystem::canonical(parent, error);
    if (error) return core::failure<Output>(core::ErrorCode::IoError, "Cannot resolve staging parent", error.message());
    value->parent_ = ::open(value->parentPath_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (value->parent_ < 0 || ::fstat(value->parent_, &value->parentIdentity_) != 0)
      return core::failure<Output>(core::ErrorCode::IoError, "Cannot hold candidate staging parent");
    std::string pattern = (value->parentPath_ / ".seam-package-XXXXXX").string();
    if (::mkdtemp(pattern.data()) == nullptr)
      return core::failure<Output>(core::ErrorCode::IoError, "Cannot create private candidate staging directory");
    value->path = pattern;
    value->stage_ = ::openat(value->parent_, value->path.filename().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (value->stage_ < 0 || ::fstat(value->stage_, &value->stageIdentity_) != 0 || !value->unchanged())
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate staging parent was redirected; provisional data retained");
    return value;
#else
    static_cast<void>(parent);
    return core::failure<Output>(core::ErrorCode::Unsupported, "Private candidate packaging is unsupported on this platform");
#endif
  }
  bool unchanged() const {
#if defined(__APPLE__) || defined(__linux__)
    if (parent_ < 0 || stage_ < 0) return false;
    struct stat parent{}, stage{};
    return ::lstat(parentPath_.c_str(), &parent) == 0 && S_ISDIR(parent.st_mode) &&
        parent.st_dev == parentIdentity_.st_dev && parent.st_ino == parentIdentity_.st_ino &&
        ::lstat(path.c_str(), &stage) == 0 && S_ISDIR(stage.st_mode) &&
        stage.st_dev == stageIdentity_.st_dev && stage.st_ino == stageIdentity_.st_ino;
#else
    return false;
#endif
  }
  core::Result<void> publishFile(std::string_view name, const std::filesystem::path& destination) {
#if defined(__APPLE__) || defined(__linux__)
    if (!unchanged() || destination.parent_path() != parentPath_ || name.empty() || name.find('/') != std::string_view::npos)
      return core::failure(core::ErrorCode::Conflict, "Candidate package staging or parent changed before publication");
    struct stat file{};
    const std::string entry{name};
    if (::fstatat(stage_, entry.c_str(), &file, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(file.st_mode))
      return core::failure(core::ErrorCode::Conflict, "Candidate package is not a regular staged file");
#if defined(__APPLE__)
    const int result = ::renameatx_np(stage_, entry.c_str(), parent_, destination.filename().c_str(), RENAME_EXCL);
#else
    const int result = static_cast<int>(::syscall(SYS_renameat2, stage_, entry.c_str(), parent_, destination.filename().c_str(), 1U));
#endif
    if (result == 0) return core::success();
    const auto failure = std::error_code{errno, std::generic_category()};
    return core::failure(failure == std::errc::file_exists ? core::ErrorCode::Conflict : core::ErrorCode::IoError,
        "Cannot publish candidate package", failure.message());
#else
    static_cast<void>(name); static_cast<void>(destination);
    return core::failure(core::ErrorCode::Unsupported, "Atomic candidate packaging is unsupported on this platform");
#endif
  }
  core::Result<void> syncAfterPublication() const {
#if defined(__APPLE__) || defined(__linux__)
    if (::fsync(stage_) == 0 && ::fsync(parent_) == 0 && unchanged()) return core::success();
#endif
    return core::failure(core::ErrorCode::IoError, "Package committed but directory sync or identity confirmation failed");
  }
private:
  std::filesystem::path parentPath_;
#if defined(__APPLE__) || defined(__linux__)
  int parent_{-1}, stage_{-1};
  struct stat parentIdentity_{}, stageIdentity_{};
#endif
};
} // namespace seam::candidate_packaging::detail
