#include "test_framework.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"

#include <string>
#include <vector>

namespace {

struct PhonemizerFixture final {
  seam::application::ProjectFactory factory{100};
  seam::domain::Project project{factory.createProject("Phonemizer fixture")};
  seam::domain::TrackId trackId{factory.addVocalTrack(project, "Track")};
  seam::domain::RegionId regionId{
      factory.addRegion(project, trackId, "Region", seam::time::Tick{0},
                        seam::time::Tick{15360})};

  seam::domain::NoteId add(std::u32string lyric,
                           seam::time::Tick start,
                           std::uint8_t midi = 60) {
    auto [token, note] = factory.makeNote(start, seam::time::Tick{960}, midi,
                                          std::move(lyric),
                                          seam::domain::Language::Japanese);
    const auto id = note.id;
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(token));
    region->notes.push_back(std::move(note));
    region->sortNotes();
    return id;
  }
};

std::vector<std::string> symbols(
    const std::vector<seam::domain::PhonemeToken>& tokens) {
  std::vector<std::string> result;
  result.reserve(tokens.size());
  for (const auto& token : tokens) result.push_back(token.symbol);
  return result;
}

}  // namespace

// Selected lyric inputs from OpenUtau.Test/Plugins/JaVcvTest.cs, JaCvvcTest.cs
// and JaPresampTest.cs at 83e02c7e4a4d9ea5fca72806b2aa27c5382be015 (MIT;
// see libs/seam-phonemizer/OPENUTAU_REFERENCE_NOTICE.md). Their expected oto
// aliases are bank-specific, not a phoneme oracle: JaCvvcTest even maps ラ/リ/ル
// through l/ly aliases. The explicit SEAM reading below retains r and its
// contracted onsets. Hiragana り/る are additional normalization counterparts.
TEST_CASE("Japanese OpenUtau lyric examples retain explicit SEAM mora readings and ownership") {
  const std::vector<std::pair<std::u32string, std::vector<std::string>>> vectors{
      {U"あ", {"a"}}, {U"お", {"o"}}, {U"ら", {"r", "a"}},
      {U"り", {"r", "i"}}, {U"る", {"r", "u"}}, {U"が", {"g", "a"}},
      {U"にょ", {"ny", "o"}}, {U"ひょ", {"hy", "o"}}, {U"びょ", {"by", "o"}},
      {U"ぴょ", {"py", "o"}}, {U"みょ", {"my", "o"}}, {U"りょ", {"ry", "o"}},
      // A katakana lyric and a voiced-katakana lyric: OpenUtau's fixture writes these as the katakana
      // given here, and its normalization is what makes them the same mora as the hiragana above.
      {U"ラ", {"r", "a"}}, {U"リ", {"r", "i"}}, {U"ル", {"r", "u"}},
      {U"ヴ", {"v", "u"}},
      {U"か\u3099", {"g", "a"}}, {U"ウ\u3099", {"v", "u"}},
  };
  for (const auto& [kana, expected] : vectors) {
    PhonemizerFixture fixture;
    const auto note = fixture.add(kana, seam::time::Tick{0});
    const auto* region = fixture.project.findRegion(fixture.regionId);
    const auto original = *region;
    const auto result = seam::phonemizer::resolveJapanesePronunciation(*region); CHECK(result);
    CHECK(result.value().pronunciation.warnings.empty());
    const auto tokens = result.value().pronunciation.tokensForNote(note);
    CHECK(symbols(tokens) == expected);
    for (std::size_t index = 0U; index < tokens.size(); ++index) {
      CHECK(tokens[index].role == (index + 1U == tokens.size()
          ? seam::domain::PhonemeRole::Nucleus : seam::domain::PhonemeRole::Onset));
      CHECK(tokens[index].key.noteId == note);
      CHECK(tokens[index].key.ordinal == index);
      CHECK(tokens[index].lyricOwner == region->notes.front().lyricTokenId);
      CHECK(tokens[index].contextId.size() == 64U);
    }
    CHECK(*region == original);
  }
}

TEST_CASE("Japanese phonemizer handles hiragana katakana and contracted mora") {
  PhonemizerFixture fixture;
  const auto noteA = fixture.add(U"きゃ", seam::time::Tick{0});
  const auto noteB = fixture.add(U"ミ", seam::time::Tick{960});

  seam::phonemizer::JapaneseKanaPhonemizer phonemizer;
  const auto result = phonemizer.phonemize(*fixture.project.findRegion(fixture.regionId));
  CHECK(result.warnings.empty());
  CHECK(symbols(result.tokensForNote(noteA)) ==
        (std::vector<std::string>{"ky", "a"}));
  CHECK(symbols(result.tokensForNote(noteB)) ==
        (std::vector<std::string>{"m", "i"}));
  CHECK(result.tokensForNote(noteA)[0].role == seam::domain::PhonemeRole::Onset);
  CHECK(result.tokensForNote(noteA)[1].role == seam::domain::PhonemeRole::Nucleus);
}

