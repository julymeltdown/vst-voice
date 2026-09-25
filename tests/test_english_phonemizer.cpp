#include "test_framework.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/phonemizer/english_phonemizer.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/synthesis/phoneme_timing_plan.hpp"

#include <algorithm>
#include <stop_token>

namespace {
struct Fixture final {
  seam::application::ProjectFactory factory{901000U};
  seam::domain::Project project{factory.createProject("English phonemes")};
  seam::domain::TrackId track{factory.addVocalTrack(project, "English")};
  seam::domain::RegionId region{factory.addRegion(project, track, "Phrase", seam::time::Tick{0}, seam::time::Tick{3840})};
  std::vector<seam::domain::NoteId> notes;

  void add(std::int64_t tick, const char32_t* text) {
    auto [lyric, note] = factory.makeNote(seam::time::Tick{tick}, seam::time::Tick{960}, 60U,
                                          std::u32string{text}, seam::domain::Language::English);
    notes.push_back(note.id);
    auto* value = project.findRegion(region);
    value->lyrics.push_back(std::move(lyric));
    value->notes.push_back(std::move(note));
  }
};
}

TEST_CASE("English phonemizer keeps stressed dictionary output and explicit hints distinct") {
  using namespace seam;
  Fixture fixture;
  fixture.add(0, U"hello");
  fixture.add(960, U"world");
  fixture.add(1920, U"-");
  fixture.add(2880, U",");
  auto* region = fixture.project.findRegion(fixture.region);
  region->findNote(fixture.notes[1])->phoneticHint = "w er1 l d";
  const auto result = phonemizer::EnglishPhonemizer{}.phonemize(*region);
  CHECK(result.warnings.empty());
  CHECK(result.tokens.size() == 10U);
  CHECK(result.tokens[0].symbol == "hh");
  CHECK(result.tokens[1].symbol == "ah0");
  CHECK(result.tokens[3].symbol == "ow1");
  CHECK(result.tokens[4].symbol == "w");
  CHECK(result.tokens[5].symbol == "er1");
  CHECK(result.tokens[8].symbol == "er1");
  CHECK(result.tokens[9].symbol == "pau");
  CHECK(phonemizer::isVowelSymbol("eh0"));
  CHECK(phonemizer::inferRole("ow1") == domain::PhonemeRole::Nucleus);
  CHECK(phonemizer::parseEnglishPhoneHint("s iy1 ng").value() ==
      (std::vector<std::string>{"s", "iy1", "ng"}));
  CHECK(!phonemizer::parseEnglishPhoneHint("not-a-phone"));
}

TEST_CASE("English CMUdict lookup remains complete across non-sorted source entries") {
  using namespace seam;
  struct Example final {
    const char32_t* word;
    std::vector<std::string> phones;
  };
  const Example examples[]{
      {U"a", {"ah0"}}, // Stable ordering retains the first CMUdict pronunciation variant.
      {U"sepultura", {"s", "eh1", "p", "uh0", "l", "t", "uh1", "r", "uh0"}},
      {U"stilted", {"s", "t", "ih1", "l", "t", "ih0", "d"}}};
  for (const auto& example : examples) {
    Fixture fixture;
    fixture.add(0, example.word);
    const auto resolved = phonemizer::resolveEnglishPronunciation(
        *fixture.project.findRegion(fixture.region));
    CHECK(resolved);
    CHECK(resolved.value().identity.resolverVersion == "7");
    CHECK(resolved.value().pronunciation.warnings.empty());
    std::vector<std::string> actual;
    for (const auto& token : resolved.value().pronunciation.tokens) actual.push_back(token.symbol);
    CHECK(actual == example.phones);
  }
}

TEST_CASE("English dictionary words survive boundary punctuation without rewriting lyric text") {
  using namespace seam;
  struct Example final {
    const char32_t* lyric;
    std::vector<std::string> phones;
  };
  const Example examples[]{
      {U"hello,", {"hh", "ah0", "l", "ow1"}},
      {U"\"hello!\"", {"hh", "ah0", "l", "ow1"}},
      {U"(world!)", {"w", "er1", "l", "d"}},
      {U"'cause", {"k", "ah0", "z"}}};
  for (const auto& example : examples) {
    Fixture fixture;
    fixture.add(0, example.lyric);
    const auto before = *fixture.project.findRegion(fixture.region);
    const auto resolved = phonemizer::resolveEnglishPronunciation(before);
    CHECK(resolved);
    CHECK(resolved.value().pronunciation.warnings.empty());
    CHECK(*fixture.project.findRegion(fixture.region) == before);
    std::vector<std::string> actual;
    for (const auto& token : resolved.value().pronunciation.tokens) actual.push_back(token.symbol);
    CHECK(actual == example.phones);
  }
}

