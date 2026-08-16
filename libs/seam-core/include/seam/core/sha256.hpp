#pragma once

#include "seam/core/result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace seam::core {

class Sha256 final {
public:
  Sha256() noexcept;

  void update(std::span<const std::byte> bytes) noexcept;
  void update(std::string_view text) noexcept;

  [[nodiscard]] std::array<std::byte, 32> digest() const noexcept;
  [[nodiscard]] std::string hexDigest() const;

private:
  void transform(const std::byte* block) noexcept;
  void finalizeInPlace() noexcept;

  std::array<std::uint32_t, 8> state_{};
  std::array<std::byte, 64> buffer_{};
  std::uint64_t totalBytes_{0};
  std::size_t bufferedBytes_{0};
  bool finalized_{false};
};

[[nodiscard]] std::string sha256Hex(std::span<const std::byte> bytes);
[[nodiscard]] std::string sha256Hex(std::string_view text);
[[nodiscard]] core::Result<std::string> sha256File(
    const std::filesystem::path& path,
    std::uint64_t maximumBytes = 4ULL * 1024ULL * 1024ULL * 1024ULL);

}  // namespace seam::core
