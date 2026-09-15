// D2's exit requires more than a lane that draws: a creator must change audible expression on the
// song, and the selected resource must really support the edited controls. These cases render the
// same project twice through the production path and check the audio actually differs.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/domain/project.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/ui/expression_lane.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voice_design/voice_recipe.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace seam;

// A source-filter singer, which is the carrier the six timbral channels need. It mirrors the retained
// D1 song's shape: an original procedural voice, not a sample bank.
voice_design::VoiceRecipe songRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "d2-song";
  recipe.seed = 5150U;
  voice_design::VoicePose pose;
  pose.phone = "a";
  pose.style = "neutral";
  pose.formants = {voice_design::ResonanceBand{640.0, 78.0, 0.0},
                   voice_design::ResonanceBand{1180.0, 96.0, -3.0},
                   voice_design::ResonanceBand{2650.0, 150.0, -7.0}};
  recipe.poses.push_back(pose);
  return recipe;
}

struct Song final {
  domain::Project project;
  domain::TrackId track;
  domain::RegionId region;
  synthesis::ProceduralSingerResource resource;
};

Song makeSong() {
  application::ProjectFactory factory{4400U};
  auto project = factory.createProject("D2 expression song");
  const auto track = factory.addVocalTrack(project, "Original procedural pilot");
  const auto region = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{3840});
  auto* target = project.findRegion(region);
  auto [lyricA, noteA] = factory.makeNote(time::Tick{0}, time::Tick{1920}, 60U, U"あ",
                                         domain::Language::Japanese);
  noteA.phoneticHint = "a";
  target->notes.push_back(std::move(noteA));
  auto [lyricB, noteB] = factory.makeNote(time::Tick{1920}, time::Tick{1920}, 62U, U"あ",
                                         domain::Language::Japanese);
  noteB.phoneticHint = "a";
  target->notes.push_back(std::move(noteB));
  target->lyrics.push_back(std::move(lyricA));
  target->lyrics.push_back(std::move(lyricB));
  target->sortNotes();
  const auto resource = voice_design::freezeVoiceRecipeResource(songRecipe());
  if (!resource) throw test::Failure{"freezing the song recipe failed: " + resource.error().message};
  // The track must record the procedural singer it uses. The carrier the six timbral channels need is
  // decided from that selection, so a project without it is a sample carrier and refuses them.
  auto* recorded = project.findVocalTrack(track);
  recorded->proceduralRecipe = domain::ProceduralRecipeReference{resource.value().identity,
                                                                "d2-song.json", "neutral"};
  const auto valid = project.validate();
  if (!valid) throw test::Failure{"the song project is invalid: " + valid.error().message};
  return Song{std::move(project), track, region, resource.value()};
}

std::vector<float> renderSong(const Song& song, const domain::Project& project) {
  auto snapshot = rendering::RenderSnapshotFactory{}.createProcedural(
      project, song.resource, song.track, song.region, 1U, rendering::RenderQuality::Final, 48000U);
  if (!snapshot) throw test::Failure{"building the snapshot failed: " + snapshot.error().message};
  auto rendered = rendering::PhraseRenderPipeline{}.render(snapshot.value());
  if (!rendered) throw test::Failure{"rendering failed: " + rendered.error().message};
  return std::move(rendered.value().rendered.audio.samples);
}

// A bounded comparison: how much of the signal's energy differs. It distinguishes a real change from
// floating-point noise without claiming anything about how the change sounds.
double relativeEnergyDifference(const std::vector<float>& lhs, const std::vector<float>& rhs) {
  const auto count = std::min(lhs.size(), rhs.size());
  if (count == 0U) return 0.0;
  double difference = 0.0;
  double reference = 0.0;
  for (std::size_t index = 0U; index < count; ++index) {
    const auto delta = static_cast<double>(lhs[index]) - static_cast<double>(rhs[index]);
    difference += delta * delta;
    reference += static_cast<double>(lhs[index]) * static_cast<double>(lhs[index]);
  }
  if (reference <= 0.0) return 0.0;
  return std::sqrt(difference / reference);
}

}  // namespace