TEST_CASE("OpenUtau English hint examples preserve exact phones roles and source identity") {
  using namespace seam;
  // OpenUtau.Test/Plugins/EnArpaTest.cs HintTest and EnArpaPlusTest.cs
  // SyllableCCVTest, OpenUtau commit 83e02c7e4a4d9ea5fca72806b2aa27c5382be015
  // (MIT; see libs/seam-phonemizer/OPENUTAU_REFERENCE_NOTICE.md).
  // These are explicit phone hints, not bank aliases: r/iy/d and m/ao/r
  // have the same symbols in SEAM. Oto suffixes, color, and release aliases
  // are intentionally outside this comparison. No dictionary reading is imported.
  struct Example final {
    const char32_t* lyric;
    const char* hint;
    std::vector<std::string> phones;
  };
  const Example examples[]{
      {U"read", "r iy d", {"r", "iy", "d"}},
      {U"asdfjkl", "r iy d", {"r", "iy", "d"}},
      {U"", "r iy d", {"r", "iy", "d"}},
      {U"more", "m ao r", {"m", "ao", "r"}}};
  const std::vector<domain::PhonemeRole> expectedRoles{
      domain::PhonemeRole::Onset, domain::PhonemeRole::Nucleus, domain::PhonemeRole::Coda};
  for (const auto& example : examples) {
    Fixture fixture; fixture.add(0, example.lyric);
    auto* region = fixture.project.findRegion(fixture.region);
    region->notes.front().phoneticHint = example.hint;
    const auto original = *region;
    const auto parsed = phonemizer::parseEnglishPhoneHint(example.hint);
    CHECK(parsed); CHECK(parsed.value() == example.phones);
    const auto resolved = phonemizer::resolveEnglishPronunciation(*region); CHECK(resolved);
    CHECK(resolved.value().pronunciation.warnings.empty());
    CHECK(*region == original);
    const auto& tokens = resolved.value().pronunciation.tokens;
    CHECK(tokens.size() == example.phones.size());
    for (std::size_t index = 0U; index < tokens.size(); ++index) {
      CHECK(tokens[index].symbol == example.phones[index]);
      CHECK(tokens[index].role == expectedRoles[index]);
      CHECK(tokens[index].key.noteId == fixture.notes.front());
      CHECK(tokens[index].key.ordinal == index);
      CHECK(tokens[index].lyricOwner == region->notes.front().lyricTokenId);
      CHECK(tokens[index].contextId.size() == 64U);
    }
  }
}

