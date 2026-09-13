#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"

#include <string>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <vector>

namespace seam::authoring {

// One installed neural bundle, resolved from its own resource record. The record
// carries the identity a project saves; the directory carries the bytes the
// worker will re-admit. Both are verified during scanning, so a catalog cannot
// claim an identity its bundle does not have.
struct InstalledNeuralResource final {
  std::string id, version, contentHash;
  std::filesystem::path directory;
  friend bool operator==(const InstalledNeuralResource&,const InstalledNeuralResource&)=default;
};

// Bounded read-only index of an installation root. Scanning verifies each bundle
// record, its canonical manifest digest and every declared asset. Resolution is
// exact: an unknown or mismatched identity is refused instead of substituting a
// different voice.
class NeuralResourceRegistry final {
public:
  [[nodiscard]] static core::Result<NeuralResourceRegistry> scan(
      const std::filesystem::path& root,std::size_t maximumResources,
      std::size_t maximumAssetBytes,
      std::size_t maximumTotalBytes,std::stop_token stop = {});
  [[nodiscard]] const std::vector<InstalledNeuralResource>& resources() const noexcept {return resources_;}
  [[nodiscard]] core::Result<std::filesystem::path> resolve(
      const domain::NeuralResourceReference& selection) const;
private:
  std::vector<InstalledNeuralResource> resources_;
};

}  // namespace seam::authoring
