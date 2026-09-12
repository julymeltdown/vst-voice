#include "seam/phonemizer/english_phonemizer.hpp"

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

namespace seam::phonemizer {
namespace {

using PhoneList = std::vector<std::string>;
struct EnglishReading final {
  PhoneList phones;
  // An index names the first phone of the following syllable. Boundaries are
  // structure, not acoustic tokens, so ordinals and literal stress stay intact.
  std::vector<std::size_t> syllableBreaks;
};

const std::map<std::string, PhoneList, std::less<>>& dictionary() {
  // This is a deliberately small, versioned bootstrap lexicon. It is not
  // presented as a complete English dictionary; spelling estimates outside
  // this lexicon carry an explicit per-note EstimatedPronunciation warning.
  static const std::map<std::string, PhoneList, std::less<>> values{
      {"a", {"ah1"}},
      {"an", {"ae1", "n"}},
      {"and", {"ae1", "n", "d"}},
      {"are", {"aa1", "r"}},
      {"be", {"b", "iy1"}},
      {"beautiful", {"b", "y", "uw1", "t", "ah0", "f", "ah0", "l"}},
      {"can", {"k", "ae1", "n"}},
      {"dream", {"d", "r", "iy1", "m"}},
      {"hello", {"hh", "eh0", "l", "ow1"}},
      {"i", {"ay1"}},
      {"is", {"ih1", "z"}},
      {"love", {"l", "ah1", "v"}},
      {"music", {"m", "y", "uw1", "z", "ih0", "k"}},
      {"of", {"ah0", "v"}},
      {"project", {"p", "r", "aa1", "j", "eh0", "k", "t"}},
      {"seam", {"s", "iy1", "m"}},
      {"sing", {"s", "ih1", "ng"}},
      {"singer", {"s", "ih1", "ng", "er0"}},
      {"the", {"dh", "ax0"}},
      {"this", {"dh", "ih1", "s"}},
      {"to", {"t", "uw1"}},
      {"voice", {"v", "oy1", "s"}},
      {"vocal", {"v", "ow1", "k", "ah0", "l"}},
      {"we", {"w", "iy1"}},
      {"world", {"w", "er1", "l", "d"}},
      {"you", {"y", "uw1"}},
  };
  return values;
}

std::vector<std::size_t> dictionarySyllableBreaks(std::string_view word) {
  // Explicit bootstrap readings take precedence over onset inference. The
  // resource fingerprint covers both this table and the phone lexicon.
  if (word == "beautiful") return {3U, 5U};
  if (word == "hello" || word == "vocal") return {2U};
  if (word == "music" || word == "project" || word == "singer") return {3U};
  return {};
}

bool legalEnglishOnset(std::span<const domain::PhonemeToken> cluster) {
  if (cluster.empty() || cluster.size() > 3U) return false;
  static const std::unordered_set<std::string> singles{
      "p", "b", "t", "d", "k", "g", "m", "n", "f", "v", "th", "dh", "s", "z", "sh", "zh", "hh", "ch", "j", "l", "r", "w", "y"};
  if (cluster.size() == 1U) return singles.contains(cluster.front().symbol);
  static const std::unordered_set<std::string> clusters{
      "p r", "p l", "p y", "b r", "b l", "b y", "t r", "t w", "t y", "d r", "d w", "d y",
      "k r", "k l", "k w", "k y", "g r", "g l", "g w", "g y", "f r", "f l", "f y", "v r", "v y",
      "th r", "th w", "th y", "sh r", "s p", "s t", "s k", "s f", "s m", "s n", "s l", "s w", "s y",
      "m y", "n y", "l y", "hh y", "s p r", "s p l", "s p y", "s t r", "s t y", "s k r", "s k l", "s k w", "s k y"};
  std::string key;
  for (const auto& phone : cluster) { if (!key.empty()) key += ' '; key += phone.symbol; }
  return clusters.contains(key);
}

void assignSyllableRoles(std::span<domain::PhonemeToken> tokens) {
  std::optional<std::size_t> previousNucleus;
  for (std::size_t nucleus = 0U; nucleus < tokens.size(); ++nucleus) {
    if (tokens[nucleus].role != domain::PhonemeRole::Nucleus) continue;
    if (previousNucleus) {
      auto onset = nucleus;
      const auto available = nucleus - *previousNucleus - 1U;
      for (std::size_t count = 1U; count <= std::min<std::size_t>(3U, available); ++count)
        if (legalEnglishOnset(tokens.subspan(nucleus - count, count))) onset = nucleus - count;
      for (auto i = *previousNucleus + 1U; i < onset; ++i)
        if (tokens[i].role == domain::PhonemeRole::Onset) tokens[i].role = domain::PhonemeRole::Coda;
    }
    previousNucleus = nucleus;
  }
  if (previousNucleus) for (auto i = *previousNucleus + 1U; i < tokens.size(); ++i)
    if (tokens[i].role == domain::PhonemeRole::Onset) tokens[i].role = domain::PhonemeRole::Coda;
  // Initial consonants and explicit nucleus-free fragments stay on this note;
  // do not invent a previous vowel or silently transfer them to another note.
}

void assignEnglishRoles(std::vector<domain::PhonemeToken>& tokens, std::span<const std::size_t> breaks) {
  for (auto& token : tokens) token.role = inferRole(token.symbol);
  std::size_t first = 0U;
  for (std::size_t i = 0U; i <= tokens.size(); ++i) {
    const bool silence = i < tokens.size() && tokens[i].role == domain::PhonemeRole::Silence;
    if (i == tokens.size() || silence || std::binary_search(breaks.begin(), breaks.end(), i)) {
      assignSyllableRoles(std::span<domain::PhonemeToken>{tokens}.subspan(first, i - first));
      first = silence ? i + 1U : i;
    }
  }
}

const std::unordered_map<std::string, std::string>& digraphs() {
  static const std::unordered_map<std::string, std::string> values{
      {"ch", "ch"}, {"sh", "sh"}, {"th", "th"}, {"ph", "f"},
      {"ng", "ng"}, {"wh", "w"}, {"qu", "k"}, {"ck", "k"},
      {"zh", "zh"},
  };
  return values;
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
        value == ';' || value == ':' || value == '-' ||
        value == '"' || value == '\'';
  });
}

