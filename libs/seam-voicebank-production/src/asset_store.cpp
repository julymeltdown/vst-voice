#include "seam/voicebank_production/asset_store.hpp"

#include "seam/core/sha256.hpp"

#include <array>
#include <algorithm>
#include <atomic>
#include <random>
#include <system_error>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace seam::voicebank_production {
namespace {

bool isLowerDigest(std::string_view value) {
  return value.size() == 64U &&
         std::all_of(value.begin(), value.end(), [](char item) {
           return (item >= '0' && item <= '9') ||
                  (item >= 'a' && item <= 'f');
         });
}

core::Result<void> validateAssetLocation(
    const std::filesystem::path& root, const AssetRecord& asset) {
  if (!isLowerDigest(asset.sha256)) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Immutable asset digest is invalid");
  }
  const auto kind = asset.kind == AssetKind::Raw ? "raw" : "derived";
  const auto expected = std::filesystem::path{kind} /
                        asset.sha256.substr(0U, 2U) /
                        (asset.sha256 + ".wav");
  if (std::filesystem::path{asset.relativePath}.lexically_normal() != expected) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Immutable asset path is not content addressed");
  }
  const std::array directories{
      root, root / kind, root / kind / asset.sha256.substr(0U, 2U)};
  std::error_code error;
  for (const auto& directory : directories) {
    const auto status = std::filesystem::symlink_status(directory, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
      return core::failure(core::ErrorCode::Conflict,
                           "Immutable asset directory is unsafe",
                           directory.string());
    }
  }
  return core::success();
}

// Flush a file or directory so a later commit never names bytes that are
// still only in the page cache. Best effort outside POSIX.
core::Result<void> syncPath(const std::filesystem::path& path) {
#ifndef _WIN32
  const auto descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (descriptor < 0) {
    return core::failure(core::ErrorCode::IoError, "Unable to open imported asset for sync",
                         path.string());
  }
  const auto synced = ::fsync(descriptor);
  ::close(descriptor);
  if (synced != 0) {
    return core::failure(core::ErrorCode::IoError, "Unable to sync imported asset",
                         path.string());
  }
#else
  (void)path;
#endif
  return core::success();
}

std::filesystem::path privateCopyPath(const std::filesystem::path& target) {
  static std::atomic<std::uint64_t> counter{0U};
  std::random_device entropy;
  const auto nonce = (static_cast<std::uint64_t>(entropy()) << 32U) ^ entropy();
  return target.parent_path() /
         ("." + target.filename().string() + "." + std::to_string(counter.fetch_add(1U)) +
          "-" + std::to_string(nonce) + ".partial");
}

// Checks the bytes on disk, not the bytes the copy meant to write.
core::Result<void> verifyCopy(const std::filesystem::path& path, const AssetRecord& asset) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size != asset.byteSize) {
    return core::failure(core::ErrorCode::Conflict,
                         "Imported asset copy differs from its source size", path.string());
  }
  auto digest = core::sha256File(path);
  if (!digest) return core::Result<void>{digest.error()};
  if (digest.value() != asset.sha256) {
    return core::failure(core::ErrorCode::Conflict,
                         "Imported asset changed while it was being copied", path.string());
  }
  return core::success();
}

}

core::Result<AssetRecord> ImmutableAssetStore::importFile(
    const std::filesystem::path& source, AssetKind kind) const {
  if (root_.empty()) {
    return core::failure<AssetRecord>(
        core::ErrorCode::InvalidState, "Immutable asset-store root is empty");
  }
  auto digest = core::sha256File(source);
  if (!digest) return core::Result<AssetRecord>{digest.error()};
  std::error_code error;
  const auto size = std::filesystem::file_size(source, error);
  if (error) {
    return core::failure<AssetRecord>(
        core::ErrorCode::IoError, "Unable to inspect source asset", error.message());
  }
  const auto kindName = kind == AssetKind::Raw ? "raw" : "derived";
  const auto relative = std::filesystem::path{kindName} /
                        digest.value().substr(0U, 2U) /
                        (digest.value() + ".wav");
  const auto target = root_ / relative;
  std::filesystem::create_directories(target.parent_path(), error);
  if (error) {
    return core::failure<AssetRecord>(
        core::ErrorCode::IoError, "Unable to create immutable asset directory",
        error.message());
  }
  AssetRecord record{
      .sha256 = digest.value(),
      .relativePath = relative.generic_string(),
      .byteSize = size,
      .kind = kind,
  };
  auto location = validateAssetLocation(root_, record);
  if (!location) return core::Result<AssetRecord>{location.error()};
  const bool present = std::filesystem::exists(target, error);
  if (error) {
    return core::failure<AssetRecord>(
        core::ErrorCode::IoError, "Unable to inspect immutable asset target",
        error.message());
  }
  // An existing file at this address is reused only when it verifies. A copy
  // interrupted by an earlier crash, or bytes that no longer hash to their
  // name, are replaced by a verified copy instead of blocking the digest.
  if (!present || !verify(record)) {
    const auto partial = privateCopyPath(target);
    const auto discard = [&partial] {
      std::error_code ignored;
      std::filesystem::remove(partial, ignored);
    };
    std::filesystem::copy_file(
        source, partial, std::filesystem::copy_options::none, error);
    if (error) {
      discard();
      return core::failure<AssetRecord>(
          core::ErrorCode::IoError, "Unable to import immutable asset",
          error.message());
    }
    auto copied = verifyCopy(partial, record);
    if (copied) copied = syncPath(partial);
    if (!copied) {
      discard();
      return core::Result<AssetRecord>{copied.error()};
    }
    std::filesystem::rename(partial, target, error);
    if (error) {
      discard();
      return core::failure<AssetRecord>(
          core::ErrorCode::IoError, "Unable to publish immutable asset",
          error.message());
    }
    const auto directory = syncPath(target.parent_path());
    if (!directory) return core::Result<AssetRecord>{directory.error()};
  }
  auto verified = verify(record);
  if (!verified) return core::Result<AssetRecord>{verified.error()};
  return record;
}

core::Result<void> ImmutableAssetStore::verify(
    const AssetRecord& asset) const {
  auto location = validateAssetLocation(root_, asset);
  if (!location) return location;
  const auto path = pathFor(asset);
  std::error_code error;
  if (std::filesystem::is_symlink(path, error) ||
      !std::filesystem::is_regular_file(path, error)) {
    return core::failure(core::ErrorCode::NotFound,
                         "Immutable asset is unavailable", path.string());
  }
  if (error) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to inspect immutable asset", error.message());
  }
  const auto size = std::filesystem::file_size(path, error);
  if (error || size != asset.byteSize) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Immutable asset size changed", path.string());
  }
  auto digest = core::sha256File(path);
  if (!digest) return core::Result<void>{digest.error()};
  if (digest.value() != asset.sha256) {
    return core::failure(core::ErrorCode::InvariantViolation,
                         "Immutable asset digest changed", path.string());
  }
  return core::success();
}

std::filesystem::path ImmutableAssetStore::pathFor(
    const AssetRecord& asset) const {
  return (root_ / asset.relativePath).lexically_normal();
}

}
