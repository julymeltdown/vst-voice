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

namespace detail {
class CrashCaptureBackend;
}

struct CrashCaptureConfig final {
  std::filesystem::path root;
};

enum class CrashCause : std::uint8_t {
  Terminate = 1U,
  FatalSignal = 2U,
  UnhandledException = 3U,
};

struct PendingCrash final {
  CrashCause cause{CrashCause::Terminate};
  std::uint32_t platformCode{0U};
  std::uint32_t processId{0U};
};

struct CrashRecoveryContext final {
  std::string candidateId;
  std::string bankId;
  std::string bankVersion;
  std::string bankContentHash;
  std::string host;
  std::uint64_t audioUnderflowFrames{0U};
  std::uint64_t audioXruns{0U};

  friend bool operator==(const CrashRecoveryContext&,
                         const CrashRecoveryContext&) = default;
};

struct CrashMarker final {
  std::int64_t schemaVersion{0};
  std::string purpose;
  std::string code;
  std::string createdAt;
  std::uint32_t platformCode{0U};
  std::uint32_t processId{0U};
  bool contextAvailable{false};
  CrashRecoveryContext context;
};

class CrashCapture final {
public:
  [[nodiscard]] static core::Result<std::unique_ptr<CrashCapture>> install(
      CrashCaptureConfig config);
  [[nodiscard]] static core::Result<std::optional<PendingCrash>> readPending(
      const CrashCaptureConfig& config);
  [[nodiscard]] static core::Result<std::optional<CrashMarker>> recoverPending(
      const CrashCaptureConfig& config);
  ~CrashCapture();

  CrashCapture(const CrashCapture&) = delete;
  CrashCapture& operator=(const CrashCapture&) = delete;

  [[nodiscard]] core::Result<void> updateContext(
      const CrashRecoveryContext& context) const;
  [[nodiscard]] core::Result<std::optional<CrashMarker>> readMarker() const;
  [[nodiscard]] core::Result<void> clearMarker() const;
  [[nodiscard]] const std::filesystem::path& root() const noexcept {
    return root_;
  }

private:
  CrashCapture(std::filesystem::path root,
               std::unique_ptr<detail::CrashCaptureBackend> backend);

  std::filesystem::path root_;
  std::unique_ptr<detail::CrashCaptureBackend> backend_;
};

[[nodiscard]] std::string_view crashCaptureBackendName() noexcept;

}