TEST_CASE("Japanese phonemizer preserves sokuon moraic nasal and long vowel") {
  PhonemizerFixture fixture;
  const auto noteA = fixture.add(U"かっ", seam::time::Tick{0});
  const auto noteB = fixture.add(U"ん", seam::time::Tick{960});
  const auto noteC = fixture.add(U"ー", seam::time::Tick{1920});

  seam::phonemizer::JapaneseKanaPhonemizer phonemizer;
  const auto result = phonemizer.phonemize(*fixture.project.findRegion(fixture.regionId));
  CHECK(symbols(result.tokensForNote(noteA)) ==
        (std::vector<std::string>{"k", "a", "cl"}));
  CHECK(symbols(result.tokensForNote(noteB)) ==
        (std::vector<std::string>{"N"}));
  CHECK(symbols(result.tokensForNote(noteC)) ==
        (std::vector<std::string>{"a"}));
  CHECK(result.tokensForNote(noteA).back().role ==
        seam::domain::PhonemeRole::Geminate);
  CHECK(result.tokensForNote(noteB).front().role ==
        seam::domain::PhonemeRole::Coda);
}

TEST_CASE("Japanese kana normalization preserves surface and resolves width and combining voice marks") {
  const std::vector<std::u32string> forms{U"がっきゃぱヴーん", U"ガッキャパヴーン", U"ｶﾞｯｷｬﾊﾟｳﾞｰﾝ", U"か\u3099っきゃは\u309aウ\u3099ーん"};
  const std::vector<std::string> expected{"g", "a", "cl", "ky", "a", "p", "a", "v", "u", "u", "N"};
  for (const auto& form : forms) {
    PhonemizerFixture fixture; fixture.add(form, seam::time::Tick{0});
    const auto before = fixture.project;
    const auto result = seam::phonemizer::resolveJapanesePronunciation(*fixture.project.findRegion(fixture.regionId)); CHECK(result);
    CHECK(result.value().pronunciation.warnings.empty()); CHECK(symbols(result.value().pronunciation.tokens) == expected);
    CHECK(fixture.project == before);
  }
}

TEST_CASE("Japanese normalized warnings retain original scalar positions and explicit hints win") {
  PhonemizerFixture fixture; const auto note = fixture.add(U"ｶﾞ漢あ\u3099", seam::time::Tick{0});
  auto* region = fixture.project.findRegion(fixture.regionId);
  const auto result = seam::phonemizer::resolveJapanesePronunciation(*region); CHECK(result);
  CHECK(result.value().pronunciation.warnings.size() == 2U);
  CHECK(result.value().pronunciation.warnings[0].characterIndex == 2U);
  CHECK(result.value().pronunciation.warnings[1].characterIndex == 4U);
  CHECK(symbols(result.value().pronunciation.tokens) == (std::vector<std::string>{"g", "a", "pau", "a", "pau"}));
  region->findNote(note)->phoneticHint = "k a";
  const auto hinted = seam::phonemizer::resolveJapanesePronunciation(*region); CHECK(hinted);
  CHECK(hinted.value().pronunciation.warnings.empty());
  CHECK(symbols(hinted.value().pronunciation.tokens) == (std::vector<std::string>{"k", "a"}));
  CHECK(region->lyrics.front().surface == U"ｶﾞ漢あ\u3099");
}

TEST_CASE("phoneme overrides replace symbols timing and lock state") {
  PhonemizerFixture fixture;
  const auto noteId = fixture.add(U"き", seam::time::Tick{0});
  auto* region = fixture.project.findRegion(fixture.regionId);
  region->phonemeOverrides.push_back(seam::domain::PhonemeOverride{
      .key = seam::domain::PhonemeKey{noteId, 0},
      .symbol = std::string{"g"},
      .timing = seam::domain::PhonemeTiming{
          .startOffset = seam::time::Microseconds{-50000},
          .endOffset = seam::time::Microseconds{0},
      },
      .locked = true,
  });

  seam::phonemizer::JapaneseKanaPhonemizer phonemizer;
  const auto result = phonemizer.phonemize(*region);
  const auto noteTokens = result.tokensForNote(noteId);
  CHECK(noteTokens.size() == 2);
  CHECK(noteTokens[0].symbol == "g");
  CHECK(noteTokens[0].locked);
  CHECK(noteTokens[0].timing.startOffset ==
        seam::time::Microseconds{-50000});
  CHECK(noteTokens[0].timing.endOffset == seam::time::Microseconds{0});
}

