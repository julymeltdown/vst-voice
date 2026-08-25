#pragma once

#include "seam/core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace seam::platform {

struct CrashCaptureConfig final {
  std::filesystem::path root;
};

struct CrashMarker final {
  std::int64_t schemaVersion{0};
  std::string purpose;
  std::string code;
  std::string createdAt;
};

class CrashCapture final {
public:
  [[nodiscard]] static core::Result<std::unique_ptr<CrashCapture>> install(
      CrashCaptureConfig config);
  ~CrashCapture();

  CrashCapture(const CrashCapture&) = delete;
  CrashCapture& operator=(const CrashCapture&) = delete;

  [[nodiscard]] core::Result<CrashMarker> writeMarker(
      std::string_view code) const;
  [[nodiscard]] core::Result<std::optional<CrashMarker>> readMarker() const;
  [[nodiscard]] core::Result<void> clearMarker() const;
  [[nodiscard]] const std::filesystem::path& root() const noexcept {
    return root_;
  }

private:
  explicit CrashCapture(std::filesystem::path root)
      : root_(std::move(root)) {}

  std::filesystem::path root_;
};

[[nodiscard]] std::string_view crashCaptureBackendName() noexcept;

}
