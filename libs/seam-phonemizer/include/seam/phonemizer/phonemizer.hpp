#pragma once

#include "seam/domain/project.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace seam::phonemizer {

enum class WarningCode {
  EmptyLyric,
  UnsupportedCharacter,
  LeadingLongVowel,
  OrphanOverride,
  InvalidOverride,
};

struct Warning final {
  WarningCode code{WarningCode::UnsupportedCharacter};
  domain::NoteId noteId;
  std::size_t characterIndex{0};
  std::string message;

  friend bool operator==(const Warning&, const Warning&) = default;
};

struct Result final {
  std::vector<domain::PhonemeToken> tokens;
  std::vector<Warning> warnings;

  [[nodiscard]] std::vector<domain::PhonemeToken> tokensForNote(
      domain::NoteId noteId) const;
};

class IPhonemizer {
public:
  virtual ~IPhonemizer() = default;

  [[nodiscard]] virtual domain::Language language() const noexcept = 0;
  [[nodiscard]] virtual Result phonemize(const domain::VocalRegion& region) const = 0;
};

[[nodiscard]] bool isVowelSymbol(std::string_view symbol) noexcept;
[[nodiscard]] bool isVoicedSymbol(std::string_view symbol) noexcept;
[[nodiscard]] domain::PhonemeRole inferRole(std::string_view symbol) noexcept;

}  // namespace seam::phonemizer
