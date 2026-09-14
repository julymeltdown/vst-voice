// The airiness channel adds a high-frequency noise band, and it is not breathiness.
//
// The two channels are adjacent in the product and easy to conflate in the code, so this file measures
// the difference rather than asserting it: breathiness moves the periodic/aperiodic balance in the band
// the voice already occupies and costs periodicity, while airiness adds the part of the same noise stream
// that the aspiration filter rejected, which is a band change that leaves the source clearly voiced.
//
// Persistence, capability refusal and the editing surface are checked exactly as they are for the
// channels beside this one. The voice is a procedural fixture, not a singer: the oracle measures the
// render, and no claim is made about how an airy vowel sounds to a listener.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/domain/airiness_automation.hpp"
#include "seam/domain/breathiness_automation.hpp"
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

voice_design::VoiceRecipe vowelRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "airiness-channel";
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
  application::ProjectFactory factory{7900U};
  auto project = factory.createProject("Airiness channel");
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

domain::Project withAiriness(domain::Project project, domain::RegionId region, float amount) {
  auto* target = project.findRegion(region);
  CHECK(target != nullptr);
  const auto inserted =
      target->airinessAutomation.upsert(domain::AirinessAutomationPoint{time::Tick{0}, amount});
  CHECK(inserted.hasValue());
  const auto valid = project.validate();
  CHECK(valid.hasValue());
  return project;
}

