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
#include <sys/file.h>
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
  auto result = request.faultInjector ? request.faultInjector(stage) : core::success();
  if (!result) return result;
  return stop.stop_requested()
      ? core::failure(core::ErrorCode::Conflict, "Installation cancelled before publication; nothing changed")
      : core::success();
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
  friend bool operator==(const TreeIdentity&, const TreeIdentity&) = default;
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

// Directory descriptors anchor publication even if an ancestor path is redirected.
class DirectoryHandle final {
public:
  explicit DirectoryHandle(const std::filesystem::path& path)
      : fd_{::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)} {}
  ~DirectoryHandle() { if (fd_ >= 0) ::close(fd_); }
  DirectoryHandle(const DirectoryHandle&) = delete;
  DirectoryHandle& operator=(const DirectoryHandle&) = delete;
  [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
  [[nodiscard]] int fd() const noexcept { return fd_; }
  [[nodiscard]] core::Result<void> sync() const {
    return ::fsync(fd_) == 0 ? core::success()
        : core::failure(core::ErrorCode::IoError, "Cannot sync held install directory");
  }
  [[nodiscard]] bool lockWriter() const { return ::flock(fd_, LOCK_EX | LOCK_NB) == 0; }
  [[nodiscard]] std::optional<TreeIdentity> identity() const {
    struct stat info{};
    if (::fstat(fd_, &info) != 0 || !S_ISDIR(info.st_mode)) return std::nullopt;
    return TreeIdentity{static_cast<std::uint64_t>(info.st_dev), static_cast<std::uint64_t>(info.st_ino)};
  }
private:
  int fd_{-1};
};

