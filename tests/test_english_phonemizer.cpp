#include "test_framework.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/phonemizer/english_phonemizer.hpp"
#include "seam/phonemizer/english_resource.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/synthesis/automatic_performance.hpp"
#include "seam/synthesis/phoneme_timing_plan.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stop_token>
#include <string>

namespace {
struct Fixture final {
  seam::application::ProjectFactory factory{901000U};
  seam::domain::Project project{factory.createProject("English phonemes")};
  seam::domain::TrackId track{factory.addVocalTrack(project, "English")};
  seam::domain::RegionId region{factory.addRegion(project, track, "Phrase", seam::time::Tick{0}, seam::time::Tick{3840})};
  std::vector<seam::domain::NoteId> notes;

  void add(std::int64_t tick, std::u32string text) {
    auto [lyric, note] = factory.makeNote(seam::time::Tick{tick}, seam::time::Tick{960}, 60U,
                                          std::move(text), seam::domain::Language::English);
    notes.push_back(note.id);
    auto* value = project.findRegion(region);
    value->lyrics.push_back(std::move(lyric));
    value->notes.push_back(std::move(note));
  }

  // One lyric token owned by adjacent legato notes: the domain's explicit melisma.
  std::vector<seam::domain::NoteId> addShared(std::int64_t tick, const std::u32string& text, std::size_t count) {
    constexpr std::int64_t duration = 480;
    auto* value = project.findRegion(region);
    auto [lyric, first] = factory.makeNote(seam::time::Tick{tick}, seam::time::Tick{duration}, 60U,
                                           text, seam::domain::Language::English);
    first.articulation = seam::domain::NoteArticulation::Legato;
    std::vector<seam::domain::NoteId> ids{first.id};
    value->lyrics.push_back(lyric);
    value->notes.push_back(first);
    for (std::size_t index = 1U; index < count; ++index) {
      auto [unused, note] = factory.makeNote(
          seam::time::Tick{tick + duration * static_cast<std::int64_t>(index)}, seam::time::Tick{duration},
          static_cast<std::uint8_t>(60U + index), text, seam::domain::Language::English);
      note.lyricTokenId = lyric.id;
      note.articulation = seam::domain::NoteArticulation::Legato;
      ids.push_back(note.id);
      value->notes.push_back(note);
    }
    notes.insert(notes.end(), ids.begin(), ids.end());
    return ids;
  }
};

std::string symbols(std::span<const seam::domain::PhonemeToken> tokens) {
  std::string result;
  for (const auto& token : tokens) {
    if (!result.empty()) result += ' ';
    result += token.symbol;
  }
  return result;
}

std::string roles(std::span<const seam::domain::PhonemeToken> tokens) {
  std::string result;
  for (const auto& token : tokens) {
    if (!result.empty()) result += ' ';
    switch (token.role) {
      case seam::domain::PhonemeRole::Onset: result += 'O'; break;
      case seam::domain::PhonemeRole::Nucleus: result += 'N'; break;
      case seam::domain::PhonemeRole::Coda: result += 'C'; break;
      case seam::domain::PhonemeRole::Silence: result += 'S'; break;
      default: result += 'X'; break;
    }
  }
  return result;
}

std::filesystem::path sourceRoot() { return std::filesystem::path{__FILE__}.parent_path().parent_path(); }

seam::formats::JsonValue readJson(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary};
  const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  auto parsed = seam::formats::parseJson(text);
  CHECK(parsed);
  return parsed ? std::move(parsed).value() : seam::formats::JsonValue{};
}

std::string field(const seam::formats::JsonValue& object, std::string_view key) {
  const auto* value = object.isObject() ? object.find(key) : nullptr;
  return value != nullptr && value->isString() ? value->asString() : std::string{};
}

std::u32string utf32(const std::string& text) {
  auto decoded = seam::domain::fromUtf8(text);
  CHECK(decoded);
  return decoded ? decoded.value() : std::u32string{};
}

std::size_t countWarnings(const seam::phonemizer::Result& result, seam::phonemizer::WarningCode code) {
  return static_cast<std::size_t>(std::count_if(result.warnings.begin(), result.warnings.end(),
      [code](const auto& warning) { return warning.code == code; }));
}
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
    CHECK(resolved.value().identity.resolverVersion == "8");
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
    CHECK(resolved); CHECK(resolved.value().identity.resolverVersion == "8");
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

