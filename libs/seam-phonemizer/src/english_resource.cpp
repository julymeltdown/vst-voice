#include "seam/phonemizer/english_resource.hpp"

#include "seam/core/sha256.hpp"
#include "seam/phonemizer/override_reconciliation.hpp"

#include "EnglishCmuDictionary.hpp"
#include "EnglishPronunciationExceptions.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifndef SEAM_ENGLISH_RESOURCE_HASH
#error "SEAM_ENGLISH_RESOURCE_HASH must bind the bundled English resource sources"
#endif

namespace seam::phonemizer {
namespace {

using PhoneList = std::vector<std::string>;

constexpr std::array<std::string_view, 93U> kVocabulary{
    "p", "b", "t", "d", "k", "g", "m", "n", "ng", "f", "v", "th", "dh", "s", "z",
    "sh", "zh", "hh", "ch", "j", "l", "r", "w", "y",
    "aa", "aa0", "aa1", "aa2", "ae", "ae0", "ae1", "ae2", "ah", "ah0", "ah1", "ah2",
    "ao", "ao0", "ao1", "ao2", "aw", "aw0", "aw1", "aw2", "ax", "ax0", "ax1", "ax2",
    "axr", "axr0", "axr1", "axr2", "ay", "ay0", "ay1", "ay2", "eh", "eh0", "eh1", "eh2",
    "er", "er0", "er1", "er2", "ey", "ey0", "ey1", "ey2", "ih", "ih0", "ih1", "ih2",
    "iy", "iy0", "iy1", "iy2", "ow", "ow0", "ow1", "ow2", "oy", "oy0", "oy1", "oy2",
    "uh", "uh0", "uh1", "uh2", "uw", "uw0", "uw1", "uw2",
    "pau"};

constexpr std::size_t kMaximumReadingPhones = 256U;
constexpr std::size_t kMaximumWordBytes = 64U;
constexpr std::size_t kMaximumExceptionEntries = 4096U;
constexpr std::size_t kMaximumExceptionTableBytes = 1024U * 1024U;
constexpr std::size_t kMaximumDeclaredSymbols = 4096U;

void field(core::Sha256& hash, std::string_view value) {
  hash.update(std::to_string(value.size()));
  hash.update(":");
  hash.update(value);
}

bool isAsciiLower(char value) noexcept { return value >= 'a' && value <= 'z'; }

// ---- Pinned CMUdict --------------------------------------------------------

const std::string& cmuDictionarySource() {
  static const std::string source = [] {
    std::string text;
    for (const auto chunk : resources::cmuEnglishDictionary) text.append(chunk);
    return text;
  }();
  return source;
}

std::string_view cmuEntryKey(std::string_view line) {
  auto key = line.substr(0U, line.find(' '));
  const auto variant = key.find('(');
  if (variant != std::string_view::npos) key = key.substr(0U, variant);
  return key;
}

int compareAsciiCaseInsensitive(std::string_view lhs, std::string_view rhs) {
  const auto common = std::min(lhs.size(), rhs.size());
  for (std::size_t index = 0U; index < common; ++index) {
    const auto left = std::tolower(static_cast<unsigned char>(lhs[index]));
    const auto right = std::tolower(static_cast<unsigned char>(rhs[index]));
    if (left != right) return left < right ? -1 : 1;
  }
  if (lhs.size() == rhs.size()) return 0;
  return lhs.size() < rhs.size() ? -1 : 1;
}

struct CmuDictionaryIndex final {
  std::vector<std::string_view> lines;
  std::vector<std::pair<std::size_t, std::size_t>> sortedRuns;
};

const CmuDictionaryIndex& cmuDictionaryIndex() {
  // The pinned resource contains a few out-of-order entries, so a binary search
  // over the raw bytes would silently miss real words. Line views are split
  // into sorted runs in one pass; variants keep their source order.
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
    const auto begin = index.lines.begin() + static_cast<std::ptrdiff_t>(first);
    const auto end = index.lines.begin() + static_cast<std::ptrdiff_t>(last);
    const auto found = std::lower_bound(begin, end, word, [](std::string_view line, std::string_view key) {
      return compareAsciiCaseInsensitive(cmuEntryKey(line), key) < 0;
    });
    if (found == end || compareAsciiCaseInsensitive(cmuEntryKey(*found), word) != 0) continue;
    earliestMatch = std::min(earliestMatch, static_cast<std::size_t>(found - index.lines.begin()));
  }
  return earliestMatch == index.lines.size() ? std::string_view{} : index.lines[earliestMatch];
}

std::optional<PhoneList> cmuPhones(std::string_view line) {
  // Upstream appends annotations such as "hiv EY1 CH AY1 V IY1 # abbrev" to 22
  // entries. They are metadata, never phones.
  if (const auto comment = line.find('#'); comment != std::string_view::npos)
    line = line.substr(0U, comment);
  PhoneList phones;
  bool headword = true;
  for (std::size_t offset = 0U; offset < line.size();) {
    while (offset < line.size() && (line[offset] == ' ' || line[offset] == '\t')) ++offset;
    const auto start = offset;
    while (offset < line.size() && line[offset] != ' ' && line[offset] != '\t') ++offset;
    if (start == offset) break;
    if (headword) { headword = false; continue; }
    std::string phone{line.substr(start, offset - start)};
    std::transform(phone.begin(), phone.end(), phone.begin(), [](unsigned char value) {
      return static_cast<char>(std::tolower(value));
    });
    if (phone == "jh") phone = "j";
    // An entry outside the frozen vocabulary is unusable, not a phone to pass on.
    if (!isEnglishVocabularySymbol(phone) || phone == "pau" || phones.size() == kMaximumReadingPhones)
      return std::nullopt;
    phones.push_back(std::move(phone));
  }
  if (phones.empty()) return std::nullopt;
  return phones;
}

// ---- SEAM exception table ---------------------------------------------------

using ExceptionTable = std::unordered_map<std::string, EnglishPhoneReading>;

bool validExceptionWord(std::string_view word) noexcept {
  return !word.empty() && word.size() <= kMaximumWordBytes &&
      std::all_of(word.begin(), word.end(), [](char value) { return isAsciiLower(value) || value == '\''; }) &&
      std::any_of(word.begin(), word.end(), isAsciiLower);
}

core::Result<ExceptionTable> parseExceptionTable(std::string_view table) {
  using Output = ExceptionTable;
  if (table.size() > kMaximumExceptionTableBytes)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "English exception table exceeds 1 MiB");
  Output entries;
  std::size_t lineNumber = 0U;
  for (std::size_t start = 0U; start < table.size();) {
    const auto newline = table.find('\n', start);
    auto line = table.substr(start, (newline == std::string_view::npos ? table.size() : newline) - start);
    start = newline == std::string_view::npos ? table.size() : newline + 1U;
    ++lineNumber;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string_view::npos || line[first] == '#') continue;
    const auto location = " (line " + std::to_string(lineNumber) + ")";
    const auto tab = line.find('\t');
    if (tab == std::string_view::npos)
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "English exception entry needs word<TAB>phones" + location);
    const auto word = line.substr(0U, tab);
    if (!validExceptionWord(word))
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "English exception word must be lowercase ASCII letters or apostrophes" + location);
    auto reading = parseEnglishPhoneReading(line.substr(tab + 1U));
    if (!reading) return core::failure<Output>(reading.error().code, reading.error().message + location);
    const auto& phones = reading.value().phones;
    if (std::find(phones.begin(), phones.end(), "pau") != phones.end() ||
        std::none_of(phones.begin(), phones.end(), [](const auto& phone) { return isVowelSymbol(phone); }))
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "English exception reading needs a vowel and no pause" + location);
    if (entries.size() == kMaximumExceptionEntries)
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "English exception table exceeds 4096 entries");
    if (!entries.emplace(std::string{word}, std::move(reading).value()).second)
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "English exception table repeats a word" + location);
  }
  return entries;
}

