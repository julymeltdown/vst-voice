#pragma once

#include "seam/core/result.hpp"

#include <filesystem>
#include <map>
#include <string>

namespace seam::voicebank {

// Expected encoded bytes for consumed bank-relative assets. The caller must
// establish the map's provenance; it does not itself confer signature trust.
using ExpectedAssetDigests = std::map<std::string, std::string, std::less<>>;

[[nodiscard]] core::Result<std::filesystem::path> resolveBankAsset(
    const std::filesystem::path& bankRoot,
    const std::filesystem::path& relativePath);

// The same checks as resolveBankAsset for many assets of one bank: the root is canonicalized once
// when the resolver opens, not once per asset. A catalogue scan resolves every unit file of every
// installed bank, and on macOS each canonicalization walks the whole path one getattrlist call per
// component. On POSIX a path built from the canonical root and components that are neither "." nor
// ".." nor symbolic links is already canonical, so it is returned without a second
// canonicalization; Windows still canonicalizes each asset (junctions are not reported as links).
class BankAssetResolver final {
public:
  [[nodiscard]] static core::Result<BankAssetResolver> open(const std::filesystem::path& bankRoot);
  [[nodiscard]] core::Result<std::filesystem::path> resolve(
      const std::filesystem::path& relativePath) const;

private:
  explicit BankAssetResolver(std::filesystem::path canonicalRoot) : root_(std::move(canonicalRoot)) {}
  std::filesystem::path root_;
};

}  // namespace seam::voicebank
