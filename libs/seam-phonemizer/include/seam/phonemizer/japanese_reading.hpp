#pragma once
#include "seam/core/result.hpp"
#include <cstddef>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace seam::phonemizer {
struct JapaneseReadingIdentity final {
  std::string engineRevision;
  std::string dictionarySha256;
  std::string helperSha256;
  friend bool operator==(const JapaneseReadingIdentity&, const JapaneseReadingIdentity&) = default;
};
enum class JapaneseReadingStatus { Known, Unknown, MissingReading };
struct JapaneseReadingToken final {
  std::size_t byteOffset{0U}, byteLength{0U};
  std::string surface;
  JapaneseReadingStatus status{JapaneseReadingStatus::Unknown};
  std::optional<std::string> lexicalReading;
  std::optional<std::string> pronunciation;
};
struct JapaneseReadingResult final {
  JapaneseReadingIdentity identity;
  std::string sourceSha256;
  std::vector<JapaneseReadingToken> tokens;
};
struct JapaneseReadingPhoneProjection final {
  std::size_t readingTokenIndex{0U};
  std::vector<std::string> phones;
};
// Converts only validated Known pronunciations to the existing SEAM Japanese
// phone inventory. Unknown readings stay empty; no fallback kana is guessed.
[[nodiscard]] core::Result<std::vector<JapaneseReadingPhoneProjection>> projectJapaneseReadingPhones(
    const JapaneseReadingResult& result, std::stop_token stop = {});
// Admission/semantic boundary for a decoded helper result. This does not spawn
// a parser, authenticate binaries, resolve note ownership or mutate lyrics.
[[nodiscard]] core::Result<void> validateJapaneseReading(std::string_view source,
    const JapaneseReadingResult& result, const JapaneseReadingIdentity& expected,
    std::stop_token stop = {});
}
