// Gender is a coupling, and the test has to prove the coupling rather than a shift.
//
// The channels beside this one each move one half of the voice: formant moves the tract's resonances and
// leaves the source alone, tension tilts the source and leaves the tract alone. Gender is defined as the
// channel that moves both together, so this file renders the same vowel five ways -- plain, gender up,
// gender down, tract-shifted-only, source-tilted-only -- and measures two independent things: where the
// third formant sits, which only the tract can move, and how steep the harmonic source is, which only the
// source can move. Gender is the only render that moves both. If gender ever decayed into a second
// formant or a second tension, one of those two comparisons would fail here.
//
// The persisted unit is bipolar and zero is exactly neutral, which is why the neutral case is checked for
// bit-identity like its neighbours. The voice is a procedural fixture, not a singer: the oracle measures
// the render, and no claim is made about identity, naturalness or how the result sounds to a listener.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/domain/gender_automation.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/rendering/render_pipeline.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/synthesis/renderer_capabilities.hpp"
#include "seam/voice_design/recipe_resource.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace seam;

constexpr double kSampleRate = 48000.0;
constexpr double kFundamentalHz = 110.0;

voice_design::VoiceRecipe vowelRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "gender-channel";
  recipe.poses = {
      {"a", "neutral", 0.0, {{700.0, 90.0, 0.0}, {1150.0, 110.0, -3.0}, {2600.0, 160.0, -8.0}}}};
  return recipe;
}

struct VowelProject final {
  domain::Project project;
  domain::TrackId track;
  domain::RegionId region;
  synthesis::ProceduralSingerResource resource;
};

VowelProject makeVowelProject() {
  application::ProjectFactory factory{8100U};
  auto project = factory.createProject("Gender channel");
  const auto track = factory.addVocalTrack(project, "Singer");
  const auto region = factory.addRegion(project, track, "Phrase", time::Tick{0}, time::Tick{3840});
  auto* target = project.findRegion(region);
  auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{3840}, 45U, U"あ",
                                        domain::Language::Japanese);
  note.phoneticHint = "a";
  target->lyrics.push_back(std::move(lyric));
  target->notes.push_back(std::move(note));
  const auto resource = voice_design::freezeVoiceRecipeResource(vowelRecipe());
  CHECK(resource.hasValue());
  return VowelProject{std::move(project), track, region, resource.value()};
}

domain::Project withGender(domain::Project project, domain::RegionId region, float amount) {
  auto* target = project.findRegion(region);
  CHECK(target != nullptr);
  const auto inserted =
      target->genderAutomation.upsert(domain::GenderAutomationPoint{time::Tick{0}, amount});
  CHECK(inserted.hasValue());
  const auto valid = project.validate();
  CHECK(valid.hasValue());
  return project;
}

domain::Project withFormant(domain::Project project, domain::RegionId region, float semitones) {
  auto* target = project.findRegion(region);
  CHECK(target != nullptr);
  const auto inserted =
      target->formantAutomation.upsert(domain::FormantAutomationPoint{time::Tick{0}, semitones});
  CHECK(inserted.hasValue());
  const auto valid = project.validate();
  CHECK(valid.hasValue());
  return project;
}

domain::Project withTension(domain::Project project, domain::RegionId region, float amount) {
  auto* target = project.findRegion(region);
  CHECK(target != nullptr);
  const auto inserted =
      target->tensionAutomation.upsert(domain::TensionAutomationPoint{time::Tick{0}, amount});
  CHECK(inserted.hasValue());
  const auto valid = project.validate();
  CHECK(valid.hasValue());
  return project;
}

std::vector<float> renderVowel(const VowelProject& fixture, const domain::Project& project) {
  auto snapshot = rendering::RenderSnapshotFactory{}.createProcedural(
      project, fixture.resource, fixture.track, fixture.region, 1U,
      rendering::RenderQuality::Final, 48000U);
  CHECK(snapshot.hasValue());
  if (!snapshot) throw test::Failure{snapshot.error().message};
  auto rendered = rendering::PhraseRenderPipeline{}.render(snapshot.value());
  if (!rendered) throw test::Failure{rendered.error().message};
  return std::move(rendered.value().rendered.audio.samples);
}

