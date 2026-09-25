#include "seam/phonemizer/english_phonemizer.hpp"

#include "EnglishCmuDictionary.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace seam::phonemizer {
namespace {

using PhoneList = std::vector<std::string>;
struct EnglishReading final {
  PhoneList phones;
  // An index names the first phone of the following syllable. Boundaries are
  // structure, not acoustic tokens, so ordinals and literal stress stay intact.
  std::vector<std::size_t> syllableBreaks;
};

const std::string& cmuDictionarySource() {
  static const std::string source = [] {
    std::string source;
    for (const auto chunk : resources::cmuEnglishDictionary) source.append(chunk);
    return source;
  }();
  return source;
}

std::string_view cmuEntryKey(std::string_view line) {
  const auto separator = line.find(' ');
  auto key = line.substr(0U, separator);
  const auto variant = key.find('(');
  if (variant != std::string_view::npos) key = key.substr(0U, variant);
  return key;
}

int compareAsciiCaseInsensitive(std::string_view lhs, std::string_view rhs) {
  const auto common = std::min(lhs.size(), rhs.size());
  for (std::size_t index = 0U; index < common; ++index) {
    const auto left = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(lhs[index])));
    const auto right = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(rhs[index])));
    if (left < right) return -1;
    if (left > right) return 1;
  }
  if (lhs.size() < rhs.size()) return -1;
  if (lhs.size() > rhs.size()) return 1;
  return 0;
}

struct CmuDictionaryIndex final {
  std::vector<std::string_view> lines;
  std::vector<std::pair<std::size_t, std::size_t>> sortedRuns;
};

const CmuDictionaryIndex& cmuDictionaryIndex() {
  // The pinned CMUdict resource contains a small number of out-of-order
  // entries, so binary-searching the raw bytes silently misses real words.
  // Build line views and partition the source into sorted runs in one pass;
  // equal pronunciation variants stay in source order without a full sort.
  static const CmuDictionaryIndex index = [] {
    const auto& source = cmuDictionarySource();
    CmuDictionaryIndex result;
    for (std::size_t start = 0U; start < source.size();) {
      const auto newline = source.find('\n', start);
      const auto end = newline == std::string::npos ? source.size() : newline;
      if (end > start) result.lines.emplace_back(source.data() + start, end - start);
      if (newline == std::string::npos) break;
      start = newline + 1U;
    }
    if (result.lines.empty()) return result;
    std::size_t runStart = 0U;
    for (std::size_t line = 1U; line < result.lines.size(); ++line) {
      if (compareAsciiCaseInsensitive(cmuEntryKey(result.lines[line - 1U]),
              cmuEntryKey(result.lines[line])) > 0) {
        result.sortedRuns.emplace_back(runStart, line);
        runStart = line;
      }
    }
    result.sortedRuns.emplace_back(runStart, result.lines.size());
    return result;
  }();
  return index;
}

std::string_view findCmuReading(std::string_view word) {
  const auto& index = cmuDictionaryIndex();
  std::size_t earliestMatch = index.lines.size();
  for (const auto& [first, last] : index.sortedRuns) {
    auto found = std::lower_bound(index.lines.begin() + static_cast<std::ptrdiff_t>(first),
        index.lines.begin() + static_cast<std::ptrdiff_t>(last), word,
        [](std::string_view line, std::string_view key) {
          return compareAsciiCaseInsensitive(cmuEntryKey(line), key) < 0;
        });
    if (found == index.lines.begin() + static_cast<std::ptrdiff_t>(last) ||
        compareAsciiCaseInsensitive(cmuEntryKey(*found), word) != 0) continue;
    const auto position = static_cast<std::size_t>(found - index.lines.begin());
    if (position < earliestMatch) earliestMatch = position;
  }
  return earliestMatch == index.lines.size() ? std::string_view{} : index.lines[earliestMatch];
}

PhoneList cmuPhones(std::string_view line) {
  std::istringstream fields{std::string{line}};
  std::string ignoredWord;
  static_cast<void>(fields >> ignoredWord);
  PhoneList phones;
  std::string phone;
  while (fields >> phone) {
    if (phone == "JH") phone = "J";
    std::transform(phone.begin(), phone.end(), phone.begin(), [](unsigned char value) {
      return static_cast<char>(std::tolower(value));
    });
    phones.push_back(phone);
  }
  return phones;
}

bool legalEnglishOnset(std::span<const std::string_view> cluster) {
  if (cluster.empty() || cluster.size() > 3U) return false;
  static const std::unordered_set<std::string_view> singles{
      "p", "b", "t", "d", "k", "g", "m", "n", "f", "v", "th", "dh", "s", "z", "sh", "zh", "hh", "ch", "j", "l", "r", "w", "y"};
  if (cluster.size() == 1U) return singles.contains(cluster.front());
  static const std::unordered_set<std::string_view> clusters{
      "p r", "p l", "p y", "b r", "b l", "b y", "t r", "t w", "t y", "d r", "d w", "d y",
      "k r", "k l", "k w", "k y", "g r", "g l", "g w", "g y", "f r", "f l", "f y", "v r", "v y",
      "th r", "th w", "th y", "sh r", "s p", "s t", "s k", "s f", "s m", "s n", "s l", "s w", "s y",
      "m y", "n y", "l y", "hh y", "s p r", "s p l", "s p y", "s t r", "s t y", "s k r", "s k l", "s k w", "s k y"};
  std::string key;
  for (const auto& phone : cluster) { if (!key.empty()) key += ' '; key += phone; }
  return clusters.contains(key);
}

