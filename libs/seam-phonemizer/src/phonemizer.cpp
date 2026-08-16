#include "seam/phonemizer/phonemizer.hpp"

#include <algorithm>

namespace seam::phonemizer {

std::vector<domain::PhonemeToken> Result::tokensForNote(domain::NoteId noteId) const {
  std::vector<domain::PhonemeToken> result;
  for (const auto& token : tokens) {
    if (token.key.noteId == noteId) {
      result.push_back(token);
    }
  }
  return result;
}

bool isVowelSymbol(std::string_view symbol) noexcept {
  return symbol == "a" || symbol == "i" || symbol == "u" ||
         symbol == "e" || symbol == "o";
}

bool isVoicedSymbol(std::string_view symbol) noexcept {
  return symbol != "pau" && symbol != "sil" && symbol != "cl" &&
         symbol != "k" && symbol != "ky" && symbol != "s" &&
         symbol != "sh" && symbol != "t" && symbol != "ch" &&
         symbol != "ts" && symbol != "h" && symbol != "hy" &&
         symbol != "f" && symbol != "p" && symbol != "py";
}

domain::PhonemeRole inferRole(std::string_view symbol) noexcept {
  if (isVowelSymbol(symbol)) return domain::PhonemeRole::Nucleus;
  if (symbol == "N") return domain::PhonemeRole::Coda;
  if (symbol == "cl") return domain::PhonemeRole::Geminate;
  if (symbol == "br") return domain::PhonemeRole::Breath;
  if (symbol == "pau" || symbol == "sil") return domain::PhonemeRole::Silence;
  return domain::PhonemeRole::Onset;
}

}  // namespace seam::phonemizer
