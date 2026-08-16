#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace seam::formats {

struct JsonParseLimits final {
  std::size_t maximumInputBytes{64U * 1024U * 1024U};
  std::size_t maximumDepth{64U};
  std::size_t maximumNodes{1'000'000U};
  std::size_t maximumStringBytes{4U * 1024U * 1024U};
  std::size_t maximumCollectionEntries{250'000U};
};

class JsonValue final {
public:
  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue, std::less<>>;
  using Storage = std::variant<std::nullptr_t, bool, std::int64_t, double,
                               std::string, Array, Object>;

  JsonValue() noexcept : storage_(nullptr) {}
  JsonValue(std::nullptr_t) noexcept : storage_(nullptr) {}
  JsonValue(bool value) : storage_(value) {}
  JsonValue(double value) : storage_(value) {}
  JsonValue(float value) : storage_(static_cast<double>(value)) {}
  JsonValue(std::int64_t value) : storage_(value) {}
  JsonValue(std::string value) : storage_(std::move(value)) {}
  JsonValue(const char* value) : storage_(std::string(value)) {}
  JsonValue(Array value) : storage_(std::move(value)) {}
  JsonValue(Object value) : storage_(std::move(value)) {}

  [[nodiscard]] bool isNull() const noexcept {
    return std::holds_alternative<std::nullptr_t>(storage_);
  }
  [[nodiscard]] bool isBool() const noexcept {
    return std::holds_alternative<bool>(storage_);
  }
  [[nodiscard]] bool isInteger() const noexcept {
    return std::holds_alternative<std::int64_t>(storage_);
  }
  [[nodiscard]] bool isNumber() const noexcept {
    return isInteger() || std::holds_alternative<double>(storage_);
  }
  [[nodiscard]] bool isString() const noexcept {
    return std::holds_alternative<std::string>(storage_);
  }
  [[nodiscard]] bool isArray() const noexcept {
    return std::holds_alternative<Array>(storage_);
  }
  [[nodiscard]] bool isObject() const noexcept {
    return std::holds_alternative<Object>(storage_);
  }

  [[nodiscard]] bool asBool() const { return std::get<bool>(storage_); }
  [[nodiscard]] double asNumber() const;
  [[nodiscard]] std::int64_t asInt64() const;
  [[nodiscard]] const std::string& asString() const {
    return std::get<std::string>(storage_);
  }
  [[nodiscard]] const Array& asArray() const { return std::get<Array>(storage_); }
  [[nodiscard]] Array& asArray() { return std::get<Array>(storage_); }
  [[nodiscard]] const Object& asObject() const { return std::get<Object>(storage_); }
  [[nodiscard]] Object& asObject() { return std::get<Object>(storage_); }

  [[nodiscard]] const JsonValue* find(std::string_view key) const noexcept;
  [[nodiscard]] JsonValue* find(std::string_view key) noexcept;

  [[nodiscard]] const Storage& storage() const noexcept { return storage_; }
  friend bool operator==(const JsonValue&, const JsonValue&) = default;

private:
  Storage storage_;
};

[[nodiscard]] core::Result<JsonValue> parseJson(
    std::string_view input,
    const JsonParseLimits& limits = {});
[[nodiscard]] std::string stringifyJson(const JsonValue& value,
                                        bool pretty = true);

}  // namespace seam::formats
