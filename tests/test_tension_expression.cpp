// The tension channel changes the source's own spectrum, and nothing else.
//
// Tension is the channel that is easiest to fake with a gain, so the measurement here is deliberately
// level-matched: both renders are normalised to the same level before their spectra are compared, and
// the tense render still has to be brighter. That is what "effort" means for a source-filter voice: the
// same note, the same fundamental, the same loudness, a different production.
//
// Persistence, capability refusal and the editing surface are checked exactly as they are for the
// channels beside this one, because those obligations do not change from channel to channel. The voice
// is a procedural fixture, not a singer: the oracle measures the render, and no claim is made about how
// a tense vowel sounds to a listener.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/domain/tension_automation.hpp"
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
  recipe.id = "tension-channel";
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
  application::ProjectFactory factory{7700U};
  auto project = factory.createProject("Tension channel");
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

double rootMeanSquare(std::span<const float> window) {
  double total = 0.0;
  for (const auto sample : window) total += static_cast<double>(sample) * static_cast<double>(sample);
  return std::sqrt(total / static_cast<double>(window.size()));
}

// Level matching is the whole point of this oracle: a gain difference must not be able to pass as
// effort, so both windows are measured at the same root-mean-square before anything is compared.
std::vector<double> levelMatched(std::span<const float> window, double target) {
  std::vector<double> result(window.size());
  const auto level = rootMeanSquare(window);
  CHECK(level > 0.0);
  const auto scale = level > 0.0 ? target / level : 0.0;
  for (std::size_t index = 0U; index < window.size(); ++index) {
    result[index] = static_cast<double>(window[index]) * scale;
  }
  return result;
}

double windowed(std::span<const double> window, std::size_t index) {
  const auto position = static_cast<double>(index) /
                        static_cast<double>(window.size() - 1U);
  return window[index] * 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * position));
}

