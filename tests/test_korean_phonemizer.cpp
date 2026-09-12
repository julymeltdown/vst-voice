#include "test_framework.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/phonemizer/korean_phonemizer.hpp"
#include "seam/phonemizer/language_resolver.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace {
struct Fixture final {
  seam::application::ProjectFactory factory{902000U};
  seam::domain::Project project{factory.createProject("Korean phonemes")};
  seam::domain::TrackId track{factory.addVocalTrack(project, "Korean")};
  seam::domain::RegionId region{factory.addRegion(project, track, "Phrase", seam::time::Tick{0}, seam::time::Tick{3840})};
  std::vector<seam::domain::NoteId> notes;

  void add(std::int64_t tick, const char32_t* text) {
    auto [lyric, note] = factory.makeNote(seam::time::Tick{tick}, seam::time::Tick{960}, 60U,
                                          std::u32string{text}, seam::domain::Language::Korean);
    notes.push_back(note.id);
    auto* value = project.findRegion(region);
    value->lyrics.push_back(std::move(lyric));
    value->notes.push_back(std::move(note));
  }
};
}

TEST_CASE("Korean Hangul phonemizer decomposes syllables, coda liaison and continuation") {
  using namespace seam;
  Fixture fixture;
  fixture.add(0, U"한");
  fixture.add(960, U"먹어");
  fixture.add(1920, U"-");
  auto* region = fixture.project.findRegion(fixture.region);
  const auto result = phonemizer::KoreanHangulPhonemizer{}.phonemize(*region);
  CHECK(result.warnings.empty());
  CHECK(result.tokens.size() == 8U);
  CHECK(result.tokens[0].symbol == "h");
  CHECK(result.tokens[1].symbol == "a");
  CHECK(result.tokens[2].symbol == "n");
  CHECK(result.tokens[3].symbol == "m");
  CHECK(result.tokens[4].symbol == "eo");
  CHECK(result.tokens[5].symbol == "k");
  CHECK(result.tokens[6].symbol == "eo");
  CHECK(result.tokens[7].symbol == "eo");
  CHECK(phonemizer::parseKoreanPhoneHint("s a r a ng").value() ==
      (std::vector<std::string>{"s", "a", "r", "a", "ng"}));
  CHECK(!phonemizer::parseKoreanPhoneHint("s unknown"));
}

TEST_CASE("Korean resolver supports Hangul and romanized bootstrap words with bounded diagnostics") {
  using namespace seam;
  Fixture fixture;
  fixture.add(0, U"안녕");
  fixture.add(960, U"sarang");
  fixture.add(1920, U"hello");
  auto* region = fixture.project.findRegion(fixture.region);
  region->findNote(fixture.notes[1])->phoneticHint = "s a r a ng";
  const auto resolved = phonemizer::resolveKoreanPronunciation(*region);
  CHECK(resolved);
  CHECK(resolved.value().identity.language == domain::Language::Korean);
  CHECK(resolved.value().identity.resolverId == "seam-builtin-ko");
  CHECK(resolved.value().identity.resourceHash.size() == 64U);
  CHECK(std::all_of(resolved.value().pronunciation.tokens.begin(),
      resolved.value().pronunciation.tokens.end(), [](const auto& token) {
        return token.contextId.size() == 64U && token.lyricOwner.valid();
      }));
  CHECK(std::any_of(resolved.value().pronunciation.warnings.begin(),
      resolved.value().pronunciation.warnings.end(), [](const auto& warning) {
        return warning.code == phonemizer::WarningCode::UnsupportedCharacter;
      }));
}

