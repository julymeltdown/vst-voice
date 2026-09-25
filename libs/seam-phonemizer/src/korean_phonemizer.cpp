#include "seam/phonemizer/korean_phonemizer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace seam::phonemizer {
namespace {

using PhoneList = std::vector<std::string>;

const std::map<std::string, PhoneList, std::less<>>& romanizedWords() {
  static const std::map<std::string, PhoneList, std::less<>> values{
      {"annyeong", {"a", "n", "n", "y", "eo", "ng"}},
      {"gamsa", {"k", "a", "m", "s", "a"}},
      {"norae", {"n", "o", "r", "ae"}},
      {"sarang", {"s", "a", "r", "a", "ng"}},
      {"seam", {"s", "i", "m"}},
  };
  return values;
}

std::optional<std::size_t> lexicalNInsertionBoundary(
    std::u32string_view word) noexcept {
  // Rule 29 depends on compound/morpheme boundaries, not just adjacent
  // spelling. Keep this intentionally lexical until a reviewed dictionary can
  // supply boundaries; applying it to every vowel-initial Hangul syllable
  // would turn ordinary liaison such as 꽃이 into a false nasal sequence.
  // Sources: NIKL Standard Pronunciation Rule 29 examples and dictionary
  // answers for 꽃잎, 깻잎, 막일, 솜이불, 홑이불, 한여름, 나뭇잎, 논일, 앞이마.
  static constexpr std::array<std::pair<std::u32string_view, std::size_t>, 9U> entries{{
      {U"꽃잎", 1U}, {U"깻잎", 1U}, {U"막일", 1U},
      {U"솜이불", 1U}, {U"홑이불", 1U}, {U"한여름", 1U},
      {U"나뭇잎", 2U}, {U"논일", 1U}, {U"앞이마", 1U},
  }};
  const auto found = std::find_if(entries.begin(), entries.end(), [word](const auto& entry) {
    return entry.first == word;
  });
  return found == entries.end() ? std::nullopt
                                : std::optional<std::size_t>{found->second};
}

std::u32string composeModernHangul(std::u32string_view text) {
  // Unicode Standard Annex #15 defines algorithmic composition for modern
  // Hangul L/V/T jamo. Accept canonically decomposed lyrics without adding a
  // locale-sensitive dependency; leave compatibility and archaic jamo intact
  // so the normal unsupported-input diagnostic remains authoritative.
  constexpr char32_t leadingBase = 0x1100U;
  constexpr char32_t vowelBase = 0x1161U;
  constexpr char32_t trailingBase = 0x11a7U;
  constexpr std::size_t leadingCount = 19U;
  constexpr std::size_t vowelCount = 21U;
  constexpr std::size_t trailingCount = 28U;
  constexpr char32_t syllableBase = 0xac00U;

  std::u32string result;
  result.reserve(text.size());
  for (std::size_t index = 0U; index < text.size();) {
    const auto leading = text[index];
    if (leading < leadingBase || leading >= leadingBase + leadingCount ||
        index + 1U >= text.size()) {
      result.push_back(leading);
      ++index;
      continue;
    }
    const auto vowel = text[index + 1U];
    if (vowel < vowelBase || vowel >= vowelBase + vowelCount) {
      result.push_back(leading);
      ++index;
      continue;
    }

    const auto leadingIndex = static_cast<std::size_t>(leading - leadingBase);
    const auto vowelIndex = static_cast<std::size_t>(vowel - vowelBase);
    std::size_t trailingIndex = 0U;
    index += 2U;
    if (index < text.size() && text[index] > trailingBase &&
        text[index] < trailingBase + trailingCount) {
      trailingIndex = static_cast<std::size_t>(text[index] - trailingBase);
      ++index;
    }
    result.push_back(static_cast<char32_t>(syllableBase +
        (leadingIndex * vowelCount + vowelIndex) * trailingCount + trailingIndex));
  }
  return result;
}

std::string lowerAscii(const std::u32string& text) {
  std::string result;
  result.reserve(text.size());
  for (const auto value : text) {
    if (value > 0x7fU) return {};
    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value))));
  }
  return result;
}

bool punctuation(std::string_view text) {
  if (text.empty()) return false;
  return std::all_of(text.begin(), text.end(), [](char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
        value == ',' || value == '.' || value == '!' || value == '?' ||
        value == ';' || value == ':' || value == '-' || value == '~' ||
        value == '\'' || value == '"';
  });
}

