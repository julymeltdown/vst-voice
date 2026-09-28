#include "seam/phonemizer/english_phonemizer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace seam::phonemizer {
namespace {

using PhoneList = std::vector<std::string>;

constexpr std::size_t kMaximumNotePhones = 256U;
constexpr std::size_t kMaximumNoteWords = 32U;
constexpr std::size_t kMaximumNamedWords = 3U;

// ---- Syllable structure (seam-en-legal-onset-v1) --------------------------------

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

// Boundaries between adjacent nuclei inside [first, last): the longest legal
// onset (at most three consonants) joins the following syllable.
void appendInferredBreaks(std::span<const std::string> phones, std::size_t first, std::size_t last,
    std::vector<std::size_t>& breaks) {
  std::optional<std::size_t> previous;
  for (auto index = first; index < last; ++index) {
    if (!isVowelSymbol(phones[index])) continue;
    if (previous) {
      const auto clusterSize = index - *previous - 1U;
      auto onsetStart = index;
      for (std::size_t count = 1U; count <= std::min<std::size_t>(3U, clusterSize); ++count)
        if (legalEnglishOnset(phones.subspan(index - count, count))) onsetStart = index - count;
      breaks.push_back(onsetStart);
    }
    previous = index;
  }
}

// Complete boundaries: explicit ones plus legal-onset inference inside each
// explicit segment. Sorted, each boundary strictly inside the reading.
std::vector<std::size_t> completeSyllableBreaks(std::span<const std::string> phones,
    std::span<const std::size_t> explicitBreaks) {
  std::vector<std::size_t> result;
  std::size_t first = 0U;
  for (std::size_t group = 0U; group <= explicitBreaks.size(); ++group) {
    const auto last = group < explicitBreaks.size() ? explicitBreaks[group] : phones.size();
    if (group > 0U) result.push_back(first);
    appendInferredBreaks(phones, first, last, result);
    first = last;
  }
  return result;
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
  // Initial consonants and nucleus-free fragments stay on this note; do not
  // invent a previous vowel or silently transfer them to another note.
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

// ---- Lyric text (seam-en-normalization-v1) ---------------------------------------

std::optional<char> foldLatinLetter(char32_t value) noexcept {
  const auto in = [value](char32_t first, char32_t last) { return value >= first && value <= last; };
  if (in(0xC0U, 0xC5U) || in(0xE0U, 0xE5U)) return 'a';
  if (value == 0xC7U || value == 0xE7U) return 'c';
  if (in(0xC8U, 0xCBU) || in(0xE8U, 0xEBU)) return 'e';
  if (in(0xCCU, 0xCFU) || in(0xECU, 0xEFU)) return 'i';
  if (value == 0xD1U || value == 0xF1U) return 'n';
  if (in(0xD2U, 0xD6U) || value == 0xD8U || in(0xF2U, 0xF6U) || value == 0xF8U) return 'o';
  if (in(0xD9U, 0xDCU) || in(0xF9U, 0xFCU)) return 'u';
  if (value == 0xDDU || value == 0xFDU || value == 0xFFU) return 'y';
  return std::nullopt;
}

std::optional<char> foldPunctuation(char32_t value) noexcept {
  switch (value) {
    case 0x2018U: case 0x2019U: case 0x02BCU: case 0x2032U: return '\'';
    case 0x201CU: case 0x201DU: case 0x00ABU: case 0x00BBU: return '"';
    case 0x2010U: case 0x2011U: case 0x2012U: case 0x2013U: case 0x2014U: case 0x2212U: return '-';
    case 0x2026U: return '.';
    case 0x00A0U: case 0x2009U: case 0x202FU: case 0x3000U: return ' ';
    case 0x00A1U: return '!';
    case 0x00BFU: return '?';
    default: return std::nullopt;
  }
}

struct NormalizedLyric final {
  std::string text;  // one lowercase ASCII byte per surface code point
  std::optional<std::size_t> unsupportedIndex;
};

NormalizedLyric normalizeEnglishLyric(const std::u32string& surface) {
  NormalizedLyric result;
  result.text.reserve(surface.size());
  for (std::size_t index = 0U; index < surface.size(); ++index) {
    const auto value = surface[index];
    std::optional<char> mapped;
    if (value < 0x80U) mapped = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    else if (const auto letter = foldLatinLetter(value)) mapped = letter;
    else mapped = foldPunctuation(value);
    if (!mapped) {
      result.unsupportedIndex = index;
      return result;
    }
    result.text.push_back(*mapped);
  }
  return result;
}

bool englishSeparator(char value) noexcept {
  switch (value) {
    case ' ': case '\t': case '\r': case '\n': case '\v': case '\f':
    case '-': case ',': case '.': case '!': case '?': case ';': case ':': case '"':
    case '(': case ')': case '[': case ']': case '{': case '}':
      return true;
    default:
      return false;
  }
}

bool englishWordCharacter(char value) noexcept {
  return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '\'';
}

std::string singleQuoted(std::string_view word) { return "'" + std::string{word} + "'"; }

enum class LyricKind { Words, Continuation, Punctuation, Unsupported };

struct WordOrigin final {
  std::string text;
  std::size_t characterIndex{0U};
  EnglishReadingSource source{EnglishReadingSource::Dictionary};
  std::string basis;
};

struct LyricAnalysis final {
  LyricKind kind{LyricKind::Unsupported};
  PhoneList phones;
  std::vector<std::size_t> breaks;  // complete syllable boundaries
  std::vector<std::size_t> origin;  // word index of each phone
  std::vector<WordOrigin> words;
  std::size_t characterIndex{0U};
  std::string message;
};

LyricAnalysis unsupported(std::size_t index, std::string message) {
  LyricAnalysis result;
  result.characterIndex = index;
  result.message = std::move(message);
  return result;
}

LyricAnalysis analyzeEnglishLyric(const EnglishPronunciationResource& resource,
    const domain::LyricToken& lyric) {
  const auto normalized = normalizeEnglishLyric(lyric.surface);
  if (normalized.unsupportedIndex)
    return unsupported(*normalized.unsupportedIndex,
        "English lyric contains a character outside supported Latin letters and punctuation");
  const auto& text = normalized.text;
  if (text == "-" || text == "~") {
    LyricAnalysis result;
    result.kind = LyricKind::Continuation;
    return result;
  }
  struct Span final { std::size_t start; std::size_t end; };
  std::vector<Span> spans;
  std::optional<std::size_t> start;
  for (std::size_t index = 0U; index <= text.size(); ++index) {
    const bool end = index == text.size();
    if (!end && englishWordCharacter(text[index])) {
      if (!start) start = index;
      continue;
    }
    if (!end && !englishSeparator(text[index]))
      return unsupported(index, "English lyric contains an unsupported symbol");
    if (start) {
      const auto word = std::string_view{text}.substr(*start, index - *start);
      // Apostrophe-only runs are quotation marks, not words.
      if (word.find_first_not_of('\'') != std::string_view::npos) {
        if (spans.size() == kMaximumNoteWords)
          return unsupported(0U, "English lyric exceeds 32 words on one note");
        spans.push_back({*start, index});
      }
      start.reset();
    }
  }
  LyricAnalysis result;
  if (spans.empty()) {
    result.kind = LyricKind::Punctuation;
    return result;
  }
  for (const auto& span : spans) {
    const auto word = std::string_view{text}.substr(span.start, span.end - span.start);
    auto reading = resource.resolveWord(word);
    if (!reading)
      return unsupported(span.start, "English word " + singleQuoted(word) +
          " has no dictionary reading or supported spelling estimate; provide an explicit phone hint");
    const auto& phones = reading->reading.phones;
    if (phones.size() > kMaximumNotePhones - result.phones.size())
      return unsupported(span.start, "English lyric exceeds 256 phones on one note");
    const auto offset = result.phones.size();
    if (offset > 0U) result.breaks.push_back(offset);
    for (const auto boundary : completeSyllableBreaks(phones, reading->reading.syllableBreaks))
      result.breaks.push_back(offset + boundary);
    result.phones.insert(result.phones.end(), phones.begin(), phones.end());
    result.origin.insert(result.origin.end(), phones.size(), result.words.size());
    result.words.push_back({std::string{word}, span.start, reading->source, std::move(reading->basis)});
  }
  result.kind = LyricKind::Words;
  return result;
}

bool estimatedSource(EnglishReadingSource source) noexcept {
  return source == EnglishReadingSource::Derived || source == EnglishReadingSource::SpellingEstimate;
}

std::optional<Warning> estimationWarning(const LyricAnalysis& analysis,
    std::span<const std::size_t> phones, domain::NoteId noteId) {
  std::vector<std::size_t> words;
  for (const auto phone : phones) {
    const auto word = analysis.origin[phone];
    if (estimatedSource(analysis.words[word].source) &&
        std::find(words.begin(), words.end(), word) == words.end()) words.push_back(word);
  }
  if (words.empty()) return std::nullopt;
  std::string message = "Estimated English pronunciation, not a dictionary reading:";
  for (std::size_t index = 0U; index < std::min(words.size(), kMaximumNamedWords); ++index) {
    const auto& word = analysis.words[words[index]];
    message += index == 0U ? " " : "; ";
    message += singleQuoted(word.text);
    message += word.source == EnglishReadingSource::Derived
        ? " derived from " + singleQuoted(word.basis) + " by " + std::string{kEnglishDerivationRule}
        : " spelled by " + std::string{kEnglishSpellingRule};
  }
  if (words.size() > kMaximumNamedWords)
    message += "; and " + std::to_string(words.size() - kMaximumNamedWords) + " more";
  message += "; verify or provide an explicit phone hint";
  return Warning{WarningCode::EstimatedPronunciation, noteId,
      analysis.words[words.front()].characterIndex, std::move(message)};
}

// ---- Note distribution (seam-en-note-distribution-v1) -----------------------------

struct NoteSlice final {
  std::vector<std::size_t> phones;  // indices into the analysis phones
  std::vector<std::size_t> breaks;  // local syllable boundaries
};

NoteSlice wholeReading(const LyricAnalysis& analysis) {
  NoteSlice slice;
  for (std::size_t index = 0U; index < analysis.phones.size(); ++index) slice.phones.push_back(index);
  slice.breaks = analysis.breaks;
  return slice;
}

std::vector<NoteSlice> distributeReading(const LyricAnalysis& analysis, std::size_t noteCount) {
  std::vector<NoteSlice> slices(noteCount);
  if (noteCount == 1U) {
    slices.front() = wholeReading(analysis);
    return slices;
  }
  struct Unit final { std::size_t start; std::size_t nucleus; std::size_t end; };
  std::vector<Unit> units;
  std::optional<std::size_t> pendingStart;
  const auto size = analysis.phones.size();
  for (std::size_t segment = 0U; segment <= analysis.breaks.size(); ++segment) {
    const auto first = segment == 0U ? 0U : analysis.breaks[segment - 1U];
    const auto last = segment < analysis.breaks.size() ? analysis.breaks[segment] : size;
    std::optional<std::size_t> nucleus;
    for (auto index = first; index < last && !nucleus; ++index)
      if (isVowelSymbol(analysis.phones[index])) nucleus = index;
    if (!nucleus) {
      // Vowel-less material joins the following syllable, or the last one.
      if (!pendingStart) pendingStart = first;
      continue;
    }
    units.push_back({pendingStart.value_or(first), *nucleus, last});
    pendingStart.reset();
  }
  if (units.empty()) {
    slices.front() = wholeReading(analysis);
    return slices;  // later notes have no vowel to sustain; the caller diagnoses them
  }
  if (pendingStart) units.back().end = size;
  const auto range = [](NoteSlice& slice, std::size_t first, std::size_t last) {
    for (auto index = first; index < last; ++index) slice.phones.push_back(index);
  };
  if (noteCount <= units.size()) {
    for (std::size_t note = 0U; note + 1U < noteCount; ++note) range(slices[note], units[note].start, units[note].end);
    auto& last = slices.back();
    const auto first = units[noteCount - 1U].start;
    range(last, first, units.back().end);
    for (auto unit = noteCount; unit < units.size(); ++unit) last.breaks.push_back(units[unit].start - first);
    return slices;
  }
  for (std::size_t note = 0U; note + 1U < units.size(); ++note) range(slices[note], units[note].start, units[note].end);
  // The last syllable's vowel carries the melisma; its coda closes the word.
  const auto& closing = units.back();
  range(slices[units.size() - 1U], closing.start, closing.nucleus + 1U);
  for (auto note = units.size(); note < noteCount; ++note) slices[note].phones.push_back(closing.nucleus);
  range(slices.back(), closing.nucleus + 1U, closing.end);
  return slices;
}

// ---- Tokens ------------------------------------------------------------------------

void appendPhone(std::vector<domain::PhonemeToken>& target, domain::NoteId note,
    std::uint16_t& ordinal, std::string symbol) {
  const auto role = inferRole(symbol);
  const auto voiced = isVoicedSymbol(symbol);
  target.push_back(domain::PhonemeToken{
      .key = domain::PhonemeKey{note, ordinal}, .symbol = std::move(symbol),
      .role = role, .voiced = voiced, .timing = {}, .locked = false});
  ++ordinal;
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
      warnings.push_back({WarningCode::InvalidOverride, noteId, 0U, valid.error().message});
      continue;
    }
    if (value.symbol && !isEnglishVocabularySymbol(*value.symbol)) {
      // Retained for correction; applying it would emit a phone no English
      // resource can declare, which is substitution by another name.
      warnings.push_back({WarningCode::InvalidOverride, noteId, value.key.ordinal,
          "English phoneme edit uses a symbol outside " + std::string{kEnglishVocabularyId}});
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

}  // namespace

core::Result<std::vector<std::string>> parseEnglishPhoneHint(std::string_view text) {
  auto reading = parseEnglishPhoneReading(text);
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
  if (const auto status = resource_.status(); !status) return core::Result<Result>{status.error()};
  std::unordered_map<domain::LyricTokenId, const domain::LyricToken*> lyrics;
  for (const auto& lyric : region.lyrics) lyrics.emplace(lyric.id, &lyric);
  std::unordered_map<domain::NoteId, std::vector<const domain::PhonemeOverride*>> overrides;
  for (const auto& edit : region.phonemeOverrides) overrides[edit.key.noteId].push_back(&edit);
  std::vector<const domain::Note*> notes;
  for (const auto& note : region.notes) notes.push_back(&note);
  std::stable_sort(notes.begin(), notes.end(), [](const auto* lhs, const auto* rhs) {
    return lhs->startTick == rhs->startTick ? lhs->id < rhs->id : lhs->startTick < rhs->startTick;
  });
  // Shared-lyric melisma groups in processing order.
  std::vector<std::size_t> heads(notes.size()), positions(notes.size()), sizes(notes.size(), 0U);
  for (std::size_t index = 0U; index < notes.size(); ++index) {
    const bool continues = index > 0U && domain::continuesSharedLyric(*notes[index - 1U], *notes[index]);
    heads[index] = continues ? heads[index - 1U] : index;
    positions[index] = continues ? positions[index - 1U] + 1U : 0U;
    ++sizes[heads[index]];
  }
  struct GroupReading final {
    std::size_t head{0U};
    LyricAnalysis analysis;
    std::vector<NoteSlice> slices;
  };
  std::optional<GroupReading> group;

  Result result;
  std::optional<std::string> previousVowel;
  bool previousVowelEstimated = false;
  std::optional<time::Tick> previousEnd, occupiedEnd;
  bool previousIsolated = true;
  for (std::size_t noteIndex = 0U; noteIndex < notes.size(); ++noteIndex) {
    const auto* note = notes[noteIndex];
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
    std::vector<bool> tokenEstimated;
    bool continuationEstimated = false;
    std::uint16_t ordinal = 0U;
    const auto lyricEntry = lyrics.find(note->lyricTokenId);
    const auto* lyric = lyricEntry == lyrics.end() ? nullptr : lyricEntry->second;
    if (note->phoneticHint) {
      const auto parsed = parseEnglishPhoneReading(*note->phoneticHint);
      if (!parsed) {
        result.warnings.push_back({WarningCode::UnsupportedCharacter, note->id, 0U, parsed.error().message});
        appendPhone(noteTokens, note->id, ordinal, "pau");
      } else {
        for (const auto& phone : parsed.value().phones) appendPhone(noteTokens, note->id, ordinal, phone);
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
      const auto groupSize = sizes[heads[noteIndex]];
      LyricAnalysis single;
      NoteSlice whole;
      const LyricAnalysis* analysis = nullptr;
      const NoteSlice* slice = nullptr;
      if (groupSize > 1U) {
        if (!group || group->head != heads[noteIndex]) {
          group = GroupReading{heads[noteIndex], analyzeEnglishLyric(resource_, *lyric), {}};
          if (group->analysis.kind == LyricKind::Words)
            group->slices = distributeReading(group->analysis, groupSize);
        }
        analysis = &group->analysis;
        if (analysis->kind == LyricKind::Words) slice = &group->slices[positions[noteIndex]];
      } else {
        single = analyzeEnglishLyric(resource_, *lyric);
        analysis = &single;
        if (single.kind == LyricKind::Words) {
          whole = wholeReading(single);
          slice = &whole;
        }
      }
      switch (analysis->kind) {
        case LyricKind::Unsupported:
          result.warnings.push_back({WarningCode::UnsupportedCharacter, note->id,
              analysis->characterIndex, analysis->message});
          appendPhone(noteTokens, note->id, ordinal, "pau");
          break;
        case LyricKind::Punctuation:
          appendPhone(noteTokens, note->id, ordinal, "pau");
          break;
        case LyricKind::Continuation:
          if (previousVowel) {
            appendPhone(noteTokens, note->id, ordinal, *previousVowel);
            continuationEstimated = previousVowelEstimated;
          } else {
            result.warnings.push_back({WarningCode::LeadingLongVowel, note->id, 0U,
                "English continuation has no preceding vowel"});
            appendPhone(noteTokens, note->id, ordinal, "pau");
          }
          break;
        case LyricKind::Words:
          if (slice->phones.empty()) {
            result.warnings.push_back({WarningCode::LeadingLongVowel, note->id, 0U,
                "Shared English lyric has no vowel to sustain on this note"});
            appendPhone(noteTokens, note->id, ordinal, "pau");
            break;
          }
          for (const auto phone : slice->phones) {
            appendPhone(noteTokens, note->id, ordinal, analysis->phones[phone]);
            tokenEstimated.push_back(estimatedSource(analysis->words[analysis->origin[phone]].source));
          }
          syllableBreaks = slice->breaks;
          if (auto warning = estimationWarning(*analysis, slice->phones, note->id))
            result.warnings.push_back(std::move(*warning));
          break;
      }
    }
    if (continuationEstimated) {
      result.warnings.push_back({WarningCode::EstimatedPronunciation, note->id, 0U,
          "Estimated English pronunciation continues from an estimate, not a dictionary reading; verify or provide an explicit phone hint"});
    }
    // Establish the source's syllables before addressed manual edits. Stress
    // remains literal; onset inference is not a full dialect/morphology model.
    assignEnglishRoles(noteTokens, syllableBreaks);
    if (const auto found = overrides.find(note->id); found != overrides.end())
      applyOverrides(found->second, note->id, noteTokens, result.warnings);
    previousVowel.reset();
    previousVowelEstimated = false;
    for (auto index = noteTokens.size(); index-- > 0U;) {
      if (noteTokens[index].role == domain::PhonemeRole::Silence) break;
      if (isVowelSymbol(noteTokens[index].symbol)) {
        previousVowel = noteTokens[index].symbol;
        previousVowelEstimated = continuationEstimated ||
            (index < tokenEstimated.size() && tokenEstimated[index]);
        break;
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