TEST_CASE("Korean complete jongseong oracle preserves all seven final sounds and coda roles") {
  using namespace seam;
  // Literal syllables in Unicode jongseong order, with NIKL word-final
  // representative sounds. This covers every table slot, not three examples.
  constexpr std::u32string_view syllables = U"가각갂갃간갅갆갇갈갉갊갋갌갍갎갏감갑값갓갔강갖갗갘같갚갛";
  constexpr std::array<std::string_view, 28U> finals{
      "", "k", "k", "k", "n", "n", "n", "t", "l", "k", "m", "l", "l", "l",
      "p", "l", "m", "p", "p", "t", "t", "ng", "t", "t", "k", "t", "p", "t"};
  CHECK(syllables.size() == finals.size());
  for (std::size_t index = 0U; index < syllables.size(); ++index) {
    Fixture fixture;
    const char32_t text[]{syllables[index], U'\0'};
    fixture.add(0, text);
    const auto resolved = phonemizer::resolveKoreanPronunciation(*fixture.project.findRegion(fixture.region));
    CHECK(resolved);
    const auto& tokens = resolved.value().pronunciation.tokens;
    CHECK(tokens.size() == (index == 0U ? 2U : 3U));
    CHECK(tokens[0].symbol == "k");
    CHECK(tokens[0].role == domain::PhonemeRole::Onset);
    CHECK(tokens[1].symbol == "a");
    CHECK(tokens[1].role == domain::PhonemeRole::Nucleus);
    if (index != 0U) {
      CHECK(tokens[2].symbol == finals[index]);
      CHECK(tokens[2].role == domain::PhonemeRole::Coda);
      CHECK(tokens[2].voiced == (finals[index] == "n" || finals[index] == "m" || finals[index] == "ng" || finals[index] == "l"));
    }
  }
}

TEST_CASE("Korean initial and medial inventories preserve phonemic contrasts") {
  using namespace seam;
  constexpr std::u32string_view initials = U"가까나다따라마바빠사싸아자짜차카타파하";
  constexpr std::array<std::string_view, 19U> onset{
      "k", "kk", "n", "t", "tt", "r", "m", "p", "pp", "s", "ss", "", "ch", "cch", "chh", "kh", "th", "ph", "h"};
  for (std::size_t index = 0U; index < initials.size(); ++index) {
    Fixture fixture;
    const char32_t text[]{initials[index], U'\0'};
    fixture.add(0, text);
    const auto result = phonemizer::KoreanHangulPhonemizer{}.phonemize(*fixture.project.findRegion(fixture.region));
    CHECK(result.tokens.size() == (onset[index].empty() ? 1U : 2U));
    CHECK(result.tokens.back().symbol == "a");
    if (!onset[index].empty()) {
      CHECK(result.tokens.front().symbol == onset[index]);
      CHECK(result.tokens.front().role == domain::PhonemeRole::Onset);
    }
  }
  constexpr std::u32string_view medials = U"아애야얘어에여예오와왜외요우워웨위유으의이";
  constexpr std::array<std::string_view, 21U> vowels{
      "a", "ae", "y a", "y ae", "eo", "e", "y eo", "y e", "o", "w a", "w ae",
      "w e", "y o", "u", "w eo", "w e", "w i", "y u", "eu", "eu i", "i"};
  for (std::size_t index = 0U; index < medials.size(); ++index) {
    Fixture fixture;
    const char32_t text[]{medials[index], U'\0'};
    fixture.add(0, text);
    const auto result = phonemizer::KoreanHangulPhonemizer{}.phonemize(*fixture.project.findRegion(fixture.region));
    const auto expected = phonemizer::parseKoreanPhoneHint(vowels[index]);
    CHECK(expected);
    CHECK(result.tokens.size() == expected.value().size());
    for (std::size_t phone = 0U; phone < result.tokens.size(); ++phone) {
      CHECK(result.tokens[phone].symbol == expected.value()[phone]);
      CHECK(result.tokens[phone].role == (expected.value()[phone] == "y" || expected.value()[phone] == "w"
          ? domain::PhonemeRole::Onset : domain::PhonemeRole::Nucleus));
      CHECK(result.tokens[phone].voiced);
    }
  }
  CHECK(phonemizer::parseKoreanPhoneHint("eo o eu u ch cch chh"));
  CHECK(phonemizer::isVowelSymbol("eo"));
  CHECK(phonemizer::isVowelSymbol("eu"));
  CHECK(!phonemizer::isVoicedSymbol("chh"));
}