std::string initialPhone(std::size_t index) {
  static constexpr std::array<std::string_view, 19U> values{
      "k", "kk", "n", "t", "tt", "r", "m", "p", "pp", "s",
      "ss", "", "ch", "cch", "chh", "kh", "th", "ph", "h"};
  return std::string{values[index]};
}

PhoneList medialPhones(std::size_t index) {
  static const std::array<PhoneList, 21U> values{{
      {"a"}, {"ae"}, {"y", "a"}, {"y", "ae"}, {"eo"}, {"e"},
      {"y", "eo"}, {"y", "e"}, {"o"}, {"w", "a"}, {"w", "ae"},
      {"w", "e"}, {"y", "o"}, {"u"}, {"w", "eo"}, {"w", "e"},
      {"w", "i"}, {"y", "u"}, {"eu"}, {"eu", "i"}, {"i"}}};
  return values[index];
}

std::string finalPhone(std::size_t index) {
  // Unicode 16.0 section 3.12 fixes the 28 jongseong indices. These are
  // word-final/pre-consonant defaults (NIKL standard pronunciation 8-11);
  // keep the original index for liaison, rather than moving this reduced sound.
  static constexpr std::array<std::string_view, 28U> values{
      "", "k", "k", "k", "n", "n", "n", "t", "l", "k", "m",
      "l", "l", "l", "p", "l", "m", "p", "p", "t", "t", "ng",
      "t", "t", "k", "t", "p", "t"};
  return std::string{values[index]};
}

struct Syllable final {
  char32_t source;
  std::string onset;
  PhoneList vowels;
  std::string coda;
  std::size_t finalIndex;
};

std::optional<Syllable> decomposeHangul(char32_t value) {
  if (value < 0xac00U || value > 0xd7a3U) return std::nullopt;
  const auto code = static_cast<std::size_t>(value - 0xac00U);
  const auto onset = code / (21U * 28U);
  const auto medial = (code / 28U) % 21U;
  const auto final = code % 28U;
  return Syllable{value, initialPhone(onset), medialPhones(medial), finalPhone(final), final};
}

