#include "seam/phonemizer/japanese_phonemizer.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

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

struct NormalizedKana {
  std::u32string text;
  std::vector<std::size_t> sourceIndices;
};

NormalizedKana normalize(const std::u32string& input) {
  // Targeted kana normalization, not general NFC/NFKC. Mapping facts are pinned
  // to Unicode 17.0 UnicodeData: FF66..FF9F and kana + 3099/309A decompositions.
  static constexpr std::u32string_view halfwidth = U"ヲァィゥェォャュョッーアイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワン";
  static_assert(halfwidth.size() == 0xff9dU - 0xff66U + 1U);
  static constexpr std::u32string_view unvoiced = U"かきくけこさしすせそたちつてとはひふへほう";
  static constexpr std::u32string_view voiced = U"がぎぐげござじずぜぞだぢづでどばびぶべぼゔ";
  static_assert(unvoiced.size() == voiced.size());
  static constexpr std::u32string_view hRow = U"はひふへほ", pRow = U"ぱぴぷぺぽ";
  NormalizedKana result; result.text.reserve(input.size()); result.sourceIndices.reserve(input.size());
  for (std::size_t index = 0U; index < input.size(); ++index) {
    auto value = input[index];
    if (value >= 0xff66U && value <= 0xff9dU) value = halfwidth[value - 0xff66U];
    else if (value == 0xff9eU) value = 0x3099U;
    else if (value == 0xff9fU) value = 0x309aU;
    else if (value == 0xff61U) value = U'。';
    else if (value == 0xff64U) value = U'、';
    else if (value == 0xff65U) value = U'・';
    value = toHiragana(value);
    if (!result.text.empty() && (value == 0x3099U || value == 0x309aU)) {
      const auto bases = value == 0x3099U ? unvoiced : hRow;
      const auto replacements = value == 0x3099U ? voiced : pRow;
      const auto position = bases.find(result.text.back());
      if (position != std::u32string_view::npos) { result.text.back() = replacements[position]; continue; }
    }
    result.text.push_back(value); result.sourceIndices.push_back(index);
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

// An explicit phone hint is a sequence its author wrote, so position decides the role of an
// ordinary consonant: it is a coda when it directly follows a vowel and no vowel follows it in
// that hint, and an onset otherwise. Vowels and symbols with their own role (N, cl, br, pau,
// sil) keep it, so "k a", "s a" and "k a N" keep their previous meaning while "a s" can now
// express the vowel-to-coda unit an inventory names.
void assignHintRoles(std::vector<domain::PhonemeToken>& tokens, std::size_t first,
                     std::span<const std::string> phones) {
  for (std::size_t index = 0U; index < phones.size(); ++index) {
    auto& token = tokens[first + index];
    if (token.role != domain::PhonemeRole::Onset) continue;
    if (index == 0U || !isVowelSymbol(phones[index - 1U])) continue;
    const bool vowelFollows = std::any_of(phones.begin() + static_cast<std::ptrdiff_t>(index) + 1,
        phones.end(), [](const auto& phone) { return isVowelSymbol(phone); });
    if (!vowelFollows) token.role = domain::PhonemeRole::Coda;
  }
}

void applyOverrides(std::span<const domain::PhonemeOverride* const> overrides,
                    domain::NoteId noteId,
                    std::vector<domain::PhonemeToken>& noteTokens,
                    std::vector<Warning>& warnings) {
  for (const auto* entry : overrides) {
    const auto& overrideValue = *entry;
    if (overrideValue.unresolved) {
      warnings.push_back({.code = WarningCode::OrphanOverride, .noteId = noteId,
          .characterIndex = overrideValue.key.ordinal,
          .message = "Phoneme edit is retained but unresolved after pronunciation changed"});
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

const std::vector<std::string>& japanesePhoneSymbols() {
  static const auto symbols = [] {
    // These explicit events are accepted by phone hints even though they are not
    // emitted by a kana spelling: moraic closure, pause/hold/glottal events and breath.
    std::set<std::string, std::less<>> unique{"N", "cl", "pau", "R", "glottal", "br"};
    for (const auto& [mora, values] : moraTable()) {
      (void)mora;
      unique.insert(values.begin(), values.end());
    }
    return std::vector<std::string>{unique.begin(), unique.end()};
  }();
  return symbols;
}

core::Result<std::vector<std::string>> parseJapanesePhoneHint(std::string_view text) {
  if (text.empty() || text.size() > 4096U)
    return core::failure<std::vector<std::string>>(core::ErrorCode::InvalidArgument, "Japanese phone hint is empty or exceeds 4096 bytes");
  const auto& inventory = japanesePhoneSymbols();
  const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  std::vector<std::string> result;
  for (std::size_t i = 0U; i < text.size();) {
    while (i < text.size() && space(text[i])) ++i;
    if (i == text.size()) break;
    const auto start = i; while (i < text.size() && !space(text[i])) ++i;
    const std::string phone{text.substr(start, i - start)};
    if (!std::binary_search(inventory.begin(), inventory.end(), phone) || result.size() >= 256U)
      return core::failure<std::vector<std::string>>(core::ErrorCode::Unsupported, "Japanese phone hint requires at most 256 supported space-separated phones");
    result.push_back(phone);
  }
  if (result.empty()) return core::failure<std::vector<std::string>>(core::ErrorCode::InvalidArgument, "Japanese phone hint has no phones");
  return core::success(std::move(result));
}

Result JapaneseKanaPhonemizer::phonemize(const domain::VocalRegion& region) const {
  return std::move(phonemize(region, {})).value();
}

core::Result<Result> JapaneseKanaPhonemizer::phonemize(const domain::VocalRegion& region, std::stop_token stop,
    std::size_t maximumTokens) const {
  const auto cancelled = [] { return core::failure<Result>(core::ErrorCode::Conflict, "Japanese phonemization cancelled"); };
  if (stop.stop_requested()) return cancelled();
  if (region.notes.size() > 10000U || region.lyrics.size() > 10000U ||
      region.phonemeOverrides.size() > 4096U) {
    return core::failure<Result>(core::ErrorCode::InvalidArgument,
                                 "Japanese phonemization input exceeds collection bounds");
  }
  Result result;
  std::unordered_map<domain::LyricTokenId, const domain::LyricToken*> lyrics;
  lyrics.reserve(region.lyrics.size());
  std::size_t lyricCharacters = 0U;
  for (const auto& lyric : region.lyrics) {
    if (stop.stop_requested()) return cancelled();
    const auto readingSize = lyric.readingHint ? lyric.readingHint->size() : 0U;
    if (lyric.surface.size() > 4096U || readingSize > 4096U ||
        (lyric.readingHint && readingSize == 0U) ||
        lyric.surface.size() > 65536U - lyricCharacters ||
        readingSize > 65536U - lyricCharacters - lyric.surface.size()) {
      return core::failure<Result>(core::ErrorCode::InvalidArgument,
                                   "Japanese phonemization lyric text exceeds bounds");
    }
    lyricCharacters += lyric.surface.size() + readingSize;
    if (lyric.readingHint) {
      for (const auto codePoint : *lyric.readingHint) {
        const auto value = static_cast<std::uint32_t>(codePoint);
        if (value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU)) {
          return core::failure<Result>(core::ErrorCode::InvalidArgument,
                                       "Japanese phonemization reading hint contains invalid Unicode");
        }
      }
    }
    lyrics.emplace(lyric.id, &lyric);
  }
  std::unordered_map<domain::NoteId, std::vector<const domain::PhonemeOverride*>> overrides;
  for (const auto& edit : region.phonemeOverrides) {
    if (stop.stop_requested()) return cancelled();
    overrides[edit.key.noteId].push_back(&edit); // Preserve source edit order within each note.
  }
  std::vector<const domain::Note*> notes;
  notes.reserve(region.notes.size());
  for (const auto& note : region.notes) {
    if (stop.stop_requested()) return cancelled();
    notes.push_back(&note);
  }
  std::stable_sort(notes.begin(), notes.end(), [](const auto* lhs, const auto* rhs) {
    if (lhs->startTick == rhs->startTick) return lhs->id < rhs->id;
    return lhs->startTick < rhs->startTick;
  });

  std::optional<std::string> previousVowel;
  std::optional<std::string> previousNoteVowel;
  const domain::Note* previousNote = nullptr;
  for (const auto* note : notes) {
    if (stop.stop_requested()) return cancelled();
    const auto lyricEntry = lyrics.find(note->lyricTokenId);
    const auto* lyric = lyricEntry == lyrics.end() ? nullptr : lyricEntry->second;
    std::vector<domain::PhonemeToken> noteTokens;
    std::uint16_t ordinal = 0;
    if (note->phoneticHint) {
      const auto phones = parseJapanesePhoneHint(*note->phoneticHint);
      if (!phones) {
        result.warnings.push_back({.code = WarningCode::UnsupportedCharacter, .noteId = note->id,
            .characterIndex = 0U, .message = phones.error().message});
        appendPhone(noteTokens, note->id, ordinal, "pau");
      } else {
        const auto first = noteTokens.size();
        for (const auto& phone : phones.value()) appendPhone(noteTokens, note->id, ordinal, phone);
        assignHintRoles(noteTokens, first, phones.value());
      }
    } else if (lyric == nullptr || lyric->surface.empty()) {
      result.warnings.push_back(Warning{
          .code = WarningCode::EmptyLyric,
          .noteId = note->id,
          .characterIndex = 0,
          .message = "Note has no lyric text; a pause token was generated",
      });
      appendPhone(noteTokens, note->id, ordinal, "pau");
    } else {
      const auto& pronunciationText = lyric->readingHint
          ? *lyric->readingHint : lyric->surface;
      const auto normalized = normalize(pronunciationText);
      const auto& text = normalized.text;
      const bool sharedContinuation = previousNote && domain::continuesSharedLyric(*previousNote, *note);
      const bool continuation = text == U"-" || text == U"ー" || text == U"〜" || sharedContinuation;
      if (continuation) {
        const auto& vowel = sharedContinuation ? previousNoteVowel : previousVowel;
        if (vowel.has_value()) {
          appendPhone(noteTokens, note->id, ordinal, *vowel);
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
          if ((index & 255U) == 0U && stop.stop_requested()) return cancelled();
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
                  .characterIndex = normalized.sourceIndices[index],
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
                .characterIndex = normalized.sourceIndices[index],
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

    if (const auto edits = overrides.find(note->id); edits != overrides.end())
      applyOverrides(edits->second, note->id, noteTokens, result.warnings);
    previousNoteVowel = lastVowel(noteTokens);
    if (const auto& vowel = previousNoteVowel) {
      previousVowel = vowel;
    }
    if (noteTokens.size() > maximumTokens - result.tokens.size())
      return core::failure<Result>(core::ErrorCode::InvalidArgument, "Resolved pronunciation exceeds token bounds");
    result.tokens.insert(result.tokens.end(), noteTokens.begin(), noteTokens.end());
    previousNote = note;
  }
  if (stop.stop_requested()) return cancelled();
  return result;
}

}  // namespace seam::phonemizer
