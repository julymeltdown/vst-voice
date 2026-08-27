#include "seam/phonemizer/japanese_phonemizer.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace seam::phonemizer {
namespace {

using PhoneList = std::vector<std::string>;

const std::map<std::u32string, PhoneList, std::less<>>& moraTable() {
  static const std::map<std::u32string, PhoneList, std::less<>> table{
      {U"あ", {"a"}}, {U"い", {"i"}}, {U"う", {"u"}}, {U"え", {"e"}}, {U"お", {"o"}},
      {U"ぁ", {"a"}}, {U"ぃ", {"i"}}, {U"ぅ", {"u"}}, {U"ぇ", {"e"}}, {U"ぉ", {"o"}},
      {U"か", {"k", "a"}}, {U"き", {"k", "i"}}, {U"く", {"k", "u"}},
      {U"け", {"k", "e"}}, {U"こ", {"k", "o"}},
      {U"が", {"g", "a"}}, {U"ぎ", {"g", "i"}}, {U"ぐ", {"g", "u"}},
      {U"げ", {"g", "e"}}, {U"ご", {"g", "o"}},
      {U"さ", {"s", "a"}}, {U"し", {"sh", "i"}}, {U"す", {"s", "u"}},
      {U"せ", {"s", "e"}}, {U"そ", {"s", "o"}},
      {U"ざ", {"z", "a"}}, {U"じ", {"j", "i"}}, {U"ず", {"z", "u"}},
      {U"ぜ", {"z", "e"}}, {U"ぞ", {"z", "o"}},
      {U"た", {"t", "a"}}, {U"ち", {"ch", "i"}}, {U"つ", {"ts", "u"}},
      {U"て", {"t", "e"}}, {U"と", {"t", "o"}},
      {U"だ", {"d", "a"}}, {U"ぢ", {"j", "i"}}, {U"づ", {"z", "u"}},
      {U"で", {"d", "e"}}, {U"ど", {"d", "o"}},
      {U"な", {"n", "a"}}, {U"に", {"n", "i"}}, {U"ぬ", {"n", "u"}},
      {U"ね", {"n", "e"}}, {U"の", {"n", "o"}},
      {U"は", {"h", "a"}}, {U"ひ", {"h", "i"}}, {U"ふ", {"f", "u"}},
      {U"へ", {"h", "e"}}, {U"ほ", {"h", "o"}},
      {U"ば", {"b", "a"}}, {U"び", {"b", "i"}}, {U"ぶ", {"b", "u"}},
      {U"べ", {"b", "e"}}, {U"ぼ", {"b", "o"}},
      {U"ぱ", {"p", "a"}}, {U"ぴ", {"p", "i"}}, {U"ぷ", {"p", "u"}},
      {U"ぺ", {"p", "e"}}, {U"ぽ", {"p", "o"}},
      {U"ま", {"m", "a"}}, {U"み", {"m", "i"}}, {U"む", {"m", "u"}},
      {U"め", {"m", "e"}}, {U"も", {"m", "o"}},
      {U"や", {"y", "a"}}, {U"ゆ", {"y", "u"}}, {U"よ", {"y", "o"}},
      {U"ゃ", {"y", "a"}}, {U"ゅ", {"y", "u"}}, {U"ょ", {"y", "o"}},
      {U"ら", {"r", "a"}}, {U"り", {"r", "i"}}, {U"る", {"r", "u"}},
      {U"れ", {"r", "e"}}, {U"ろ", {"r", "o"}},
      {U"わ", {"w", "a"}}, {U"ゐ", {"w", "i"}}, {U"ゑ", {"w", "e"}},
      {U"を", {"o"}}, {U"ゎ", {"w", "a"}}, {U"ゔ", {"v", "u"}},
      {U"きゃ", {"ky", "a"}}, {U"きゅ", {"ky", "u"}}, {U"きょ", {"ky", "o"}},
      {U"ぎゃ", {"gy", "a"}}, {U"ぎゅ", {"gy", "u"}}, {U"ぎょ", {"gy", "o"}},
      {U"しゃ", {"sh", "a"}}, {U"しゅ", {"sh", "u"}}, {U"しょ", {"sh", "o"}},
      {U"しぇ", {"sh", "e"}},
      {U"じゃ", {"j", "a"}}, {U"じゅ", {"j", "u"}}, {U"じょ", {"j", "o"}},
      {U"じぇ", {"j", "e"}},
      {U"ちゃ", {"ch", "a"}}, {U"ちゅ", {"ch", "u"}}, {U"ちょ", {"ch", "o"}},
      {U"ちぇ", {"ch", "e"}},
      {U"にゃ", {"ny", "a"}}, {U"にゅ", {"ny", "u"}}, {U"にょ", {"ny", "o"}},
      {U"ひゃ", {"hy", "a"}}, {U"ひゅ", {"hy", "u"}}, {U"ひょ", {"hy", "o"}},
      {U"びゃ", {"by", "a"}}, {U"びゅ", {"by", "u"}}, {U"びょ", {"by", "o"}},
      {U"ぴゃ", {"py", "a"}}, {U"ぴゅ", {"py", "u"}}, {U"ぴょ", {"py", "o"}},
      {U"みゃ", {"my", "a"}}, {U"みゅ", {"my", "u"}}, {U"みょ", {"my", "o"}},
      {U"りゃ", {"ry", "a"}}, {U"りゅ", {"ry", "u"}}, {U"りょ", {"ry", "o"}},
      {U"ふぁ", {"f", "a"}}, {U"ふぃ", {"f", "i"}}, {U"ふぇ", {"f", "e"}},
      {U"ふぉ", {"f", "o"}}, {U"ふゅ", {"fy", "u"}},
      {U"てぃ", {"t", "i"}}, {U"でぃ", {"d", "i"}},
      {U"とぅ", {"t", "u"}}, {U"どぅ", {"d", "u"}},
      {U"うぃ", {"w", "i"}}, {U"うぇ", {"w", "e"}}, {U"うぉ", {"w", "o"}},
      {U"つぁ", {"ts", "a"}}, {U"つぃ", {"ts", "i"}},
      {U"つぇ", {"ts", "e"}}, {U"つぉ", {"ts", "o"}},
      {U"ゔぁ", {"v", "a"}}, {U"ゔぃ", {"v", "i"}},
      {U"ゔぇ", {"v", "e"}}, {U"ゔぉ", {"v", "o"}}, {U"ゔゅ", {"vy", "u"}},
  };
  return table;
}

char32_t toHiragana(char32_t value) noexcept {
  if (value >= U'ァ' && value <= U'ヶ') {
    return value - 0x60;
  }
  return value;
}

bool isSmallKana(char32_t value) noexcept {
  switch (value) {
    case U'ぁ': case U'ぃ': case U'ぅ': case U'ぇ': case U'ぉ':
    case U'ゃ': case U'ゅ': case U'ょ': case U'ゎ':
      return true;
    default:
      return false;
  }
}

bool isSeparator(char32_t value) noexcept {
  switch (value) {
    case U' ': case U'\t': case U'\n': case U'\r':
    case U'、': case U'。': case U'！': case U'？':
    case U'!': case U'?': case U',': case U'.': case U'・':
      return true;
    default:
      return false;
  }
}

std::u32string normalize(const std::u32string& input) {
  std::u32string result;
  result.reserve(input.size());
  for (const auto value : input) {
    result.push_back(toHiragana(value));
  }
  return result;
}

std::optional<std::string> lastVowel(const std::vector<domain::PhonemeToken>& tokens) {
  for (auto iterator = tokens.rbegin(); iterator != tokens.rend(); ++iterator) {
    if (isVowelSymbol(iterator->symbol)) {
      return iterator->symbol;
    }
  }
  return std::nullopt;
}

void appendPhone(std::vector<domain::PhonemeToken>& target,
                 domain::NoteId noteId,
                 std::uint16_t& ordinal,
                 std::string symbol) {
  target.push_back(domain::PhonemeToken{
      .key = domain::PhonemeKey{noteId, ordinal},
      .symbol = std::move(symbol),
      .role = domain::PhonemeRole::Nucleus,
      .voiced = true,
      .timing = {},
      .locked = false,
  });
  auto& token = target.back();
  token.role = inferRole(token.symbol);
  token.voiced = isVoicedSymbol(token.symbol);
  ++ordinal;
}

void applyOverrides(const domain::VocalRegion& region,
                    domain::NoteId noteId,
                    std::vector<domain::PhonemeToken>& noteTokens,
                    std::vector<Warning>& warnings) {
  for (const auto& overrideValue : region.phonemeOverrides) {
    if (overrideValue.key.noteId != noteId) {
      continue;
    }
    const auto validation = overrideValue.validate();
    if (!validation) {
      warnings.push_back(Warning{
          .code = WarningCode::InvalidOverride,
          .noteId = noteId,
          .characterIndex = 0,
          .message = validation.error().message,
      });
      continue;
    }
    const auto index = static_cast<std::size_t>(overrideValue.key.ordinal);
    if (index < noteTokens.size()) {
      auto& token = noteTokens[index];
      if (overrideValue.symbol.has_value()) {
        token.symbol = *overrideValue.symbol;
        token.role = inferRole(token.symbol);
        token.voiced = isVoicedSymbol(token.symbol);
      }
      token.timing = overrideValue.timing;
      token.locked = overrideValue.locked;
      continue;
    }
    if (overrideValue.symbol.has_value() && index == noteTokens.size()) {
      domain::PhonemeToken token{
          .key = overrideValue.key,
          .symbol = *overrideValue.symbol,
          .role = inferRole(*overrideValue.symbol),
          .voiced = isVoicedSymbol(*overrideValue.symbol),
          .timing = overrideValue.timing,
          .locked = overrideValue.locked,
      };
      noteTokens.push_back(std::move(token));
      continue;
    }
    warnings.push_back(Warning{
        .code = WarningCode::OrphanOverride,
        .noteId = noteId,
        .characterIndex = index,
        .message = "Phoneme override does not match a generated token",
    });
  }
}

}  // namespace