TEST_CASE("Korean boundary rules retain original finals for liaison and contextual assimilation") {
  using namespace seam;
  const std::pair<const char32_t*, std::string_view> cases[]{
      {U"강", "k a ng"}, {U"밤", "p a m"}, {U"밥", "p a p"},
      {U"먹어", "m eo k eo"}, {U"옷이", "o s i"}, {U"꽃이", "kk o chh i"},
      {U"값이", "k a p ss i"}, {U"앉아", "a n ch a"}, {U"읽어", "i l k eo"},
      {U"많아", "m a n a"}, {U"싫어", "s i r eo"}, {U"강아", "k a ng a"},
      {U"국물", "k u ng m u l"}, {U"신라", "s i l l a"}, {U"밥도", "p a p tt o"},
      {U"좋다", "ch o th a"}, {U"밭이", "p a chh i"}, {U"학교", "h a k kk y o"},
      {U"읽고", "i l kk o"}};
  for (const auto& [text, expectedText] : cases) {
    Fixture fixture;
    fixture.add(0, text);
    const auto result = phonemizer::KoreanHangulPhonemizer{}.phonemize(*fixture.project.findRegion(fixture.region));
    const auto expected = phonemizer::parseKoreanPhoneHint(expectedText);
    CHECK(expected);
    std::string actual;
    for (const auto& token : result.tokens) {
      if (!actual.empty()) actual += ' ';
      actual += token.symbol;
    }
    if (actual != expectedText) throw seam::test::Failure{
        "Korean boundary phones: expected '" + std::string{expectedText} + "', got '" + actual + "'"};
  }
}

TEST_CASE("Korean cross-note liaison preserves ownership and stops at rests or explicit hints") {
  using namespace seam;
  Fixture fixture;
  fixture.add(0, U"먹");
  fixture.add(960, U"어");
  auto* region = fixture.project.findRegion(fixture.region);
  const auto original = *region;
  const auto joined = phonemizer::resolveKoreanPronunciation(*region);
  CHECK(joined);
  const auto first = joined.value().pronunciation.tokensForNote(fixture.notes.front());
  const auto second = joined.value().pronunciation.tokensForNote(fixture.notes.back());
  CHECK(first.size() == 2U);
  CHECK(second.size() == 2U);
  CHECK(first.back().symbol == "eo");
  CHECK(second.front().symbol == "k");
  CHECK(second.front().role == domain::PhonemeRole::Onset);
  CHECK(*region == original);
  region->notes.back().startTick = time::Tick{1920};
  const auto separated = phonemizer::resolveKoreanPronunciation(*region);
  CHECK(separated);
  CHECK(separated.value().pronunciation.tokensForNote(fixture.notes.front()).back().role == domain::PhonemeRole::Coda);
  CHECK(separated.value().pronunciation.tokensForNote(fixture.notes.back()).size() == 1U);
  *region = original;
  region->notes.back().phoneticHint = "eo";
  const auto hinted = phonemizer::resolveKoreanPronunciation(*region);
  CHECK(hinted);
  CHECK(hinted.value().pronunciation.tokensForNote(fixture.notes.front()).back().role == domain::PhonemeRole::Coda);
  CHECK(hinted.value().pronunciation.tokensForNote(fixture.notes.back()).size() == 1U);
}

TEST_CASE("Korean compatibility jamo remain in source order with an explicit diagnostic") {
  using namespace seam;
  Fixture fixture;
  fixture.add(0, U"아ㅁ오");
  const auto result = phonemizer::KoreanHangulPhonemizer{}.phonemize(*fixture.project.findRegion(fixture.region));
  CHECK(result.tokens.size() == 3U);
  CHECK(result.tokens[0].symbol == "a");
  CHECK(result.tokens[1].symbol == "m");
  CHECK(result.tokens[2].symbol == "o");
  CHECK(result.warnings.size() == 1U);
  CHECK(result.warnings.front().code == phonemizer::WarningCode::EstimatedPronunciation);
}

TEST_CASE("Korean explicit hints and romanized words preserve coda and onset ownership") {
  using namespace seam;
  Fixture fixture;
  fixture.add(0, U"안녕");
  auto* region = fixture.project.findRegion(fixture.region);
  const auto hangul = phonemizer::KoreanHangulPhonemizer{}.phonemize(*region);
  region->lyrics.front().surface = U"annyeong";
  const auto romanized = phonemizer::KoreanHangulPhonemizer{}.phonemize(*region);
  CHECK(romanized.tokens == hangul.tokens);
  region->notes.front().phoneticHint = "a n n y eo ng";
  const auto hinted = phonemizer::KoreanHangulPhonemizer{}.phonemize(*region);
  CHECK(hinted.tokens == hangul.tokens);
  CHECK(hinted.tokens[1].role == domain::PhonemeRole::Coda);
  CHECK(hinted.tokens[2].role == domain::PhonemeRole::Onset);
  CHECK(hinted.tokens.back().role == domain::PhonemeRole::Coda);
}