std::span<const float> analyseWindow(const std::vector<float>& samples) {
  CHECK(samples.size() > 20000U);
  const auto start = samples.size() / 2U;
  const auto count = std::min<std::size_t>(9600U, samples.size() - start);
  return std::span<const float>{samples}.subspan(start, count);
}

double windowed(std::span<const float> window, std::size_t index) {
  const auto position = static_cast<double>(index) /
                        static_cast<double>(window.size() - 1U);
  return static_cast<double>(window[index]) * 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * position));
}

double magnitudeAt(std::span<const float> window, double frequencyHz) {
  const auto omega = 2.0 * std::numbers::pi * frequencyHz / kSampleRate;
  double real = 0.0;
  double imaginary = 0.0;
  for (std::size_t index = 0U; index < window.size(); ++index) {
    const auto value = windowed(window, index);
    real += value * std::cos(omega * static_cast<double>(index));
    imaginary += value * std::sin(omega * static_cast<double>(index));
  }
  return std::sqrt(real * real + imaginary * imaginary);
}

// Where the third formant sits. A peak position is the tract's own property: a source tilt changes how
// tall the peak is, not where the filter puts it.
double thirdFormantHz(std::span<const float> window) {
  double best = 0.0;
  double bestFrequency = 0.0;
  for (auto frequency = 2000.0; frequency <= 3600.0; frequency += 25.0) {
    const auto magnitude = magnitudeAt(window, frequency);
    if (magnitude > best) {
      best = magnitude;
      bestFrequency = frequency;
    }
  }
  for (auto frequency = bestFrequency - 25.0; frequency <= bestFrequency + 25.0; frequency += 5.0) {
    const auto magnitude = magnitudeAt(window, frequency);
    if (magnitude > best) {
      best = magnitude;
      bestFrequency = frequency;
    }
  }
  return bestFrequency;
}

double rootMeanSquare(std::span<const float> window) {
  double total = 0.0;
  for (const auto sample : window) total += static_cast<double>(sample) * static_cast<double>(sample);
  return std::sqrt(total / static_cast<double>(window.size()));
}

struct Periodicity final {
  double fundamentalHz{0.0};
  double coherence{0.0};
};

Periodicity periodicity(std::span<const float> window) {
  const auto minimumLag = static_cast<std::size_t>(kSampleRate / 400.0);
  const auto maximumLag = std::min<std::size_t>(
      static_cast<std::size_t>(kSampleRate / 60.0), window.size() / 2U);
  Periodicity result;
  double best = -1.0;
  std::size_t bestLag = minimumLag;
  for (auto lag = minimumLag; lag <= maximumLag; ++lag) {
    double correlation = 0.0;
    double energy = 0.0;
    for (std::size_t index = 0U; index + lag < window.size(); ++index) {
      correlation += static_cast<double>(window[index]) * static_cast<double>(window[index + lag]);
      energy += static_cast<double>(window[index]) * static_cast<double>(window[index]);
    }
    if (energy <= 0.0) continue;
    const auto normalized = correlation / energy;
    if (normalized > best) {
      best = normalized;
      bestLag = lag;
    }
  }
  result.fundamentalHz = kSampleRate / static_cast<double>(bestLag);
  result.coherence = best < 0.0 ? 0.0 : best;
  return result;
}

TEST_CASE("A gender curve is bipolar, ordered and interpolated") {
  domain::GenderAutomation curve;
  CHECK(curve.points().empty());
  CHECK(curve.valueAt(time::Tick{0}) == 0.0F);
  CHECK(curve.upsert({time::Tick{0}, 0.0F}).hasValue());
  CHECK(curve.upsert({time::Tick{960}, 0.8F}).hasValue());
  CHECK_NEAR(curve.valueAt(time::Tick{480}), 0.4, 1e-6);
  CHECK_NEAR(curve.valueAt(time::Tick{-10}), 0.0, 1e-6);
  CHECK_NEAR(curve.valueAt(time::Tick{4000}), 0.8, 1e-6);
  // The channel is bipolar, so both directions are ordinary values and only the bound is a mistake.
  CHECK(curve.upsert({time::Tick{960}, -0.8F}).hasValue());
  CHECK_NEAR(curve.valueAt(time::Tick{960}), -0.8, 1e-6);
  CHECK(!curve.upsert({time::Tick{960}, 1.2F}).hasValue());
  CHECK(!curve.upsert({time::Tick{960}, -1.2F}).hasValue());
  CHECK(!curve.upsert({time::Tick{960}, std::numeric_limits<float>::quiet_NaN()}).hasValue());
  CHECK(!curve.replacePoints({{time::Tick{960}, 0.2F}, {time::Tick{480}, 0.4F}}).hasValue());
  CHECK(curve.erase(time::Tick{960}));
  CHECK(!curve.erase(time::Tick{960}));
  CHECK(curve.points().size() == 1U);
}