domain::Project withBreathiness(domain::Project project, domain::RegionId region, float amount) {
  auto* target = project.findRegion(region);
  CHECK(target != nullptr);
  const auto inserted =
      target->breathinessAutomation.upsert(domain::BreathinessAutomationPoint{time::Tick{0}, amount});
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

double rootMeanSquare(std::span<const float> window) {
  double total = 0.0;
  for (const auto sample : window) total += static_cast<double>(sample) * static_cast<double>(sample);
  return std::sqrt(total / static_cast<double>(window.size()));
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

// A broadband band measurement. The added air is noise rather than a harmonic, so the band has no narrow
// peaks for a grid to fall between, and the grid can be coarse.
double bandEnergy(std::span<const float> window, double fromHz, double toHz, double stepHz) {
  double total = 0.0;
  for (double frequency = fromHz; frequency <= toHz; frequency += stepHz) {
    const auto magnitude = magnitudeAt(window, frequency);
    total += magnitude * magnitude;
  }
  return total;
}

// Air against the band the voice already occupies: what changed is where the energy is, not how much.
double airRatio(std::span<const float> window) {
  const auto low = bandEnergy(window, 1000.0, 4000.0, 250.0);
  const auto high = bandEnergy(window, 7000.0, 16000.0, 250.0);
  return low > 0.0 ? high / low : 0.0;
}

struct Periodicity final {
  double fundamentalHz{0.0};
  double coherence{0.0};
};

Periodicity periodicity(std::span<const float> window, double minimumHz, double maximumHz) {
  const auto minimumLag = static_cast<std::size_t>(kSampleRate / maximumHz);
  const auto maximumLag = std::min<std::size_t>(
      static_cast<std::size_t>(kSampleRate / minimumHz), window.size() / 2U);
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

TEST_CASE("An airiness curve is bounded, ordered and interpolated") {
  domain::AirinessAutomation curve;
  CHECK(curve.points().empty());
  CHECK(curve.valueAt(time::Tick{0}) == 0.0F);
  CHECK(curve.upsert({time::Tick{0}, 0.0F}).hasValue());
  CHECK(curve.upsert({time::Tick{960}, 0.9F}).hasValue());
  CHECK_NEAR(curve.valueAt(time::Tick{480}), 0.45, 1e-6);
  CHECK_NEAR(curve.valueAt(time::Tick{-10}), 0.0, 1e-6);
  CHECK_NEAR(curve.valueAt(time::Tick{4000}), 0.9, 1e-6);
  CHECK(!curve.upsert({time::Tick{960}, 1.4F}).hasValue());
  CHECK(!curve.upsert({time::Tick{960}, -0.3F}).hasValue());
  CHECK(!curve.upsert({time::Tick{960}, std::numeric_limits<float>::quiet_NaN()}).hasValue());
  CHECK(!curve.replacePoints({{time::Tick{960}, 0.2F}, {time::Tick{480}, 0.4F}}).hasValue());
  CHECK(curve.erase(time::Tick{960}));
  CHECK(!curve.erase(time::Tick{960}));
  CHECK(curve.points().size() == 1U);
}

TEST_CASE("An airiness curve survives a save and older documents still load") {
  const auto fixture = makeVowelProject();
  const auto project = withAiriness(fixture.project, fixture.region, 0.9F);
  formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(project);
  CHECK(encoded.hasValue());
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded.hasValue());
  if (!decoded) return;
  const auto* region = decoded.value().findRegion(fixture.region);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->airinessAutomation.points().size() == 1U);
  CHECK_NEAR(region->airinessAutomation.points().front().amount, 0.9, 1e-6);

  // A schema-14 document predates the channel and means an empty curve, not a refusal.
  auto older = formats::parseJson(encoded.value());
  CHECK(older.hasValue());
  if (!older) return;
  auto tree = older.value();
  tree.asObject()["schemaVersion"] = formats::JsonValue{std::int64_t{14}};
  auto* tracks = tree.find("vocalTracks");
  CHECK(tracks != nullptr && tracks->isArray());
  auto& regions = tracks->asArray().front().asObject()["regions"].asArray();
  regions.front().asObject().erase("airinessAutomation");
  const auto reloaded = codec.decode(formats::stringifyJson(tree));
  CHECK(reloaded.hasValue());
  if (!reloaded) return;
  const auto* olderRegion = reloaded.value().findRegion(fixture.region);
  CHECK(olderRegion != nullptr);
  if (olderRegion != nullptr) CHECK(olderRegion->airinessAutomation.points().empty());

  // A schema-15 document whose curve leaves the supported range is a parse error, not a clamped value.
  auto outOfRange = tree;
  outOfRange.asObject()["schemaVersion"] = formats::JsonValue{std::int64_t{15}};
  auto* outOfRangeTracks = outOfRange.find("vocalTracks");
  CHECK(outOfRangeTracks != nullptr);
  auto& outOfRangeRegions = outOfRangeTracks->asArray().front().asObject()["regions"].asArray();
  outOfRangeRegions.front().asObject()["airinessAutomation"] = formats::JsonValue{
      formats::JsonValue::Array{formats::JsonValue{formats::JsonValue::Object{
          {"tick", formats::JsonValue{std::int64_t{0}}},
          {"amount", formats::JsonValue{2.0}}}}}};
  const auto rejected = codec.decode(formats::stringifyJson(outOfRange));
  CHECK(!rejected.hasValue());
  CHECK(rejected.error().code == core::ErrorCode::ParseError);
}

TEST_CASE("Only the carrier that owns the noise band advertises the airiness channel") {
  const auto sourceFilter = synthesis::rendererCapabilities(synthesis::RendererCarrier::SourceFilter);
  CHECK(sourceFilter.supports(synthesis::RendererControl::Airiness));
  CHECK(sourceFilter.supports(synthesis::RendererControl::Breathiness));
  const auto bank = synthesis::rendererCapabilities(synthesis::RendererCarrier::SampleBank);
  CHECK(!bank.supports(synthesis::RendererControl::Airiness));
  CHECK(bank.supports(synthesis::RendererControl::Dynamics));

  synthesis::RendererControlRequest request;
  request.require(synthesis::RendererControl::Airiness);
  CHECK(synthesis::validateRendererCapabilities(
            synthesis::RendererCarrier::SourceFilter, request).hasValue());
  const auto refused = synthesis::validateRendererCapabilities(
      synthesis::RendererCarrier::SampleBank, request);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("airiness") != std::string::npos);
}