void appendPhone(std::vector<domain::PhonemeToken>& target, domain::NoteId note,
    std::uint16_t& ordinal, std::string symbol) {
  const auto role = inferRole(symbol);
  const auto voiced = isVoicedSymbol(symbol);
  target.push_back(domain::PhonemeToken{
      .key = domain::PhonemeKey{note, ordinal}, .symbol = std::move(symbol),
      .role = role, .voiced = voiced, .timing = {}, .locked = false});
  ++ordinal;
}

void appendPhones(std::vector<domain::PhonemeToken>& target, domain::NoteId note,
    std::uint16_t& ordinal, const PhoneList& phones) {
  for (const auto& phone : phones) appendPhone(target, note, ordinal, phone);
}

void applyOverrides(std::span<const domain::PhonemeOverride* const> overrides,
    domain::NoteId noteId, std::vector<domain::PhonemeToken>& tokens,
    std::vector<Warning>& warnings) {
  for (const auto* entry : overrides) {
    const auto& value = *entry;
    if (value.unresolved) {
      warnings.push_back({WarningCode::OrphanOverride, noteId, value.key.ordinal,
          "English phoneme edit is retained but unresolved after pronunciation changed"});
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
        const auto inferred = inferRole(token.symbol);
        // A manual consonant replacement edits the already addressed syllable;
        // it must not silently move a coda onto the following vowel's onset.
        if (inferred != domain::PhonemeRole::Onset ||
            (token.role != domain::PhonemeRole::Onset && token.role != domain::PhonemeRole::Coda))
          token.role = inferred;
        token.voiced = isVoicedSymbol(token.symbol);
      }
      token.timing = value.timing;
      token.locked = value.locked;
    } else if (value.symbol && index == tokens.size()) {
      auto role = inferRole(*value.symbol);
      if (role == domain::PhonemeRole::Onset) for (auto token = tokens.rbegin(); token != tokens.rend(); ++token) {
        if (token->role == domain::PhonemeRole::Silence) break;
        if (token->role == domain::PhonemeRole::Nucleus) { role = domain::PhonemeRole::Coda; break; }
      }
      tokens.push_back(domain::PhonemeToken{
          .key = value.key, .symbol = *value.symbol,
          .role = role, .voiced = isVoicedSymbol(*value.symbol),
          .timing = value.timing, .locked = value.locked});
    } else {
      warnings.push_back({WarningCode::OrphanOverride, noteId, index,
          "English phoneme override does not match a generated token"});
    }
  }
}

