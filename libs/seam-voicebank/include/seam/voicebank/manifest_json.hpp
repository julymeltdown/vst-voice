#pragma once

#include "seam/voicebank/voicebank.hpp"

#include <filesystem>
#include <string>

namespace seam::voicebank {

class ManifestJsonCodec final {
public:
  [[nodiscard]] core::Result<std::string> encode(const Manifest& manifest) const;
  [[nodiscard]] core::Result<Manifest> decode(std::string_view json) const;
  [[nodiscard]] core::Result<void> save(const Manifest& manifest,
                                        const std::filesystem::path& path) const;
  [[nodiscard]] core::Result<Manifest> load(const std::filesystem::path& path) const;
};

}  // namespace seam::voicebank
