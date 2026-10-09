#include "install_transaction_internal.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <optional>
#include <random>
#include <set>
#include <system_error>

#if !defined(_WIN32)
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

namespace seam::distribution::install_internal {
namespace {

std::atomic<std::uint64_t> gStagingSequence{0U};

bool safeComponent(std::string_view value) noexcept {
  if (value.empty() || value.front() == '.' || value == "." || value == "..") return false;
  return std::all_of(value.begin(), value.end(), [](char character) {
    const auto byte = static_cast<unsigned char>(character);
    return std::isalnum(byte) != 0 || character == '.' || character == '-' || character == '_';
  });
}

core::Result<void> checkpoint(const ContainerInstallRequest& request, std::stop_token stop, InstallStage stage) {
  if (stop.stop_requested())
    return core::failure(core::ErrorCode::Conflict, "Installation cancelled before publication; nothing changed");
  return request.faultInjector ? request.faultInjector(stage) : core::success();
}

std::string hex(const std::array<std::byte, 32U>& digest) {
  static constexpr std::string_view kDigits = "0123456789abcdef";
  std::string output;
  output.reserve(64U);
  for (const auto value : digest) {
    const auto byte = std::to_integer<unsigned>(value);
    output.push_back(kDigits[(byte >> 4U) & 0x0fU]);
    output.push_back(kDigits[byte & 0x0fU]);
  }
  return output;
}

bool absent(const std::filesystem::path& path, bool& unsafe) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory || (!error && status.type() == std::filesystem::file_type::not_found))
    return true;
  unsafe = error || std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status);
  return false;
}

struct TreeIdentity final {
  std::uint64_t device{0U};
  std::uint64_t inode{0U};
};

#if !defined(_WIN32)
std::optional<TreeIdentity> directoryIdentity(const std::filesystem::path& path) {
  struct stat info{};
  if (::lstat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) return std::nullopt;
  return TreeIdentity{static_cast<std::uint64_t>(info.st_dev), static_cast<std::uint64_t>(info.st_ino)};
}

core::Result<void> syncDirectory(const std::filesystem::path& path) {
  const int descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor < 0) return core::failure(core::ErrorCode::IoError, "Cannot open an install directory for sync", path.string());
  const int synced = ::fsync(descriptor);
  ::close(descriptor);
  return synced == 0 ? core::success()
                     : core::failure(core::ErrorCode::IoError, "Cannot sync an install directory", path.string());
}

bool createPrivateDirectory(const std::filesystem::path& path, bool& collided) {
  if (::mkdir(path.c_str(), 0700) == 0) return true;
  collided = errno == EEXIST;
  return false;
}

// Atomic publication: create-new never replaces anything; swap exchanges the staged tree with the
// installed one in a single step so the version is never missing.
bool commitRename(const std::filesystem::path& staged, const std::filesystem::path& target, bool swap) {
#if defined(__APPLE__)
  return ::renameatx_np(AT_FDCWD, staged.c_str(), AT_FDCWD, target.c_str(), swap ? RENAME_SWAP : RENAME_EXCL) == 0;
#elif defined(__linux__)
  return ::syscall(SYS_renameat2, AT_FDCWD, staged.c_str(), AT_FDCWD, target.c_str(), swap ? 2U : 1U) == 0;
#else
  if (swap) return false;
  std::error_code error;
  std::filesystem::rename(staged, target, error);
  return !error;
#endif
}
#else
std::optional<TreeIdentity> directoryIdentity(const std::filesystem::path&) { return std::nullopt; }
core::Result<void> syncDirectory(const std::filesystem::path&) { return core::success(); }
bool createPrivateDirectory(const std::filesystem::path& path, bool& collided) {
  std::error_code error;
  const bool created = std::filesystem::create_directory(path, error);
  collided = !created && !error;
  return created;
}
bool commitRename(const std::filesystem::path& staged, const std::filesystem::path& target, bool swap) {
  if (swap) return false;
  std::error_code error;
  std::filesystem::rename(staged, target, error);
  return !error;
}
#endif

bool isTree(const std::filesystem::path& path, const TreeIdentity& identity) {
  const auto current = directoryIdentity(path);
  return current && current->device == identity.device && current->inode == identity.inode;
}

// Removes only the tree this call created or moved aside, while the path still names that tree.
void removeOwnedTree(const std::filesystem::path& path, const std::optional<TreeIdentity>& identity) noexcept {
  try {
    if (!identity || !isTree(path, *identity)) return;
    std::error_code error;
    std::filesystem::remove_all(path, error);
  } catch (...) {
    // Conservatively retain the tree when ownership cannot be proven.
  }
}