// Surface-level boundary rules. Suffix/compound distinctions, irregular stems
// and lexical exceptions require a dictionary; this does not claim that review.
void connectSyllables(Syllable& left, Syllable& right) {
  // Before initial k the final cluster lk keeps l (NIKL article 11).
  if (left.finalIndex == 9U && right.onset == "k") {
    left.coda = "l"; right.onset = "kk"; return;
  }
  // NIKL Standard Pronunciation Rule 10 treats 밟- as [밥] before a
  // consonant, unlike the default ㄼ -> [ㄹ]. 넓- keeps [ㄹ] in inflection
  // (넓고 [널꼬]) but has listed compounds with [ㅂ] (넓적/넓죽/넓둥글-).
  // Rule 25 tensifies ㄱ/ㄷ/ㅅ/ㅈ after the ㄼ adjective stem 넓-.
  if (!right.onset.empty() && left.source == U'밟') {
    left.coda = "p";
  } else if (!right.onset.empty() && left.source == U'넓') {
    if (right.source == U'적' || right.source == U'죽' || right.source == U'둥') {
      left.coda = "p";
    } else if (right.onset == "k") {
      right.onset = "kk";
    } else if (right.onset == "t") {
      right.onset = "tt";
    } else if (right.onset == "s") {
      right.onset = "ss";
    } else if (right.onset == "ch") {
      right.onset = "cch";
    }
  } else if (!right.onset.empty() && left.finalIndex == 13U) {
    // The ㄾ final is [ㄹ] before a consonant. In the listed stem+ending
    // pattern, ㄱ/ㄷ/ㅅ/ㅈ are also tensified (핥고 [할꼬], 핥다 [할따]).
    left.coda = "l";
    if (right.onset == "k") right.onset = "kk";
    else if (right.onset == "t") right.onset = "tt";
    else if (right.onset == "s") right.onset = "ss";
    else if (right.onset == "ch") right.onset = "cch";
  }
  if (right.onset.empty()) {
    // A final ng stays a coda; it is not an initial consonant in Korean.
    if (left.finalIndex == 0U || left.finalIndex == 21U) return;
    static constexpr std::array<std::string_view, 28U> retained{
        "", "", "", "k", "", "n", "", "", "", "l", "l", "l", "l", "l",
        "l", "", "", "", "p", "", "", "ng", "", "", "", "", "", ""};
    static constexpr std::array<std::string_view, 28U> released{
        "", "k", "kk", "ss", "n", "ch", "n", "t", "r", "k", "m", "p", "ss", "th",
        "ph", "r", "m", "p", "ss", "s", "ss", "", "ch", "chh", "kh", "th", "ph", ""};
    left.coda = retained[left.finalIndex];
    right.onset = released[left.finalIndex];
    if (right.vowels == PhoneList{"i"}) {
      if (right.onset == "t") right.onset = "ch";
      else if (right.onset == "th") right.onset = "chh";
    }
    return;
  }
  const auto aspirate = [](std::string_view phone) -> std::string {
    if (phone == "k") return "kh";
    if (phone == "t") return "th";
    if (phone == "p") return "ph";
    if (phone == "ch") return "chh";
    return {};
  };
  if (left.finalIndex == 6U || left.finalIndex == 15U || left.finalIndex == 27U) {
    const auto aspirated = aspirate(right.onset);
    if (!aspirated.empty() || right.onset == "s") {
      left.coda = left.finalIndex == 6U ? "n" : left.finalIndex == 15U ? "l" : "";
      right.onset = aspirated.empty() ? "ss" : aspirated;
    }
  } else if (right.onset == "h") {
    const auto last = left.finalIndex == 5U || left.finalIndex == 22U ? "ch" : left.coda;
    const auto aspirated = aspirate(last);
    if (!aspirated.empty()) {
      left.coda = left.finalIndex == 5U ? "n" :
          left.finalIndex == 9U || left.finalIndex == 14U ? "l" : "";
      right.onset = aspirated;
    }
  }
  if ((left.coda == "n" && right.onset == "r") || (left.coda == "l" && right.onset == "n")) {
    left.coda = "l"; right.onset = "l";
  } else {
    if (right.onset == "r" && (left.coda == "m" || left.coda == "ng" ||
        left.coda == "k" || left.coda == "t" || left.coda == "p")) right.onset = "n";
    if (right.onset == "n" || right.onset == "m") {
      if (left.coda == "k") left.coda = "ng";
      else if (left.coda == "t") left.coda = "n";
      else if (left.coda == "p") left.coda = "m";
    }
  }
  if (left.coda == "k" || left.coda == "t" || left.coda == "p") {
    if (right.onset == "k") right.onset = "kk";
    else if (right.onset == "t") right.onset = "tt";
    else if (right.onset == "p") right.onset = "pp";
    else if (right.onset == "s") right.onset = "ss";
    else if (right.onset == "ch") right.onset = "cch";
  }
}

std::optional<std::string> compatibilityJamo(char32_t value) {
  static const std::unordered_map<char32_t, std::string> consonants{
      {U'ㄱ', "k"}, {U'ㄲ', "kk"}, {U'ㄴ', "n"}, {U'ㄷ', "t"},
      {U'ㄸ', "tt"}, {U'ㄹ', "r"}, {U'ㅁ', "m"}, {U'ㅂ', "p"},
      {U'ㅃ', "pp"}, {U'ㅅ', "s"}, {U'ㅆ', "ss"}, {U'ㅇ', "ng"},
      {U'ㅈ', "ch"}, {U'ㅉ', "cch"}, {U'ㅊ', "chh"}, {U'ㅋ', "kh"},
      {U'ㅌ', "th"}, {U'ㅍ', "ph"}, {U'ㅎ', "h"},
  };
  const auto found = consonants.find(value);
  return found == consonants.end() ? std::nullopt : std::optional{found->second};
}

void appendPhone(std::vector<domain::PhonemeToken>& target, domain::NoteId note,
    std::uint16_t& ordinal, std::string symbol, std::optional<domain::PhonemeRole> roleOverride = {}) {
  const auto role = roleOverride.value_or(inferRole(symbol));
  const auto voiced = isVoicedSymbol(symbol);
  target.push_back(domain::PhonemeToken{
      .key = domain::PhonemeKey{note, ordinal}, .symbol = std::move(symbol),
      .role = role, .voiced = voiced,
      .timing = {}, .locked = false});
  ++ordinal;
}