double magnitudeAt(std::span<const double> window, double frequencyHz) {
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

// The harmonics of the score's own fundamental, with a narrow search around each expected position: a
// fixed grid lands between peaks, which makes a band ratio depend on the grid rather than on the source.
double harmonicBandEnergy(std::span<const double> window, double fundamentalHz,
                          int firstHarmonic, int lastHarmonic) {
  double total = 0.0;
  for (auto harmonic = firstHarmonic; harmonic <= lastHarmonic; ++harmonic) {
    const auto centre = fundamentalHz * static_cast<double>(harmonic);
    if (centre >= kSampleRate * 0.45) break;
    double peak = 0.0;
    for (auto offset = -25.0; offset <= 25.0; offset += 5.0) {
      peak = std::max(peak, magnitudeAt(window, centre + offset));
    }
    total += peak * peak;
  }
  return total;
}

// The source's measured tilt: energy above 2 kHz against energy around the first formant.
double measuredTilt(std::span<const double> window, double fundamentalHz) {
  const auto low = harmonicBandEnergy(window, fundamentalHz, 3, 10);
  const auto high = harmonicBandEnergy(window, fundamentalHz, 19, 54);
  return low > 0.0 ? high / low : 0.0;
}

// Normalised autocorrelation peak: how periodic the window is and where its period sits. It is asked to
// tell whether the fundamental moved, not to certify a pitch.
struct Periodicity final {
  double fundamentalHz{0.0};
  double coherence{0.0};
};

Periodicity periodicity(std::span<const double> window, double minimumHz, double maximumHz) {
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
      correlation += window[index] * window[index + lag];
      energy += window[index] * window[index];
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

TEST_CASE("A tension curve is bounded, ordered and interpolated") {
  domain::TensionAutomation curve;
  CHECK(curve.points().empty());
  CHECK(curve.valueAt(time::Tick{0}) == 0.0F);
  CHECK(curve.upsert({time::Tick{0}, 0.0F}).hasValue());
  CHECK(curve.upsert({time::Tick{960}, 0.8F}).hasValue());
  CHECK_NEAR(curve.valueAt(time::Tick{480}), 0.4, 1e-6);
  CHECK_NEAR(curve.valueAt(time::Tick{-10}), 0.0, 1e-6);
  CHECK_NEAR(curve.valueAt(time::Tick{4000}), 0.8, 1e-6);
  CHECK(!curve.upsert({time::Tick{960}, 1.2F}).hasValue());
  CHECK(!curve.upsert({time::Tick{960}, -0.2F}).hasValue());
  CHECK(!curve.upsert({time::Tick{960}, std::numeric_limits<float>::infinity()}).hasValue());
  CHECK(!curve.replacePoints({{time::Tick{960}, 0.2F}, {time::Tick{480}, 0.4F}}).hasValue());
  CHECK(curve.erase(time::Tick{960}));
  CHECK(!curve.erase(time::Tick{960}));
  CHECK(curve.points().size() == 1U);
}

TEST_CASE("A tension curve survives a save and older documents still load") {
  const auto fixture = makeVowelProject();
  const auto project = withTension(fixture.project, fixture.region, 0.8F);
  formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(project);
  CHECK(encoded.hasValue());
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded.hasValue());
  if (!decoded) return;
  const auto* region = decoded.value().findRegion(fixture.region);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->tensionAutomation.points().size() == 1U);
  CHECK_NEAR(region->tensionAutomation.points().front().amount, 0.8, 1e-6);

  // A schema-13 document predates the channel and means an empty curve, not a refusal.
  auto older = formats::parseJson(encoded.value());
  CHECK(older.hasValue());
  if (!older) return;
  auto tree = older.value();
  tree.asObject()["schemaVersion"] = formats::JsonValue{std::int64_t{13}};
  auto* tracks = tree.find("vocalTracks");
  CHECK(tracks != nullptr && tracks->isArray());
  auto& regions = tracks->asArray().front().asObject()["regions"].asArray();
  regions.front().asObject().erase("tensionAutomation");
  const auto reloaded = codec.decode(formats::stringifyJson(tree));
  CHECK(reloaded.hasValue());
  if (!reloaded) return;
  const auto* olderRegion = reloaded.value().findRegion(fixture.region);
  CHECK(olderRegion != nullptr);
  if (olderRegion != nullptr) CHECK(olderRegion->tensionAutomation.points().empty());

  // A schema-14 document whose curve leaves the supported range is a parse error, not a clamped value.
  auto outOfRange = tree;
  outOfRange.asObject()["schemaVersion"] = formats::JsonValue{std::int64_t{14}};
  auto* outOfRangeTracks = outOfRange.find("vocalTracks");
  CHECK(outOfRangeTracks != nullptr);
  auto& outOfRangeRegions = outOfRangeTracks->asArray().front().asObject()["regions"].asArray();
  outOfRangeRegions.front().asObject()["tensionAutomation"] = formats::JsonValue{
      formats::JsonValue::Array{formats::JsonValue{formats::JsonValue::Object{
          {"tick", formats::JsonValue{std::int64_t{0}}},
          {"amount", formats::JsonValue{-0.5}}}}}};
  const auto rejected = codec.decode(formats::stringifyJson(outOfRange));
  CHECK(!rejected.hasValue());
  CHECK(rejected.error().code == core::ErrorCode::ParseError);
}

TEST_CASE("Only the carrier that generates the harmonic source advertises the tension channel") {
  const auto sourceFilter = synthesis::rendererCapabilities(synthesis::RendererCarrier::SourceFilter);
  CHECK(sourceFilter.supports(synthesis::RendererControl::Tension));
  CHECK(sourceFilter.supports(synthesis::RendererControl::Breathiness));
  const auto bank = synthesis::rendererCapabilities(synthesis::RendererCarrier::SampleBank);
  CHECK(!bank.supports(synthesis::RendererControl::Tension));
  CHECK(bank.supports(synthesis::RendererControl::Dynamics));

  synthesis::RendererControlRequest request;
  request.require(synthesis::RendererControl::Tension);
  CHECK(synthesis::validateRendererCapabilities(
            synthesis::RendererCarrier::SourceFilter, request).hasValue());
  const auto refused = synthesis::validateRendererCapabilities(
      synthesis::RendererCarrier::SampleBank, request);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("tension") != std::string::npos);
}

