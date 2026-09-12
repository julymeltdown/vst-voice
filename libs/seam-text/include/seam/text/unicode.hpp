#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace seam::text {

[[nodiscard]] core::Result<std::u32string> decodeUtf8Strict(
    std::string_view text);
[[nodiscard]] core::Result<std::string> encodeUtf8Strict(
    std::u32string_view text);

[[nodiscard]] bool isCombiningMark(char32_t codePoint) noexcept;
[[nodiscard]] bool isCjkCodePoint(char32_t codePoint) noexcept;
[[nodiscard]] bool isCjkBreakOpportunity(char32_t codePoint) noexcept;
[[nodiscard]] std::size_t utf8DisplayWidth(std::string_view text) noexcept;
[[nodiscard]] std::string truncateUtf8ToDisplayWidth(
    std::string_view text, std::size_t maximumColumns);

struct Utf8LineRange final { std::size_t offset; std::size_t length; };
// Byte ranges partition the input exactly, retaining whitespace/newline bytes.
// Uses the same display-cluster rules as truncation; not a general UAX14 layout engine.
[[nodiscard]] core::Result<std::vector<Utf8LineRange>> wrapUtf8ToDisplayWidth(
    std::string_view text, std::size_t maximumColumns, std::size_t maximumLines = 131072U);

}  // namespace seam::text
