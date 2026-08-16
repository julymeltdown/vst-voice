#pragma once

#include "seam/core/result.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace seam::formats {

class JsonValue final {
public:
  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue, std::less<>>;
  using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

  JsonValue() noexcept : storage_(nullptr) {}
  JsonValue(std::nullptr_t) noexcept : storage_(nullptr) {}
  JsonValue(bool value) : storage_(value) {}
  JsonValue(double value) : storage_(value) {}
  JsonValue(std::int64_t value) : storage_(static_cast<double>(value)) {}
  JsonValue(std::string value) : storage_(std::move(value)) {}
  JsonValue(const char* value) : storage_(std::string(value)) {}
  JsonValue(Array value) : storage_(std::move(value)) {}
  JsonValue(Object value) : storage_(std::move(value)) {}

  [[nodiscard]] bool isNull() const noexcept { return std::holds_alternative<std::nullptr_t>(storage_); }
  [[nodiscard]] bool isBool() const noexcept { return std::holds_alternative<bool>(storage_); }
  [[nodiscard]] bool isNumber() const noexcept { return std::holds_alternative<double>(storage_); }
  [[nodiscard]] bool isString() const noexcept { return std::holds_alternative<std::string>(storage_); }
  [[nodiscard]] bool isArray() const noexcept { return std::holds_alternative<Array>(storage_); }
  [[nodiscard]] bool isObject() const noexcept { return std::holds_alternative<Object>(storage_); }

  [[nodiscard]] bool asBool() const { return std::get<bool>(storage_); }
  [[nodiscard]] double asNumber() const { return std::get<double>(storage_); }
  [[nodiscard]] std::int64_t asInt64() const;
  [[nodiscard]] const std::string& asString() const { return std::get<std::string>(storage_); }
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

[[nodiscard]] core::Result<JsonValue> parseJson(std::string_view input);
[[nodiscard]] std::string stringifyJson(const JsonValue& value, bool pretty = true);

}  // namespace seam::formats
