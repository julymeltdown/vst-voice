#pragma once

#include "seam/core/result.hpp"
#include "seam/platform/crash_capture.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace seam::platform::detail {

template <typename Handle, Handle Invalid>
class CrashWriterSlot final {
public:
  static_assert(std::atomic<Handle>::is_always_lock_free);
  static_assert(std::atomic<unsigned>::is_always_lock_free);

  void publish(Handle handle) noexcept {
    handle_.store(handle, std::memory_order_seq_cst);
  }

  [[nodiscard]] Handle load() const noexcept {
    return handle_.load(std::memory_order_seq_cst);
  }

  [[nodiscard]] Handle acquire() noexcept {
    writers_.fetch_add(1U, std::memory_order_seq_cst);
    return handle_.load(std::memory_order_seq_cst);
  }

  void release() noexcept {
    writers_.fetch_sub(1U, std::memory_order_seq_cst);
  }

  template <typename WaitForWriter>
  [[nodiscard]] bool retire(Handle expected, WaitForWriter waitForWriter) noexcept {
    // One SC order binds retained old handles to counted writers before closing
    // acquisition; writers entering after invalidation cannot retain that handle.
    if (!handle_.compare_exchange_strong(expected, Invalid,
                                         std::memory_order_seq_cst)) {
      return false;
    }
    while (writers_.load(std::memory_order_seq_cst) != 0U) {
      waitForWriter();
    }
    return true;
  }

private:
  std::atomic<Handle> handle_{Invalid};
  std::atomic<unsigned> writers_{0U};
};

inline constexpr std::array<char, 8U> kCrashPrimitiveMagic{
    'S', 'E', 'A', 'M', 'C', 'R', 'S', 'H'};
inline constexpr std::uint8_t kCrashPrimitiveVersion{1U};
inline constexpr std::uint32_t kCrashPrimitiveCommit{0x43525348U};

struct CrashPrimitiveRecord final {
  std::array<char, 8U> magic{};
  std::uint8_t version{0U};
  std::uint8_t cause{0U};
  std::uint16_t reserved{0U};
  std::uint32_t platformCode{0U};
  std::uint32_t processId{0U};
  std::uint32_t checksum{0U};
  std::uint32_t commit{0U};
  std::array<std::uint8_t, 4U> padding{};
};

static_assert(sizeof(CrashPrimitiveRecord) == 32U);

[[nodiscard]] constexpr std::uint32_t crashPrimitiveChecksum(
    CrashCause cause, std::uint32_t platformCode,
    std::uint32_t processId) noexcept {
  return 0x5345414DU ^ static_cast<std::uint32_t>(kCrashPrimitiveVersion) ^
         (static_cast<std::uint32_t>(cause) << 8U) ^ platformCode ^ processId ^
         kCrashPrimitiveCommit;
}

[[nodiscard]] constexpr CrashPrimitiveRecord makeCrashPrimitive(
    CrashCause cause, std::uint32_t platformCode,
    std::uint32_t processId) noexcept {
  return CrashPrimitiveRecord{
      .magic = kCrashPrimitiveMagic,
      .version = kCrashPrimitiveVersion,
      .cause = static_cast<std::uint8_t>(cause),
      .reserved = 0U,
      .platformCode = platformCode,
      .processId = processId,
      .checksum = crashPrimitiveChecksum(cause, platformCode, processId),
      .commit = kCrashPrimitiveCommit,
      .padding = {},
  };
}

class CrashCaptureBackend {
public:
  virtual ~CrashCaptureBackend() = default;

  CrashCaptureBackend(const CrashCaptureBackend&) = delete;
  CrashCaptureBackend& operator=(const CrashCaptureBackend&) = delete;

protected:
  CrashCaptureBackend() = default;
};

[[nodiscard]] core::Result<std::unique_ptr<CrashCaptureBackend>>
installCrashCaptureBackend(const std::filesystem::path& primitivePath);

}