void appendPhones(std::vector<domain::PhonemeToken>& target, domain::NoteId note,
    std::uint16_t& ordinal, const PhoneList& phones) {
  const auto first = target.size();
  for (const auto& phone : phones) appendPhone(target, note, ordinal, phone);
  // Flat user hints retain their exact symbols. Position consonants after a
  // nucleus as codas except the following syllable's one onset plus glide.
  bool hasNucleus = false;
  for (std::size_t index = first; index < target.size();) {
    if (target[index].role == domain::PhonemeRole::Nucleus) { hasNucleus = true; ++index; continue; }
    if (target[index].role == domain::PhonemeRole::Silence) { hasNucleus = false; ++index; continue; }
    const auto begin = index;
    while (index < target.size() && target[index].role != domain::PhonemeRole::Nucleus &&
        target[index].role != domain::PhonemeRole::Silence) ++index;
    if (!hasNucleus) continue;
    std::size_t onsetCount = 0U;
    if (index < target.size() && target[index].role == domain::PhonemeRole::Nucleus) {
      onsetCount = (target[index - 1U].symbol == "y" || target[index - 1U].symbol == "w") ? 2U : 1U;
    }
    const auto codaEnd = index - std::min(onsetCount, index - begin);
    for (auto coda = begin; coda < codaEnd; ++coda) target[coda].role = domain::PhonemeRole::Coda;
  }
}

void applyOverrides(std::span<const domain::PhonemeOverride* const> overrides,
    domain::NoteId noteId, std::vector<domain::PhonemeToken>& tokens,
    std::vector<Warning>& warnings) {
  for (const auto* entry : overrides) {
    const auto& value = *entry;
    if (value.unresolved) {
      warnings.push_back({WarningCode::OrphanOverride, noteId, value.key.ordinal,
          "Korean phoneme edit is retained but unresolved after pronunciation changed"});
      continue;
    }
    const auto valid = value.validate();
    if (!valid) {
      warnings.push_back({WarningCode::InvalidOverride, noteId, 0U,
          valid.error().message});
      continue;
    }
    const auto index = static_cast<std::size_t>(value.key.ordinal);
    if (index < tokens.size()) {
      auto& token = tokens[index];
      if (value.symbol) {
        token.symbol = *value.symbol;
        const auto role = inferRole(token.symbol);
        token.role = token.role == domain::PhonemeRole::Coda && role == domain::PhonemeRole::Onset
            ? domain::PhonemeRole::Coda : role;
        token.voiced = isVoicedSymbol(token.symbol);
      }
      token.timing = value.timing;
      token.locked = value.locked;
    } else if (value.symbol && index == tokens.size()) {
      tokens.push_back(domain::PhonemeToken{
          .key = value.key, .symbol = *value.symbol,
          .role = inferRole(*value.symbol), .voiced = isVoicedSymbol(*value.symbol),
          .timing = value.timing, .locked = value.locked});
    } else {
      warnings.push_back({WarningCode::OrphanOverride, noteId, index,
          "Korean phoneme override does not match a generated token"});
    }
  }
}

}  // namespace

core::Result<std::vector<std::string>> parseKoreanPhoneHint(std::string_view text) {
  using Output = std::vector<std::string>;
  if (text.empty() || text.size() > 4096U)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Korean phone hint is empty or exceeds 4096 bytes");
  static const auto inventory = [] {
    return std::unordered_set<std::string>{
        "a", "ae", "e", "i", "eo", "o", "eu", "u", "y", "w", "k", "kk", "n", "t", "tt",
        "r", "l", "m", "p", "pp", "s", "ss", "ng", "ch", "cch", "kh",
        "chh", "th", "ph", "h", "pau", "N"};
  }();
  Output result;
  for (std::size_t offset = 0U; offset < text.size();) {
    while (offset < text.size() && std::isspace(static_cast<unsigned char>(text[offset]))) ++offset;
    if (offset == text.size()) break;
    const auto start = offset;
    while (offset < text.size() && !std::isspace(static_cast<unsigned char>(text[offset]))) ++offset;
    std::string phone{text.substr(start, offset - start)};
    if (!inventory.contains(phone) || result.size() >= 256U)
      return core::failure<Output>(core::ErrorCode::Unsupported,
          "Korean phone hint requires at most 256 supported space-separated phones");
    result.push_back(std::move(phone));
  }
  if (result.empty()) return core::failure<Output>(core::ErrorCode::InvalidArgument,
      "Korean phone hint has no phones");
  return core::success(std::move(result));
}