TEST_CASE("English ao hints retain stress and edits cannot cross a changed vowel context") {
  using namespace seam;
  Fixture fixture; fixture.add(0, U"more"); fixture.add(960, U"-");
  auto* region = fixture.project.findRegion(fixture.region);
  const auto estimated = phonemizer::resolveEnglishPronunciation(*region); CHECK(estimated);
  CHECK(estimated.value().pronunciation.warnings.empty());
  CHECK(estimated.value().pronunciation.tokensForNote(fixture.notes.front())[1].symbol == "ao1");

  for (const auto* vowel : {"ao", "ao0", "ao1", "ao2"}) {
    region->notes.front().phoneticHint = std::string{"m "} + vowel + " r";
    const auto resolved = phonemizer::resolveEnglishPronunciation(*region); CHECK(resolved);
    CHECK(resolved.value().pronunciation.warnings.empty());
    CHECK(resolved.value().pronunciation.tokens[1].symbol == vowel);
    CHECK(resolved.value().pronunciation.tokens[1].role == domain::PhonemeRole::Nucleus);
    CHECK(resolved.value().pronunciation.tokens[1].voiced);
    const auto continued = resolved.value().pronunciation.tokensForNote(fixture.notes.back());
    CHECK(continued.size() == 1U); CHECK(continued.front().symbol == vowel);
    CHECK(continued.front().lyricOwner == region->notes.back().lyricTokenId);
    CHECK(resolved.value().identity != estimated.value().identity);
  }
  region->notes.front().phoneticHint = "m ao1 r";
  const auto base = phonemizer::resolveEnglishPronunciation(*region); CHECK(base);
  const auto key = base.value().pronunciation.tokens[1].key;
  const auto context = phonemizer::phonemeEditContextId(base.value(), key); CHECK(context);
  region->phonemeOverrides = {{.key = key, .symbol = "ow1", .locked = true, .sourceContextId = context}};
  const auto edited = phonemizer::resolveEnglishPronunciation(*region); CHECK(edited);
  CHECK(edited.value().pronunciation.tokens[1].symbol == "ow1");
  CHECK(edited.value().pronunciation.tokens[1].locked);
  region->notes.front().phoneticHint = "m ao2 r";
  const auto stale = phonemizer::resolveEnglishPronunciation(*region); CHECK(stale);
  CHECK(stale.value().pronunciation.tokens[1].symbol == "ao2");
  CHECK(!stale.value().pronunciation.tokens[1].locked);
  CHECK(stale.value().pronunciation.warnings.size() == 1U);
  CHECK(stale.value().pronunciation.warnings.front().code == phonemizer::WarningCode::OrphanOverride);
  CHECK(region->phonemeOverrides.front().sourceContextId == context);
  region->phonemeOverrides.clear(); region->notes.front().phoneticHint.reset();
  const auto restored = phonemizer::resolveEnglishPronunciation(*region); CHECK(restored);
  CHECK(restored.value().identity == estimated.value().identity);
  CHECK(!phonemizer::parseEnglishPhoneHint("ao3"));
}

TEST_CASE("English resolver binds tokens to source identity and rejects unknown words without fallback") {
  using namespace seam;
  Fixture fixture;
  fixture.add(0, U"hello");
  fixture.add(960, U"未知");
  const auto resolved = phonemizer::resolveEnglishPronunciation(*fixture.project.findRegion(fixture.region));
  CHECK(resolved);
  CHECK(resolved.value().identity.language == domain::Language::English);
  CHECK(resolved.value().identity.resolverId == "seam-builtin-en");
  CHECK(resolved.value().identity.resourceHash.size() == 64U);
  CHECK(!resolved.value().pronunciation.tokens.empty());
  CHECK(resolved.value().pronunciation.tokens.front().contextId.size() == 64U);
  CHECK(resolved.value().pronunciation.tokens.front().lyricOwner.valid());
  CHECK(phonemizer::resolvePronunciation(*fixture.project.findRegion(fixture.region)));
  CHECK(std::any_of(resolved.value().pronunciation.warnings.begin(),
      resolved.value().pronunciation.warnings.end(), [](const auto& warning) {
        return warning.code == phonemizer::WarningCode::UnsupportedCharacter;
      }));
  CHECK(phonemizer::inspectEnglishPronunciation(*fixture.project.findRegion(fixture.region)).warnings.size() >= 1U);
}

TEST_CASE("generic pronunciation resolver rejects mixed explicit language regions") {
  using namespace seam;
  Fixture fixture;
  fixture.add(0, U"hello");
  fixture.add(960, U"안녕");
  auto* region = fixture.project.findRegion(fixture.region);
  region->findLyric(region->notes.back().lyricTokenId)->language = domain::Language::Korean;
  CHECK(!phonemizer::resolvePronunciation(*region));
  CHECK(phonemizer::inspectPronunciation(*region).warnings.size() == 1U);
}

TEST_CASE("English dictionary readings and continuations remain distinct from explicit hints") {
  using namespace seam;
  Fixture fixture;
  fixture.add(0, U"read");
  fixture.add(960, U"-");
  fixture.add(1920, U"hello");
  auto* region = fixture.project.findRegion(fixture.region);
  const auto before = *region;
  const auto estimated = phonemizer::resolveEnglishPronunciation(*region);
  CHECK(estimated);
  CHECK(*region == before);
  CHECK(estimated.value().pronunciation.warnings.empty());
  CHECK(estimated.value().pronunciation.tokensForNote(fixture.notes.front())[1].symbol == "eh1");
  CHECK(estimated.value().pronunciation.tokensForNote(fixture.notes[1]).front().symbol == "eh1");
  region->notes.front().phoneticHint = "r iy1 d";
  const auto explicitReading = phonemizer::resolveEnglishPronunciation(*region);
  CHECK(explicitReading);
  CHECK(explicitReading.value().pronunciation.warnings.empty());
  CHECK(explicitReading.value().pronunciation.tokensForNote(fixture.notes[0])[1].symbol == "iy1");
  CHECK(explicitReading.value().pronunciation.tokensForNote(fixture.notes[1]).front().symbol == "iy1");
  CHECK(explicitReading.value().identity != estimated.value().identity);
  CHECK(phonemizer::isVowelSymbol("ax0"));
  CHECK(phonemizer::isVowelSymbol("axr1"));
  CHECK(phonemizer::inferRole("ax0") == domain::PhonemeRole::Nucleus);
  CHECK(!phonemizer::parseEnglishPhoneHint("p1 aa1"));
}

