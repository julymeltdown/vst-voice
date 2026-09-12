#include "test_framework.hpp"
#include "seam/phonemizer/override_reconciliation.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

namespace {
using namespace seam::domain;
using seam::phonemizer::reconcilePhonemeOverrides;
using seam::phonemizer::EditCorrespondence;

std::vector<PhonemeToken> tokens(const std::string& text) {
  std::vector<PhonemeToken> result;
  for (const auto symbol : text) {
    result.push_back({.key = {NoteId{1U}, static_cast<std::uint16_t>(result.size())},
                      .symbol = std::string(1U, symbol)});
  }
  return result;
}

PhonemeOverride locked(std::uint16_t ordinal) {
  return {.key = {NoteId{1U}, ordinal}, .symbol = "custom",
          .timing = {.startOffset = -1000, .endOffset = 2000}, .locked = true};
}
}

TEST_CASE("inserted onset cannot inherit the original vowel timing lock") {
  VocalRegion region{.id = RegionId{1U}, .durationTick = seam::time::Tick{480},
      .lyrics = {{LyricTokenId{1U}, U"あ", Language::Japanese}},
      .notes = {{.id = NoteId{1U}, .durationTick = seam::time::Tick{480},
                 .lyricTokenId = LyricTokenId{1U}}}};
  seam::phonemizer::JapaneseKanaPhonemizer adapter;
  const auto before = adapter.phonemize(region).tokens;
  region.lyrics.front().surface = U"か";
  const auto after = adapter.phonemize(region).tokens;
  const std::vector edits{locked(0U)};
  const auto result = reconcilePhonemeOverrides(NoteId{1U}, before, after, edits);
  CHECK(result);
  CHECK(result.value().front().original == edits.front());
  CHECK(result.value().front().rebound.has_value());
  auto expected = edits.front();
  expected.key.ordinal = 1U;
  CHECK(*result.value().front().rebound == expected);
}

TEST_CASE("ambiguous repeated deleted and orphaned edits retain their original payload") {
  for (const auto& pair : std::vector<std::pair<std::string, std::string>>{
      {"a", "aa"}, {"aa", "a"}, {"ab", "ba"}, {"a", ""}}) {
    const std::vector edits{locked(0U), locked(9U)};
    const auto result = reconcilePhonemeOverrides(NoteId{1U}, tokens(pair.first), tokens(pair.second), edits);
    CHECK(result);
    CHECK(result.value().size() == edits.size());
    CHECK(!result.value()[0].rebound);
    CHECK(result.value()[0].original == edits[0]);
    CHECK(result.value()[0].correspondence == EditCorrespondence::RemovedOrAmbiguous);
    CHECK(!result.value()[1].rebound);
    CHECK(result.value()[1].original == edits[1]);
    CHECK(result.value()[1].correspondence == EditCorrespondence::MissingOriginal);
  }
}

TEST_CASE("correspondence includes phoneme role and voicing not only spelling") {
  const auto before = tokens("a");
  const std::vector edits{locked(0U)};
  auto after = before;
  after[0].role = PhonemeRole::Coda;
  CHECK(!reconcilePhonemeOverrides(NoteId{1U}, before, after, edits).value()[0].rebound);
  after = before;
  after[0].voiced = false;
  CHECK(!reconcilePhonemeOverrides(NoteId{1U}, before, after, edits).value()[0].rebound);
}

TEST_CASE("reconciliation rejects unbounded mixed note duplicate and edited base inputs") {
  const auto base = tokens("a");
  const std::vector edits{locked(0U)};
  CHECK(!reconcilePhonemeOverrides(NoteId{1U}, tokens(std::string(257U, 'a')), base, edits));
  CHECK(!reconcilePhonemeOverrides(NoteId{1U}, base, base, std::vector(257U, locked(0U))));
  CHECK(!reconcilePhonemeOverrides(NoteId{1U}, base, base, std::vector{locked(0U), locked(0U)}));
  for (unsigned scenario = 0U; scenario < 5U; ++scenario) {
    auto bad = base;
    if (scenario == 0U) bad[0].key.noteId = NoteId{2U};
    if (scenario == 1U) bad[0].key.ordinal = 1U;
    if (scenario == 2U) bad[0].locked = true;
    if (scenario == 3U) bad[0].timing.startOffset = 1;
    if (scenario == 4U) bad[0].symbol = std::string(1025U, 'a');
    CHECK(!reconcilePhonemeOverrides(NoteId{1U}, bad, base, edits));
  }
  CHECK(reconcilePhonemeOverrides(NoteId{1U}, tokens(std::string(256U, 'a')),
                                  tokens(std::string(256U, 'a')), edits));
}

