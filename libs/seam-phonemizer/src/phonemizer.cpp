#include "seam/phonemizer/phonemizer.hpp"

#include <algorithm>
#include <array>
#include <string_view>

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
  static constexpr std::array<std::string_view, 24U> vowels{
      "a", "i", "u", "e", "o", "aa", "ae", "ah", "ao", "aw",
      "ay", "eh", "er", "ey", "ih", "iy", "ow", "oy", "uh", "uw",
      "eo", "eu", "ax", "axr"};
  if (symbol.empty()) return false;
  auto base = symbol;
  if (base.size() > 1U && (base.back() == '0' || base.back() == '1' || base.back() == '2'))
    base.remove_suffix(1U);
  return std::find(vowels.begin(), vowels.end(), base) != vowels.end();
}

bool isNasalSymbol(std::string_view symbol) noexcept {
  return symbol=="m" || symbol=="n" || symbol=="ng" || symbol=="N";
}

bool isVoicedSymbol(std::string_view symbol) noexcept {
  return symbol != "pau" && symbol != "sil" && symbol != "cl" &&
         symbol != "R" && symbol != "glottal" && symbol != "br" &&
         symbol != "k" && symbol != "ky" && symbol != "kk" && symbol != "s" &&
         symbol != "sh" && symbol != "t" && symbol != "ch" &&
         symbol != "ts" && symbol != "h" && symbol != "hh" && symbol != "hy" && symbol != "fy" &&
         symbol != "f" && symbol != "p" && symbol != "py" && symbol != "pp" &&
         symbol != "tt" && symbol != "th" && symbol != "ph" && symbol != "kh" &&
         symbol != "ss" && symbol != "cch" && symbol != "chh";
}

domain::PhonemeRole inferRole(std::string_view symbol) noexcept {
  if (isVowelSymbol(symbol)) return domain::PhonemeRole::Nucleus;
  if (symbol == "N") return domain::PhonemeRole::Coda;
  if (symbol == "cl" || symbol == "R" || symbol == "glottal") return domain::PhonemeRole::Geminate;
  if (symbol == "br") return domain::PhonemeRole::Breath;
  if (symbol == "pau" || symbol == "sil") return domain::PhonemeRole::Silence;
  return domain::PhonemeRole::Onset;
}

}  // namespace seam::phonemizer