TEST_CASE("A gender curve survives a save and older documents still load") {
  const auto fixture = makeVowelProject();
  const auto project = withGender(fixture.project, fixture.region, -0.7F);
  formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(project);
  CHECK(encoded.hasValue());
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded.hasValue());
  if (!decoded) return;
  const auto* region = decoded.value().findRegion(fixture.region);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->genderAutomation.points().size() == 1U);
  CHECK_NEAR(region->genderAutomation.points().front().amount, -0.7, 1e-6);

  // A schema-15 document predates the channel and means an empty curve, not a refusal.
  auto older = formats::parseJson(encoded.value());
  CHECK(older.hasValue());
  if (!older) return;
  auto tree = older.value();
  tree.asObject()["schemaVersion"] = formats::JsonValue{std::int64_t{15}};
  auto* tracks = tree.find("vocalTracks");
  CHECK(tracks != nullptr && tracks->isArray());
  auto& regions = tracks->asArray().front().asObject()["regions"].asArray();
  regions.front().asObject().erase("genderAutomation");
  const auto reloaded = codec.decode(formats::stringifyJson(tree));
  CHECK(reloaded.hasValue());
  if (!reloaded) return;
  const auto* olderRegion = reloaded.value().findRegion(fixture.region);
  CHECK(olderRegion != nullptr);
  if (olderRegion != nullptr) CHECK(olderRegion->genderAutomation.points().empty());

  // A schema-16 document whose curve leaves the bipolar range is a parse error, not a clamped value.
  auto outOfRange = tree;
  outOfRange.asObject()["schemaVersion"] = formats::JsonValue{std::int64_t{16}};
  auto* outOfRangeTracks = outOfRange.find("vocalTracks");
  CHECK(outOfRangeTracks != nullptr);
  auto& outOfRangeRegions = outOfRangeTracks->asArray().front().asObject()["regions"].asArray();
  outOfRangeRegions.front().asObject()["genderAutomation"] = formats::JsonValue{
      formats::JsonValue::Array{formats::JsonValue{formats::JsonValue::Object{
          {"tick", formats::JsonValue{std::int64_t{0}}},
          {"amount", formats::JsonValue{-1.5}}}}}};
  const auto rejected = codec.decode(formats::stringifyJson(outOfRange));
  CHECK(!rejected.hasValue());
  CHECK(rejected.error().code == core::ErrorCode::ParseError);
}

TEST_CASE("Only the carrier that owns both halves advertises the gender channel") {
  const auto sourceFilter = synthesis::rendererCapabilities(synthesis::RendererCarrier::SourceFilter);
  CHECK(sourceFilter.supports(synthesis::RendererControl::Gender));
  CHECK(sourceFilter.supports(synthesis::RendererControl::Formant));
  CHECK(sourceFilter.supports(synthesis::RendererControl::Tension));
  const auto bank = synthesis::rendererCapabilities(synthesis::RendererCarrier::SampleBank);
  CHECK(!bank.supports(synthesis::RendererControl::Gender));
  CHECK(bank.supports(synthesis::RendererControl::Dynamics));

  synthesis::RendererControlRequest request;
  request.require(synthesis::RendererControl::Gender);
  CHECK(synthesis::validateRendererCapabilities(
            synthesis::RendererCarrier::SourceFilter, request).hasValue());
  const auto refused = synthesis::validateRendererCapabilities(
      synthesis::RendererCarrier::SampleBank, request);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("gender") != std::string::npos);
}