std::string uniqueStagingName(const ContainerInstallRequest& request) {
  thread_local std::mt19937_64 generator{std::random_device{}()};
  std::array<char, 17U> nonce{};
  std::snprintf(nonce.data(), nonce.size(), "%016llx", static_cast<unsigned long long>(generator()));
#if defined(_WIN32)
  const auto process = std::uint64_t{0U};
#else
  const auto process = static_cast<std::uint64_t>(::getpid());
#endif
  return ".staging-" + request.id + "-" + request.version + "-" + std::to_string(process) + "-" +
         std::to_string(gStagingSequence.fetch_add(1U, std::memory_order_relaxed)) + "-" + nonce.data();
}

core::Result<std::filesystem::path> realDirectory(const std::filesystem::path& path, std::string_view label, bool& created) {
  bool unsafe = false;
  if (absent(path, unsafe)) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) return core::failure<std::filesystem::path>(core::ErrorCode::IoError, "Unable to create the " + std::string{label}, error.message());
    created = true;
  } else if (unsafe) {
    return core::failure<std::filesystem::path>(core::ErrorCode::Conflict, "The " + std::string{label} + " must be a real directory", path.string());
  }
  std::error_code error;
  auto canonical = std::filesystem::canonical(path, error);
  if (error) return core::failure<std::filesystem::path>(core::ErrorCode::IoError, "Unable to resolve the " + std::string{label}, error.message());
  return canonical;
}

}  // namespace