TEST_CASE("Editing a supported channel on the song changes the rendered audio") {
  auto song = makeSong();
  const auto baseline = renderSong(song, song.project);
  CHECK(!baseline.empty());
  // The same project rendered twice is deterministic, so a later difference is the edit's.
  const auto repeated = renderSong(song, song.project);
  CHECK(relativeEnergyDifference(baseline, repeated) == 0.0);

  const std::vector<std::pair<ui::ExpressionChannel, float>> edits{
      {ui::ExpressionChannel::Formant, 4.0F},
      {ui::ExpressionChannel::Breathiness, 0.35F},
      {ui::ExpressionChannel::Tension, 0.35F},
      {ui::ExpressionChannel::Airiness, 0.35F},
      {ui::ExpressionChannel::Gender, -0.35F},
      {ui::ExpressionChannel::Growl, 0.35F},
  };
  for (const auto& [channel, amount] : edits) {
    auto edited = song.project;
    auto* target = edited.findRegion(song.region);
    CHECK(target != nullptr);
    if (target == nullptr) continue;
    const auto insertion = time::Tick{0};
    switch (channel) {
      case ui::ExpressionChannel::Formant:
        CHECK(target->formantAutomation.upsert({insertion, amount}).hasValue());
        break;
      case ui::ExpressionChannel::Breathiness:
        CHECK(target->breathinessAutomation.upsert({insertion, amount}).hasValue());
        break;
      case ui::ExpressionChannel::Tension:
        CHECK(target->tensionAutomation.upsert({insertion, amount}).hasValue());
        break;
      case ui::ExpressionChannel::Airiness:
        CHECK(target->airinessAutomation.upsert({insertion, amount}).hasValue());
        break;
      case ui::ExpressionChannel::Gender:
        CHECK(target->genderAutomation.upsert({insertion, amount}).hasValue());
        break;
      case ui::ExpressionChannel::Growl:
        CHECK(target->growlAutomation.upsert({insertion, amount}).hasValue());
        break;
    }
    CHECK(edited.validate().hasValue());
    const auto rendered = renderSong(song, edited);
    // A drawn curve that does not reach the audio is not editing; this is the audible consequence.
    CHECK(relativeEnergyDifference(baseline, rendered) > 1e-4);
  }
}

TEST_CASE("A sample carrier refuses the timbral channels by name and the source carrier allows them") {
  auto song = makeSong();
  for (std::size_t index = 0U; index < ui::kExpressionChannelCount; ++index) {
    const auto channel = ui::expressionChannelAt(index);
    CHECK(ui::validateExpressionCarrier(song.project, song.track, channel).hasValue());
  }
  // Removing the procedural selection leaves a sample carrier, which owns no excitation or tract.
  auto bankProject = song.project;
  auto* bankTrack = bankProject.findVocalTrack(song.track);
  CHECK(bankTrack != nullptr);
  if (bankTrack == nullptr) return;
  bankTrack->proceduralRecipe.reset();
  for (std::size_t index = 0U; index < ui::kExpressionChannelCount; ++index) {
    const auto channel = ui::expressionChannelAt(index);
    const auto descriptor = ui::describeExpressionChannel(channel);
    const auto allowed = ui::validateExpressionCarrier(bankProject, song.track, channel);
    CHECK(!allowed.hasValue());
    if (!allowed)
      CHECK(allowed.error().message.find(std::string{descriptor.label}) != std::string::npos);
  }
}

TEST_CASE("A stored channel keeps its own unit and a refused curve stays visible") {
  auto song = makeSong();
  auto* target = song.project.findRegion(song.region);
  CHECK(target != nullptr);
  if (target == nullptr) return;
  CHECK(target->formantAutomation.upsert({time::Tick{960}, 3.0F}).hasValue());
  // The units differ per channel, so a surface cannot present semitones as a normalized share.
  CHECK(ui::describeExpressionChannel(ui::ExpressionChannel::Formant).unit == "semitones");
  CHECK(ui::describeExpressionChannel(ui::ExpressionChannel::Growl).unit == "normalized share");
  CHECK(ui::describeExpressionChannel(ui::ExpressionChannel::Gender).bipolar);
  CHECK(!ui::describeExpressionChannel(ui::ExpressionChannel::Breathiness).bipolar);
  const auto stored = ui::readExpressionPoints(*target, ui::ExpressionChannel::Formant);
  CHECK(stored.size() == 1U);
  CHECK(stored.front().amount == 3.0F);
  // The stored curve is what the renderer consumes, so the audio reflects it.
  const auto quiet = makeSong();
  const auto withoutCurve = renderSong(quiet, quiet.project);
  const auto withCurve = renderSong(song, song.project);
  CHECK(relativeEnergyDifference(withoutCurve, withCurve) > 1e-4);
}
