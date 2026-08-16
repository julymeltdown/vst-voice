#include "seam/text/unicode.hpp"

#include <cstdint>

namespace seam::text {

core::Result<std::u32string> decodeUtf8Strict(std::string_view text) {
  std::u32string output;
  output.reserve(text.size());
  for (std::size_t index = 0U; index < text.size();) {
    const auto first = static_cast<std::uint8_t>(text[index]);
    std::uint32_t value = 0U;
    std::size_t length = 0U;
    if (first <= 0x7FU) {
      value = first;
      length = 1U;
    } else if (first >= 0xC2U && first <= 0xDFU) {
      value = first & 0x1FU;
      length = 2U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      value = first & 0x0FU;
      length = 3U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      value = first & 0x07U;
      length = 4U;
    } else {
      return core::failure<std::u32string>(
          core::ErrorCode::ParseError, "Invalid UTF-8 leading byte");
    }
    if (index + length > text.size()) {
      return core::failure<std::u32string>(
          core::ErrorCode::ParseError, "Truncated UTF-8 sequence");
    }
    for (std::size_t continuation = 1U; continuation < length;
         ++continuation) {
      const auto byte = static_cast<std::uint8_t>(text[index + continuation]);
      if ((byte & 0xC0U) != 0x80U) {
        return core::failure<std::u32string>(
            core::ErrorCode::ParseError,
            "Invalid UTF-8 continuation byte");
      }
      value = (value << 6U) | (byte & 0x3FU);
    }
    const bool overlong =
        (length == 2U && value < 0x80U) ||
        (length == 3U && value < 0x800U) ||
        (length == 4U && value < 0x10000U);
    if (overlong || value > 0x10FFFFU ||
        (value >= 0xD800U && value <= 0xDFFFU)) {
      return core::failure<std::u32string>(
          core::ErrorCode::ParseError, "Invalid UTF-8 code point");
    }
    output.push_back(static_cast<char32_t>(value));
    index += length;
  }
  return output;
}

core::Result<std::string> encodeUtf8Strict(std::u32string_view text) {
  std::string output;
  output.reserve(text.size());
  for (const auto codePoint : text) {
    const auto value = static_cast<std::uint32_t>(codePoint);
    if (value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU)) {
      return core::failure<std::string>(core::ErrorCode::InvalidArgument,
                                        "Invalid Unicode scalar value");
    }
    if (value <= 0x7FU) {
      output.push_back(static_cast<char>(value));
    } else if (value <= 0x7FFU) {
      output.push_back(static_cast<char>(0xC0U | (value >> 6U)));
      output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else if (value <= 0xFFFFU) {
      output.push_back(static_cast<char>(0xE0U | (value >> 12U)));
      output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
      output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else {
      output.push_back(static_cast<char>(0xF0U | (value >> 18U)));
      output.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
      output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
      output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    }
  }
  return output;
}

bool isCombiningMark(char32_t codePoint) noexcept {
  const auto value = static_cast<std::uint32_t>(codePoint);
  return (value >= 0x0300U && value <= 0x036FU) ||
         (value >= 0x1AB0U && value <= 0x1AFFU) ||
         (value >= 0x1DC0U && value <= 0x1DFFU) ||
         (value >= 0x20D0U && value <= 0x20FFU) ||
         (value >= 0xFE20U && value <= 0xFE2FU);
}

bool isCjkCodePoint(char32_t codePoint) noexcept {
  const auto value = static_cast<std::uint32_t>(codePoint);
  return (value >= 0x1100U && value <= 0x11FFU) ||
         (value >= 0x2E80U && value <= 0x9FFFU) ||
         (value >= 0xAC00U && value <= 0xD7AFU) ||
         (value >= 0x3040U && value <= 0x30FFU) ||
         (value >= 0x31F0U && value <= 0x31FFU) ||
         (value >= 0xF900U && value <= 0xFAFFU) ||
         (value >= 0x20000U && value <= 0x2FA1FU);
}

bool isCjkBreakOpportunity(char32_t codePoint) noexcept {
  return isCjkCodePoint(codePoint) || codePoint == U' ' ||
         codePoint == U'\t' || codePoint == U'-' || codePoint == U'/' ||
         codePoint == U'·' || codePoint == U'、' || codePoint == U'。';
}

}  // namespace seam::text