// Create-new never replaces anything; swap makes the new version visible in one step.
bool commitRename(const DirectoryHandle& root, const std::filesystem::path& stagingName,
                  const DirectoryHandle& product, const std::string& version, bool swap) {
#if defined(__APPLE__)
  return ::renameatx_np(root.fd(), stagingName.c_str(), product.fd(), version.c_str(),
                       swap ? RENAME_SWAP : RENAME_EXCL) == 0;
#elif defined(__linux__)
  return ::syscall(SYS_renameat2, root.fd(), stagingName.c_str(), product.fd(), version.c_str(),
                   swap ? 2U : 1U) == 0;
#else
  (void)root; (void)stagingName; (void)product; (void)version; (void)swap;
  return false;
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
// Windows transaction semantics remain TODO; never claim a durable fallback.
class DirectoryHandle final {
public:
  explicit DirectoryHandle(const std::filesystem::path&) {}
  core::Result<void> sync() const { return core::failure(core::ErrorCode::Unsupported, "Directory sync unavailable"); }
  bool valid() const { return false; }
  bool lockWriter() const { return false; }
  std::optional<TreeIdentity> identity() const { return std::nullopt; }
};
bool commitRename(const DirectoryHandle&, const std::filesystem::path&,
                  const DirectoryHandle&, const std::string&, bool) { return false; }
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

core::Result<std::filesystem::path> realDirectory(const std::filesystem::path& path, std::string_view label,
    bool& created, const ContainerInstallRequest& request, std::stop_token stop) {
  bool unsafe = false;
  if (absent(path, unsafe)) {
    std::error_code error;
    auto current = std::filesystem::absolute(path, error).lexically_normal();
    if (error) return core::failure<std::filesystem::path>(core::ErrorCode::IoError, "Cannot resolve install directory", error.message());
    std::vector<std::filesystem::path> missing;
    while (!std::filesystem::exists(current, error) && !error) {
      missing.push_back(current);
      const auto parent = current.parent_path();
      if (parent == current) break;
      current = parent;
    }
    if (error) return core::failure<std::filesystem::path>(core::ErrorCode::IoError, "Cannot inspect install ancestors", error.message());
    std::filesystem::create_directories(path, error);
    if (error) return core::failure<std::filesystem::path>(core::ErrorCode::IoError, "Unable to create the " + std::string{label}, error.message());
    created = true;
    // Flush every newly introduced parent entry, deepest first. This includes ancestors
    // above the requested install root, not only the product directory within it.
    for (const auto& directory : missing) {
      auto checked = checkpoint(request, stop, InstallStage::DirectoryCreatedBeforeSync);
      if (!checked) return core::Result<std::filesystem::path>{checked.error()};
      const auto parent = std::filesystem::canonical(directory.parent_path(), error);
      if (error) return core::failure<std::filesystem::path>(core::ErrorCode::IoError, "Cannot resolve new install parent", error.message());
      checked = syncDirectory(parent);
      if (!checked) return core::Result<std::filesystem::path>{checked.error()};
    }
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
#if !defined(__APPLE__) && !defined(__linux__)
  return core::failure<Output>(core::ErrorCode::Unsupported,
      "Atomic resource installation requires a supported directory-lock and rename backend; Windows remains TODO");
#endif
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
  const auto root = realDirectory(request.installRoot, "install root", rootCreated, request, stop);
  if (!root) return core::Result<Output>{root.error()};
  DirectoryHandle rootHandle{root.value()};
  if (!rootHandle.valid() || !rootHandle.lockWriter())
    return core::failure<Output>(core::ErrorCode::Conflict, "Install root is busy or unavailable; another installation may be active");
  const auto rootIdentity = rootHandle.identity();
  if (!rootIdentity || !isTree(root.value(), *rootIdentity))
    return core::failure<Output>(core::ErrorCode::Conflict, "Install root changed before staging");
  const auto product = realDirectory(root.value() / request.id, "product install directory", productCreated, request, stop);
  if (!product) return core::Result<Output>{product.error()};
  DirectoryHandle productHandle{product.value()};
  const auto productIdentity = productHandle.identity();
  if (!productHandle.valid() || !productIdentity || !isTree(product.value(), *productIdentity))
    return core::failure<Output>(core::ErrorCode::Conflict, "Product install directory changed before staging");
  const auto target = product.value() / request.version;
  bool unsafeTarget = false;
  const bool replacing = !absent(target, unsafeTarget);
  if (unsafeTarget)
    return core::failure<Output>(core::ErrorCode::Conflict, "Existing installation target is unsafe", target.string());
  if (replacing && !request.replaceExisting)
    return core::failure<Output>(core::ErrorCode::Conflict, "This version is already installed", target.string());

  const auto originalTargetIdentity = replacing ? directoryIdentity(target) : std::nullopt;
  if (replacing && !originalTargetIdentity)
    return core::failure<Output>(core::ErrorCode::Conflict, "Installed version changed before staging", target.string());

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
  if (!stagingIdentity)
    return core::failure<Output>(core::ErrorCode::Conflict, "Cannot identify newly created staging directory");
  struct Cleanup final {
    std::filesystem::path path;
    std::optional<TreeIdentity> identity;
    std::filesystem::path emptyProduct;
    std::optional<TreeIdentity> emptyProductIdentity;
    bool armed{true};
    ~Cleanup() {
      if (!armed) return;
      removeOwnedTree(path, identity);
      std::error_code ignored;
      if (!emptyProduct.empty() && emptyProductIdentity && isTree(emptyProduct, *emptyProductIdentity))
        std::filesystem::remove(emptyProduct, ignored);  // Only succeeds while empty.
    }
  } cleanup{staging, stagingIdentity, productCreated ? product.value() : std::filesystem::path{}, productIdentity};

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
  if (!isTree(root.value(), *rootIdentity) || !isTree(product.value(), *productIdentity))
    return core::failure<Output>(core::ErrorCode::Conflict, "Installation parent directory changed before commit");
  if (replacing && previousIdentity != originalTargetIdentity)
    return core::failure<Output>(core::ErrorCode::Conflict, "Installed version changed during installation", target.string());
  if (!commitRename(rootHandle, staging.filename(), productHandle, request.version, replacing))
    return core::failure<Output>(core::ErrorCode::Conflict,
        replacing ? "Unable to swap in the new installation atomically; the installed version is unchanged"
                  : "Unable to publish the installation without replacing a concurrent one", target.string());
  // Committed. After a swap the staging path names the previous installation.
  cleanup.armed = false;
  Output output{target, replacing, true, {}};
  checked = request.faultInjector ? request.faultInjector(InstallStage::AfterCommitBeforeSync) : core::success();
  if (checked && (!isTree(root.value(), *rootIdentity) || !isTree(product.value(), *productIdentity)))
    checked = core::failure(core::ErrorCode::Conflict, "Installation parent path changed after commit");
  if (checked) checked = productHandle.sync();
  if (checked) checked = rootHandle.sync();
  // A previous interrupted attempt can leave existing but unsynced ancestors.
  // Flush the parent chain on this volume, including on retry, before confirming durability.
  for (auto parent = root.value().parent_path(); checked && !parent.empty();) {
    const auto parentIdentity = directoryIdentity(parent);
    if (!parentIdentity) {
      checked = core::failure(core::ErrorCode::IoError, "Cannot identify install ancestor for sync", parent.string());
      break;
    }
    if (parentIdentity->device != rootIdentity->device) break;
    checked = syncDirectory(parent);
    const auto next = parent.parent_path();
    if (next == parent) break;
    parent = next;
  }
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
