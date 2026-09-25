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

TEST_CASE("Korean rule-29 nasal insertion is lexical and resource-addressed") {
  using namespace seam;
  const std::pair<const char32_t*, std::string_view> cases[]{
      {U"꽃잎", "kk o n n i p"}, {U"깻잎", "kk ae n n i p"},
      {U"막일", "m a ng n i l"}, {U"솜이불", "s o m n i p u l"},
      {U"홑이불", "h o n n i p u l"}, {U"한여름", "h a n n y eo r eu m"},
      {U"나뭇잎", "n a m u n n i p"}, {U"논일", "n o n n i l"},
      {U"앞이마", "a m n i m a"},
  };
  for (const auto& [text, expectedText] : cases) {
    Fixture fixture;
    fixture.add(0, text);
    const auto resolved = phonemizer::resolveKoreanPronunciation(
        *fixture.project.findRegion(fixture.region));
    CHECK(resolved);
    CHECK(resolved.value().pronunciation.warnings.empty());
    CHECK(resolved.value().identity.resourceHash.size() == 64U);
    CHECK(resolved.value().identity.sequenceHash.size() == 64U);
    std::string actual;
    for (const auto& token : resolved.value().pronunciation.tokens) {
      if (!actual.empty()) actual += ' ';
      actual += token.symbol;
    }
    if (actual != expectedText) throw seam::test::Failure{
        "Korean Rule 29 phones for '" + std::string{expectedText} +
        "': got '" + actual + "'"};
  }

  // This is ordinary particle liaison, not a lexical compound: do not inject ㄴ.
  Fixture ordinary;
  ordinary.add(0, U"꽃이");
  const auto resolved = phonemizer::resolveKoreanPronunciation(
      *ordinary.project.findRegion(ordinary.region));
  CHECK(resolved);
  std::string actual;
  for (const auto& token : resolved.value().pronunciation.tokens) {
    if (!actual.empty()) actual += ' ';
    actual += token.symbol;
  }
  CHECK(actual == "kk o chh i");
}

TEST_CASE("Korean resolver composes canonically decomposed modern Hangul") {
  using namespace seam;
  Fixture precomposed;
  precomposed.add(0, U"꽃잎");
  Fixture decomposed;
  decomposed.add(0, U"\u1101\u1169\u11be\u110b\u1175\u11c1");

  const auto resolve = [](Fixture& fixture) {
    return phonemizer::resolveKoreanPronunciation(
        *fixture.project.findRegion(fixture.region));
  };
  const auto canonical = resolve(precomposed);
  const auto decomposedResult = resolve(decomposed);
  CHECK(canonical);
  CHECK(decomposedResult);
  CHECK(canonical.value().identity.resourceHash ==
      decomposedResult.value().identity.resourceHash);
  CHECK(canonical.value().identity.sequenceHash ==
      decomposedResult.value().identity.sequenceHash);

  std::string actual;
  for (const auto& token : decomposedResult.value().pronunciation.tokens) {
    if (!actual.empty()) actual += ' ';
    actual += token.symbol;
  }
  CHECK(actual == "kk o n n i p");

  Fixture decomposedCompoundVowel;
  decomposedCompoundVowel.add(0, U"\u110b\u116b");
  const auto compoundVowel = resolve(decomposedCompoundVowel);
  CHECK(compoundVowel);
  std::string compoundVowelPhones;
  for (const auto& token : compoundVowel.value().pronunciation.tokens) {
    if (!compoundVowelPhones.empty()) compoundVowelPhones += ' ';
    compoundVowelPhones += token.symbol;
  }
  CHECK(compoundVowelPhones == "w ae");
}

