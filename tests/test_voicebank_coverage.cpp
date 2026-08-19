#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/voicebank/coverage.hpp"
#include "seam/rendering/project_renderer.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/voicebank/wav.hpp"

#include <vector>

namespace {
seam::domain::PhonemeToken token(seam::domain::NoteId note,
                                  std::uint16_t ordinal,
                                  std::string symbol) {
  return seam::domain::PhonemeToken{
      .key = {.noteId = note, .ordinal = ordinal},
      .symbol = std::move(symbol),
      .role = seam::domain::PhonemeRole::Nucleus,
      .voiced = true,
      .timing = {},
      .locked = false,
  };
}
seam::domain::VocalRegion regionWithNotes(
    const std::vector<std::pair<seam::domain::NoteId, int>>& notes) {
  seam::domain::VocalRegion region;
  region.id = seam::domain::RegionId{10U};
  region.name = "Coverage";
  region.startTick = seam::time::Tick{0};
  region.durationTick = seam::time::Tick{7680};
  std::uint64_t lyricId = 100U;
  int index = 0;
  for (const auto& [noteId, midi] : notes) {
    region.lyrics.push_back({seam::domain::LyricTokenId{lyricId}, U"a",
                             seam::domain::Language::Japanese});
    region.notes.push_back(seam::domain::Note{
        .id = noteId,
        .startTick = seam::time::Tick{index * 960},
        .durationTick = seam::time::Tick{960},
        .midiKey = static_cast<std::uint8_t>(midi),
        .lyricTokenId = seam::domain::LyricTokenId{lyricId},
        .articulation = seam::domain::NoteArticulation::Normal,
        .slurGroup = std::nullopt,
    });
    ++lyricId;
    ++index;
  }
  return region;
}
}  // namespace

TEST_CASE("voicebank_inventory_summarizes_styles_layers_kinds_and_special_units") {
  auto sustain = seam::test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 60, seam::voicebank::UnitKind::Sustain);
  auto release = seam::test::support::makeUnit(
      "release", {"a"}, "audio/release.wav", 67,
      seam::voicebank::UnitKind::Release);
  release.style = "soft";
  auto breath = seam::test::support::makeUnit(
      "breath", {"br"}, "audio/breath.wav", 60,
      seam::voicebank::UnitKind::Breath);
  breath.enabled = false;
  auto manifest = seam::test::support::makeManifest({sustain, release, breath});
  manifest.styles = {"original", "soft"};

  const auto inventory = seam::voicebank::VoicebankCoverageAnalyzer::inventory(manifest);
  CHECK(inventory.enabledUnitCount == 2U);
  CHECK(inventory.disabledUnitCount == 1U);
  CHECK(inventory.styles.size() == 2U);
  CHECK(inventory.rootPitchLayers.size() == 2U);
  CHECK(inventory.hasSustain);
  CHECK(inventory.hasRelease);
  CHECK(inventory.hasBreath);
  CHECK(!inventory.phoneSequences.empty());
}