TEST_CASE("English spelling fallback remains bounded and does not carry guesses across a pause") {
  using namespace seam;
  Fixture fixture;
  fixture.add(0, U"haxz");
  fixture.add(960, U",");
  fixture.add(1920, U"-");
  auto* region = fixture.project.findRegion(fixture.region);
  const auto result = phonemizer::EnglishPhonemizer{}.phonemize(*region);
  std::string phones;
  for (const auto& token : result.tokensForNote(fixture.notes.front())) {
    if (!phones.empty()) phones += ' ';
    phones += token.symbol;
  }
  CHECK(phones == "hh ae0 k s z");
  CHECK(phonemizer::parseEnglishPhoneHint(phones));
  CHECK(result.tokensForNote(fixture.notes.back()).front().symbol == "pau");
  CHECK(std::any_of(result.warnings.begin(), result.warnings.end(), [&](const auto& warning) {
    return warning.noteId == fixture.notes.back() && warning.code == phonemizer::WarningCode::LeadingLongVowel;
  }));
}

TEST_CASE("English and Korean reject stale replacement and appended phoneme contexts") {
  using namespace seam;
  for (const auto language : {domain::Language::English, domain::Language::Korean}) {
    for (const bool append : {false, true}) {
      Fixture fixture;
      fixture.add(0, U"a");
      auto* region = fixture.project.findRegion(fixture.region);
      region->lyrics.front().language = language;
      region->notes.front().phoneticHint = language == domain::Language::English ? "aa1" : "a";
      const auto base = phonemizer::resolvePronunciation(*region);
      CHECK(base);
      CHECK(base.value().pronunciation.tokens.size() == 1U);
      const domain::PhonemeKey key{region->notes.front().id,
          static_cast<std::uint16_t>(append ? 1U : 0U)};
      const auto context = phonemizer::phonemeEditContextId(base.value(), key);
      CHECK(context);
      region->phonemeOverrides = {{.key = key,
          .symbol = language == domain::Language::English ? "iy1" : "u",
          .timing = {.startOffset = 1000}, .locked = true, .sourceContextId = context}};
      const auto valid = phonemizer::resolvePronunciation(*region);
      CHECK(valid);
      CHECK(valid.value().pronunciation.warnings.empty());
      CHECK(valid.value().pronunciation.tokens[key.ordinal].locked);
      const auto original = *region;

      region->phonemeOverrides.front().sourceContextId = std::string(64U, 'b');
      const auto invalid = phonemizer::resolvePronunciation(*region);
      CHECK(invalid);
      CHECK(invalid.value().pronunciation.tokens == base.value().pronunciation.tokens);
      CHECK(std::any_of(invalid.value().pronunciation.warnings.begin(),
          invalid.value().pronunciation.warnings.end(), [](const auto& warning) {
            return warning.code == phonemizer::WarningCode::OrphanOverride;
          }));
      CHECK(!region->phonemeOverrides.front().unresolved);
      const auto invalidIdentity = invalid.value().identity;
      region->phonemeOverrides.front().sourceContextId = std::string(64U, 'c');
      const auto anotherInvalid = phonemizer::resolvePronunciation(*region);
      CHECK(anotherInvalid);
      CHECK(anotherInvalid.value().identity.inputHash != invalidIdentity.inputHash);

      *region = original;
      region->notes.front().phoneticHint = language == domain::Language::English ? "k aa1" : "k a";
      const auto savedEdit = region->phonemeOverrides.front();
      const auto changed = phonemizer::resolvePronunciation(*region);
      CHECK(changed);
      CHECK(changed.value().pronunciation.tokens.size() == 2U);
      CHECK(changed.value().pronunciation.tokens[0].symbol == "k");
      CHECK(changed.value().pronunciation.tokens[1].symbol ==
          (language == domain::Language::English ? "aa1" : "a"));
      CHECK(std::none_of(changed.value().pronunciation.tokens.begin(),
          changed.value().pronunciation.tokens.end(), [](const auto& token) { return token.locked; }));
      CHECK(region->phonemeOverrides.front() == savedEdit);
      CHECK(std::any_of(changed.value().pronunciation.warnings.begin(),
          changed.value().pronunciation.warnings.end(), [](const auto& warning) {
            return warning.code == phonemizer::WarningCode::OrphanOverride;
          }));
    }
  }
}