// ---- Spelling estimate (seam-en-spelling-v1) --------------------------------

const std::unordered_map<std::string_view, std::string_view>& digraphs() {
  static const std::unordered_map<std::string_view, std::string_view> values{
      {"ch", "ch"}, {"sh", "sh"}, {"th", "th"}, {"ph", "f"},
      {"ng", "ng"}, {"wh", "w"}, {"qu", "k"}, {"ck", "k"},
      {"zh", "zh"},
      // Common vowel spellings are estimates, not a pronunciation dictionary.
      // Ambiguous patterns deliberately choose one reading and stay visible as
      // EstimatedPronunciation at the call site.
      {"ai", "ey0"}, {"ay", "ey0"}, {"ee", "iy0"}, {"ea", "iy0"},
      {"oa", "ow0"}, {"oo", "uw0"}, {"oi", "oy0"}, {"oy", "oy0"},
      {"ow", "aw0"}, {"ou", "aw0"},
  };
  return values;
}

PhoneList spellingEstimate(std::string_view word) {
  PhoneList result;
  for (std::size_t index = 0U; index < word.size();) {
    if (word[index] == '\'') { ++index; continue; }
    // A final silent e commonly marks a preceding single-letter vowel as long
    // across one consonant. Only this narrow pattern is applied.
    if (word[index] == 'e' && index + 1U == word.size() && !result.empty()) {
      const auto vowel = isVowelSymbol(result.back())
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
      const auto found = digraphs().find(word.substr(index, 2U));
      if (found != digraphs().end()) {
        result.emplace_back(found->second);
        index += 2U;
        continue;
      }
    }
    const char value = word[index++];
    switch (value) {
      case 'a': result.emplace_back("ae0"); break;
      case 'e': result.emplace_back("eh0"); break;
      case 'i': result.emplace_back("ih0"); break;
      case 'o': result.emplace_back("aa0"); break;
      case 'u': result.emplace_back("uh0"); break;
      case 'y': result.emplace_back("iy0"); break;
      case 'h': result.emplace_back("hh"); break;
      case 'x': result.emplace_back("k"); result.emplace_back("s"); break;
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
  if (result.size() > kMaximumReadingPhones ||
      std::none_of(result.begin(), result.end(), [](const auto& phone) { return isVowelSymbol(phone); }))
    return {};
  return result;
}

bool endsWith(std::string_view text, std::string_view suffix) noexcept {
  return text.size() > suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

}  // namespace

// ---- Vocabulary ----------------------------------------------------------------

std::span<const std::string_view> englishVocabulary() noexcept { return kVocabulary; }

bool isEnglishVocabularySymbol(std::string_view symbol) noexcept {
  return std::find(kVocabulary.begin(), kVocabulary.end(), symbol) != kVocabulary.end();
}

const std::string& englishVocabularySha256() {
  static const std::string digest = [] {
    core::Sha256 hash;
    hash.update(kEnglishVocabularyId);
    hash.update("\n");
    for (const auto symbol : kVocabulary) {
      hash.update(symbol);
      hash.update("\n");
    }
    return hash.hexDigest();
  }();
  return digest;
}

core::Result<EnglishPhoneReading> parseEnglishPhoneReading(std::string_view text) {
  using Output = EnglishPhoneReading;
  if (text.empty() || text.size() > 4096U)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "English phone hint is empty or exceeds 4096 bytes");
  Output result;
  for (std::size_t offset = 0U; offset < text.size();) {
    while (offset < text.size() && std::isspace(static_cast<unsigned char>(text[offset]))) ++offset;
    if (offset == text.size()) break;
    const auto start = offset;
    while (offset < text.size() && !std::isspace(static_cast<unsigned char>(text[offset]))) ++offset;
    const auto phone = text.substr(start, offset - start);
    if (phone == ".") {
      if (result.phones.empty() || (!result.syllableBreaks.empty() && result.syllableBreaks.back() == result.phones.size()))
        return core::failure<Output>(core::ErrorCode::InvalidArgument, "English syllable separator requires phones on both sides");
      result.syllableBreaks.push_back(result.phones.size());
      continue;
    }
    if (!isEnglishVocabularySymbol(phone) || result.phones.size() >= kMaximumReadingPhones)
      return core::failure<Output>(core::ErrorCode::Unsupported,
          "English hint requires at most 256 supported space-separated phones and optional spaced dot syllable boundaries");
    result.phones.emplace_back(phone);
  }
  if (result.phones.empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "English phone hint has no phones");
  if (!result.syllableBreaks.empty() && result.syllableBreaks.back() == result.phones.size())
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "English syllable separator requires phones on both sides");
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

// ---- Resource ------------------------------------------------------------------

struct EnglishPronunciationResource::State final {
  std::string resourceHash;
  ExceptionTable exceptions;
  std::optional<core::Error> defect;
};

EnglishPronunciationResource::EnglishPronunciationResource(std::shared_ptr<const State> state)
    : state_(std::move(state)) {}

const EnglishPronunciationResource& EnglishPronunciationResource::builtin() {
  static const EnglishPronunciationResource resource{[] {
    auto state = std::make_shared<State>();
    state->resourceHash = SEAM_ENGLISH_RESOURCE_HASH;
    auto exceptions = parseExceptionTable(resources::englishPronunciationExceptions);
    // A malformed bundled table is a build defect: resolution fails closed with
    // the parse error instead of silently dropping reviewed corrections.
    if (exceptions) state->exceptions = std::move(exceptions).value();
    else state->defect = core::Error{core::ErrorCode::InvariantViolation,
        "Bundled English exception table is invalid: " + exceptions.error().message, {}};
    return std::shared_ptr<const State>{std::move(state)};
  }()};
  return resource;
}

core::Result<EnglishPronunciationResource> EnglishPronunciationResource::withExceptions(
    std::string_view table) const {
  using Output = EnglishPronunciationResource;
  if (state_->defect) return core::Result<Output>{*state_->defect};
  auto overlay = parseExceptionTable(table);
  if (!overlay) return core::Result<Output>{overlay.error()};
  auto state = std::make_shared<State>();
  state->exceptions = state_->exceptions;
  for (auto& [word, reading] : overlay.value()) state->exceptions.insert_or_assign(word, std::move(reading));
  if (state->exceptions.size() > kMaximumExceptionEntries)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Layered English exceptions exceed 4096 entries");
  core::Sha256 hash;
  field(hash, kEnglishExceptionOverlayRule);
  field(hash, state_->resourceHash);
  field(hash, table);
  state->resourceHash = hash.hexDigest();
  return Output{std::shared_ptr<const State>{std::move(state)}};
}

const std::string& EnglishPronunciationResource::resourceHash() const noexcept {
  return state_->resourceHash;
}

std::optional<EnglishWordReading> EnglishPronunciationResource::lexicalReading(std::string_view word) const {
  if (word.empty() || word.size() > kMaximumWordBytes) return std::nullopt;
  if (const auto found = state_->exceptions.find(std::string{word}); found != state_->exceptions.end())
    return EnglishWordReading{found->second, EnglishReadingSource::Exception, {}};
  const auto line = findCmuReading(word);
  if (line.empty()) return std::nullopt;
  auto phones = cmuPhones(line);
  if (!phones) return std::nullopt;
  return EnglishWordReading{EnglishPhoneReading{std::move(*phones), {}}, EnglishReadingSource::Dictionary, {}};
}

namespace {

// seam-en-derivation-v1: regular forms that lyrics use but the lexicon often
// omits. Each result names its lexical basis and remains flagged for review.
std::optional<EnglishWordReading> deriveWord(const EnglishPronunciationResource& resource,
    std::string_view word) {
  const auto derived = [](EnglishWordReading basisReading, std::string basis) {
    basisReading.source = EnglishReadingSource::Derived;
    basisReading.basis = std::move(basis);
    return basisReading;
  };
  // Dropped final g: "lovin'"/"lovin" from "loving" when the basis ends in an
  // unstressed vowel plus "ng"; only that "ng" becomes "n".
  std::string gerund;
  if (endsWith(word, "in'") && word.size() > 4U) gerund = std::string{word.substr(0U, word.size() - 1U)} + "g";
  else if (endsWith(word, "in") && word.size() > 3U) gerund = std::string{word} + "g";
  if (!gerund.empty()) {
    if (auto basis = resource.lexicalReading(gerund)) {
      auto& phones = basis->reading.phones;
      if (phones.size() >= 2U && phones.back() == "ng" && isVowelSymbol(phones[phones.size() - 2U]) &&
          phones[phones.size() - 2U].back() == '0') {
        phones.back() = "n";
        return derived(std::move(*basis), gerund);
      }
    }
  }
  // Possessive "'s": sibilant + "ih0 z", voiceless + "s", otherwise "z".
  if (endsWith(word, "'s") && word.size() > 3U) {
    const std::string stem{word.substr(0U, word.size() - 2U)};
    if (auto basis = resource.lexicalReading(stem); basis && basis->reading.phones.size() + 2U <= kMaximumReadingPhones) {
      auto& phones = basis->reading.phones;
      const auto& last = phones.back();
      if (last == "s" || last == "z" || last == "sh" || last == "zh" || last == "ch" || last == "j") {
        phones.emplace_back("ih0");
        phones.emplace_back("z");
      } else if (last == "p" || last == "t" || last == "k" || last == "f" || last == "th") {
        phones.emplace_back("s");
      } else {
        phones.emplace_back("z");
      }
      return derived(std::move(*basis), stem);
    }
  }
  // Plural possessive "s'" sounds like its plural.
  if (endsWith(word, "s'") && word.size() > 3U) {
    const std::string plural{word.substr(0U, word.size() - 1U)};
    if (auto basis = resource.lexicalReading(plural)) return derived(std::move(*basis), plural);
  }
  return std::nullopt;
}

}  // namespace

std::optional<EnglishWordReading> EnglishPronunciationResource::resolveWord(std::string_view word) const {
  if (word.empty() || word.size() > kMaximumWordBytes) return std::nullopt;
  if (auto reading = lexicalReading(word)) return reading;
  if (auto reading = deriveWord(*this, word)) return reading;
  // Quote-like apostrophes around a word ("'hello'") are not part of it; entries
  // such as "'cause" and "lovin'" were tried with them first.
  auto stripped = word;
  while (!stripped.empty() && stripped.front() == '\'') stripped.remove_prefix(1U);
  while (!stripped.empty() && stripped.back() == '\'') stripped.remove_suffix(1U);
  if (stripped.empty()) return std::nullopt;
  if (stripped != word) {
    if (auto reading = lexicalReading(stripped)) return reading;
    if (auto reading = deriveWord(*this, stripped)) return reading;
  }
  auto estimate = spellingEstimate(stripped);
  if (estimate.empty()) return std::nullopt;
  return EnglishWordReading{EnglishPhoneReading{std::move(estimate), {}},
      EnglishReadingSource::SpellingEstimate, {}};
}

const std::string& englishLexiconSha256() {
  static const std::string digest = [] {
    core::Sha256 hash;
    for (const auto chunk : resources::cmuEnglishDictionary) hash.update(chunk);
    return hash.hexDigest();
  }();
  return digest;
}

const std::string& englishExceptionsSha256() {
  static const std::string digest = core::sha256Hex(resources::englishPronunciationExceptions);
  return digest;
}

core::Result<void> EnglishPronunciationResource::status() const {
  if (state_->defect) return core::Result<void>{*state_->defect};
  return core::success();
}

// ---- Coverage ------------------------------------------------------------------

core::Result<EnglishVocabularyCoverage> checkEnglishVocabularyCoverage(
    std::span<const domain::PhonemeToken> tokens,
    const EnglishVocabularyDeclaration& declaration) {
  using Output = EnglishVocabularyCoverage;
  if (declaration.symbols.size() > kMaximumDeclaredSymbols || tokens.size() > kMaximumPronunciationTokens)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "English coverage input exceeds bounds");
  std::unordered_set<std::string_view> declared;
  for (const auto& symbol : declaration.symbols) {
    if (symbol.empty() || symbol.size() > 64U || !declared.insert(symbol).second)
      return core::failure<Output>(core::ErrorCode::InvalidArgument,
          "Resource vocabulary declares an empty, oversized or repeated symbol");
  }
  Output result;
  std::unordered_set<std::string> required;
  std::unordered_map<std::string, std::size_t> missingIndex;
  for (const auto& token : tokens) {
    if (!isEnglishVocabularySymbol(token.symbol))
      return core::failure<Output>(core::ErrorCode::InvalidArgument,
          "Resolved phone is outside the frozen English vocabulary", token.key.toString());
    if (token.symbol == "pau" || token.role == domain::PhonemeRole::Silence) continue;
    auto requiredSymbol = token.symbol;
    if (declaration.stress == EnglishStressCoverage::FoldLexicalStress && isVowelSymbol(requiredSymbol) &&
        (requiredSymbol.back() == '0' || requiredSymbol.back() == '1' || requiredSymbol.back() == '2'))
      requiredSymbol.pop_back();
    required.insert(requiredSymbol);
    if (declared.contains(requiredSymbol)) continue;
    auto [entry, inserted] = missingIndex.try_emplace(token.symbol, result.missing.size());
    if (inserted) result.missing.push_back({token.symbol, requiredSymbol, 0U, {}});
    auto& missing = result.missing[entry->second];
    ++missing.occurrences;
    if (missing.keys.size() < kMaximumEnglishCoverageKeysPerPhone) missing.keys.push_back(token.key);
  }
  result.requiredSymbols = required.size();
  return result;
}