TEST_CASE("A bank that has no harmonic source refuses the curve instead of dropping it") {
  const auto fixture = makeVowelProject();
  auto project = withTension(fixture.project, fixture.region, 0.8F);
  const auto manifest = test::support::makeManifest({test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 45, voicebank::UnitKind::Sustain)});
  const auto bankRoot = test::support::temporaryDirectory("tension-bank-refusal");
  std::filesystem::create_directories(bankRoot / "audio");
  auto refused = rendering::RenderSnapshotFactory{}.create(
      project, manifest, fixture.track, rendering::PhraseSegment{.regionId = fixture.region}, 1U,
      rendering::RenderQuality::Preview, bankRoot, 48000U);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("tension") != std::string::npos);

  project = withTension(fixture.project, fixture.region, 0.0F);
  auto neutral = rendering::RenderSnapshotFactory{}.create(
      project, manifest, fixture.track, rendering::PhraseSegment{.regionId = fixture.region}, 1U,
      rendering::RenderQuality::Preview, bankRoot, 48000U);
  if (!neutral) CHECK(neutral.error().code != core::ErrorCode::Unsupported);
}

struct CarrierFixture final {
  application::ProjectFactory factory{7800U};
  domain::RegionId regionId{};
  domain::TrackId trackId{};
  application::EditorSession session;

  explicit CarrierFixture(bool procedural) : session(makeProject(procedural)) {}

  domain::Project makeProject(bool procedural) {
    procedural_ = procedural;
    auto project = factory.createProject("Tension editing");
    trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", time::Tick{0}, time::Tick{3840});
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{3840}, 45U, U"あ",
                                          domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    if (procedural) {
      project.findVocalTrack(trackId)->proceduralRecipe = domain::ProceduralRecipeReference{
          .resource = {domain::SingerResourceKind::Procedural, "tension-channel", "1.0.0",
                       std::string(64U, 'a')},
          .path = "recipe.json",
          .style = "neutral"};
    }
    return project;
  }

 private:
  bool procedural_{false};
};

TEST_CASE("A tension nudge is an undoable edit on the singer that can take it") {
  CarrierFixture fixture{true};
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.setPlayheadTick(time::Tick{480});
  CHECK_NEAR(controller.tensionAtPlayhead(), 0.0, 1e-6);
  CHECK(controller.nudgeTension(3).hasValue());
  const auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->tensionAutomation.points().size() == 1U);
  CHECK(region->tensionAutomation.points().front().tick == time::Tick{480});
  CHECK_NEAR(region->tensionAutomation.points().front().amount, 0.3, 1e-6);
  CHECK_NEAR(controller.tensionAtPlayhead(), 0.3, 1e-6);

  CHECK(controller.nudgeTension(2).hasValue());
  CHECK(region->tensionAutomation.points().size() == 1U);
  CHECK_NEAR(region->tensionAutomation.points().front().amount, 0.5, 1e-6);

  CHECK(controller.nudgeTension(100).hasValue());
  CHECK_NEAR(region->tensionAutomation.points().front().amount, domain::kMaximumTension, 1e-6);

  CHECK(fixture.session.undo());
  CHECK_NEAR(fixture.session.project().findRegion(fixture.regionId)
                 ->tensionAutomation.points().front().amount,
             0.5, 1e-6);
  CHECK(fixture.session.undo());
  CHECK_NEAR(fixture.session.project().findRegion(fixture.regionId)
                 ->tensionAutomation.points().front().amount,
             0.3, 1e-6);
  CHECK(fixture.session.undo());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->tensionAutomation.points().empty());

  CHECK(controller.nudgeTension(1).hasValue());
  CHECK(!fixture.session.project().findRegion(fixture.regionId)->tensionAutomation.points().empty());
  CHECK(controller.resetTensionCurve().hasValue());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->tensionAutomation.points().empty());
  CHECK(fixture.session.undo());
  CHECK(!fixture.session.project().findRegion(fixture.regionId)->tensionAutomation.points().empty());

  // A downward nudge at the neutral floor is not an edit, so it must not fill the undo history.
  CHECK(controller.resetTensionCurve().hasValue());
  CHECK(controller.nudgeTension(-1).hasValue());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->tensionAutomation.points().empty());
}