core::Result<ContainerInstallOutcome> installContainerTree(const ContainerInstallRequest& request, std::stop_token stop) {
  using Output = ContainerInstallOutcome;
  if (!request.container || !request.finalizeStaged)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Installation request is incomplete");
  if (!safeComponent(request.id) || !safeComponent(request.version))
    return core::failure<Output>(core::ErrorCode::Unsupported, "Resource ID or version is unsafe for installation");
  const auto& container = *request.container;
  if (std::any_of(container.entries.begin(), container.entries.end(),
          [](const auto& entry) { return entry.path == kInstallReceiptEntry; }))
    return core::failure<Output>(core::ErrorCode::Conflict, "A package cannot carry its own install receipt");
  auto checked = checkpoint(request, stop, InstallStage::Verified);
  if (!checked) return core::Result<Output>{checked.error()};
  bool rootCreated = false, productCreated = false;
  const auto root = realDirectory(request.installRoot, "install root", rootCreated);
  if (!root) return core::Result<Output>{root.error()};
  const auto product = realDirectory(root.value() / request.id, "product install directory", productCreated);
  if (!product) return core::Result<Output>{product.error()};
  if (productCreated) {
    checked = syncDirectory(root.value());
    if (!checked) return core::Result<Output>{checked.error()};
  }
  const auto target = product.value() / request.version;
  bool unsafeTarget = false;
  const bool replacing = !absent(target, unsafeTarget);
  if (unsafeTarget)
    return core::failure<Output>(core::ErrorCode::Conflict, "Existing installation target is unsafe", target.string());
  if (replacing && !request.replaceExisting)
    return core::failure<Output>(core::ErrorCode::Conflict, "This version is already installed", target.string());

  // A private directory created new for this call; nothing is removed to make room for it.
  std::filesystem::path staging;
  for (int attempt = 0; attempt < 8 && staging.empty(); ++attempt) {
    bool collided = false;
    const auto candidate = root.value() / uniqueStagingName(request);
    if (createPrivateDirectory(candidate, collided)) staging = candidate;
    else if (!collided)
      return core::failure<Output>(core::ErrorCode::IoError, "Unable to create a private installation staging directory", candidate.string());
  }
  if (staging.empty())
    return core::failure<Output>(core::ErrorCode::Conflict, "Unable to find an unused installation staging name");
  const auto stagingIdentity = directoryIdentity(staging);
  struct Cleanup final {
    std::filesystem::path path;
    std::optional<TreeIdentity> identity;
    std::filesystem::path emptyProduct;
    bool armed{true};
    ~Cleanup() {
      if (!armed) return;
      removeOwnedTree(path, identity);
      std::error_code ignored;
      if (!emptyProduct.empty()) std::filesystem::remove(emptyProduct, ignored);  // Only succeeds while empty.
    }
  } cleanup{staging, stagingIdentity, productCreated ? product.value() : std::filesystem::path{}};

  std::set<std::filesystem::path> createdDirectories;
  std::set<std::string> expected{std::string{kInstallReceiptEntry}};
  for (const auto& entry : container.entries) {
    checked = stop.stop_requested()
        ? core::failure(core::ErrorCode::Conflict, "Installation cancelled before publication; nothing changed")
        : core::success();
    if (!checked) return core::Result<Output>{checked.error()};
    auto bytes = readSignedContainerEntry(container, container.packagePath, entry.path, request.maximumEntryBytes);
    if (!bytes) return core::Result<Output>{bytes.error()};
    core::Sha256 hash;
    hash.update(bytes.value());
    if (hash.digest() != entry.sha256)
      return core::failure<Output>(core::ErrorCode::Conflict, "Install entry checksum mismatch", entry.path);
    const auto destination = staging / std::filesystem::path{entry.path};
    for (auto directory = destination.parent_path(); directory != staging && directory.native().size() > staging.native().size();
         directory = directory.parent_path())
      createdDirectories.insert(directory);
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) return core::failure<Output>(core::ErrorCode::IoError, "Unable to create an installed asset directory", entry.path);
    checked = core::durableAtomicWriteNew(destination, bytes.value());
    if (!checked) return core::Result<Output>{checked.error()};
    expected.insert(entry.path);
    checked = checkpoint(request, stop, InstallStage::EntryStaged);
    if (!checked) return core::Result<Output>{checked.error()};
  }
  checked = request.finalizeStaged(staging);
  if (!checked) return core::Result<Output>{checked.error()};
  // The staged tree must be exactly the signed entries, byte for byte, plus the family receipt.
  for (const auto& entry : container.entries) {
    const auto digest = core::sha256File(staging / std::filesystem::path{entry.path}, std::max<std::uint64_t>(entry.payloadSize, 1U));
    if (!digest || digest.value() != hex(entry.sha256))
      return core::failure<Output>(core::ErrorCode::Conflict, "Staged installation differs from the signed entry", entry.path);
  }
  std::set<std::string> present;
  std::error_code walkError;
  for (std::filesystem::recursive_directory_iterator iterator{staging, walkError}, end; !walkError && iterator != end;
       iterator.increment(walkError)) {
    const auto status = iterator->symlink_status(walkError);
    if (walkError) break;
    if (std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status)) continue;
    if (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
      return core::failure<Output>(core::ErrorCode::Conflict, "Staged installation contains a link or special file");
    present.insert(iterator->path().lexically_relative(staging).generic_string());
  }
  if (walkError || present != expected)
    return core::failure<Output>(core::ErrorCode::Conflict, "Staged installation holds unexpected or missing files");
  checked = checkpoint(request, stop, InstallStage::StagedVerified);
  if (!checked) return core::Result<Output>{checked.error()};
  std::vector<std::filesystem::path> directories{createdDirectories.begin(), createdDirectories.end()};
  std::sort(directories.begin(), directories.end(), [](const auto& left, const auto& right) {
    return left.native().size() > right.native().size();
  });
  directories.push_back(staging);
  for (const auto& directory : directories) {
    checked = syncDirectory(directory);
    if (!checked) return core::Result<Output>{checked.error()};
  }
  const auto finalDigest = core::sha256File(container.packagePath, request.maximumArchiveBytes);
  if (!finalDigest || finalDigest.value() != container.packageDigest)
    return core::failure<Output>(core::ErrorCode::Conflict, "Package changed during installation");
  checked = checkpoint(request, stop, InstallStage::BeforeCommit);
  if (!checked) return core::Result<Output>{checked.error()};
  if (stagingIdentity && !isTree(staging, *stagingIdentity))
    return core::failure<Output>(core::ErrorCode::Conflict, "Installation staging directory was replaced; nothing was published");
  const auto previousIdentity = replacing ? directoryIdentity(target) : std::nullopt;
  if (replacing && !previousIdentity)
    return core::failure<Output>(core::ErrorCode::Conflict, "Installed version changed during installation", target.string());
  if (!commitRename(staging, target, replacing))
    return core::failure<Output>(core::ErrorCode::Conflict,
        replacing ? "Unable to swap in the new installation atomically; the installed version is unchanged"
                  : "Unable to publish the installation without replacing a concurrent one", target.string());
  // Committed. After a swap the staging path names the previous installation.
  cleanup.armed = false;
  Output output{target, replacing, true, {}};
  checked = request.faultInjector ? request.faultInjector(InstallStage::AfterCommitBeforeSync) : core::success();
  if (checked) checked = syncDirectory(product.value());
  if (checked && replacing) checked = syncDirectory(root.value());
  if (!checked) {
    output.durabilityConfirmed = false;
    output.diagnostic = "Installed and visible, but its directory entry may not be durable yet; verify the receipt before relying on it. " +
        checked.error().message;
    if (replacing) output.diagnostic += " The previous version is retained at " + staging.string() + ".";
    return output;
  }
  if (replacing) {
    removeOwnedTree(staging, previousIdentity);
    bool unsafeLeftover = false;
    if (!absent(staging, unsafeLeftover))
      output.diagnostic = "The previous version could not be removed and is retained at " + staging.string() + ".";
  }
  return output;
}

}  // namespace seam::distribution::install_internal
