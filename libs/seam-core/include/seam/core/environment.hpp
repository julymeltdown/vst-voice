#pragma once

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace seam::core {

// Returns an owned snapshot so callers never retain a CRT environment pointer.
// Environment mutation still belongs to process setup/tests and must not race
// this read; the production application treats its environment as immutable.
inline std::optional<std::string> environmentVariable(std::string_view name) {
  if (name.empty() || name.find('\0') != std::string_view::npos) return std::nullopt;
  const std::string key{name};
#if defined(_WIN32)
  char* value = nullptr;
  std::size_t length = 0U;
  if (::_dupenv_s(&value, &length, key.c_str()) != 0 || value == nullptr) {
    std::free(value);
    return std::nullopt;
  }
  std::string result{value};
  std::free(value);
  return result;
#else
  const char* value = std::getenv(key.c_str());
  if (value == nullptr) return std::nullopt;
  return std::string{value};
#endif
}

}  // namespace seam::core