PhoneList fallbackWord(std::string_view word) {
  PhoneList result;
  for (std::size_t index = 0U; index < word.size();) {
    if (word[index] == '\'') { ++index; continue; }
    if (index + 1U < word.size()) {
      const auto found = digraphs().find(std::string{word.substr(index, 2U)});
      if (found != digraphs().end()) {
        result.push_back(found->second); index += 2U; continue;
      }
    }
    const char value = word[index++];
    switch (value) {
      case 'a': result.push_back("ae0"); break;
      case 'e': result.push_back("eh0"); break;
      case 'i': result.push_back("ih0"); break;
      case 'o': result.push_back("aa0"); break;
      case 'u': result.push_back("uh0"); break;
      case 'y': result.push_back("iy0"); break;
      case 'h': result.push_back("hh"); break;
      case 'x': result.push_back("k"); result.push_back("s"); break;
      case 'b': case 'd': case 'f': case 'g': case 'j':
      case 'k': case 'l': case 'm': case 'n': case 'p': case 'r':
      case 's': case 't': case 'v': case 'w': case 'z':
        result.emplace_back(1U, value); break;
      default: return {};
    }
  }
  const auto vowel = std::any_of(result.begin(), result.end(), [](const auto& phone) {
    return isVowelSymbol(phone);
  });
  return vowel ? result : PhoneList{};
}

core::Result<EnglishReading> parseEnglishReading(std::string_view text) {
  using Output = EnglishReading;
  if (text.empty() || text.size() > 4096U)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "English phone hint is empty or exceeds 4096 bytes");
  static const auto inventory = [] {
    std::unordered_set<std::string> values{
        "aa", "ae", "ah", "ax", "axr", "aw", "ay", "eh", "er",
        "ey", "ih", "iy", "ow", "oy", "uh", "uw", "p", "b", "t",
        "d", "k", "g", "m", "n", "ng", "f", "v", "th", "dh", "s",
        "z", "sh", "zh", "hh", "ch", "j", "l", "r", "w", "y", "pau",
    };
    std::unordered_set<std::string> expanded = values;
    for (const auto& value : values) if (isVowelSymbol(value)) {
      expanded.insert(value + "0"); expanded.insert(value + "1"); expanded.insert(value + "2");
    }
    return expanded;
  }();
  Output result;
  for (std::size_t offset = 0U; offset < text.size();) {
    while (offset < text.size() && std::isspace(static_cast<unsigned char>(text[offset]))) ++offset;
    if (offset == text.size()) break;
    const auto start = offset;
    while (offset < text.size() && !std::isspace(static_cast<unsigned char>(text[offset]))) ++offset;
    std::string phone{text.substr(start, offset - start)};
    if (phone == ".") {
      if (result.phones.empty() || (!result.syllableBreaks.empty() && result.syllableBreaks.back() == result.phones.size()))
        return core::failure<Output>(core::ErrorCode::InvalidArgument, "English syllable separator requires phones on both sides");
      result.syllableBreaks.push_back(result.phones.size());
      continue;
    }
    if (!inventory.contains(phone) || result.phones.size() >= 256U)
      return core::failure<Output>(core::ErrorCode::Unsupported,
          "English hint requires at most 256 supported space-separated phones and optional spaced dot syllable boundaries");
    result.phones.push_back(std::move(phone));
  }
  if (result.phones.empty()) return core::failure<Output>(core::ErrorCode::InvalidArgument,
      "English phone hint has no phones");
  if (!result.syllableBreaks.empty()) {
    std::size_t first = 0U;
    for (std::size_t group = 0U; group <= result.syllableBreaks.size(); ++group) {
      const auto end = group < result.syllableBreaks.size() ? result.syllableBreaks[group] : result.phones.size();
      const auto segment = std::span<const std::string>{result.phones}.subspan(first, end - first);
      if (std::count_if(segment.begin(), segment.end(), [](const auto& phone) { return isVowelSymbol(phone); }) != 1 ||
          std::find(segment.begin(), segment.end(), "pau") != segment.end())
        return core::failure<Output>(core::ErrorCode::InvalidArgument, "Each explicit English syllable must contain exactly one vowel and no pause");
      first = end;
    }
  }
  return core::success(std::move(result));
}

}  // namespace

core::Result<std::vector<std::string>> parseEnglishPhoneHint(std::string_view text) {
  auto reading = parseEnglishReading(text);
  if (!reading) return core::Result<std::vector<std::string>>{reading.error()};
  return std::move(reading.value().phones);
}

Result EnglishPhonemizer::phonemize(const domain::VocalRegion& region) const {
  return std::move(phonemize(region, {})).value();
}