TEST_CASE("A singer without a harmonic source refuses the nudge and keeps the stored curve") {
  CarrierFixture fixture{false};
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.setPlayheadTick(time::Tick{960});
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->tensionAutomation.upsert({time::Tick{0}, 0.5F}).hasValue());
  const auto refused = controller.nudgeTension(1);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("tension") != std::string::npos);
  CHECK(refused.error().message.find("source-filter") != std::string::npos);
  CHECK(region->tensionAutomation.points().size() == 1U);
  CHECK_NEAR(controller.tensionAtPlayhead(), 0.5, 1e-6);

  CHECK(controller.resetTensionCurve().hasValue());
  CHECK(region->tensionAutomation.points().empty());
}

TEST_CASE("Tension brightens the source at the same level and leaves the melody alone") {
  const auto fixture = makeVowelProject();
  const auto plain = renderVowel(fixture, fixture.project);
  const auto neutral = renderVowel(fixture, withTension(fixture.project, fixture.region, 0.0F));
  const auto half = renderVowel(fixture, withTension(fixture.project, fixture.region, 0.5F));
  const auto tense = renderVowel(fixture, withTension(fixture.project, fixture.region, 1.0F));
  CHECK(!plain.empty());
  CHECK(tense.size() == plain.size());
  // A curve that asks for nothing is exactly the render that had no curve at all.
  CHECK(neutral == plain);
  CHECK(tense != plain);

  const auto plainWindow = analyseWindow(plain);
  const auto tenseWindow = analyseWindow(tense);
  const auto halfWindow = analyseWindow(half);
  const auto level = rootMeanSquare(plainWindow);
  // Everything below compares renders that carry the same level, so a gain change cannot pass as effort.
  const auto plainMatched = levelMatched(plainWindow, level);
  const auto halfMatched = levelMatched(halfWindow, level);
  const auto tenseMatched = levelMatched(tenseWindow, level);
  const auto plainTilt = measuredTilt(plainMatched, 110.0);
  const auto halfTilt = measuredTilt(halfMatched, 110.0);
  const auto tenseTilt = measuredTilt(tenseMatched, 110.0);
  // The source moved its own spectrum upward, and it kept moving as the curve rose.
  CHECK(halfTilt > plainTilt * 1.2);
  CHECK(tenseTilt > halfTilt);
  CHECK(tenseTilt > plainTilt * 2.0);

  // Effort is not a louder voice and not a different note: the fundamental of the level-matched render
  // stays where the score put it, and the source stays periodic.
  const auto plainEvidence = periodicity(plainMatched, 60.0, 400.0);
  const auto tenseEvidence = periodicity(tenseMatched, 60.0, 400.0);
  CHECK_NEAR(plainEvidence.fundamentalHz, 110.0, 40.0);
  CHECK_NEAR(tenseEvidence.fundamentalHz, plainEvidence.fundamentalHz, 2.0);
  CHECK(tenseEvidence.coherence > 0.8);

  // The channel changes the spectrum, so it does change the level it produces; that is why the render is
  // compared after matching rather than by asserting that the level is untouched.
  const auto tenseLevel = rootMeanSquare(tenseWindow);
  CHECK(tenseLevel > level * 0.5);
  CHECK(tenseLevel < level * 2.0);
}

}  // namespace