TEST_CASE("English engineering fixtures keep stress clusters sources and lyric text") {
  using namespace seam;
  const auto fixture = readJson(sourceRoot() / "tests/fixtures/pronunciation/en-us-engineering-fixtures.json");
  CHECK(field(fixture, "reviewStatus") == "engineering fixtures pending native-speaker review");
  CHECK(field(fixture, "nativeSpeakerReview") == "not-performed");
  const auto* cases = fixture.isObject() ? fixture.find("lexical") : nullptr;
  CHECK(cases != nullptr && cases->isArray() && cases->asArray().size() >= 20U);
  if (cases == nullptr || !cases->isArray()) return;
  const auto& resource = phonemizer::EnglishPronunciationResource::builtin();
  CHECK(resource.status());
  for (const auto& item : cases->asArray()) {
    const auto lyric = field(item, "lyric");
    Fixture english; english.add(0, utf32(lyric));
    const auto* region = english.project.findRegion(english.region);
    const auto before = *region;
    const auto resolved = phonemizer::resolveEnglishPronunciation(*region); CHECK(resolved);
    if (!resolved) continue;
    CHECK(*english.project.findRegion(english.region) == before);  // Lyric text stays editable and unchanged.
    CHECK(resolved.value().identity.resolverId == field(fixture, "resolverId"));
    CHECK(resolved.value().identity.resolverVersion == field(fixture, "resolverVersion"));
    const auto& tokens = resolved.value().pronunciation.tokens;
    CHECK(symbols(tokens) == field(item, "phones"));
    CHECK(roles(tokens) == field(item, "roles"));
    CHECK(std::all_of(tokens.begin(), tokens.end(), [](const auto& token) {
      return phonemizer::isEnglishVocabularySymbol(token.symbol);
    }));
    const auto source = field(item, "source");
    const auto& warnings = resolved.value().pronunciation.warnings;
    if (source == "dictionary" || source == "exception") {
      CHECK(warnings.empty());
    } else {
      CHECK(warnings.size() == 1U);
      if (warnings.size() == 1U) {
        CHECK(warnings.front().code == phonemizer::WarningCode::EstimatedPronunciation);
        CHECK(warnings.front().message.find("'" + lyric + "'") != std::string::npos);
        const auto basis = field(item, "basis");
        if (!basis.empty()) CHECK(warnings.front().message.find("'" + basis + "'") != std::string::npos);
      }
    }
    if (source != "dictionary") {
      const auto word = resource.resolveWord(lyric); CHECK(word);
      if (word) {
        const auto expected = source == "exception" ? phonemizer::EnglishReadingSource::Exception
            : source == "derived" ? phonemizer::EnglishReadingSource::Derived
                                  : phonemizer::EnglishReadingSource::SpellingEstimate;
        CHECK(word->source == expected);
        CHECK(word->basis == field(item, "basis"));
      }
    }
  }
}

TEST_CASE("English shared-lyric syllables distribute consistently across note counts") {
  using namespace seam;
  const auto fixture = readJson(sourceRoot() / "tests/fixtures/pronunciation/en-us-engineering-fixtures.json");
  const auto* cases = fixture.isObject() ? fixture.find("distributions") : nullptr;
  CHECK(cases != nullptr && cases->isArray() && cases->asArray().size() >= 12U);
  if (cases == nullptr || !cases->isArray()) return;
  for (const auto& item : cases->asArray()) {
    const auto lyric = utf32(field(item, "lyric"));
    const auto& expected = item.find("notes")->asArray();
    Fixture english;
    const auto ids = english.addShared(0, lyric, expected.size());
    auto* region = english.project.findRegion(english.region);
    const auto before = *region;
    const auto resolved = phonemizer::resolveEnglishPronunciation(*region); CHECK(resolved);
    if (!resolved) continue;
    CHECK(*region == before);
    const auto& pronunciation = resolved.value().pronunciation;
    std::vector<domain::PhonemeToken> collapsed;
    for (std::size_t note = 0U; note < ids.size(); ++note) {
      const auto tokens = pronunciation.tokensForNote(ids[note]);
      CHECK(symbols(tokens) == field(expected[note], "phones"));
      CHECK(roles(tokens) == field(expected[note], "roles"));
      for (std::size_t index = 0U; index < tokens.size(); ++index) {
        CHECK(tokens[index].key.ordinal == index);
        CHECK(tokens[index].lyricOwner == region->notes.front().lyricTokenId);
        const bool sustained = index == 0U && !collapsed.empty() &&
            tokens[index].role == domain::PhonemeRole::Nucleus &&
            collapsed.back().role == domain::PhonemeRole::Nucleus && collapsed.back().symbol == tokens[index].symbol;
        if (!sustained) collapsed.push_back(tokens[index]);
      }
    }
    // The same lyric on one note: identical phones, stress marks and roles once
    // the sustained vowels of extra notes are collapsed.
    Fixture single; single.add(0, lyric);
    const auto whole = phonemizer::resolveEnglishPronunciation(*single.project.findRegion(single.region)); CHECK(whole);
    if (whole) {
      CHECK(symbols(collapsed) == symbols(whole.value().pronunciation.tokens));
      CHECK(roles(collapsed) == roles(whole.value().pronunciation.tokens));
    }
    const auto* perNote = item.find("estimatedWarningsPerNote");
    const auto estimatedPerNote = perNote != nullptr ? static_cast<std::size_t>(perNote->asInt64()) : 0U;
    CHECK(countWarnings(pronunciation, phonemizer::WarningCode::EstimatedPronunciation) == estimatedPerNote * ids.size());
    CHECK(pronunciation.warnings.size() == estimatedPerNote * ids.size());
    const auto timing = synthesis::compilePhonemeTimingPlan(english.project, *region, pronunciation.tokens, 48000U);
    CHECK(timing);
    if (timing) CHECK(timing.value().size() == pronunciation.tokens.size());
  }
}