TEST_CASE("alignment agrees with exhaustive independent correspondence enumeration") {
  std::vector<std::string> words{""};
  for (std::size_t length = 1U; length <= 4U; ++length) {
    for (std::size_t mask = 0U; mask < (1U << length); ++mask) {
      std::string word(length, 'a');
      for (std::size_t bit = 0U; bit < length; ++bit) {
        if ((mask & (1U << bit)) != 0U) word[bit] = 'b';
      }
      words.push_back(word);
    }
  }
  using Alignment = std::vector<std::pair<std::size_t, std::size_t>>;
  for (const auto& a : words) for (const auto& b : words) {
    std::vector<Alignment> best;
    Alignment path;
    std::size_t maximum = 0U;
    std::function<void(std::size_t, std::size_t)> enumerate = [&](std::size_t i, std::size_t j) {
      if (path.size() > maximum) { maximum = path.size(); best.clear(); }
      if (path.size() == maximum) best.push_back(path);
      for (std::size_t x = i; x < a.size(); ++x) for (std::size_t y = j; y < b.size(); ++y) {
        if (a[x] != b[y]) continue;
        path.emplace_back(x, y);
        enumerate(x + 1U, y + 1U);
        path.pop_back();
      }
    };
    enumerate(0U, 0U);
    std::vector<PhonemeOverride> edits;
    for (std::size_t i = 0U; i < a.size(); ++i) edits.push_back(locked(static_cast<std::uint16_t>(i)));
    const auto result = reconcilePhonemeOverrides(NoteId{1U}, tokens(a), tokens(b), edits);
    CHECK(result);
    for (std::size_t i = 0U; i < a.size(); ++i) {
      std::optional<std::size_t> expected;
      bool mandatory = true;
      for (const auto& alignment : best) {
        const auto match = std::find_if(alignment.begin(), alignment.end(),
            [i](const auto& pair) { return pair.first == i; });
        if (match == alignment.end()) { mandatory = false; break; }
        if (expected && *expected != match->second) { mandatory = false; break; }
        expected = match->second;
      }
      CHECK(result.value()[i].rebound.has_value() == mandatory);
      if (mandatory) CHECK(result.value()[i].rebound->key.ordinal == *expected);
    }
  }
}

TEST_CASE("unit spans move only when all original sounds remain contiguous") {
  using seam::phonemizer::reconcilePhonemeSpan;
  const auto moved = reconcilePhonemeSpan(NoteId{1U}, tokens("ab"), tokens("xab"),
                                         {NoteId{1U}, 0U}, 2U);
  CHECK(moved);
  CHECK(moved.value().has_value());
  CHECK(moved.value()->ordinal == 1U);
  for (const auto& changed : {"axb", "a", "aab", "ba", "ac"}) {
    const auto result = reconcilePhonemeSpan(NoteId{1U}, tokens("ab"), tokens(changed),
                                            {NoteId{1U}, 0U}, 2U);
    CHECK(result);
    CHECK(!result.value());
  }
}

TEST_CASE("seam correspondence requires both neighboring sounds not just incoming ordinal") {
  using seam::phonemizer::reconcilePhonemeSpan;
  // The incoming b survives, but its predecessor a became x: old seam is unresolved.
  const auto result = reconcilePhonemeSpan(NoteId{1U}, tokens("ab"), tokens("xb"),
                                          {NoteId{1U}, 0U}, 2U);
  CHECK(result);
  CHECK(!result.value());
  const auto incoming = reconcilePhonemeSpan(NoteId{1U}, tokens("ab"), tokens("xb"),
                                            {NoteId{1U}, 1U}, 1U);
  CHECK(incoming);
  CHECK(incoming.value().has_value());
}

TEST_CASE("span reconciliation bounds identity and preserves absent spans as unresolved") {
  using seam::phonemizer::reconcilePhonemeSpan;
  const auto base = tokens("ab");
  CHECK(!reconcilePhonemeSpan(NoteId{1U}, base, base, {NoteId{2U}, 0U}, 1U));
  CHECK(!reconcilePhonemeSpan(NoteId{1U}, base, base, {NoteId{1U}, 0U}, 0U));
  CHECK(!reconcilePhonemeSpan(NoteId{1U}, base, base, {NoteId{1U}, 0U}, 257U));
  for (const auto ordinal : {1U, 65535U}) {
    const auto result = reconcilePhonemeSpan(NoteId{1U}, base, base,
        {NoteId{1U}, static_cast<std::uint16_t>(ordinal)}, 2U);
    CHECK(result);
    CHECK(!result.value());
  }
  CHECK(!reconcilePhonemeSpan(NoteId{1U}, tokens(std::string(257U, 'a')), base,
                              {NoteId{1U}, 65535U}, 1U));
}

