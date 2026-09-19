#pragma once

#include <cmath>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace seam::core {

// Whole-token, locale-independent decimal parsing for non-realtime input paths.
// Avoid floating from_chars, unavailable in older supported Apple libc++.
template <typename Floating>
bool parseFiniteDecimal(std::string_view text, Floating& output) {
  static_assert(std::is_floating_point_v<Floating>);
  if (text.empty() || text.front() == '+' ||
      text.find_first_not_of("0123456789.eE+-") != std::string_view::npos) return false;
  Floating value{};
  std::istringstream stream{std::string{text}};
  stream.imbue(std::locale::classic());
  stream >> std::noskipws >> value;
  if (!stream.eof() || stream.fail() || !std::isfinite(value)) return false;
  const auto mantissa = text.substr(0, text.find_first_of("eE"));
  if (value == Floating{0} && mantissa.find_first_of("123456789") != std::string_view::npos)
    return false; // Do not silently accept underflow rounded to zero.
  output = value;
  return true;
}

}  // namespace seam::core
