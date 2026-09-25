// D2's exit requires more than a lane that draws: a creator must change audible expression on the
// song, and the selected resource must really support the edited controls. These cases render the
// same project twice through the production path and check the audio actually differs.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/domain/project.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/rendering/project_renderer.hpp"
#include "seam/rendering/pcm_cache.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/ui/expression_lane.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voice_design/voice_recipe.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
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

double estimateFundamentalHz(const std::vector<float>& samples, std::size_t start,
                             double minimumHz, double maximumHz) {
  constexpr double sampleRate = 48000.0;
  constexpr std::size_t windowFrames = 6000U;
  if (start > samples.size() || samples.size() - start < windowFrames) return 0.0;
  const auto minimumLag = static_cast<std::size_t>(sampleRate / maximumHz);
  const auto maximumLag = static_cast<std::size_t>(sampleRate / minimumHz);
  const auto window = std::span<const float>{samples}.subspan(start, windowFrames);
  const auto normalizedCorrelation = [&](std::size_t lag) {
    double correlation = 0.0;
    double energy = 0.0;
    for (std::size_t index = 0U; index + lag < window.size(); ++index) {
      const auto current = static_cast<double>(window[index]);
      correlation += current * static_cast<double>(window[index + lag]);
      energy += current * current;
    }
    return energy > 0.0 ? correlation / energy : -1.0;
  };
  double bestCorrelation = -1.0;
  std::size_t bestLag = minimumLag;
  for (auto lag = minimumLag; lag <= maximumLag; ++lag) {
    const auto normalized = normalizedCorrelation(lag);
    if (normalized > bestCorrelation) {
      bestCorrelation = normalized;
      bestLag = lag;
    }
  }
  auto fractionalLag = static_cast<double>(bestLag);
  if (bestLag > minimumLag && bestLag < maximumLag) {
    const auto left = normalizedCorrelation(bestLag - 1U);
    const auto center = normalizedCorrelation(bestLag);
    const auto right = normalizedCorrelation(bestLag + 1U);
    const auto curvature = left - 2.0 * center + right;
    if (curvature < 0.0) fractionalLag += 0.5 * (left - right) / curvature;
  }
  return sampleRate / fractionalLag;
}

double centsBetween(double lhsHz, double rhsHz) {
  if (lhsHz <= 0.0 || rhsHz <= 0.0) return std::numeric_limits<double>::infinity();
  return 1200.0 * std::log2(lhsHz / rhsHz);
}

}  // namespace