TEST_CASE("voicebank_coverage_distinguishes_missing_disabled_style_and_pitch") {
  auto ka = seam::test::support::makeUnit(
      "ka", {"k", "a"}, "audio/ka.wav", 60);
  auto sa = seam::test::support::makeUnit(
      "sa", {"s", "a"}, "audio/sa.wav", 60);
  sa.enabled = false;
  auto ta = seam::test::support::makeUnit(
      "ta", {"t", "a"}, "audio/ta.wav", 60);
  ta.style = "soft";
  auto na = seam::test::support::makeUnit(
      "na", {"n", "a"}, "audio/na.wav", 90);
  auto manifest = seam::test::support::makeManifest({ka, sa, ta, na});
  manifest.styles = {"original", "soft"};

  const auto n1 = seam::domain::NoteId{1U};
  const auto n2 = seam::domain::NoteId{2U};
  const auto n3 = seam::domain::NoteId{3U};
  const auto n4 = seam::domain::NoteId{4U};
  const auto n5 = seam::domain::NoteId{5U};
  auto region = regionWithNotes({{n1, 60}, {n2, 60}, {n3, 60}, {n4, 60}, {n5, 60}});
  const std::vector tokens{
      token(n1, 0, "k"), token(n1, 1, "a"),
      token(n2, 0, "s"), token(n2, 1, "a"),
      token(n3, 0, "t"), token(n3, 1, "a"),
      token(n4, 0, "n"), token(n4, 1, "a"),
      token(n5, 0, "m"), token(n5, 1, "a"),
  };
  const auto report = seam::voicebank::VoicebankCoverageAnalyzer::analyzeRegion(
      manifest, seam::domain::TrackId{7U}, region, tokens, "original", 12);
  CHECK(report.summary.totalPhonemes == 10U);
  CHECK(report.summary.coveredPhonemes == 2U);
  CHECK(report.summary.disabledUnitCount >= 1U);
  CHECK(report.summary.unsupportedStyleCount >= 1U);
  CHECK(report.summary.unsupportedPitchRangeCount >= 1U);
  CHECK(report.summary.missingUnitCount >= 1U);
  CHECK(!report.complete());
}

TEST_CASE("project_renderer_continues_unaffected_regions_and_reports_failed_phrase") {
  const auto root = seam::test::support::temporaryDirectory("u3-partial-render");
  std::filesystem::create_directories(root / "audio");
  const auto samples = seam::test::support::sineWave(48000U, 220.0, 0.3);
  CHECK(seam::voicebank::writePcm16Wav(root / "audio/a.wav", 48000U, 1U,
                                       samples));
  auto manifest = seam::test::support::makeManifest({
      seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 60,
                                    seam::voicebank::UnitKind::Sustain,
                                    samples.size())});

  seam::application::ProjectFactory factory{1U};
  auto project = factory.createProject("Partial coverage");
  const auto trackId = factory.addVocalTrack(project, "VOICE");
  auto* track = project.findVocalTrack(trackId);
  CHECK(track != nullptr);
  track->voicebank = {.id = manifest.id, .version = manifest.version,
                      .contentHash = std::string(64U, 'a')};
  const auto goodRegionId = factory.addRegion(
      project, trackId, "GOOD", seam::time::Tick{0}, seam::time::Tick{1920});
  const auto badRegionId = factory.addRegion(
      project, trackId, "BAD", seam::time::Tick{1920}, seam::time::Tick{1920});
  auto [goodLyric, goodNote] = factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{960}, 60U, U"あ",
      seam::domain::Language::Japanese);
  auto [badLyric, badNote] = factory.makeNote(
      seam::time::Tick{0}, seam::time::Tick{960}, 60U, U"か",
      seam::domain::Language::Japanese);
  project.findRegion(goodRegionId)->lyrics.push_back(std::move(goodLyric));
  project.findRegion(goodRegionId)->notes.push_back(std::move(goodNote));
  project.findRegion(badRegionId)->lyrics.push_back(std::move(badLyric));
  project.findRegion(badRegionId)->notes.push_back(std::move(badNote));
  CHECK(project.validate());

  const seam::rendering::TrackVoicebankSource source{
      .trackId = trackId,
      .manifest = manifest,
      .bankRoot = root,
      .contentHash = std::string(64U, 'a'),
      .trust = seam::voicebank::VoicebankTrust::DevelopmentFixture,
  };
  seam::rendering::ProductionProjectRenderer renderer;
  const std::array sources{source};
  const auto rendered = renderer.render(
      project, sources, trackId, goodRegionId, 1U, 48000U);
  CHECK(rendered);
  CHECK(!rendered.value().interleaved.empty());
  CHECK(rendered.value().regionCount == 1U);
  CHECK(rendered.value().diagnostics.size() == 1U);
  CHECK(rendered.value().diagnostics.front().regionId == badRegionId);
  CHECK(rendered.value().diagnostics.front().code ==
        seam::core::ErrorCode::NotFound);
}