TEST_CASE("A bank that owns neither half refuses the curve instead of dropping it") {
  const auto fixture = makeVowelProject();
  auto project = withGender(fixture.project, fixture.region, 0.7F);
  const auto manifest = test::support::makeManifest({test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 45, voicebank::UnitKind::Sustain)});
  const auto bankRoot = test::support::temporaryDirectory("gender-bank-refusal");
  std::filesystem::create_directories(bankRoot / "audio");
  auto refused = rendering::RenderSnapshotFactory{}.create(
      project, manifest, fixture.track, rendering::PhraseSegment{.regionId = fixture.region}, 1U,
      rendering::RenderQuality::Preview, bankRoot, 48000U);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("gender") != std::string::npos);

  project = withGender(fixture.project, fixture.region, 0.0F);
  auto neutral = rendering::RenderSnapshotFactory{}.create(
      project, manifest, fixture.track, rendering::PhraseSegment{.regionId = fixture.region}, 1U,
      rendering::RenderQuality::Preview, bankRoot, 48000U);
  if (!neutral) CHECK(neutral.error().code != core::ErrorCode::Unsupported);
}

struct CarrierFixture final {
  application::ProjectFactory factory{8200U};
  domain::RegionId regionId{};
  domain::TrackId trackId{};
  application::EditorSession session;

  explicit CarrierFixture(bool procedural) : session(makeProject(procedural)) {}

  domain::Project makeProject(bool procedural) {
    procedural_ = procedural;
    auto project = factory.createProject("Gender editing");
    trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", time::Tick{0}, time::Tick{3840});
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{3840}, 45U, U"あ",
                                          domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    if (procedural) {
      project.findVocalTrack(trackId)->proceduralRecipe = domain::ProceduralRecipeReference{
          .resource = {domain::SingerResourceKind::Procedural, "gender-channel", "1.0.0",
                       std::string(64U, 'a')},
          .path = "recipe.json",
          .style = "neutral"};
    }
    return project;
  }

 private:
  bool procedural_{false};
};

TEST_CASE("A gender nudge is a bipolar undoable edit on the singer that can take it") {
  CarrierFixture fixture{true};
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.setPlayheadTick(time::Tick{480});
  CHECK_NEAR(controller.genderAtPlayhead(), 0.0, 1e-6);
  CHECK(controller.nudgeGender(3).hasValue());
  // A committed command replaces the project, so the region has to be looked up again after every edit
  // instead of held.
  const auto region = [&fixture]() {
    return fixture.session.project().findRegion(fixture.regionId);
  };
  CHECK(region() != nullptr);
  if (region() == nullptr) return;
  CHECK(region()->genderAutomation.points().size() == 1U);
  CHECK(region()->genderAutomation.points().front().tick == time::Tick{480});
  CHECK_NEAR(region()->genderAutomation.points().front().amount, 0.3, 1e-6);

  // Both directions are ordinary edits, and the bound is the channel's own.
  CHECK(controller.nudgeGender(-5).hasValue());
  CHECK(region()->genderAutomation.points().size() == 1U);
  CHECK_NEAR(region()->genderAutomation.points().front().amount, -0.2, 1e-6);
  CHECK(controller.nudgeGender(-100).hasValue());
  CHECK_NEAR(region()->genderAutomation.points().front().amount, -domain::kMaximumGender, 1e-6);
  CHECK(controller.nudgeGender(100).hasValue());
  CHECK_NEAR(region()->genderAutomation.points().front().amount, domain::kMaximumGender, 1e-6);

  // A nudge that lands back on neutral removes the point, and every step so far is undoable in order.
  CHECK(controller.nudgeGender(0).hasValue());
  CHECK(controller.nudgeGender(-10).hasValue());
  // Ten tenths down from the channel's maximum is the neutral value exactly, not a hundred-millionth away
  // from it: a residual point here would be a stored curve that no nudge can clear.
  CHECK_NEAR(controller.genderAtPlayhead(), 0.0, 1e-9);
  CHECK(region()->genderAutomation.points().empty());
  CHECK(fixture.session.undo());
  CHECK_NEAR(fixture.session.project().findRegion(fixture.regionId)
                 ->genderAutomation.points().front().amount,
             domain::kMaximumGender, 1e-6);
  CHECK(fixture.session.undo());
  CHECK_NEAR(fixture.session.project().findRegion(fixture.regionId)
                 ->genderAutomation.points().front().amount,
             -domain::kMaximumGender, 1e-6);

  // Resetting is a real edit too.
  CHECK(controller.resetGenderCurve().hasValue());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->genderAutomation.points().empty());
  CHECK(fixture.session.undo());
  CHECK(!fixture.session.project().findRegion(fixture.regionId)->genderAutomation.points().empty());
}