TEST_CASE("English shared-lyric groups keep note hints literal continuations and chain boundaries") {
  using namespace seam;
  Fixture english;
  const auto ids = english.addShared(0, U"beautiful", 3U);
  english.add(1440, U"-");
  auto* region = english.project.findRegion(english.region);
  region->findNote(ids[1])->phoneticHint = "t iy1";
  const auto resolved = phonemizer::resolveEnglishPronunciation(*region); CHECK(resolved);
  if (resolved) {
    const auto& pronunciation = resolved.value().pronunciation;
    CHECK(pronunciation.warnings.empty());
    CHECK(symbols(pronunciation.tokensForNote(ids[0])) == "b y uw1");
    CHECK(symbols(pronunciation.tokensForNote(ids[1])) == "t iy1");  // Explicit input replaces only its share.
    CHECK(symbols(pronunciation.tokensForNote(ids[2])) == "f ah0 l");
    CHECK(symbols(pronunciation.tokensForNote(english.notes.back())) == "ah0");
  }

  Fixture unsupported;
  const auto bad = unsupported.addShared(0, U"x2", 2U);
  const auto refused = phonemizer::resolveEnglishPronunciation(*unsupported.project.findRegion(unsupported.region));
  CHECK(refused);
  if (refused) {
    for (const auto id : bad) CHECK(symbols(refused.value().pronunciation.tokensForNote(id)) == "pau");
    CHECK(countWarnings(refused.value().pronunciation, phonemizer::WarningCode::UnsupportedCharacter) == 2U);
  }

  // Only an adjacent legato continuation shares ownership; a gap re-reads the word.
  Fixture gap;
  const auto separated = gap.addShared(0, U"hello", 2U);
  gap.project.findRegion(gap.region)->findNote(separated[1])->startTick = time::Tick{960};
  const auto independent = phonemizer::resolveEnglishPronunciation(*gap.project.findRegion(gap.region));
  CHECK(independent);
  if (independent) {
    CHECK(symbols(independent.value().pronunciation.tokensForNote(separated[0])) == "hh ah0 l ow1");
    CHECK(symbols(independent.value().pronunciation.tokensForNote(separated[1])) == "hh ah0 l ow1");
  }
}