TEST_CASE("region correspondence preserves cross note spans and both sides of joins") {
  const auto stream = [](const std::string& first, const std::string& second) {
    auto result = tokens(first);
    auto tail = tokens(second);
    for (auto& token : tail) token.key.noteId = NoteId{2U};
    result.insert(result.end(), tail.begin(), tail.end());
    return result;
  };
  const auto before = stream("a", "b");
  const auto moved = seam::phonemizer::reconcileRegionPhonemes(before, stream("xa", "b"));
  CHECK(moved);
  const auto unit = moved.value().mapSpan({NoteId{1U}, 0U}, 2U);
  CHECK(unit);
  CHECK(unit->noteId == NoteId{1U});
  CHECK(unit->ordinal == 1U);
  CHECK((moved.value().mapBoundary({NoteId{2U}, 0U}) == PhonemeKey{NoteId{2U}, 0U}));
  const auto inserted = seam::phonemizer::reconcileRegionPhonemes(before, stream("ax", "b"));
  CHECK(inserted);
  CHECK(!inserted.value().mapSpan({NoteId{1U}, 0U}, 2U));
  CHECK(!inserted.value().mapBoundary({NoteId{2U}, 0U}));
  const auto changed = seam::phonemizer::reconcileRegionPhonemes(before, stream("x", "b"));
  CHECK(changed);
  CHECK(!changed.value().mapBoundary({NoteId{2U}, 0U}));
}

TEST_CASE("region correspondence cannot transfer identical sounds to another note") {
  const auto before = tokens("a");
  auto after = before;
  after[0].key.noteId = NoteId{2U};
  const auto result = seam::phonemizer::reconcileRegionPhonemes(before, after);
  CHECK(result);
  CHECK(!result.value().mapSpan({NoteId{1U}, 0U}, 1U));
  CHECK(!result.value().mapBoundary({NoteId{1U}, 0U}));
  auto invalid = tokens("ab");
  invalid[1].key.noteId = NoteId{2U};
  invalid[1].key.ordinal = 0U;
  invalid.push_back(before[0]);
  CHECK(!seam::phonemizer::reconcileRegionPhonemes(invalid, before));
  CHECK(!seam::phonemizer::reconcileRegionPhonemes(tokens(std::string(4097U, 'a')), before));
  CHECK(!seam::phonemizer::reconcileRegionPhonemes(tokens(std::string(257U, 'a')), before));
}

TEST_CASE("resolved pronunciation identity is deterministic and input bound") {
  VocalRegion region{.id = RegionId{1U}, .durationTick = seam::time::Tick{480},
      .lyrics = {{LyricTokenId{1U}, U"あ", Language::Japanese}},
      .notes = {{.id = NoteId{1U}, .durationTick = seam::time::Tick{480},
                 .lyricTokenId = LyricTokenId{1U}}}};
  const auto baseline = seam::phonemizer::resolveJapanesePronunciation(region);
  CHECK(baseline);
  CHECK(baseline.value().identity.validate());
  CHECK(baseline.value().identity == seam::phonemizer::resolveJapanesePronunciation(region).value().identity);
  region.name = "Cosmetic rename";
  region.notes.front().midiKey = 72U;
  CHECK(baseline.value().identity == seam::phonemizer::resolveJapanesePronunciation(region).value().identity);
  region.lyrics.front().surface = U"か";
  const auto changed = seam::phonemizer::resolveJapanesePronunciation(region);
  CHECK(changed);
  CHECK(changed.value().identity.resourceHash == baseline.value().identity.resourceHash);
  CHECK(changed.value().identity.inputHash != baseline.value().identity.inputHash);
  CHECK(changed.value().identity.sequenceHash != baseline.value().identity.sequenceHash);
  region.phonemeOverrides = {{.key = {NoteId{1U}, 1U}, .timing = {.startOffset = -1000}, .locked = true}};
  const auto edited = seam::phonemizer::resolveJapanesePronunciation(region);
  CHECK(edited);
  CHECK(edited.value().identity.sequenceHash != changed.value().identity.sequenceHash);
  region.phonemeOverrides.front().unresolved = true;
  const auto unresolved = seam::phonemizer::resolveJapanesePronunciation(region);
  CHECK(unresolved);
  CHECK(unresolved.value().identity.sequenceHash == changed.value().identity.sequenceHash);
  CHECK(unresolved.value().identity.inputHash != changed.value().identity.inputHash);
}