TEST_CASE("A singer without both halves refuses the nudge and keeps the stored curve") {
  CarrierFixture fixture{false};
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.setPlayheadTick(time::Tick{960});
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->genderAutomation.upsert({time::Tick{0}, -0.5F}).hasValue());
  const auto refused = controller.nudgeGender(1);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("gender") != std::string::npos);
  CHECK(refused.error().message.find("source-filter") != std::string::npos);
  CHECK(region->genderAutomation.points().size() == 1U);
  CHECK_NEAR(controller.genderAtPlayhead(), -0.5, 1e-6);

  CHECK(controller.resetGenderCurve().hasValue());
  CHECK(region->genderAutomation.points().empty());
}

TEST_CASE("Gender is the two halves composed, and neither half alone is gender") {
  const auto fixture = makeVowelProject();
  const auto plain = renderVowel(fixture, fixture.project);
  const auto neutral = renderVowel(fixture, withGender(fixture.project, fixture.region, 0.0F));
  const auto up = renderVowel(fixture, withGender(fixture.project, fixture.region, 1.0F));
  const auto down = renderVowel(fixture, withGender(fixture.project, fixture.region, -1.0F));
  const auto tractOnly = renderVowel(
      fixture, withFormant(fixture.project, fixture.region, domain::kGenderFormantSemitones));
  const auto sourceOnly = renderVowel(
      fixture,
      withTension(fixture.project, fixture.region,
                  domain::kGenderTiltDbPerOctave / domain::kTensionTiltDbPerOctave));

  // The exact claim, checked exactly: gender up is the tract half and the source half applied together,
  // and the render where the two neighbouring channels are given those same amounts is bit-identical.
  // This is what "coupled" means, and it cannot be satisfied by a channel that moves one half twice.
  auto composed = withFormant(fixture.project, fixture.region, domain::kGenderFormantSemitones);
  composed = withTension(std::move(composed), fixture.region,
                         domain::kGenderTiltDbPerOctave / domain::kTensionTiltDbPerOctave);
  CHECK(up == renderVowel(fixture, composed));

  // Neither neighbour alone is gender, and a neutral curve is the source that had no curve at all.
  CHECK(neutral == plain);
  CHECK(up != plain);
  CHECK(down != plain);
  CHECK(up != down);
  CHECK(up != tractOnly);
  CHECK(up != sourceOnly);

  // The tract half is real, symmetric, and the same tract movement the formant channel makes at that
  // amount -- so gender is not quietly inventing a different tract mapping.
  const auto plainFormant = thirdFormantHz(analyseWindow(plain));
  const auto upFormant = thirdFormantHz(analyseWindow(up));
  const auto downFormant = thirdFormantHz(analyseWindow(down));
  const auto tractFormant = thirdFormantHz(analyseWindow(tractOnly));
  const auto sourceFormant = thirdFormantHz(analyseWindow(sourceOnly));
  CHECK(upFormant > plainFormant);
  CHECK(downFormant < plainFormant);
  CHECK_NEAR(tractFormant, upFormant, plainFormant * 0.03);
  // The source-only render leaves the tract where it was, which is why it cannot be gender either.
  CHECK_NEAR(sourceFormant, plainFormant, plainFormant * 0.03);

  // And it is not a pitch control.
  const auto plainWindow = analyseWindow(plain);
  const auto upWindow = analyseWindow(up);
  const auto plainEvidence = periodicity(plainWindow);
  const auto upEvidence = periodicity(upWindow);
  CHECK_NEAR(plainEvidence.fundamentalHz, kFundamentalHz, 40.0);
  CHECK_NEAR(upEvidence.fundamentalHz, plainEvidence.fundamentalHz, 2.0);
  CHECK(upEvidence.coherence > 0.8);
  CHECK(rootMeanSquare(upWindow) > rootMeanSquare(plainWindow) * 0.5);
  CHECK(rootMeanSquare(upWindow) < rootMeanSquare(plainWindow) * 2.0);
}

}  // namespace