TEST_CASE("English lyric diagnostics name the word stay bounded and keep the lyric text") {
  using namespace seam;
  struct Example final {
    std::u32string lyric;
    std::size_t characterIndex;
    std::string fragment;
  };
  const Example examples[]{
      {U"hello \u4e16\u754c", 6U, "outside supported Latin letters"},
      {U"x2 hello", 0U, "'x2'"},
      {U"hello x2", 6U, "'x2'"},
      {U"rock & roll", 5U, "unsupported symbol"}};
  for (const auto& example : examples) {
    Fixture english; english.add(0, example.lyric);
    const auto before = *english.project.findRegion(english.region);
    const auto resolved = phonemizer::resolveEnglishPronunciation(before); CHECK(resolved);
    if (!resolved) continue;
    CHECK(*english.project.findRegion(english.region) == before);
    CHECK(symbols(resolved.value().pronunciation.tokens) == "pau");
    const auto& warnings = resolved.value().pronunciation.warnings;
    CHECK(warnings.size() == 1U);
    if (warnings.size() != 1U) continue;
    CHECK(warnings.front().code == phonemizer::WarningCode::UnsupportedCharacter);
    CHECK(warnings.front().characterIndex == example.characterIndex);
    CHECK(warnings.front().message.find(example.fragment) != std::string::npos);
  }

  std::u32string words;
  for (int index = 0; index < 32; ++index) words += U"la ";
  Fixture bounded; bounded.add(0, words);
  const auto thirtyTwo = phonemizer::resolveEnglishPronunciation(*bounded.project.findRegion(bounded.region));
  CHECK(thirtyTwo);
  if (thirtyTwo) {
    CHECK(thirtyTwo.value().pronunciation.warnings.empty());
    CHECK(thirtyTwo.value().pronunciation.tokens.size() == 64U);
  }
  Fixture excessive; excessive.add(0, words + U"la");
  const auto thirtyThree = phonemizer::resolveEnglishPronunciation(*excessive.project.findRegion(excessive.region));
  CHECK(thirtyThree);
  if (thirtyThree) {
    CHECK(symbols(thirtyThree.value().pronunciation.tokens) == "pau");
    CHECK(countWarnings(thirtyThree.value().pronunciation, phonemizer::WarningCode::UnsupportedCharacter) == 1U);
  }

  Fixture estimates; estimates.add(0, U"zaiz gaim");
  const auto two = phonemizer::resolveEnglishPronunciation(*estimates.project.findRegion(estimates.region));
  CHECK(two);
  if (two) {
    CHECK(symbols(two.value().pronunciation.tokens) == "z ey0 z g ey0 m");
    CHECK(two.value().pronunciation.warnings.size() == 1U);
    const auto& message = two.value().pronunciation.warnings.front().message;
    CHECK(message.find("'zaiz'") != std::string::npos);
    CHECK(message.find("'gaim'") != std::string::npos);
  }
  Fixture several; several.add(0, U"zaiz gaim vaiz zoiv vuzz");
  const auto five = phonemizer::resolveEnglishPronunciation(*several.project.findRegion(several.region));
  CHECK(five);
  if (five) {
    CHECK(five.value().pronunciation.warnings.size() == 1U);
    CHECK(five.value().pronunciation.warnings.front().message.find("and 2 more") != std::string::npos);
    CHECK(five.value().pronunciation.warnings.front().message.size() < 512U);
  }

  // A phoneme edit outside the English vocabulary is retained but not applied.
  Fixture edited; edited.add(0, U"hello");
  auto* region = edited.project.findRegion(edited.region);
  const auto base = phonemizer::resolveEnglishPronunciation(*region); CHECK(base);
  if (!base) return;
  const domain::PhonemeKey vowel{edited.notes.front(), 1U};
  region->phonemeOverrides = {{.key = vowel, .symbol = "a", .locked = true,
      .sourceContextId = phonemizer::phonemeEditContextId(base.value(), vowel)}};
  const auto refused = phonemizer::resolveEnglishPronunciation(*region); CHECK(refused);
  if (refused) {
    CHECK(refused.value().pronunciation.tokens[1].symbol == "ah0");
    CHECK(!refused.value().pronunciation.tokens[1].locked);
    CHECK(countWarnings(refused.value().pronunciation, phonemizer::WarningCode::InvalidOverride) == 1U);
    CHECK(region->phonemeOverrides.front().symbol == std::optional<std::string>{"a"});
  }
}

TEST_CASE("CMUdict annotations never become English phones") {
  using namespace seam;
  const std::pair<std::u32string, std::string> examples[]{
      {U"hiv", "ey1 ch ay1 v iy1"}, {U"gdp", "g iy1 d iy1 p iy1"},
      {U"d'artagnan", "d ah0 r t ae1 ng y ah0 n"}, {U"aalborg", "ao1 l b ao0 r g"}};
  for (const auto& [word, phones] : examples) {
    Fixture english; english.add(0, word);
    const auto resolved = phonemizer::resolveEnglishPronunciation(*english.project.findRegion(english.region));
    CHECK(resolved);
    if (!resolved) continue;
    CHECK(resolved.value().pronunciation.warnings.empty());
    CHECK(symbols(resolved.value().pronunciation.tokens) == phones);
  }
}