TEST_CASE("unsupported Japanese lyric creates a visible warning and pause") {
  PhonemizerFixture fixture;
  const auto noteId = fixture.add(U"漢", seam::time::Tick{0});

  seam::phonemizer::JapaneseKanaPhonemizer phonemizer;
  const auto result = phonemizer.phonemize(*fixture.project.findRegion(fixture.regionId));
  CHECK(result.warnings.size() == 1);
  CHECK(result.warnings.front().code ==
        seam::phonemizer::WarningCode::UnsupportedCharacter);
  CHECK(symbols(result.tokensForNote(noteId)) ==
        (std::vector<std::string>{"pau"}));
}

TEST_CASE("an explicit phone hint gives an ordinary consonant its place in the syllable") {
  // A hint is a sequence the author wrote, so a bare symbol cannot decide onset versus coda
  // on its own: "a s" is the vowel-to-coda unit a voicebank inventory names, while "s a" and
  // "k a N" keep the meaning they always had.
  PhonemizerFixture fixture;
  const auto id = fixture.add(U"漢", seam::time::Tick{0});
  auto* region = fixture.project.findRegion(fixture.regionId);
  const auto roles = [&](std::string hint) {
    region->findNote(id)->phoneticHint = std::move(hint);
    seam::phonemizer::JapaneseKanaPhonemizer phonemizer;
    const auto result = phonemizer.phonemize(*region);
    std::vector<seam::domain::PhonemeRole> values;
    for (const auto& token : result.tokensForNote(id)) values.push_back(token.role);
    return values;
  };
  using seam::domain::PhonemeRole;
  CHECK(roles("k a") == (std::vector<PhonemeRole>{PhonemeRole::Onset, PhonemeRole::Nucleus}));
  CHECK(roles("s a") == (std::vector<PhonemeRole>{PhonemeRole::Onset, PhonemeRole::Nucleus}));
  CHECK(roles("k a N") == (std::vector<PhonemeRole>{PhonemeRole::Onset, PhonemeRole::Nucleus, PhonemeRole::Coda}));
  // A consonant directly after the vowel with no vowel after it is the coda of that syllable.
  CHECK(roles("a s") == (std::vector<PhonemeRole>{PhonemeRole::Nucleus, PhonemeRole::Coda}));
  CHECK(roles("a N") == (std::vector<PhonemeRole>{PhonemeRole::Nucleus, PhonemeRole::Coda}));
  CHECK(roles("m a s") == (std::vector<PhonemeRole>{PhonemeRole::Onset, PhonemeRole::Nucleus, PhonemeRole::Coda}));
  // A consonant that a later vowel claims stays the onset of the next syllable, which is what
  // keeps the maximal-onset reading of a multi-syllable hint intact.
  CHECK(roles("k a s a") == (std::vector<PhonemeRole>{PhonemeRole::Onset, PhonemeRole::Nucleus,
                                                      PhonemeRole::Onset, PhonemeRole::Nucleus}));
  // Symbols with their own role are never re-read by position.
  CHECK(roles("a cl") == (std::vector<PhonemeRole>{PhonemeRole::Nucleus, PhonemeRole::Geminate}));
  CHECK(roles("cl k a") == (std::vector<PhonemeRole>{PhonemeRole::Geminate, PhonemeRole::Onset, PhonemeRole::Nucleus}));
  region->findNote(id)->phoneticHint.reset();
}


TEST_CASE("explicit Japanese phone hints change pronunciation without replacing displayed lyrics") {
  PhonemizerFixture fixture;
  const auto id = fixture.add(U"漢", seam::time::Tick{0});
  auto* region = fixture.project.findRegion(fixture.regionId);
  const auto original = region->lyrics;
  const auto before = seam::phonemizer::resolveJapanesePronunciation(*region); CHECK(before);
  region->findNote(id)->phoneticHint = "k a N";
  const auto hinted = seam::phonemizer::resolveJapanesePronunciation(*region); CHECK(hinted);
  CHECK(symbols(hinted.value().pronunciation.tokens) == (std::vector<std::string>{"k", "a", "N"}));
  CHECK(hinted.value().pronunciation.warnings.empty()); CHECK(region->lyrics == original);
  CHECK(hinted.value().identity.inputHash != before.value().identity.inputHash);
  CHECK(hinted.value().identity.sequenceHash != before.value().identity.sequenceHash);
  region->findNote(id)->phoneticHint = "not-a-phone";
  CHECK(!seam::phonemizer::resolveJapanesePronunciation(*region));
  const auto visible = seam::phonemizer::JapaneseKanaPhonemizer{}.phonemize(*region);
  CHECK(!visible.warnings.empty()); CHECK(region->lyrics == original);
  region->findNote(id)->phoneticHint.reset();
  const auto restored = seam::phonemizer::resolveJapanesePronunciation(*region); CHECK(restored);
  CHECK(restored.value().identity == before.value().identity);
  CHECK(!seam::phonemizer::parseJapanesePhoneHint(" \t"));
  CHECK(!seam::phonemizer::parseJapanesePhoneHint(std::string(4097U, 'a')));
  CHECK(seam::phonemizer::parseJapanesePhoneHint(" ky\ta  "));
}