TEST_CASE("Korean ㄼ and ㄾ rules preserve stem-specific finals and tensing") {
  using namespace seam;
  const std::pair<const char32_t*, std::string_view> cases[]{
      {U"밟고", "p a p kk o"}, {U"밟는", "p a m n eu n"},
      {U"밟아", "p a l p a"}, {U"넓고", "n eo l kk o"},
      {U"넓다", "n eo l tt a"}, {U"넓지", "n eo l cch i"},
      {U"넓적하다", "n eo p cch eo kh a t a"},
      {U"넓죽하다", "n eo p cch u kh a t a"},
      {U"핥고", "h a l kk o"}, {U"핥다", "h a l tt a"},
      {U"핥소", "h a l ss o"}, {U"핥지", "h a l cch i"},
      {U"핥아", "h a l th a"},
  };
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
        "Korean ㄼ phones: expected '" + std::string{expectedText} + "', got '" + actual + "'"};
  }

  Fixture split;
  split.add(0, U"밟");
  split.add(960, U"고");
  auto* region = split.project.findRegion(split.region);
  const auto original = *region;
  const auto resolved = phonemizer::resolveKoreanPronunciation(*region);
  CHECK(resolved);
  const auto first = resolved.value().pronunciation.tokensForNote(split.notes.front());
  const auto second = resolved.value().pronunciation.tokensForNote(split.notes.back());
  CHECK(first.size() == 3U);
  CHECK(first[2].symbol == "p");
  CHECK(first[2].role == domain::PhonemeRole::Coda);
  CHECK(second.size() == 2U);
  CHECK(second[0].symbol == "kk");
  CHECK(second[0].role == domain::PhonemeRole::Onset);
  CHECK(*region == original);

  Fixture splitTense;
  splitTense.add(0, U"핥");
  splitTense.add(960, U"고");
  region = splitTense.project.findRegion(splitTense.region);
  const auto splitTenseResolved = phonemizer::resolveKoreanPronunciation(*region);
  CHECK(splitTenseResolved);
  const auto splitTenseCoda = splitTenseResolved.value().pronunciation.tokensForNote(
      splitTense.notes.front());
  const auto splitTenseOnset = splitTenseResolved.value().pronunciation.tokensForNote(
      splitTense.notes.back());
  CHECK(splitTenseCoda.size() == 3U);
  CHECK(splitTenseCoda.back().symbol == "l");
  CHECK(splitTenseOnset.size() == 2U);
  CHECK(splitTenseOnset.front().symbol == "kk");
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

TEST_CASE("selected OpenUtau Korean boundary examples retain exact phones across note splits") {
  using namespace seam;
  using Role = domain::PhonemeRole;
  // Source-comment examples in OpenUtau.Core/KoreanPhonemizerUtil.cs Variate,
  // commit 83e02c7e4a4d9ea5fca72806b2aa27c5382be015 (MIT; see the reference
  // notice in libs/seam-phonemizer). These four transformations have explicit
  // semantic mappings: ㅌ -> th, ㅋ -> kh, ㅊ -> chh, ㄲ -> kk, ㅡ -> eu,
  // and a coda/following lateral ㄹ -> l. They are not oto alias comparisons
  // or a claim of parity with every rule in the upstream Korean utility.
  struct Example final {
    const char32_t* word;
    const char32_t* first;
    const char32_t* second;
    std::size_t firstPhoneCount;
    std::vector<std::string> phones;
    std::vector<Role> roles;
  };
  const Example examples[]{
      {U"많다", U"많", U"다", 3U, {"m", "a", "n", "th", "a"},
          {Role::Onset, Role::Nucleus, Role::Coda, Role::Onset, Role::Nucleus}},
      {U"끓다", U"끓", U"다", 3U, {"kk", "eu", "l", "th", "a"},
          {Role::Onset, Role::Nucleus, Role::Coda, Role::Onset, Role::Nucleus}},
      {U"축하", U"축", U"하", 2U, {"chh", "u", "kh", "a"},
          {Role::Onset, Role::Nucleus, Role::Onset, Role::Nucleus}},
      {U"칼날", U"칼", U"날", 3U, {"kh", "a", "l", "l", "a", "l"},
          {Role::Onset, Role::Nucleus, Role::Coda, Role::Onset, Role::Nucleus, Role::Coda}}};
  for (const auto& example : examples) {
    for (const bool split : {false, true}) {
      Fixture fixture;
      fixture.add(0, split ? example.first : example.word);
      if (split) fixture.add(960, example.second);
      const auto* region = fixture.project.findRegion(fixture.region);
      const auto original = *region;
      const auto resolved = phonemizer::resolveKoreanPronunciation(*region); CHECK(resolved);
      CHECK(resolved.value().pronunciation.warnings.empty());
      CHECK(*region == original);
      const auto& tokens = resolved.value().pronunciation.tokens;
      CHECK(tokens.size() == example.phones.size());
      for (std::size_t index = 0U; index < tokens.size(); ++index) {
        const bool onSecond = split && index >= example.firstPhoneCount;
        const auto noteIndex = onSecond ? 1U : 0U;
        CHECK(tokens[index].symbol == example.phones[index]);
        CHECK(tokens[index].role == example.roles[index]);
        CHECK(tokens[index].key.noteId == fixture.notes[noteIndex]);
        CHECK(tokens[index].key.ordinal == (onSecond ? index - example.firstPhoneCount : index));
        CHECK(tokens[index].lyricOwner == region->notes[noteIndex].lyricTokenId);
        CHECK(tokens[index].contextId.size() == 64U);
      }
    }
  }
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
