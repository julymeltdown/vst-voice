#include "seam/text/unicode.hpp"

#include <cstdint>
#include <optional>

namespace seam::text {

namespace {

struct Utf8Scalar final {
  char32_t value{0};
  std::size_t length{0U};
};

[[nodiscard]] std::optional<Utf8Scalar> decodeScalar(
    std::string_view text, std::size_t index) noexcept {
  if (index >= text.size()) return std::nullopt;
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
    return std::nullopt;
  }
  if (index + length > text.size()) return std::nullopt;
  for (std::size_t continuation = 1U; continuation < length;
       ++continuation) {
    const auto byte = static_cast<std::uint8_t>(text[index + continuation]);
    if ((byte & 0xC0U) != 0x80U) return std::nullopt;
    value = (value << 6U) | (byte & 0x3FU);
  }
  const bool overlong =
      (length == 2U && value < 0x80U) ||
      (length == 3U && value < 0x800U) ||
      (length == 4U && value < 0x10000U);
  if (overlong || value > 0x10FFFFU ||
      (value >= 0xD800U && value <= 0xDFFFU)) {
    return std::nullopt;
  }
  return Utf8Scalar{static_cast<char32_t>(value), length};
}

[[nodiscard]] std::size_t displayWidth(char32_t codePoint) noexcept {
  const auto value = static_cast<std::uint32_t>(codePoint);
  if (isCombiningMark(codePoint) || value == 0x200BU || value == 0x200CU ||
      value == 0x200DU || value == 0x2060U ||
      (value >= 0xFE00U && value <= 0xFE0FU) ||
      (value >= 0xE0100U && value <= 0xE01EFU) ||
      (value >= 0x1F3FBU && value <= 0x1F3FFU)) {
    return 0U;
  }
  if (isCjkCodePoint(codePoint) ||
      (value >= 0xFE10U && value <= 0xFE19U) ||
      (value >= 0xFE30U && value <= 0xFE6BU) ||
      (value >= 0xFF01U && value <= 0xFF60U) ||
      (value >= 0xFFE0U && value <= 0xFFE6U) ||
      (value >= 0x1F1E6U && value <= 0x1F1FFU) ||
      (value >= 0x1F300U && value <= 0x1FAFFU)) {
    return 2U;
  }
  return 1U;
}

[[nodiscard]] bool isZeroAdvance(char32_t codePoint) noexcept {
  return displayWidth(codePoint) == 0U;
}

[[nodiscard]] bool isEmojiPresentationBase(char32_t codePoint) noexcept {
  const auto value = static_cast<std::uint32_t>(codePoint);
  return (value >= 0x1F000U && value <= 0x1FAFFU) ||
         (value >= 0x2600U && value <= 0x27BFU);
}

[[nodiscard]] bool isRegionalIndicator(char32_t codePoint) noexcept {
  const auto value = static_cast<std::uint32_t>(codePoint);
  return value >= 0x1F1E6U && value <= 0x1F1FFU;
}

struct DisplayCluster final {
  std::size_t end{0U};
  std::size_t width{0U};
};

[[nodiscard]] std::optional<DisplayCluster> nextDisplayCluster(
    std::string_view text, std::size_t index) noexcept {
  const auto first = decodeScalar(text, index);
  if (!first.has_value()) return std::nullopt;
  auto end = index + first->length;
  const auto width = displayWidth(first->value);
  if (isRegionalIndicator(first->value)) {
    const auto second = decodeScalar(text, end);
    if (second.has_value() && isRegionalIndicator(second->value)) {
      end += second->length;
    }
    return DisplayCluster{.end = end, .width = 2U};
  }
  bool joinNext = false;
  while (end < text.size()) {
    const auto next = decodeScalar(text, end);
    if (!next.has_value()) break;
    if (isZeroAdvance(next->value)) {
      joinNext = next->value == 0x200DU || joinNext;
      end += next->length;
      continue;
    }
    if (joinNext && isEmojiPresentationBase(next->value)) {
      joinNext = false;
      end += next->length;
      continue;
    }
    break;
  }
  return DisplayCluster{.end = end, .width = width};
}

}

core::Result<std::u32string> decodeUtf8Strict(std::string_view text) {
  std::u32string output;
  output.reserve(text.size());
  for (std::size_t index = 0U; index < text.size();) {
    const auto scalar = decodeScalar(text, index);
    if (!scalar.has_value()) {
      return core::failure<std::u32string>(
          core::ErrorCode::ParseError, "Invalid UTF-8 scalar");
    }
    output.push_back(scalar->value);
    index += scalar->length;
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

std::size_t utf8DisplayWidth(std::string_view text) noexcept {
  std::size_t width = 0U;
  for (std::size_t index = 0U; index < text.size();) {
    const auto cluster = nextDisplayCluster(text, index);
    if (!cluster.has_value()) {
      ++width;
      ++index;
      continue;
    }
    width += cluster->width;
    index = cluster->end;
  }
  return width;
}

std::string truncateUtf8ToDisplayWidth(std::string_view text,
                                       std::size_t maximumColumns) {
  std::string result;
  result.reserve(text.size());
  std::size_t width = 0U;
  for (std::size_t index = 0U; index < text.size();) {
    const auto cluster = nextDisplayCluster(text, index);
    if (!cluster.has_value()) {
      if (width == maximumColumns) break;
      result.push_back(text[index]);
      ++width;
      ++index;
      continue;
    }
    if (width + cluster->width > maximumColumns) break;
    result.append(text.substr(index, cluster->end - index));
    width += cluster->width;
    index = cluster->end;
  }
  return result;
}

}  // namespace seam::text