Result KoreanHangulPhonemizer::phonemize(const domain::VocalRegion& region) const {
  return std::move(phonemize(region, {})).value();
}

core::Result<Result> KoreanHangulPhonemizer::phonemize(const domain::VocalRegion& region,
    std::stop_token stop, std::size_t maximumTokens) const {
  const auto cancelled = [] {
    return core::failure<Result>(core::ErrorCode::Conflict, "Korean phonemization cancelled");
  };
  if (stop.stop_requested()) return cancelled();
  std::unordered_map<domain::LyricTokenId, const domain::LyricToken*> lyrics;
  for (const auto& lyric : region.lyrics) lyrics.emplace(lyric.id, &lyric);
  std::unordered_map<domain::NoteId, std::vector<const domain::PhonemeOverride*>> overrides;
  for (const auto& edit : region.phonemeOverrides) overrides[edit.key.noteId].push_back(&edit);
  std::vector<const domain::Note*> notes;
  for (const auto& note : region.notes) notes.push_back(&note);
  std::stable_sort(notes.begin(), notes.end(), [](const auto* lhs, const auto* rhs) {
    return lhs->startTick == rhs->startTick ? lhs->id < rhs->id : lhs->startTick < rhs->startTick;
  });
  Result result;
  std::optional<std::string> previousVowel;
  for (std::size_t noteIndex = 0U; noteIndex < notes.size(); ++noteIndex) {
    const auto* note = notes[noteIndex];
    if (stop.stop_requested()) return cancelled();
    if (noteIndex > 0U && notes[noteIndex - 1U]->endTick() != note->startTick) previousVowel.reset();
    std::vector<domain::PhonemeToken> noteTokens;
    std::uint16_t ordinal = 0U;
    const auto lyricEntry = lyrics.find(note->lyricTokenId);
    const auto* lyric = lyricEntry == lyrics.end() ? nullptr : lyricEntry->second;
    if (note->phoneticHint) {
      const auto parsed = parseKoreanPhoneHint(*note->phoneticHint);
      if (!parsed) {
        result.warnings.push_back({WarningCode::UnsupportedCharacter, note->id, 0U, parsed.error().message});
        appendPhone(noteTokens, note->id, ordinal, "pau");
      } else appendPhones(noteTokens, note->id, ordinal, parsed.value());
    } else if (lyric == nullptr || lyric->surface.empty()) {
      result.warnings.push_back({WarningCode::EmptyLyric, note->id, 0U,
          "Note has no Korean lyric text; a pause token was generated"});
      appendPhone(noteTokens, note->id, ordinal, "pau");
    } else if (lyric->language != domain::Language::Korean && lyric->language != domain::Language::Unspecified) {
      result.warnings.push_back({WarningCode::UnsupportedCharacter, note->id, 0U,
          "Korean phonemizer received a lyric in another language"});
      appendPhone(noteTokens, note->id, ordinal, "pau");
    } else {
      const auto word = lowerAscii(lyric->surface);
      if (word == "-" || word == "~") {
        if (previousVowel) appendPhone(noteTokens, note->id, ordinal, *previousVowel);
        else {
          result.warnings.push_back({WarningCode::LeadingLongVowel, note->id, 0U,
              "Korean continuation has no preceding vowel"});
          appendPhone(noteTokens, note->id, ordinal, "pau");
        }
      } else if (!word.empty() && punctuation(word)) {
        appendPhone(noteTokens, note->id, ordinal, "pau");
      } else if (!word.empty()) {
        const auto found = romanizedWords().find(word);
        if (found != romanizedWords().end()) appendPhones(noteTokens, note->id, ordinal, found->second);
        else {
          result.warnings.push_back({WarningCode::UnsupportedCharacter, note->id, 0U,
              "Romanized Korean word is outside the bundled bootstrap lexicon"});
          appendPhone(noteTokens, note->id, ordinal, "pau");
        }
      } else {
        const auto normalized = composeModernHangul(lyric->surface);
        using ReadingUnit = std::variant<Syllable, std::string>;
        std::vector<ReadingUnit> reading;
        bool supported = true;
        for (std::size_t index = 0U; index < normalized.size(); ++index) {
          if (stop.stop_requested()) return cancelled();
          const auto syllable = decomposeHangul(normalized[index]);
          if (!syllable) {
            const auto consonant = compatibilityJamo(normalized[index]);
            if (consonant) {
              reading.emplace_back(*consonant);
              continue;
            }
            supported = false;
            result.warnings.push_back({WarningCode::UnsupportedCharacter, note->id, index,
                "Unsupported Korean syllable or jamo"});
            break;
          }
          reading.emplace_back(*syllable);
        }
        if (!supported || reading.empty()) {
          noteTokens.clear(); ordinal = 0U; appendPhone(noteTokens, note->id, ordinal, "pau");
        } else {
          if (const auto boundary = lexicalNInsertionBoundary(normalized);
              boundary && *boundary < reading.size()) {
            if (auto* following = std::get_if<Syllable>(&reading[*boundary]);
                following != nullptr && following->onset.empty()) {
              following->onset = "n";
            }
          }
          const auto contextSyllable = [&](std::size_t index, bool last) -> std::optional<Syllable> {
            const auto* source = notes[index];
            const auto found = lyrics.find(source->lyricTokenId);
            if (source->phoneticHint || found == lyrics.end() || found->second->surface.empty() ||
                (found->second->language != domain::Language::Korean && found->second->language != domain::Language::Unspecified)) return {};
            return decomposeHangul(last ? found->second->surface.back() : found->second->surface.front());
          };
          std::size_t first = 0U;
          if (noteIndex > 0U && notes[noteIndex - 1U]->endTick() == note->startTick &&
              std::holds_alternative<Syllable>(reading.front())) {
            if (const auto previous = contextSyllable(noteIndex - 1U, true)) {
              reading.insert(reading.begin(), *previous);
              first = 1U;
            }
          }
          const auto last = reading.size();
          if (noteIndex + 1U < notes.size() && note->endTick() == notes[noteIndex + 1U]->startTick &&
              std::holds_alternative<Syllable>(reading.back())) {
            if (const auto next = contextSyllable(noteIndex + 1U, false)) reading.emplace_back(*next);
          }
          for (std::size_t index = 1U; index < reading.size(); ++index) {
            if (stop.stop_requested()) return cancelled();
            auto* previous = std::get_if<Syllable>(&reading[index - 1U]);
            auto* current = std::get_if<Syllable>(&reading[index]);
            if (previous && current) connectSyllables(*previous, *current);
          }
          for (std::size_t index = first; index < last; ++index) {
            if (const auto* syllable = std::get_if<Syllable>(&reading[index])) {
              if (!syllable->onset.empty()) appendPhone(noteTokens, note->id, ordinal, syllable->onset);
              appendPhones(noteTokens, note->id, ordinal, syllable->vowels);
              if (!syllable->coda.empty()) appendPhone(noteTokens, note->id, ordinal, syllable->coda, domain::PhonemeRole::Coda);
            } else {
              appendPhone(noteTokens, note->id, ordinal, std::get<std::string>(reading[index]));
              result.warnings.push_back({WarningCode::EstimatedPronunciation, note->id, index - first,
                  "Isolated Korean compatibility jamo has no syllable position; verify its pronunciation or use a phone hint"});
            }
          }
        }
      }
    }
    if (const auto found = overrides.find(note->id); found != overrides.end())
      applyOverrides(found->second, note->id, noteTokens, result.warnings);
    previousVowel.reset();
    for (auto iterator = noteTokens.rbegin(); iterator != noteTokens.rend(); ++iterator) {
      if (isVowelSymbol(iterator->symbol)) { previousVowel = iterator->symbol; break; }
    }
    if (result.tokens.size() > maximumTokens || noteTokens.size() > maximumTokens - result.tokens.size())
      return core::failure<Result>(core::ErrorCode::InvalidArgument,
          "Resolved Korean pronunciation exceeds token bounds");
    result.tokens.insert(result.tokens.end(), noteTokens.begin(), noteTokens.end());
  }
  return core::success(std::move(result));
}

}  // namespace seam::phonemizer