TEST_CASE("English resource manifest binds dictionary exceptions vocabulary and resolver identity") {
  using namespace seam;
  const auto root = sourceRoot();
  const auto manifest = readJson(root / "assets/pronunciation/en-us/seam-en-us.resource.json");
  const auto intake = readJson(root / "third_party/manifest.yml");
  const auto dictionary = core::sha256File(root / "assets/pronunciation/en-us/cmudict.dict");
  const auto exceptions = core::sha256File(root / "assets/pronunciation/en-us/seam-en-exceptions.tsv");
  CHECK(dictionary); CHECK(exceptions);
  if (!dictionary || !exceptions || !manifest.isObject()) return;
  const auto* lexicon = manifest.find("lexicon");
  const auto* table = manifest.find("exceptions");
  const auto* vocabulary = manifest.find("vocabulary");
  const auto* rules = manifest.find("rules");
  const auto* review = manifest.find("review");
  CHECK(lexicon && table && vocabulary && rules && review);
  if (!lexicon || !table || !vocabulary || !rules || !review) return;

  CHECK(phonemizer::englishLexiconSha256() == dictionary.value());
  CHECK(field(*lexicon, "sha256") == dictionary.value());
  bool intakeFound = false;
  if (const auto* dependencies = intake.isObject() ? intake.find("distributedDependencies") : nullptr) {
    for (const auto& entry : dependencies->asArray()) {
      if (field(entry, "name") != "CMU Pronouncing Dictionary") continue;
      intakeFound = true;
      CHECK(field(entry, "resourceSha256") == dictionary.value());
      CHECK(field(entry, "revision") == field(*lexicon, "revision"));
      CHECK(field(entry, "sourceSha256") == field(*lexicon, "archiveSha256"));
      CHECK(field(entry, "license") == field(*lexicon, "license"));
    }
  }
  CHECK(intakeFound);

  CHECK(phonemizer::englishExceptionsSha256() == exceptions.value());
  CHECK(field(*table, "sha256") == exceptions.value());
  std::ifstream tableFile{root / "assets/pronunciation/en-us/seam-en-exceptions.tsv", std::ios::binary};
  std::int64_t entries = 0;
  for (std::string line; std::getline(tableFile, line);)
    if (!line.empty() && line.front() != '#' && line.find('\t') != std::string::npos) ++entries;
  CHECK(table->find("entries") != nullptr && table->find("entries")->asInt64() == entries);
  CHECK(field(*table, "review") == "engineering entries pending native-speaker review");

  CHECK(field(*vocabulary, "id") == phonemizer::kEnglishVocabularyId);
  CHECK(field(*vocabulary, "sha256") == phonemizer::englishVocabularySha256());
  const auto listed = vocabulary->find("symbols");
  const auto frozen = phonemizer::englishVocabulary();
  CHECK(frozen.size() == 93U);
  CHECK(listed != nullptr && listed->asArray().size() == frozen.size());
  if (listed != nullptr && listed->asArray().size() == frozen.size())
    for (std::size_t index = 0U; index < frozen.size(); ++index)
      CHECK(listed->asArray()[index].asString() == frozen[index]);

  CHECK(field(*rules, "normalization") == phonemizer::kEnglishNormalizationRule);
  CHECK(field(*rules, "syllabification") == phonemizer::kEnglishSyllabificationRule);
  CHECK(field(*rules, "noteDistribution") == phonemizer::kEnglishNoteDistributionRule);
  CHECK(field(*rules, "derivation") == phonemizer::kEnglishDerivationRule);
  CHECK(field(*rules, "spelling") == phonemizer::kEnglishSpellingRule);
  CHECK(field(*rules, "exceptionOverlay") == phonemizer::kEnglishExceptionOverlayRule);
  CHECK(field(*review, "nativeSpeakerReview") == "not-performed");

  Fixture english; english.add(0, U"hello");
  const auto resolved = phonemizer::resolveEnglishPronunciation(*english.project.findRegion(english.region));
  CHECK(resolved);
  if (resolved) {
    CHECK(field(manifest, "resolverId") == resolved.value().identity.resolverId);
    CHECK(field(manifest, "resolverVersion") == resolved.value().identity.resolverVersion);
    CHECK(resolved.value().identity.resourceHash == phonemizer::EnglishPronunciationResource::builtin().resourceHash());
    CHECK(resolved.value().identity.resourceHash.size() == 64U);
  }
}

