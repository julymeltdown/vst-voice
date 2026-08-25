#pragma once

#include "seam/core/result.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace seam::standalone {

inline constexpr std::string_view kExternalBetaEulaDocumentVersion =
    "external-beta-eula-1.0";

struct EulaAcceptanceRecord final {
  std::string documentVersion;
  std::string documentSha256;
  std::string acceptedAtUtc;
};

class EulaAcceptanceStore final {
public:
  [[nodiscard]] static core::Result<std::optional<EulaAcceptanceRecord>> load(
      const std::filesystem::path& path);
  [[nodiscard]] static core::Result<void> save(
      const std::filesystem::path& path,
      const EulaAcceptanceRecord& record);
  [[nodiscard]] static bool matches(
      const EulaAcceptanceRecord& record, std::string_view documentVersion,
      std::string_view documentSha256) noexcept;
};

}