TEST_CASE("resolver bounds repeated lyric expansion and retains empty lyric warnings") {
  VocalRegion region{.id = RegionId{1U}, .durationTick = seam::time::Tick{480},
      .lyrics = {{LyricTokenId{1U}, U"", Language::Japanese}},
      .notes = {{.id = NoteId{1U}, .durationTick = seam::time::Tick{480},
                 .lyricTokenId = LyricTokenId{1U}}}};
  const auto empty = seam::phonemizer::resolveJapanesePronunciation(region);
  CHECK(empty);
  CHECK(!empty.value().pronunciation.warnings.empty());
  region.lyrics.front().surface = std::u32string(4096U, U'あ');
  for (std::uint64_t id = 2U; id <= 18U; ++id) {
    auto note = region.notes.front(); note.id = NoteId{id}; region.notes.push_back(note);
  }
  CHECK(!seam::phonemizer::resolveJapanesePronunciation(region));
  region.notes.resize(1U);
  region.lyrics.front().surface = std::u32string{static_cast<char32_t>(0xd800U)};
  CHECK(!seam::phonemizer::resolveJapanesePronunciation(region));
}

TEST_CASE("inspection preserves ordinary warnings and reports resolution failure explicitly") {
  VocalRegion region{.id = RegionId{1U}, .durationTick = seam::time::Tick{480},
      .lyrics = {{LyricTokenId{1U}, U"", Language::Japanese}},
      .notes = {{.id = NoteId{1U}, .durationTick = seam::time::Tick{480},
                 .lyricTokenId = LyricTokenId{1U}}}};
  const auto ordinary = seam::phonemizer::inspectJapanesePronunciation(region);
  CHECK(ordinary.tokens.size() == 1U);
  CHECK(ordinary.warnings.front().code == seam::phonemizer::WarningCode::EmptyLyric);
  region.lyrics.front().surface = std::u32string(4097U, U'あ');
  const auto failed = seam::phonemizer::inspectJapanesePronunciation(region);
  CHECK(failed.tokens.empty());
  CHECK(failed.warnings.size() == 1U);
  CHECK(failed.warnings.front().code == seam::phonemizer::WarningCode::ResolutionFailure);
}

TEST_CASE("resolved token contexts remain stable for timing but change with sound context") {
  VocalRegion region{.id = RegionId{1U}, .durationTick = seam::time::Tick{1920},
      .lyrics = {{LyricTokenId{1U}, U"あ", Language::Japanese},
                 {LyricTokenId{2U}, U"い", Language::Japanese}},
      .notes = {{.id = NoteId{1U}, .durationTick = seam::time::Tick{480},
                 .lyricTokenId = LyricTokenId{1U}},
                {.id = NoteId{2U}, .startTick = seam::time::Tick{960},
                 .durationTick = seam::time::Tick{480}, .lyricTokenId = LyricTokenId{2U}}}};
  const auto baseline = seam::phonemizer::resolveJapanesePronunciation(region);
  CHECK(baseline);
  const auto first = baseline.value().pronunciation.tokens[0];
  const auto second = baseline.value().pronunciation.tokens[1];
  CHECK(first.contextId.size() == 64U);
  CHECK(first.lyricOwner == LyricTokenId{1U});
  CHECK(first.validate());
  CHECK(first.contextId != second.contextId);
  region.phonemeOverrides = {{.key = first.key, .timing = {.startOffset = -1000}, .locked = true}};
  const auto timed = seam::phonemizer::resolveJapanesePronunciation(region);
  CHECK(timed);
  CHECK(timed.value().pronunciation.tokens[0].contextId == first.contextId);
  CHECK(timed.value().identity.sequenceHash != baseline.value().identity.sequenceHash);
  region.phonemeOverrides.clear();
  region.lyrics[0].surface = U"か";
  const auto inserted = seam::phonemizer::resolveJapanesePronunciation(region);
  CHECK(inserted);
  CHECK(inserted.value().pronunciation.tokens[1].symbol == "a");
  CHECK(inserted.value().pronunciation.tokens[1].contextId != first.contextId);
  CHECK(inserted.value().pronunciation.tokens[2].contextId == second.contextId);
  region.lyrics[0].surface = U"ああ";
  const auto repeated = seam::phonemizer::resolveJapanesePronunciation(region);
  CHECK(repeated);
  CHECK(repeated.value().pronunciation.tokens[0].contextId != first.contextId);
  CHECK(repeated.value().pronunciation.tokens[0].contextId != repeated.value().pronunciation.tokens[1].contextId);
  auto malformed = first;
  malformed.contextId = "not-a-digest";
  CHECK(!malformed.validate());
  malformed = first;
  malformed.lyricOwner = {};
  CHECK(!malformed.validate());
}
