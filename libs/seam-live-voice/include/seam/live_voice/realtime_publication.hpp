#pragma once

#include "seam/live_voice/live_resources.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace seam::live_voice {

class LiveResourcePublication final {
 public:
  class ReadHandle final {
   public:
    ReadHandle() = default;
    ReadHandle(const ReadHandle&) = delete;
    ReadHandle& operator=(const ReadHandle&) = delete;
    ReadHandle(ReadHandle&& other) noexcept;
    ReadHandle& operator=(ReadHandle&& other) noexcept;
    ~ReadHandle();

    [[nodiscard]] const LiveVoicebankResources* get() const noexcept {
      return value_;
    }
    [[nodiscard]] const LiveVoicebankResources* operator->() const noexcept {
      return value_;
    }
    [[nodiscard]] explicit operator bool() const noexcept {
      return value_ != nullptr;
    }

   private:
    friend class LiveResourcePublication;
    ReadHandle(const LiveResourcePublication* owner, std::size_t slot,
               const LiveVoicebankResources* value) noexcept;
    void release() noexcept;
    const LiveResourcePublication* owner_{nullptr};
    std::size_t slot_{0U};
    const LiveVoicebankResources* value_{nullptr};
  };

  [[nodiscard]] ReadHandle acquire() const noexcept;
  [[nodiscard]] bool publish(
      std::shared_ptr<const LiveVoicebankResources> resources);
  void clear() noexcept;

 private:
  static constexpr std::size_t kSlotCount = 3U;
  struct Slot final {
    std::shared_ptr<const LiveVoicebankResources> resources;
    mutable std::atomic<std::uint32_t> readers{0U};
  };
  std::array<Slot, kSlotCount> slots_{};
  std::atomic<int> published_{-1};
  std::mutex writerMutex_;
};

}
