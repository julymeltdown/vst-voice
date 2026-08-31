#include "seam/voicebank_production/asset_store.hpp"

#include "seam/core/sha256.hpp"

#include <array>
#include <algorithm>
#include <system_error>

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
  if (!std::filesystem::exists(target, error)) {
    if (error) {
      return core::failure<AssetRecord>(
          core::ErrorCode::IoError, "Unable to inspect immutable asset target",
          error.message());
    }
    std::filesystem::copy_file(
        source, target, std::filesystem::copy_options::none, error);
    if (error) {
      return core::failure<AssetRecord>(
          core::ErrorCode::IoError, "Unable to import immutable asset",
          error.message());
    }
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