TEST_CASE("English exception overlays are validated bounded and take precedence") {
  using namespace seam;
  const auto& builtin = phonemizer::EnglishPronunciationResource::builtin();
  CHECK(builtin.lexicalReading("ba")->source == phonemizer::EnglishReadingSource::Exception);
  CHECK(builtin.lexicalReading("hello")->source == phonemizer::EnglishReadingSource::Dictionary);
  for (const auto* table : {"hello hh ah0 l ow1", "Hello\thh ah0 l ow1", "hello\ta i u", "hello\thh l",
           "hello\thh ah0 pau l ow1", "hello\thh ah0 l ow1\nhello\thh eh0 l ow1", "\thh ah0",
           "hel lo\thh ah0", "hello\thh ah0 . . l ow1"})
    CHECK(!builtin.withExceptions(table));
  std::string many;
  for (std::size_t index = 0U; index < 4097U; ++index) {
    std::string word{"q"};
    for (auto value = index; ; value /= 26U) {
      word += static_cast<char>('a' + static_cast<char>(value % 26U));
      if (value < 26U) break;
    }
    many += word + "\taa1\n";
  }
  CHECK(!builtin.withExceptions(many));

  const auto overlay = builtin.withExceptions("# reviewed later\r\n\r\nhello\thh ah0 . l ow1\r\n");
  CHECK(overlay);
  if (!overlay) return;
  const auto reading = overlay.value().lexicalReading("hello");
  CHECK(reading && reading->source == phonemizer::EnglishReadingSource::Exception);
  CHECK(reading && reading->reading.syllableBreaks == std::vector<std::size_t>{2U});
  CHECK(overlay.value().lexicalReading("ba")->source == phonemizer::EnglishReadingSource::Exception);
  Fixture english; english.add(0, U"hello");
  const auto resolved = phonemizer::resolveEnglishPronunciation(*english.project.findRegion(english.region), overlay.value());
  CHECK(resolved);
  if (resolved) {
    CHECK(resolved.value().pronunciation.warnings.empty());
    CHECK(roles(resolved.value().pronunciation.tokens) == "O N O N");
    CHECK(resolved.value().identity.resourceHash == overlay.value().resourceHash());
  }
}