// ---- Resource-change reconciliation --------------------------------------------

core::Result<EnglishResourceReconciliation> reconcileEnglishResourceChange(
    const domain::VocalRegion& region, const EnglishPronunciationResource& before,
    const EnglishPronunciationResource& after, std::stop_token stop) {
  using Output = EnglishResourceReconciliation;
  auto base = region;
  base.phonemeOverrides.clear();
  const auto previous = resolveEnglishPronunciation(base, before, stop);
  if (!previous) return core::Result<Output>{previous.error()};
  const auto next = resolveEnglishPronunciation(base, after, stop);
  if (!next) return core::Result<Output>{next.error()};
  Output result;
  result.overrides = region.phonemeOverrides;
  result.outcomes.assign(result.overrides.size(), EnglishEditReconciliation::Unchanged);
  std::vector<domain::NoteId> order;
  std::unordered_map<domain::NoteId, std::vector<std::size_t>> verified;
  for (std::size_t index = 0U; index < result.overrides.size(); ++index) {
    const auto& edit = result.overrides[index];
    if (!edit.sourceContextId || edit.unresolved) continue;
    const auto context = phonemeEditContextId(previous.value(), edit.key);
    if (!context || *context != *edit.sourceContextId) continue;
    auto& indices = verified[edit.key.noteId];
    if (indices.empty()) order.push_back(edit.key.noteId);
    indices.push_back(index);
  }
  const auto sameSounds = [](std::span<const domain::PhonemeToken> lhs, std::span<const domain::PhonemeToken> rhs) {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](const auto& left, const auto& right) {
      return left.symbol == right.symbol && left.role == right.role && left.voiced == right.voiced;
    });
  };
  for (const auto noteId : order) {
    if (stop.stop_requested())
      return core::failure<Output>(core::ErrorCode::Conflict, "English resource reconciliation cancelled");
    const auto& indices = verified[noteId];
    const auto oldTokens = previous.value().pronunciation.tokensForNote(noteId);
    const auto newTokens = next.value().pronunciation.tokensForNote(noteId);
    const auto rebind = [&](std::size_t index, domain::PhonemeOverride edit) {
      const auto context = phonemeEditContextId(next.value(), edit.key);
      if (!context) {
        result.overrides[index].unresolved = true;
        result.outcomes[index] = EnglishEditReconciliation::Unresolved;
        return;
      }
      edit.sourceContextId = *context;
      edit.unresolved = false;
      result.overrides[index] = std::move(edit);
      result.outcomes[index] = EnglishEditReconciliation::Rebound;
    };
    if (sameSounds(oldTokens, newTokens)) {
      // Only the resource identity changed for this note; every address holds.
      for (const auto index : indices) rebind(index, result.overrides[index]);
      continue;
    }
    std::vector<domain::PhonemeOverride> edits;
    for (const auto index : indices) edits.push_back(result.overrides[index]);
    const auto reconciled = reconcilePhonemeOverrides(noteId, oldTokens, newTokens, edits);
    if (!reconciled) return core::Result<Output>{reconciled.error()};
    for (std::size_t offset = 0U; offset < indices.size(); ++offset) {
      const auto& item = reconciled.value()[offset];
      if (item.correspondence == EditCorrespondence::Matched && item.rebound) {
        rebind(indices[offset], *item.rebound);
      } else {
        result.overrides[indices[offset]].unresolved = true;
        result.outcomes[indices[offset]] = EnglishEditReconciliation::Unresolved;
      }
    }
  }
  return result;
}

}  // namespace seam::phonemizer