core::Result<Result> EnglishPhonemizer::phonemize(const domain::VocalRegion& region,
    std::stop_token stop, std::size_t maximumTokens) const {
  const auto cancelled = [] {
    return core::failure<Result>(core::ErrorCode::Conflict, "English phonemization cancelled");
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
  bool previousVowelEstimated = false;
  std::optional<time::Tick> previousEnd, occupiedEnd;
  bool previousIsolated = true;
  for (const auto* note : notes) {
    if (stop.stop_requested()) return cancelled();
    const bool overlap = occupiedEnd && note->startTick < *occupiedEnd;
    if (!previousEnd || *previousEnd != note->startTick || overlap || !previousIsolated) {
      previousVowel.reset(); previousVowelEstimated = false;
    }
    previousEnd = note->endTick();
    if (!occupiedEnd || *occupiedEnd < *previousEnd) occupiedEnd = previousEnd;
    previousIsolated = !overlap;
    std::vector<domain::PhonemeToken> noteTokens;
    std::vector<std::size_t> syllableBreaks;
    bool estimated = false;
    std::uint16_t ordinal = 0U;
    const auto lyricEntry = lyrics.find(note->lyricTokenId);
    const auto* lyric = lyricEntry == lyrics.end() ? nullptr : lyricEntry->second;
    if (note->phoneticHint) {
      const auto parsed = parseEnglishReading(*note->phoneticHint);
      if (!parsed) {
        result.warnings.push_back({WarningCode::UnsupportedCharacter, note->id, 0U, parsed.error().message});
        appendPhone(noteTokens, note->id, ordinal, "pau");
      } else {
        appendPhones(noteTokens, note->id, ordinal, parsed.value().phones);
        syllableBreaks = parsed.value().syllableBreaks;
      }
    } else if (lyric == nullptr || lyric->surface.empty()) {
      result.warnings.push_back({WarningCode::EmptyLyric, note->id, 0U,
          "Note has no English lyric text; a pause token was generated"});
      appendPhone(noteTokens, note->id, ordinal, "pau");
    } else if (lyric->language != domain::Language::English && lyric->language != domain::Language::Unspecified) {
      result.warnings.push_back({WarningCode::UnsupportedCharacter, note->id, 0U,
          "English phonemizer received a lyric in another language"});
      appendPhone(noteTokens, note->id, ordinal, "pau");
    } else {
      const auto word = lowerAscii(lyric->surface);
      if (word.empty()) {
        result.warnings.push_back({WarningCode::UnsupportedCharacter, note->id, 0U,
            "English lyric contains non-ASCII or unsupported text"});
        appendPhone(noteTokens, note->id, ordinal, "pau");
      } else if (word == "-" || word == "~") {
        if (previousVowel) {
          appendPhone(noteTokens, note->id, ordinal, *previousVowel);
          estimated = previousVowelEstimated;
        }
        else {
          result.warnings.push_back({WarningCode::LeadingLongVowel, note->id, 0U,
              "English continuation has no preceding vowel"});
          appendPhone(noteTokens, note->id, ordinal, "pau");
        }
      } else if (punctuation(word)) {
        appendPhone(noteTokens, note->id, ordinal, "pau");
      } else {
        const auto found = dictionary().find(word);
        const auto phones = found == dictionary().end() ? fallbackWord(word) : found->second;
        if (found != dictionary().end()) syllableBreaks = dictionarySyllableBreaks(word);
        estimated = found == dictionary().end() && !phones.empty();
        if (phones.empty()) {
          result.warnings.push_back({WarningCode::UnsupportedCharacter, note->id, 0U,
              "English word is outside the bundled bootstrap pronunciation lexicon"});
          appendPhone(noteTokens, note->id, ordinal, "pau");
        } else appendPhones(noteTokens, note->id, ordinal, phones);
      }
    }
    if (estimated) {
      result.warnings.push_back({WarningCode::EstimatedPronunciation, note->id, 0U,
          "Estimated English pronunciation from seam-en-spelling-v1, not a dictionary reading; verify or provide an explicit phone hint"});
    }
    // Establish the source's syllables before addressed manual edits. Stress
    // remains literal; onset inference is not a full dialect/morphology model.
    assignEnglishRoles(noteTokens, syllableBreaks);
    if (const auto found = overrides.find(note->id); found != overrides.end())
      applyOverrides(found->second, note->id, noteTokens, result.warnings);
    previousVowel.reset();
    previousVowelEstimated = false;
    for (auto iterator = noteTokens.rbegin(); iterator != noteTokens.rend(); ++iterator) {
      if (iterator->role == domain::PhonemeRole::Silence) break;
      if (isVowelSymbol(iterator->symbol)) {
        previousVowel = iterator->symbol; previousVowelEstimated = estimated; break;
      }
    }
    if (result.tokens.size() > maximumTokens || noteTokens.size() > maximumTokens - result.tokens.size())
      return core::failure<Result>(core::ErrorCode::InvalidArgument,
          "Resolved English pronunciation exceeds token bounds");
    result.tokens.insert(result.tokens.end(), noteTokens.begin(), noteTokens.end());
  }
  return core::success(std::move(result));
}

}  // namespace seam::phonemizer