TEST_CASE("English resource changes reconcile locks and invalidate dependent requests") {
  using namespace seam;
  const auto& builtin = phonemizer::EnglishPronunciationResource::builtin();
  const std::string revision = "# revised entry\nhello\thh eh0 l ow1\n";
  const auto revised = builtin.withExceptions(revision); CHECK(revised);
  if (!revised) return;
  CHECK(revised.value().resourceHash() != builtin.resourceHash());
  CHECK(revised.value().resourceHash().size() == 64U);
  CHECK(builtin.withExceptions(revision).value().resourceHash() == revised.value().resourceHash());

  Fixture english; english.add(0, U"hello"); english.add(960, U"world");
  auto* region = english.project.findRegion(english.region);
  const auto base = phonemizer::resolveEnglishPronunciation(*region, builtin); CHECK(base);
  const auto next = phonemizer::resolveEnglishPronunciation(*region, revised.value()); CHECK(next);
  if (!base || !next) return;
  CHECK(phonemizer::resolveEnglishPronunciation(*region).value().identity == base.value().identity);
  CHECK(next.value().identity.resourceHash != base.value().identity.resourceHash);
  CHECK(next.value().identity.sequenceHash != base.value().identity.sequenceHash);
  CHECK(symbols(next.value().pronunciation.tokensForNote(english.notes[0])) == "hh eh0 l ow1");
  const auto oldWorld = base.value().pronunciation.tokensForNote(english.notes[1]);
  const auto newWorld = next.value().pronunciation.tokensForNote(english.notes[1]);
  CHECK(symbols(oldWorld) == symbols(newWorld));
  CHECK(oldWorld.front().contextId != newWorld.front().contextId);  // Contexts bind the resource.

  const domain::PhonemeKey consonant{english.notes[0], 2U}, vowel{english.notes[0], 1U}, onset{english.notes[1], 0U};
  region->phonemeOverrides = {
      {.key = consonant, .timing = {.startOffset = 1000}, .locked = true,
       .sourceContextId = phonemizer::phonemeEditContextId(base.value(), consonant)},
      {.key = vowel, .symbol = "aa1", .locked = true,
       .sourceContextId = phonemizer::phonemeEditContextId(base.value(), vowel)},
      {.key = onset, .locked = true, .sourceContextId = phonemizer::phonemeEditContextId(base.value(), onset)}};
  const auto saved = region->phonemeOverrides;
  const auto applied = phonemizer::resolveEnglishPronunciation(*region, builtin); CHECK(applied);
  if (!applied) return;
  CHECK(applied.value().pronunciation.warnings.empty());
  CHECK(applied.value().pronunciation.tokens[2].locked);
  CHECK(applied.value().pronunciation.tokens[1].symbol == "aa1");

  const auto stale = phonemizer::resolveEnglishPronunciation(*region, revised.value()); CHECK(stale);
  if (!stale) return;
  CHECK(std::none_of(stale.value().pronunciation.tokens.begin(), stale.value().pronunciation.tokens.end(),
      [](const auto& token) { return token.locked; }));
  CHECK(countWarnings(stale.value().pronunciation, phonemizer::WarningCode::OrphanOverride) == 3U);
  CHECK(region->phonemeOverrides == saved);

  const auto proposal = phonemizer::reconcileEnglishResourceChange(*region, builtin, revised.value());
  CHECK(proposal);
  if (!proposal) return;
  CHECK(region->phonemeOverrides == saved);  // A proposal, not a mutation.
  using Outcome = phonemizer::EnglishEditReconciliation;
  CHECK(proposal.value().outcomes == (std::vector<Outcome>{Outcome::Rebound, Outcome::Unresolved, Outcome::Rebound}));
  CHECK(proposal.value().overrides[0].key == consonant);
  CHECK(proposal.value().overrides[0].sourceContextId != saved[0].sourceContextId);
  CHECK(proposal.value().overrides[1].unresolved);
  CHECK(proposal.value().overrides[1].sourceContextId == saved[1].sourceContextId);
  region->phonemeOverrides = proposal.value().overrides;
  const auto reconciled = phonemizer::resolveEnglishPronunciation(*region, revised.value()); CHECK(reconciled);
  if (reconciled) {
    const auto& tokens = reconciled.value().pronunciation.tokens;
    CHECK(tokens[2].locked);
    CHECK(tokens[2].timing.startOffset == std::optional<time::Microseconds>{1000});
    CHECK(tokens[1].symbol == "eh0");
    CHECK(!tokens[1].locked);
    CHECK(tokens[4].locked);
    CHECK(countWarnings(reconciled.value().pronunciation, phonemizer::WarningCode::OrphanOverride) == 1U);
  }
  region->phonemeOverrides = saved;  // Undo restores the prior intent exactly.
  CHECK(phonemizer::resolveEnglishPronunciation(*region, builtin).value().identity == applied.value().identity);

  // A performance request captured under the previous resource is refused.
  Fixture performance; performance.add(0, U"hello"); performance.add(960, U"world");
  const auto* performanceRegion = performance.project.findRegion(performance.region);
  const auto previous = phonemizer::resolveEnglishPronunciation(*performanceRegion, builtin);
  const auto current = phonemizer::resolveEnglishPronunciation(*performanceRegion, revised.value());
  CHECK(previous); CHECK(current);
  if (!previous || !current) return;
  const synthesis::AutomaticPerformanceRequest request{
      .takeId = "take-english-resource",
      .regionId = performance.region,
      .capturedRevision = performanceRegion->performance.revision,
      .resource = {domain::SingerResourceKind::Neural, "fixture-neural", "1.0.0", std::string(64U, 'a')},
      .pronunciation = previous.value().identity,
      .generatorId = "fixture-generator",
      .generatorVersion = "1.0.0",
      .seed = 7U,
      .range = {time::Tick{0}, time::Tick{1920}}};
  CHECK(synthesis::generateAutomaticPerformance(performance.project, *performanceRegion, previous.value(), request));
  const auto refused = synthesis::generateAutomaticPerformance(performance.project, *performanceRegion, current.value(), request);
  CHECK(!refused);
  if (!refused) CHECK(refused.error().code == core::ErrorCode::Conflict);
}

