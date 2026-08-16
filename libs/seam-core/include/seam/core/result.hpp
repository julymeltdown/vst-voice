#pragma once

#include "seam/core/error.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace seam::core {

template <typename T>
class [[nodiscard]] Result final {
public:
  Result(const T& value) : storage_(value) {}
  Result(T&& value) : storage_(std::move(value)) {}
  Result(const Error& error) : storage_(error) {}
  Result(Error&& error) : storage_(std::move(error)) {}

  [[nodiscard]] bool hasValue() const noexcept { return std::holds_alternative<T>(storage_); }
  explicit operator bool() const noexcept { return hasValue(); }

  [[nodiscard]] T& value() & {
    if (!hasValue()) {
      throw std::logic_error("Result does not contain a value");
    }
    return std::get<T>(storage_);
  }

  [[nodiscard]] const T& value() const& {
    if (!hasValue()) {
      throw std::logic_error("Result does not contain a value");
    }
    return std::get<T>(storage_);
  }

  [[nodiscard]] T&& value() && {
    if (!hasValue()) {
      throw std::logic_error("Result does not contain a value");
    }
    return std::move(std::get<T>(storage_));
  }

  [[nodiscard]] const Error& error() const& {
    if (hasValue()) {
      throw std::logic_error("Result does not contain an error");
    }
    return std::get<Error>(storage_);
  }

private:
  std::variant<T, Error> storage_;
};

template <>
class [[nodiscard]] Result<void> final {
public:
  Result() noexcept = default;
  Result(const Error& error) : error_(error), ok_(false) {}
  Result(Error&& error) : error_(std::move(error)), ok_(false) {}

  [[nodiscard]] bool hasValue() const noexcept { return ok_; }
  explicit operator bool() const noexcept { return ok_; }

  [[nodiscard]] const Error& error() const& {
    if (ok_) {
      throw std::logic_error("Result does not contain an error");
    }
    return error_;
  }

private:
  Error error_{};
  bool ok_{true};
};

inline Result<void> success() noexcept { return Result<void>{}; }

template <typename T>
inline Result<std::decay_t<T>> success(T&& value) {
  return Result<std::decay_t<T>>{std::forward<T>(value)};
}

inline Result<void> failure(ErrorCode code, std::string message, std::string context = {}) {
  return Result<void>{Error{code, std::move(message), std::move(context)}};
}

template <typename T>
inline Result<T> failure(ErrorCode code, std::string message, std::string context = {}) {
  return Result<T>{Error{code, std::move(message), std::move(context)}};
}

}  // namespace seam::core
