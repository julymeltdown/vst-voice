#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>

namespace seam::core {

template <typename Tag>
class Id final {
public:
  using value_type = std::uint64_t;

  constexpr Id() noexcept = default;
  explicit constexpr Id(value_type value) noexcept : value_(value) {}

  [[nodiscard]] constexpr value_type value() const noexcept { return value_; }
  [[nodiscard]] constexpr bool valid() const noexcept { return value_ != 0; }
  [[nodiscard]] std::string toString() const {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << value_;
    return stream.str();
  }

  friend constexpr bool operator==(Id, Id) noexcept = default;
  friend constexpr auto operator<=>(Id, Id) noexcept = default;

private:
  value_type value_{0};
};

class IdGenerator final {
public:
  explicit IdGenerator(std::uint64_t first = 1) noexcept : next_(first) {}

  template <typename Tag>
  [[nodiscard]] Id<Tag> next() noexcept {
    return Id<Tag>{next_.fetch_add(1, std::memory_order_relaxed)};
  }

  void reserveAtLeast(std::uint64_t nextValue) noexcept {
    auto current = next_.load(std::memory_order_relaxed);
    while (current < nextValue &&
           !next_.compare_exchange_weak(current, nextValue,
                                        std::memory_order_relaxed,
                                        std::memory_order_relaxed)) {
    }
  }

  [[nodiscard]] std::uint64_t nextValue() const noexcept {
    return next_.load(std::memory_order_relaxed);
  }

private:
  std::atomic<std::uint64_t> next_;
};

template <typename Tag>
inline std::ostream& operator<<(std::ostream& stream, Id<Tag> id) {
  return stream << id.toString();
}

}  // namespace seam::core

namespace std {
template <typename Tag>
struct hash<seam::core::Id<Tag>> {
  size_t operator()(const seam::core::Id<Tag>& id) const noexcept {
    return hash<std::uint64_t>{}(id.value());
  }
};
}  // namespace std