TEST_CASE("English vocabulary coverage lists missing bank phones without substitution") {
  using namespace seam;
  Fixture english; english.add(0, U"hello"); english.add(960, U"world"); english.add(1920, U",");
  const auto resolved = phonemizer::resolveEnglishPronunciation(*english.project.findRegion(english.region));
  CHECK(resolved);
  if (!resolved) return;
  const auto& tokens = resolved.value().pronunciation.tokens;
  std::vector<std::string> all;
  for (const auto symbol : phonemizer::englishVocabulary()) all.emplace_back(symbol);
  const auto complete = phonemizer::checkEnglishVocabularyCoverage(tokens, {"fixture-complete", all});
  CHECK(complete && complete.value().complete());
  CHECK(complete && complete.value().requiredSymbols == 7U);  // hh ah0 l ow1 w er1 d; pause excluded.

  auto withoutOw = all;
  std::erase(withoutOw, "ow1");
  const auto missing = phonemizer::checkEnglishVocabularyCoverage(tokens, {"fixture-missing", withoutOw});
  CHECK(missing && missing.value().missing.size() == 1U);
  if (missing && missing.value().missing.size() == 1U) {
    const auto& phone = missing.value().missing.front();
    CHECK(phone.symbol == "ow1"); CHECK(phone.requiredSymbol == "ow1"); CHECK(phone.occurrences == 1U);
    CHECK(phone.keys == (std::vector<domain::PhonemeKey>{{english.notes[0], 3U}}));
  }

  std::vector<std::string> bare;
  for (const auto symbol : phonemizer::englishVocabulary())
    if (symbol.back() != '0' && symbol.back() != '1' && symbol.back() != '2') bare.emplace_back(symbol);
  const auto exact = phonemizer::checkEnglishVocabularyCoverage(tokens, {"fixture-bare", bare});
  CHECK(exact && exact.value().missing.size() == 3U);
  if (exact && exact.value().missing.size() == 3U) {
    CHECK(exact.value().missing[0].symbol == "ah0");
    CHECK(exact.value().missing[1].symbol == "ow1");
    CHECK(exact.value().missing[2].symbol == "er1");
  }
  const auto folded = phonemizer::checkEnglishVocabularyCoverage(tokens,
      {"fixture-bare", bare, phonemizer::EnglishStressCoverage::FoldLexicalStress});
  CHECK(folded && folded.value().complete());

  // A Japanese-style inventory is reported missing, never used as a stand-in.
  const std::vector<std::string> japanese{"a", "i", "u", "e", "o", "k", "s", "t", "n", "h", "m", "y", "r", "w", "g", "z", "d", "b", "p", "N"};
  const auto foreign = phonemizer::checkEnglishVocabularyCoverage(tokens,
      {"fixture-japanese", japanese, phonemizer::EnglishStressCoverage::FoldLexicalStress});
  CHECK(foreign && !foreign.value().complete());
  if (foreign) {
    const auto& list = foreign.value().missing;
    CHECK(std::any_of(list.begin(), list.end(), [](const auto& phone) { return phone.symbol == "l" && phone.requiredSymbol == "l"; }));
    CHECK(std::any_of(list.begin(), list.end(), [](const auto& phone) { return phone.symbol == "ah0" && phone.requiredSymbol == "ah"; }));
    CHECK(std::none_of(list.begin(), list.end(), [](const auto& phone) { return phone.symbol == "w" || phone.symbol == "d"; }));
  }

  std::vector<domain::PhonemeToken> repeated;
  for (std::uint16_t index = 0U; index < 40U; ++index)
    repeated.push_back({.key = {english.notes[0], index}, .symbol = "aa1"});
  const auto capped = phonemizer::checkEnglishVocabularyCoverage(repeated, {"fixture-empty", {}});
  CHECK(capped && capped.value().missing.size() == 1U);
  if (capped && capped.value().missing.size() == 1U) {
    CHECK(capped.value().missing.front().occurrences == 40U);
    CHECK(capped.value().missing.front().keys.size() == phonemizer::kMaximumEnglishCoverageKeysPerPhone);
  }
  const std::vector<domain::PhonemeToken> japaneseToken{{.key = {english.notes[0], 0U}, .symbol = "a"}};
  const auto invalid = phonemizer::checkEnglishVocabularyCoverage(japaneseToken, {"fixture-complete", all});
  CHECK(!invalid && invalid.error().code == core::ErrorCode::InvalidArgument);
  CHECK(!phonemizer::checkEnglishVocabularyCoverage(tokens, {"fixture-duplicate", {"hh", "hh"}}));
}