bool legalEnglishOnset(std::span<const domain::PhonemeToken> cluster) {
  std::array<std::string_view, 3U> symbols{};
  for (std::size_t index = 0U; index < cluster.size(); ++index) symbols[index] = cluster[index].symbol;
  return legalEnglishOnset(std::span<const std::string_view>{symbols}.first(cluster.size()));
}

bool legalEnglishOnset(std::span<const std::string> cluster) {
  std::array<std::string_view, 3U> symbols{};
  for (std::size_t index = 0U; index < cluster.size(); ++index) symbols[index] = cluster[index];
  return legalEnglishOnset(std::span<const std::string_view>{symbols}.first(cluster.size()));
}

std::vector<std::size_t> dictionarySyllableBreaks(std::span<const std::string> phones) {
  std::vector<std::size_t> nuclei;
  for (std::size_t index = 0U; index < phones.size(); ++index)
    if (isVowelSymbol(phones[index])) nuclei.push_back(index);

  std::vector<std::size_t> breaks;
  for (std::size_t index = 1U; index < nuclei.size(); ++index) {
    const auto nextNucleus = nuclei[index];
    const auto clusterStart = nuclei[index - 1U] + 1U;
    const auto clusterSize = nextNucleus - clusterStart;
    auto onsetStart = nextNucleus;
    for (std::size_t count = 1U; count <= std::min<std::size_t>(3U, clusterSize); ++count) {
      const auto candidate = std::span<const std::string>{phones}.subspan(nextNucleus - count, count);
      if (legalEnglishOnset(candidate)) onsetStart = nextNucleus - count;
    }
    breaks.push_back(onsetStart);
  }
  return breaks;
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
      // Common vowel spellings are spelling estimates, not a pronunciation
      // dictionary. Ambiguous patterns deliberately choose one reading and
      // remain visible as EstimatedPronunciation at the call site.
      {"ai", "ey0"}, {"ay", "ey0"}, {"ee", "iy0"}, {"ea", "iy0"},
      {"oa", "ow0"}, {"oo", "uw0"}, {"oi", "oy0"}, {"oy", "oy0"},
      {"ow", "aw0"}, {"ou", "aw0"},
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
        value == ';' || value == ':' || value == '-' || value == '"' ||
        value == '\'' || value == '(' || value == ')' || value == '[' ||
        value == ']' || value == '{' || value == '}';
  });
}

bool englishBoundaryPunctuation(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
      value == ',' || value == '.' || value == '!' || value == '?' ||
      value == ';' || value == ':' || value == '"' || value == '(' ||
      value == ')' || value == '[' || value == ']' || value == '{' || value == '}';
}

std::string_view trimEnglishBoundaryPunctuation(std::string_view text) {
  while (!text.empty() && englishBoundaryPunctuation(text.front())) text.remove_prefix(1U);
  while (!text.empty() && englishBoundaryPunctuation(text.back())) text.remove_suffix(1U);
  return text;
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
    // A final silent e commonly marks a preceding single-letter vowel as
    // "long" across one consonant. Apply this narrow pattern only; exceptions
    // stay estimates and can be corrected with a hint.
    if (word[index] == 'e' && index + 1U == word.size() && !result.empty()) {
      const auto vowel = !result.empty() && isVowelSymbol(result.back())
          ? result.size() - 1U
          : result.size() >= 2U && isVowelSymbol(result[result.size() - 2U])
              ? result.size() - 2U : result.size();
      if (vowel < result.size()) {
        auto& preceding = result[vowel];
        if (preceding == "ae0") preceding = "ey0";
        else if (preceding == "eh0") preceding = "iy0";
        else if (preceding == "ih0") preceding = "ay0";
        else if (preceding == "aa0") preceding = "ow0";
        else if (preceding == "uh0") preceding = "uw0";
        ++index;
        continue;
      }
    }
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
      case 'c':
        result.emplace_back(index < word.size() &&
            (word[index] == 'e' || word[index] == 'i' || word[index] == 'y') ? "s" : "k");
        break;
      case 'g':
        result.emplace_back(index < word.size() &&
            (word[index] == 'e' || word[index] == 'i' || word[index] == 'y') ? "j" : "g");
        break;
      case 'b': case 'd': case 'f': case 'j':
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
        "aa", "ae", "ah", "ao", "ax", "axr", "aw", "ay", "eh", "er",
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
        const auto pronunciationWord = trimEnglishBoundaryPunctuation(word);
        const auto reading = findCmuReading(pronunciationWord);
        const auto phones = reading.empty() ? fallbackWord(pronunciationWord) : cmuPhones(reading);
        if (!reading.empty()) syllableBreaks = dictionarySyllableBreaks(phones);
        estimated = reading.empty() && !phones.empty();
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
