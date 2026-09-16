#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace seam::platform::detail {

inline std::optional<std::wstring> wideFromUtf8(std::string_view text) {
  if (text.empty()) return std::wstring{};
  if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return std::nullopt;
  const auto size = static_cast<int>(text.size());
  const int required = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, nullptr, 0);
  if (required <= 0) return std::nullopt;
  std::wstring result(static_cast<std::size_t>(required), L'\0');
  if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size,
          result.data(), required) != required) return std::nullopt;
  return result;
}

inline std::optional<std::string> utf8FromWide(std::wstring_view text) {
  if (text.empty()) return std::string{};
  if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return std::nullopt;
  const auto size = static_cast<int>(text.size());
  const int required = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size,
      nullptr, 0, nullptr, nullptr);
  if (required <= 0) return std::nullopt;
  std::string result(static_cast<std::size_t>(required), '\0');
  if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size,
          result.data(), required, nullptr, nullptr) != required) return std::nullopt;
  return result;
}

}  // namespace seam::platform::detail

#endif