TEST_CASE("generic pronunciation admission precedes language indexing and honours cancellation") {
  using namespace seam;
  Fixture fixture;
  fixture.add(0, U"hello");
  fixture.add(960, U"안녕");
  auto* region = fixture.project.findRegion(fixture.region);
  region->lyrics.back().language = domain::Language::Korean;
  std::stop_source stopped;
  stopped.request_stop();
  const auto cancelled = phonemizer::resolvePronunciation(*region, stopped.get_token());
  CHECK(!cancelled);
  CHECK(cancelled.error().code == core::ErrorCode::Conflict);
  const auto selectedCancelled = phonemizer::resolvePronunciationForLanguage(
      *region, domain::Language::Japanese, stopped.get_token());
  CHECK(!selectedCancelled);
  CHECK(selectedCancelled.error().code == core::ErrorCode::Conflict);

  region->lyrics.resize(phonemizer::kMaximumPronunciationNotes + 1U);
  const auto tooMany = phonemizer::resolvePronunciation(*region);
  CHECK(!tooMany);
  CHECK(tooMany.error().code == core::ErrorCode::InvalidArgument);
  const auto selectedTooMany = phonemizer::resolvePronunciationForLanguage(
      *region, domain::Language::Japanese);
  CHECK(!selectedTooMany);
  CHECK(selectedTooMany.error().code == core::ErrorCode::InvalidArgument);
  CHECK(!phonemizer::resolvePronunciation(*region, stopped.get_token()));
}

TEST_CASE("English dictionary vowels consonant clusters and terminal codas have position-aware roles") {
  using namespace seam;
  using Role = domain::PhonemeRole;
  const std::vector<std::pair<const char32_t*, std::vector<Role>>> examples{
      {U"sing", {Role::Onset, Role::Nucleus, Role::Coda}},
      {U"singer", {Role::Onset, Role::Nucleus, Role::Coda, Role::Nucleus}},
      {U"world", {Role::Onset, Role::Nucleus, Role::Coda, Role::Coda}},
      {U"and", {Role::Nucleus, Role::Coda, Role::Coda}},
      {U"dream", {Role::Onset, Role::Onset, Role::Nucleus, Role::Coda}},
      {U"banana", {Role::Onset, Role::Nucleus, Role::Onset, Role::Nucleus, Role::Onset, Role::Nucleus}},
      {U"computer", {Role::Onset, Role::Nucleus, Role::Coda, Role::Onset, Role::Onset, Role::Nucleus, Role::Onset, Role::Nucleus}},
      {U"music", {Role::Onset, Role::Onset, Role::Nucleus, Role::Onset, Role::Nucleus, Role::Coda}},
      {U"beautiful", {Role::Onset, Role::Onset, Role::Nucleus, Role::Onset, Role::Nucleus, Role::Onset, Role::Nucleus, Role::Coda}},
      {U"project", {Role::Onset, Role::Onset, Role::Nucleus, Role::Onset, Role::Nucleus, Role::Coda, Role::Coda}}};
  for (const auto& [word, expected] : examples) {
    Fixture fixture; fixture.add(0, word);
    const auto resolved = phonemizer::resolveEnglishPronunciation(*fixture.project.findRegion(fixture.region));
    CHECK(resolved); CHECK(resolved.value().identity.resolverVersion == "7");
    CHECK(resolved.value().pronunciation.warnings.empty());
    std::vector<Role> actual;
    for (const auto& token : resolved.value().pronunciation.tokens) actual.push_back(token.role);
    CHECK(actual == expected);
  }
}

