#include "test_framework.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"

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