TEST_CASE("A bank that has no noise band refuses the curve instead of dropping it") {
  const auto fixture = makeVowelProject();
  auto project = withAiriness(fixture.project, fixture.region, 0.9F);
  const auto manifest = test::support::makeManifest({test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 45, voicebank::UnitKind::Sustain)});
  const auto bankRoot = test::support::temporaryDirectory("airiness-bank-refusal");
  std::filesystem::create_directories(bankRoot / "audio");
  auto refused = rendering::RenderSnapshotFactory{}.create(
      project, manifest, fixture.track, rendering::PhraseSegment{.regionId = fixture.region}, 1U,
      rendering::RenderQuality::Preview, bankRoot, 48000U);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("airiness") != std::string::npos);

  project = withAiriness(fixture.project, fixture.region, 0.0F);
  auto neutral = rendering::RenderSnapshotFactory{}.create(
      project, manifest, fixture.track, rendering::PhraseSegment{.regionId = fixture.region}, 1U,
      rendering::RenderQuality::Preview, bankRoot, 48000U);
  if (!neutral) CHECK(neutral.error().code != core::ErrorCode::Unsupported);
}

struct CarrierFixture final {
  application::ProjectFactory factory{8000U};
  domain::RegionId regionId{};
  domain::TrackId trackId{};
  application::EditorSession session;

  explicit CarrierFixture(bool procedural) : session(makeProject(procedural)) {}

  domain::Project makeProject(bool procedural) {
    procedural_ = procedural;
    auto project = factory.createProject("Airiness editing");
    trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", time::Tick{0}, time::Tick{3840});
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{3840}, 45U, U"あ",
                                          domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    if (procedural) {
      project.findVocalTrack(trackId)->proceduralRecipe = domain::ProceduralRecipeReference{
          .resource = {domain::SingerResourceKind::Procedural, "airiness-channel", "1.0.0",
                       std::string(64U, 'a')},
          .path = "recipe.json",
          .style = "neutral"};
    }
    return project;
  }

 private:
  bool procedural_{false};
};

TEST_CASE("An airiness nudge is an undoable edit on the singer that can take it") {
  CarrierFixture fixture{true};
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.setPlayheadTick(time::Tick{480});
  CHECK_NEAR(controller.airinessAtPlayhead(), 0.0, 1e-6);
  CHECK(controller.nudgeAiriness(4).hasValue());
  const auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->airinessAutomation.points().size() == 1U);
  CHECK(region->airinessAutomation.points().front().tick == time::Tick{480});
  CHECK_NEAR(region->airinessAutomation.points().front().amount, 0.4, 1e-6);
  CHECK_NEAR(controller.airinessAtPlayhead(), 0.4, 1e-6);

  CHECK(controller.nudgeAiriness(2).hasValue());
  CHECK(region->airinessAutomation.points().size() == 1U);
  CHECK_NEAR(region->airinessAutomation.points().front().amount, 0.6, 1e-6);

  CHECK(controller.nudgeAiriness(100).hasValue());
  CHECK_NEAR(region->airinessAutomation.points().front().amount, domain::kMaximumAiriness, 1e-6);

  CHECK(fixture.session.undo());
  CHECK_NEAR(fixture.session.project().findRegion(fixture.regionId)
                 ->airinessAutomation.points().front().amount,
             0.6, 1e-6);
  CHECK(fixture.session.undo());
  CHECK_NEAR(fixture.session.project().findRegion(fixture.regionId)
                 ->airinessAutomation.points().front().amount,
             0.4, 1e-6);
  CHECK(fixture.session.undo());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->airinessAutomation.points().empty());

  CHECK(controller.nudgeAiriness(1).hasValue());
  CHECK(!fixture.session.project().findRegion(fixture.regionId)->airinessAutomation.points().empty());
  CHECK(controller.resetAirinessCurve().hasValue());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->airinessAutomation.points().empty());
  CHECK(fixture.session.undo());
  CHECK(!fixture.session.project().findRegion(fixture.regionId)->airinessAutomation.points().empty());

  // Ten steps up and ten steps back down land on the neutral value exactly, so the curve is cleared
  // rather than left holding a point that is neutral to seven decimal places.
  CHECK(controller.resetAirinessCurve().hasValue());
  CHECK(controller.nudgeAiriness(10).hasValue());
  CHECK(controller.nudgeAiriness(-10).hasValue());
  CHECK_NEAR(controller.airinessAtPlayhead(), 0.0, 1e-9);
  CHECK(fixture.session.project().findRegion(fixture.regionId)->airinessAutomation.points().empty());
}