TEST_CASE("English cluster syllabification changes real timing nucleus ownership without altering stress") {
  using namespace seam;
  Fixture fixture; fixture.add(0, U"singer");
  auto* region = fixture.project.findRegion(fixture.region);
  const auto singer = phonemizer::resolveEnglishPronunciation(*region); CHECK(singer);
  const auto timing = synthesis::compilePhonemeTimingPlan(fixture.project, *region, singer.value().pronunciation.tokens, 48000U); CHECK(timing);
  CHECK(timing.value()[2].nucleusKey == std::optional{singer.value().pronunciation.tokens[1].key});
  CHECK(timing.value()[2].syllableIndex == 0U);
  CHECK(timing.value()[2].nucleusFrame < timing.value()[3].nucleusFrame);
  CHECK(singer.value().pronunciation.tokens[1].symbol == "ih1");
  CHECK(singer.value().pronunciation.tokens[3].symbol == "er0");

  region->notes.front().phoneticHint = "eh1 k s t r ax0";
  const auto extra = phonemizer::resolveEnglishPronunciation(*region); CHECK(extra);
  const auto extraTiming = synthesis::compilePhonemeTimingPlan(fixture.project, *region, extra.value().pronunciation.tokens, 48000U); CHECK(extraTiming);
  CHECK(extra.value().pronunciation.tokens[1].role == domain::PhonemeRole::Coda); // k before legal str onset.
  CHECK(extraTiming.value()[1].nucleusKey == std::optional{extra.value().pronunciation.tokens[0].key});
  for (std::size_t i = 2U; i < 5U; ++i) {
    CHECK(extra.value().pronunciation.tokens[i].role == domain::PhonemeRole::Onset);
    CHECK(extraTiming.value()[i].nucleusKey == std::optional{extra.value().pronunciation.tokens[5].key});
  }

  region->notes.front().phoneticHint = "ae2 t l ax0 s";
  const auto atlas = phonemizer::resolveEnglishPronunciation(*region); CHECK(atlas);
  CHECK(atlas.value().pronunciation.tokens[0].symbol == "ae2");
  CHECK(atlas.value().pronunciation.tokens[1].role == domain::PhonemeRole::Coda); // tl is not an inferred onset cluster.
  CHECK(atlas.value().pronunciation.tokens[2].role == domain::PhonemeRole::Onset);
  CHECK(atlas.value().pronunciation.tokens[4].role == domain::PhonemeRole::Coda);

  region->notes.front().phoneticHint.reset();
  region->lyrics.front().surface = U"extra"; // CMUdict reading, still not singing qualification.
  const auto spelling = phonemizer::resolveEnglishPronunciation(*region); CHECK(spelling);
  CHECK(spelling.value().pronunciation.tokens.size() == 6U);
  CHECK(spelling.value().pronunciation.tokens[1].role == domain::PhonemeRole::Coda);
  for (std::size_t i = 2U; i < 5U; ++i) CHECK(spelling.value().pronunciation.tokens[i].role == domain::PhonemeRole::Onset);
  CHECK(spelling.value().pronunciation.warnings.empty());
  CHECK(spelling.value().pronunciation.tokens[0].symbol == "eh1");
}

TEST_CASE("English spelling fallback recognizes common vowel digraphs and soft c g") {
  using namespace seam;
  struct Example final {
    const char32_t* word;
    std::vector<std::string> expected;
  };
  const Example examples[]{
      {U"zaiz", {"z", "ey0", "z"}},
      {U"citix", {"s", "ih0", "t", "ih0", "k", "s"}},
      {U"gaim", {"g", "ey0", "m"}},
  };
  for (const auto& example : examples) {
    Fixture fixture;
    fixture.add(0, example.word);
    const auto resolved = phonemizer::resolveEnglishPronunciation(
        *fixture.project.findRegion(fixture.region));
    CHECK(resolved);
    const auto tokens = resolved.value().pronunciation.tokensForNote(fixture.notes.front());
    std::vector<std::string> actual;
    for (const auto& token : tokens) actual.push_back(token.symbol);
    CHECK(actual == example.expected);
    CHECK(resolved.value().pronunciation.warnings.size() == 1U);
    CHECK(resolved.value().pronunciation.warnings.front().code ==
        phonemizer::WarningCode::EstimatedPronunciation);
    CHECK(resolved.value().identity.resourceHash.size() == 64U);
  }
}