TEST_CASE("Editing a supported channel on the song changes the rendered audio") {
  auto song = makeSong();
  const auto baseline = renderSong(song, song.project);
  CHECK(!baseline.empty());
  // The same project rendered twice is deterministic, so a later difference is the edit's.
  const auto repeated = renderSong(song, song.project);
  CHECK(relativeEnergyDifference(baseline, repeated) == 0.0);
  const auto firstNotePitch = estimateFundamentalHz(baseline, 16'800U, 250.0, 275.0);
  const auto secondNotePitch = estimateFundamentalHz(baseline, 64'800U, 280.0, 310.0);
  CHECK(firstNotePitch > 0.0);
  CHECK(secondNotePitch > 0.0);

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
    CHECK(rendered.size() == baseline.size());
    const auto firstDrift = std::abs(centsBetween(
        estimateFundamentalHz(rendered, 16'800U, 250.0, 275.0), firstNotePitch));
    const auto secondDrift = std::abs(centsBetween(
        estimateFundamentalHz(rendered, 64'800U, 280.0, 310.0), secondNotePitch));
    if (firstDrift > 8.0 || secondDrift > 8.0) {
      throw test::Failure{std::string{ui::describeExpressionChannel(channel).label} +
          " changed the ordinary-note fundamental by " + std::to_string(firstDrift) +
          " / " + std::to_string(secondDrift) + " cents"};
    }
  }
}

TEST_CASE("all supported timbral expression curves survive project save and reload") {
  auto song = makeSong();
  auto* target = song.project.findRegion(song.region);
  CHECK(target != nullptr);
  if (target == nullptr) return;

  CHECK(target->formantAutomation.upsert({time::Tick{0}, 4.0F}).hasValue());
  CHECK(target->breathinessAutomation.upsert({time::Tick{0}, 0.35F}).hasValue());
  CHECK(target->tensionAutomation.upsert({time::Tick{0}, 0.35F}).hasValue());
  CHECK(target->airinessAutomation.upsert({time::Tick{0}, 0.35F}).hasValue());
  CHECK(target->genderAutomation.upsert({time::Tick{0}, -0.35F}).hasValue());
  CHECK(target->growlAutomation.upsert({time::Tick{0}, 0.35F}).hasValue());

  const formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(song.project);
  CHECK(encoded.hasValue());
  if (!encoded) return;
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded.hasValue());
  if (!decoded) return;

  const auto* reloaded = decoded.value().findRegion(song.region);
  CHECK(reloaded != nullptr);
  if (reloaded == nullptr) return;
  CHECK(reloaded->formantAutomation == target->formantAutomation);
  CHECK(reloaded->breathinessAutomation == target->breathinessAutomation);
  CHECK(reloaded->tensionAutomation == target->tensionAutomation);
  CHECK(reloaded->airinessAutomation == target->airinessAutomation);
  CHECK(reloaded->genderAutomation == target->genderAutomation);
  CHECK(reloaded->growlAutomation == target->growlAutomation);

  const auto beforeSave = renderSong(song, song.project);
  const auto afterReload = renderSong(song, decoded.value());
  CHECK(!beforeSave.empty());
  CHECK(afterReload == beforeSave);
}

TEST_CASE("neutral timbral automation is exactly identical to an unedited song") {
  auto song = makeSong();
  const auto baseline = renderSong(song, song.project);
  auto neutral = song.project;
  auto* target = neutral.findRegion(song.region);
  CHECK(target != nullptr);
  if (target == nullptr) return;

  CHECK(target->formantAutomation.upsert({time::Tick{0}, 0.0F}).hasValue());
  CHECK(target->breathinessAutomation.upsert({time::Tick{0}, 0.0F}).hasValue());
  CHECK(target->tensionAutomation.upsert({time::Tick{0}, 0.0F}).hasValue());
  CHECK(target->airinessAutomation.upsert({time::Tick{0}, 0.0F}).hasValue());
  CHECK(target->genderAutomation.upsert({time::Tick{0}, 0.0F}).hasValue());
  CHECK(target->growlAutomation.upsert({time::Tick{0}, 0.0F}).hasValue());
  CHECK(neutral.validate().hasValue());

  CHECK(renderSong(song, neutral) == baseline);
}

TEST_CASE("procedural project PCM cache reuses exact expression identity only") {
  auto song = makeSong();
  const auto root = test::support::temporaryDirectory("procedural-expression-cache");
  rendering::PcmCache cache{root / "cache"};
  const std::vector<rendering::TrackSingerSource> sources{
      rendering::TrackProceduralSource{song.track, song.resource, "neutral"}};
  const rendering::ProductionProjectRenderer renderer;
  const auto render = [&](const domain::Project& project) {
    return renderer.renderWithSources(project, sources, song.track, song.region, 1U,
        48000U, rendering::RenderQuality::Final, {}, &cache);
  };

  const auto cold = render(song.project);
  CHECK(cold.hasValue());
  if (!cold) return;
  const auto warm = render(song.project);
  CHECK(warm.hasValue());
  if (!warm) return;
  CHECK(cold.value().cacheHits == 0U);
  CHECK(warm.value().cacheHits == 1U);
  CHECK(warm.value().interleaved == cold.value().interleaved);

  cache.clearMemory();
  const auto diskWarm = render(song.project);
  CHECK(diskWarm.hasValue());
  if (!diskWarm) return;
  CHECK(diskWarm.value().cacheHits == 1U);
  CHECK(diskWarm.value().interleaved == cold.value().interleaved);
  CHECK(cache.stats().diskHits >= 1U);

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
    switch (channel) {
      case ui::ExpressionChannel::Formant:
        CHECK(target->formantAutomation.upsert({time::Tick{0}, amount}).hasValue());
        break;
      case ui::ExpressionChannel::Breathiness:
        CHECK(target->breathinessAutomation.upsert({time::Tick{0}, amount}).hasValue());
        break;
      case ui::ExpressionChannel::Tension:
        CHECK(target->tensionAutomation.upsert({time::Tick{0}, amount}).hasValue());
        break;
      case ui::ExpressionChannel::Airiness:
        CHECK(target->airinessAutomation.upsert({time::Tick{0}, amount}).hasValue());
        break;
      case ui::ExpressionChannel::Gender:
        CHECK(target->genderAutomation.upsert({time::Tick{0}, amount}).hasValue());
        break;
      case ui::ExpressionChannel::Growl:
        CHECK(target->growlAutomation.upsert({time::Tick{0}, amount}).hasValue());
        break;
    }
    const auto changed = render(edited);
    CHECK(changed.hasValue());
    if (!changed) continue;
    CHECK(changed.value().cacheHits == 0U);
    CHECK(changed.value().phraseContentHashes != cold.value().phraseContentHashes);
    CHECK(changed.value().interleaved != cold.value().interleaved);

    const auto changedWarm = render(edited);
    CHECK(changedWarm.hasValue());
    if (!changedWarm) continue;
    CHECK(changedWarm.value().cacheHits == 1U);
    CHECK(changedWarm.value().interleaved == changed.value().interleaved);
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
