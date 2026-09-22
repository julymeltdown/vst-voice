#include "test_framework.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/native_ui/conversion_review_model.hpp"

#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace {

seam::authoring::InterchangeImportDraft conversionDraft() {
  seam::application::ProjectFactory factory{62000U};
  seam::authoring::InterchangeImportDraft draft;
  draft.project = factory.createProject("Converted song 日本語 한국어");
  draft.sourcePath = "/input/原曲 노래.ustx";
  draft.sourceHash = std::string(64U, 'a');
  for (std::size_t trackIndex = 0U; trackIndex < 2U; ++trackIndex) {
    const auto trackId = factory.addVocalTrack(draft.project, "Imported vocal");
    auto* track = draft.project.findVocalTrack(trackId);
    track->voicebank = {"interchange.unresolved.voicebank", "0.0.0-interchange", ""};
    for (std::size_t regionIndex = 0U; regionIndex < trackIndex + 1U;
         ++regionIndex) {
      const auto regionId = factory.addRegion(draft.project, trackId, "Phrase",
          seam::time::Tick{0}, seam::time::Tick{1920});
      auto* region = draft.project.findRegion(regionId);
      auto [lyric, note] = factory.makeNote(seam::time::Tick{0},
          seam::time::Tick{960}, 60U, U"歌");
      region->lyrics.push_back(std::move(lyric));
      region->notes.push_back(std::move(note));
    }
  }
  draft.issues = {
      {seam::authoring::InterchangeFormat::Ustx, false, "tracks[0]", "Check singer"},
      {seam::authoring::InterchangeFormat::Ustx, true, "parts[0].notes[0]", "Lost expression"},
      {seam::authoring::InterchangeFormat::Ustx, false, "parts[1]", "Check timing"},
  };
  return draft;
}

}  // namespace

TEST_CASE("conversion review counts the exact draft without editing it") {
  const auto draft = conversionDraft();
  const auto projectBefore = draft.project;
  const auto issuesBefore = draft.issues;
  const seam::native_ui::ConversionReviewModel model{draft};
  CHECK(model.formatName() == "USTX");
  CHECK(model.trackCount() == 2U);
  CHECK(model.regionCount() == 3U);
  CHECK(model.noteCount() == 3U);
  CHECK(model.warningCount() == 2U);
  CHECK(model.lossCount() == 1U);
  CHECK(model.hasLosses());
  CHECK(model.issueCount() == 3U);
  CHECK(model.sourcePath() == draft.sourcePath);
  CHECK(model.sourceHash() == draft.sourceHash);
  CHECK(model.summary() == "USTX import: 2 tracks, 3 vocal regions, 3 notes\n1 losses; 2 warnings");
  CHECK(model.sourceDetails().find(draft.sourcePath.string()) != std::string::npos);
  CHECK(model.sourceDetails().find(draft.sourceHash) != std::string::npos);
  CHECK(draft.project == projectBefore);
  CHECK(draft.issues == issuesBefore);
}

TEST_CASE("conversion review borrows rows and retains the full unicode detail") {
  auto draft = conversionDraft();
  std::string longMessage;
  for (std::size_t i = 0U; i < 1024U; ++i) {
    longMessage += "発音 손실 🎵 — ";
  }
  const auto location = "parts[1].notes[4095].expressions/原文/긴 위치";
  draft.issues.back() = {seam::authoring::InterchangeFormat::Ustx, true,
                        location, longMessage};
  const seam::native_ui::ConversionReviewModel model{draft};
  CHECK(model.issue(2U) == &draft.issues[2U]);
  CHECK(model.issue(2U)->message.data() == draft.issues[2U].message.data());
  CHECK(model.issueDetails(2U) ==
      "Loss (USTX)\nLocation: " + std::string{location} + "\n\n" + longMessage);
  CHECK(model.issue(3U) == nullptr);
  CHECK(model.issue(std::numeric_limits<std::size_t>::max()) == nullptr);
  CHECK(model.issueDetails(3U).empty());
}

TEST_CASE("conversion review displays all admitted report rows including the last") {
  auto draft = conversionDraft();
  draft.issues.clear();
  for (std::size_t i = 0U; i < 4096U; ++i) {
    draft.issues.push_back({seam::authoring::InterchangeFormat::Smf,
        i % 2U == 0U, "tick:" + std::to_string(i), "Issue " + std::to_string(i)});
  }
  draft.format = seam::authoring::InterchangeFormat::Smf;
  const seam::native_ui::ConversionReviewModel model{draft};
  CHECK(model.formatName() == "MIDI");
  CHECK(model.issueCount() == 4096U);
  CHECK(model.warningCount() == 2048U);
  CHECK(model.lossCount() == 2048U);
  CHECK(model.issue(4095U) == &draft.issues.back());
  CHECK(model.issueDetails(4095U) == "Warning (MIDI)\nLocation: tick:4095\n\nIssue 4095");
}

TEST_CASE("conversion review distinguishes incomplete identity from availability") {
  auto draft = conversionDraft();
  auto& complete = draft.project.vocalTracks()[0U];
  complete.voicebank = {"org.example.singer", "1.0.0", std::string(64U, 'b')};
  const seam::native_ui::ConversionReviewModel model{draft};
  CHECK(model.unresolvedSingerCount() == 1U);
  CHECK(model.singerDisclosure().find("1 of 2 vocal tracks") != std::string::npos);
  CHECK(model.singerDisclosure().find("availability is not verified") != std::string::npos);
  CHECK(model.singerDisclosure().find("No singer is automatically substituted") != std::string::npos);
  CHECK(model.singerDisclosure().find("before preview or export") != std::string::npos);
}

TEST_CASE("conversion review recognizes recipe and neural identities without claiming resolution") {
  auto draft = conversionDraft();
  auto& recipe = draft.project.vocalTracks()[0U];
  recipe.proceduralRecipe = seam::domain::ProceduralRecipeReference{
      {seam::domain::SingerResourceKind::Procedural, "recipe.singer", "11",
       std::string(64U, 'c')}, "/recipes/singer.json", "neutral"};
  auto& neural = draft.project.vocalTracks()[1U];
  neural.neuralResource = seam::domain::NeuralResourceReference{
      {seam::domain::SingerResourceKind::Neural, "neural.singer", "1",
       std::string(64U, 'd')}};
  const seam::native_ui::ConversionReviewModel model{draft};
  CHECK(model.unresolvedSingerCount() == 0U);
  CHECK(model.singerDisclosure().find("availability is not verified") != std::string::npos);
}

TEST_CASE("conversion review handles an empty MIDI draft and counts audio tracks separately") {
  seam::authoring::InterchangeImportDraft draft;
  draft.format = seam::authoring::InterchangeFormat::Smf;
  draft.project.audioTracks().push_back(seam::domain::AudioTrack{});
  const seam::native_ui::ConversionReviewModel model{draft};
  CHECK(model.formatName() == "MIDI");
  CHECK(model.trackCount() == 1U);
  CHECK(model.regionCount() == 0U);
  CHECK(model.noteCount() == 0U);
  CHECK(model.issueCount() == 0U);
  CHECK(model.warningCount() == 0U);
  CHECK(model.lossCount() == 0U);
  CHECK(!model.hasLosses());
  CHECK(model.unresolvedSingerCount() == 0U);
  CHECK(model.issue(0U) == nullptr);
  CHECK(model.issueDetails(0U).empty());
  static_assert(!std::is_constructible_v<seam::native_ui::ConversionReviewModel,
                                        seam::authoring::InterchangeImportDraft&&>);
}