TEST_CASE("English explicit syllable boundaries override estimates and reject malformed separators") {
  using namespace seam;
  Fixture fixture; fixture.add(0, U"hint");
  auto* region = fixture.project.findRegion(fixture.region);
  region->notes.front().phoneticHint = "ae1 t r ax0";
  const auto automatic = phonemizer::resolveEnglishPronunciation(*region); CHECK(automatic);
  CHECK(automatic.value().pronunciation.tokens[1].role == domain::PhonemeRole::Onset);
  region->notes.front().phoneticHint = "ae1 t . r ax0";
  const auto explicitBoundary = phonemizer::resolveEnglishPronunciation(*region); CHECK(explicitBoundary);
  CHECK(explicitBoundary.value().pronunciation.warnings.empty());
  CHECK(explicitBoundary.value().pronunciation.tokens[1].role == domain::PhonemeRole::Coda);
  CHECK(explicitBoundary.value().pronunciation.tokens[2].role == domain::PhonemeRole::Onset);
  CHECK(explicitBoundary.value().identity != automatic.value().identity);
  CHECK(phonemizer::parseEnglishPhoneHint("ae1 t . r ax0").value() ==
      (std::vector<std::string>{"ae1", "t", "r", "ax0"}));
  for (const auto text : {". ae1", "ae1 .", "ae1 . . iy0", "t . ae1", "ae1 iy0 . ax0", "ae1 . pau"})
    CHECK(!phonemizer::parseEnglishPhoneHint(text));
  const auto automaticTiming = synthesis::compilePhonemeTimingPlan(fixture.project, *region, automatic.value().pronunciation.tokens, 48000U); CHECK(automaticTiming);
  const auto explicitTiming = synthesis::compilePhonemeTimingPlan(fixture.project, *region, explicitBoundary.value().pronunciation.tokens, 48000U); CHECK(explicitTiming);
  CHECK(automaticTiming.value()[1].nucleusFrame > explicitTiming.value()[1].nucleusFrame);
}

TEST_CASE("English estimated spellings explicit pauses and continuations retain note ownership") {
  using namespace seam;
  Fixture fixture; fixture.add(0, U"haxz"); fixture.add(960, U"-"); fixture.add(2880, U"-");
  auto* region = fixture.project.findRegion(fixture.region);
  const auto before = *region;
  const auto resolved = phonemizer::resolveEnglishPronunciation(*region); CHECK(resolved);
  CHECK(*region == before);
  const auto first = resolved.value().pronunciation.tokensForNote(fixture.notes[0]);
  CHECK(first[2].role == domain::PhonemeRole::Coda); CHECK(first[3].role == domain::PhonemeRole::Coda);
  CHECK(first[1].symbol == "ae0");
  const auto continued = resolved.value().pronunciation.tokensForNote(fixture.notes[1]);
  CHECK(continued.size() == 1U); CHECK(continued.front().symbol == "ae0");
  CHECK(continued.front().key.noteId == fixture.notes[1]);
  CHECK(continued.front().lyricOwner == region->notes[1].lyricTokenId);
  CHECK(resolved.value().pronunciation.tokensForNote(fixture.notes[2]).front().symbol == "pau");
  CHECK(std::count_if(resolved.value().pronunciation.warnings.begin(), resolved.value().pronunciation.warnings.end(),
      [](const auto& value) { return value.code == phonemizer::WarningCode::EstimatedPronunciation; }) == 2);
  region->notes.front().phoneticHint = "aa1 t pau k iy0 n";
  const auto paused = phonemizer::resolveEnglishPronunciation(*region); CHECK(paused);
  const auto phones = paused.value().pronunciation.tokensForNote(fixture.notes[0]);
  CHECK(phones[1].role == domain::PhonemeRole::Coda);
  CHECK(phones[2].role == domain::PhonemeRole::Silence);
  CHECK(phones[3].role == domain::PhonemeRole::Onset);
  CHECK(phones[5].role == domain::PhonemeRole::Coda);
  region->notes.front().phoneticHint = "aa1 pau";
  const auto trailingPause = phonemizer::resolveEnglishPronunciation(*region); CHECK(trailingPause);
  CHECK(trailingPause.value().pronunciation.tokensForNote(fixture.notes[1]).front().symbol == "pau");
  region->notes.front().phoneticHint = "aa1";
  region->notes[1].startTick = time::Tick{480};
  region->notes[2].startTick = time::Tick{1440};
  const auto overlapping = phonemizer::resolveEnglishPronunciation(*region); CHECK(overlapping);
  CHECK(overlapping.value().pronunciation.tokensForNote(fixture.notes[1]).front().symbol == "pau");
  CHECK(overlapping.value().pronunciation.tokensForNote(fixture.notes[2]).front().symbol == "pau");

  Fixture melisma; melisma.add(0, U"sing"); melisma.add(960, U"-");
  const auto held = phonemizer::resolveEnglishPronunciation(*melisma.project.findRegion(melisma.region)); CHECK(held);
  CHECK(held.value().pronunciation.tokensForNote(melisma.notes[0]).back().role == domain::PhonemeRole::Coda);
  CHECK(held.value().pronunciation.tokensForNote(melisma.notes[0]).back().key.noteId == melisma.notes[0]);
  CHECK(held.value().pronunciation.tokensForNote(melisma.notes[1]).size() == 1U);
  CHECK(held.value().pronunciation.tokensForNote(melisma.notes[1]).front().symbol == "ih1");
}