TEST_CASE("A singer without a noise band refuses the nudge and keeps the stored curve") {
  CarrierFixture fixture{false};
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.setPlayheadTick(time::Tick{960});
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->airinessAutomation.upsert({time::Tick{0}, 0.6F}).hasValue());
  const auto refused = controller.nudgeAiriness(1);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("airiness") != std::string::npos);
  CHECK(refused.error().message.find("source-filter") != std::string::npos);
  CHECK(region->airinessAutomation.points().size() == 1U);
  CHECK_NEAR(controller.airinessAtPlayhead(), 0.6, 1e-6);

  CHECK(controller.resetAirinessCurve().hasValue());
  CHECK(region->airinessAutomation.points().empty());
}

TEST_CASE("Airiness adds a high band and keeps the source voiced, unlike breathiness") {
  const auto fixture = makeVowelProject();
  const auto plain = renderVowel(fixture, fixture.project);
  const auto neutral = renderVowel(fixture, withAiriness(fixture.project, fixture.region, 0.0F));
  const auto airy = renderVowel(fixture, withAiriness(fixture.project, fixture.region, 1.0F));
  const auto breathy = renderVowel(fixture, withBreathiness(fixture.project, fixture.region, 1.0F));
  CHECK(!plain.empty());
  CHECK(airy.size() == plain.size());
  // A curve that asks for nothing is exactly the render that had no curve at all.
  CHECK(neutral == plain);
  CHECK(airy != plain);

  const auto plainWindow = analyseWindow(plain);
  const auto airyWindow = analyseWindow(airy);
  const auto breathyWindow = analyseWindow(breathy);
  const auto plainRatio = airRatio(plainWindow);
  const auto airyRatio = airRatio(airyWindow);
  // Air is a band: the high-frequency energy rises against the band the voice already occupies.
  CHECK(airyRatio > plainRatio * 1.5);

  // Airiness is not breathiness. Both change the aperiodic content of the source, and the difference that
  // matters is that an airy source is still clearly voiced while a maximally breathy one is not.
  const auto plainEvidence = periodicity(plainWindow, 60.0, 400.0);
  const auto airyEvidence = periodicity(airyWindow, 60.0, 400.0);
  const auto breathyEvidence = periodicity(breathyWindow, 60.0, 400.0);
  CHECK(airyEvidence.coherence > 0.9);
  CHECK(airyEvidence.coherence > breathyEvidence.coherence + 0.1);
  // Neither channel moves the melody.
  CHECK_NEAR(plainEvidence.fundamentalHz, 110.0, 40.0);
  CHECK_NEAR(airyEvidence.fundamentalHz, plainEvidence.fundamentalHz, 2.0);

  // Air is added, not balanced, so it does raise the level; the rise stays small enough that the channel
  // cannot stand in for a gain.
  const auto plainLevel = rootMeanSquare(plainWindow);
  const auto airyLevel = rootMeanSquare(airyWindow);
  CHECK(airyLevel > plainLevel);
  CHECK(airyLevel < plainLevel * 1.5);
}

}  // namespace