Result JapaneseKanaPhonemizer::phonemize(const domain::VocalRegion& region) const {
  Result result;
  std::unordered_map<domain::LyricTokenId, const domain::LyricToken*> lyrics;
  lyrics.reserve(region.lyrics.size());
  for (const auto& lyric : region.lyrics) {
    lyrics.emplace(lyric.id, &lyric);
  }
  std::vector<const domain::Note*> notes;
  notes.reserve(region.notes.size());
  for (const auto& note : region.notes) {
    notes.push_back(&note);
  }
  std::stable_sort(notes.begin(), notes.end(), [](const auto* lhs, const auto* rhs) {
    if (lhs->startTick == rhs->startTick) return lhs->id < rhs->id;
    return lhs->startTick < rhs->startTick;
  });

  std::optional<std::string> previousVowel;
  for (const auto* note : notes) {
    const auto lyricEntry = lyrics.find(note->lyricTokenId);
    const auto* lyric = lyricEntry == lyrics.end() ? nullptr : lyricEntry->second;
    std::vector<domain::PhonemeToken> noteTokens;
    std::uint16_t ordinal = 0;
    if (lyric == nullptr || lyric->surface.empty()) {
      result.warnings.push_back(Warning{
          .code = WarningCode::EmptyLyric,
          .noteId = note->id,
          .characterIndex = 0,
          .message = "Note has no lyric text; a pause token was generated",
      });
      appendPhone(noteTokens, note->id, ordinal, "pau");
    } else {
      const auto text = normalize(lyric->surface);
      const bool continuation = text == U"-" || text == U"ー" || text == U"〜";
      if (continuation) {
        if (previousVowel.has_value()) {
          appendPhone(noteTokens, note->id, ordinal, *previousVowel);
        } else {
          result.warnings.push_back(Warning{
              .code = WarningCode::LeadingLongVowel,
              .noteId = note->id,
              .characterIndex = 0,
              .message = "A continuation lyric has no preceding vowel",
          });
          appendPhone(noteTokens, note->id, ordinal, "pau");
        }
      } else {
        for (std::size_t index = 0; index < text.size(); ++index) {
          const auto value = text[index];
          if (isSeparator(value)) {
            appendPhone(noteTokens, note->id, ordinal, "pau");
            continue;
          }
          if (value == U'ー' || value == U'〜') {
            const auto vowel = lastVowel(noteTokens).value_or(previousVowel.value_or(""));
            if (vowel.empty()) {
              result.warnings.push_back(Warning{
                  .code = WarningCode::LeadingLongVowel,
                  .noteId = note->id,
                  .characterIndex = index,
                  .message = "Long-vowel mark has no preceding vowel",
              });
            } else {
              appendPhone(noteTokens, note->id, ordinal, vowel);
            }
            continue;
          }
          if (value == U'っ') {
            appendPhone(noteTokens, note->id, ordinal, "cl");
            continue;
          }
          if (value == U'ん') {
            appendPhone(noteTokens, note->id, ordinal, "N");
            continue;
          }

          std::u32string mora(1, value);
          if (index + 1 < text.size() && isSmallKana(text[index + 1])) {
            std::u32string combined = mora;
            combined.push_back(text[index + 1]);
            if (moraTable().contains(combined)) {
              mora = std::move(combined);
              ++index;
            }
          }
          const auto iterator = moraTable().find(mora);
          if (iterator == moraTable().end()) {
            result.warnings.push_back(Warning{
                .code = WarningCode::UnsupportedCharacter,
                .noteId = note->id,
                .characterIndex = index,
                .message = "Unsupported Japanese lyric character",
            });
            appendPhone(noteTokens, note->id, ordinal, "pau");
            continue;
          }
          for (const auto& phone : iterator->second) {
            appendPhone(noteTokens, note->id, ordinal, phone);
          }
        }
      }
    }

    applyOverrides(region, note->id, noteTokens, result.warnings);
    if (const auto vowel = lastVowel(noteTokens)) {
      previousVowel = vowel;
    }
    result.tokens.insert(result.tokens.end(), noteTokens.begin(), noteTokens.end());
  }
  return result;
}

}  // namespace seam::phonemizer