TEST_CASE("English role-aware overrides preserve current locks and reject a changed syllable context") {
  using namespace seam;
  Fixture fixture; fixture.add(0, U"singer");
  auto* region = fixture.project.findRegion(fixture.region);
  const auto base = phonemizer::resolveEnglishPronunciation(*region); CHECK(base);
  const auto key = base.value().pronunciation.tokens[2].key;
  const auto context = phonemizer::phonemeEditContextId(base.value(), key); CHECK(context);
  region->phonemeOverrides = {{.key = key, .symbol = "n", .timing = {.startOffset = 1000}, .locked = true, .sourceContextId = context}};
  const auto replaced = phonemizer::resolveEnglishPronunciation(*region); CHECK(replaced);
  CHECK(replaced.value().pronunciation.tokens[2].role == domain::PhonemeRole::Coda);
  CHECK(replaced.value().pronunciation.tokens[2].locked);
  CHECK(replaced.value().pronunciation.tokens[2].timing.startOffset == std::optional<time::Microseconds>{1000});
  region->notes.front().phoneticHint = "s ih1 . ng er0";
  const auto stale = phonemizer::resolveEnglishPronunciation(*region); CHECK(stale);
  CHECK(stale.value().pronunciation.tokens[2].symbol == "ng");
  CHECK(stale.value().pronunciation.tokens[2].role == domain::PhonemeRole::Onset); // User's explicit boundary, not blanket ng=Coda.
  CHECK(!stale.value().pronunciation.tokens[2].locked);
  CHECK(std::any_of(stale.value().pronunciation.warnings.begin(), stale.value().pronunciation.warnings.end(),
      [](const auto& value) { return value.code == phonemizer::WarningCode::OrphanOverride; }));
  CHECK(region->phonemeOverrides.front().sourceContextId == context);

  region->phonemeOverrides.clear();
  region->notes.front().phoneticHint = "s ih1 ng er0"; // Inferred, not a dictionary/dot boundary.
  const auto inferred = phonemizer::resolveEnglishPronunciation(*region); CHECK(inferred);
  const auto inferredKey = inferred.value().pronunciation.tokens[2].key;
  region->phonemeOverrides = {{.key = inferredKey, .symbol = "n", .locked = true,
      .sourceContextId = phonemizer::phonemeEditContextId(inferred.value(), inferredKey)}};
  const auto inferredReplacement = phonemizer::resolveEnglishPronunciation(*region); CHECK(inferredReplacement);
  CHECK(inferredReplacement.value().pronunciation.tokens[2].role == domain::PhonemeRole::Coda);

  region->phonemeOverrides.clear();
  region->notes.front().phoneticHint = "s ih1";
  const auto open = phonemizer::resolveEnglishPronunciation(*region); CHECK(open);
  const domain::PhonemeKey appended{region->notes.front().id, 2U};
  region->phonemeOverrides = {{.key = appended, .symbol = "ng", .locked = true,
      .sourceContextId = phonemizer::phonemeEditContextId(open.value(), appended)}};
  const auto appendedCoda = phonemizer::resolveEnglishPronunciation(*region); CHECK(appendedCoda);
  CHECK(appendedCoda.value().pronunciation.tokens.back().key == appended);
  CHECK(appendedCoda.value().pronunciation.tokens.back().role == domain::PhonemeRole::Coda);
  CHECK(appendedCoda.value().pronunciation.tokens.back().locked);
}
