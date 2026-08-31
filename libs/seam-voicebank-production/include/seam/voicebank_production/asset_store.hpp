#pragma once

#include "seam/core/result.hpp"
#include "seam/voicebank_production/project.hpp"

#include <filesystem>

namespace seam::voicebank_production {

class ImmutableAssetStore final {
public:
  explicit ImmutableAssetStore(std::filesystem::path root)
      : root_(std::move(root)) {}

  [[nodiscard]] core::Result<AssetRecord> importFile(
      const std::filesystem::path& source, AssetKind kind) const;
  [[nodiscard]] core::Result<void> verify(const AssetRecord& asset) const;
  [[nodiscard]] std::filesystem::path pathFor(
      const AssetRecord& asset) const;
  [[nodiscard]] const std::filesystem::path& root() const noexcept {
    return root_;
  }

private:
  std::filesystem::path root_;
};

}
