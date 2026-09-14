// Growl is roughness, and the interesting part of it is what it cannot do.
//
// The other channels are defined by a change they make; this one is also defined by a bound. The
// contract asks that it stay finite and bounded, so the oracle measures three things: the subharmonic at
// half the note's fundamental that the plain render does not have, the period doubling that follows from
// it, and the fact that no sample leaves the renderer's own safety bound at the channel's maximum. A
// roughness that clipped or grew would pass a "does it sound different" check and fail this one.
//
// A half-rate accumulator follows the note frequency, while the modulation depth bounds its gain.
// Persistence, capability refusal and the editing
// surface are checked exactly as they are for the channels beside this one. The voice is a procedural
// fixture, not a singer, and no claim is made about how a growl sounds to a listener.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/domain/growl_automation.hpp"
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
constexpr double kSubharmonicHz = kFundamentalHz / 2.0;

voice_design::VoiceRecipe vowelRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "growl-channel";
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
  application::ProjectFactory factory{8300U};
  auto project = factory.createProject("Growl channel");
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

domain::Project withGrowl(domain::Project project, domain::RegionId region, float amount) {
  auto* target = project.findRegion(region);
  CHECK(target != nullptr);
  const auto inserted =
      target->growlAutomation.upsert(domain::GrowlAutomationPoint{time::Tick{0}, amount});
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
  const auto count = std::min<std::size_t>(19200U, samples.size() - start);
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

// The narrow peak search around a frequency, so a measurement does not depend on landing exactly on it.
double peakNear(std::span<const float> window, double frequencyHz, double spanHz) {
  double peak = 0.0;
  for (auto offset = -spanHz; offset <= spanHz; offset += 1.0) {
    peak = std::max(peak, magnitudeAt(window, frequencyHz + offset));
  }
  return peak;
}

// Normalised autocorrelation at one lag: how much the signal repeats a fixed number of samples later.
double correlationAtLag(std::span<const float> window, double lag) {
  const auto whole = static_cast<std::size_t>(lag);
  double correlation = 0.0;
  double energy = 0.0;
  for (std::size_t index = 0U; index + whole < window.size(); ++index) {
    correlation += static_cast<double>(window[index]) * static_cast<double>(window[index + whole]);
    energy += static_cast<double>(window[index]) * static_cast<double>(window[index]);
  }
  return energy > 0.0 ? correlation / energy : 0.0;
}

double maximumMagnitude(std::span<const float> window) {
  double peak = 0.0;
  for (const auto sample : window) peak = std::max(peak, std::abs(static_cast<double>(sample)));
  return peak;
}

// Energy half a fundamental away from each harmonic. A subharmonic-rate modulation of the source puts the
// roughness there, and that is where the vocal tract still passes it: a plain subharmonic tone at half the
// fundamental is filtered away before it reaches the output.
double interHarmonicEnergy(std::span<const float> window) {
  double total = 0.0;
  for (auto harmonic = 3; harmonic <= 40; ++harmonic) {
    const auto centre = kFundamentalHz * static_cast<double>(harmonic) + kSubharmonicHz;
    if (centre >= kSampleRate * 0.4) break;
    const auto peak = peakNear(window, centre, 12.0);
    total += peak * peak;
  }
  return total;
}

double rootMeanSquare(std::span<const float> window) {
  double total = 0.0;
  for (const auto sample : window) total += static_cast<double>(sample) * static_cast<double>(sample);
  return std::sqrt(total / static_cast<double>(window.size()));
}

TEST_CASE("A growl curve is bounded, ordered and interpolated") {
  domain::GrowlAutomation curve;
  CHECK(curve.points().empty());
  CHECK(curve.valueAt(time::Tick{0}) == 0.0F);
  CHECK(curve.upsert({time::Tick{0}, 0.0F}).hasValue());
  CHECK(curve.upsert({time::Tick{960}, 0.7F}).hasValue());
  CHECK_NEAR(curve.valueAt(time::Tick{480}), 0.35, 1e-6);
  CHECK_NEAR(curve.valueAt(time::Tick{-10}), 0.0, 1e-6);
  CHECK_NEAR(curve.valueAt(time::Tick{4000}), 0.7, 1e-6);
  CHECK(!curve.upsert({time::Tick{960}, 1.3F}).hasValue());
  CHECK(!curve.upsert({time::Tick{960}, -0.4F}).hasValue());
  CHECK(!curve.upsert({time::Tick{960}, std::numeric_limits<float>::quiet_NaN()}).hasValue());
  CHECK(!curve.replacePoints({{time::Tick{960}, 0.2F}, {time::Tick{480}, 0.4F}}).hasValue());
  CHECK(curve.erase(time::Tick{960}));
  CHECK(!curve.erase(time::Tick{960}));
  CHECK(curve.points().size() == 1U);
}

TEST_CASE("A growl curve survives a save and older documents still load") {
  const auto fixture = makeVowelProject();
  const auto project = withGrowl(fixture.project, fixture.region, 0.7F);
  formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(project);
  CHECK(encoded.hasValue());
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded.hasValue());
  if (!decoded) return;
  const auto* region = decoded.value().findRegion(fixture.region);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->growlAutomation.points().size() == 1U);
  CHECK_NEAR(region->growlAutomation.points().front().amount, 0.7, 1e-6);

  // A schema-16 document predates the channel and means an empty curve, not a refusal.
  auto older = formats::parseJson(encoded.value());
  CHECK(older.hasValue());
  if (!older) return;
  auto tree = older.value();
  tree.asObject()["schemaVersion"] = formats::JsonValue{std::int64_t{16}};
  auto* tracks = tree.find("vocalTracks");
  CHECK(tracks != nullptr && tracks->isArray());
  auto& regions = tracks->asArray().front().asObject()["regions"].asArray();
  regions.front().asObject().erase("growlAutomation");
  const auto reloaded = codec.decode(formats::stringifyJson(tree));
  CHECK(reloaded.hasValue());
  if (!reloaded) return;
  const auto* olderRegion = reloaded.value().findRegion(fixture.region);
  CHECK(olderRegion != nullptr);
  if (olderRegion != nullptr) CHECK(olderRegion->growlAutomation.points().empty());

  // A schema-17 document whose curve leaves the supported range is a parse error, not a clamped value.
  auto outOfRange = tree;
  outOfRange.asObject()["schemaVersion"] = formats::JsonValue{std::int64_t{17}};
  auto* outOfRangeTracks = outOfRange.find("vocalTracks");
  CHECK(outOfRangeTracks != nullptr);
  auto& outOfRangeRegions = outOfRangeTracks->asArray().front().asObject()["regions"].asArray();
  outOfRangeRegions.front().asObject()["growlAutomation"] = formats::JsonValue{
      formats::JsonValue::Array{formats::JsonValue{formats::JsonValue::Object{
          {"tick", formats::JsonValue{std::int64_t{0}}},
          {"amount", formats::JsonValue{3.0}}}}}};
  const auto rejected = codec.decode(formats::stringifyJson(outOfRange));
  CHECK(!rejected.hasValue());
  CHECK(rejected.error().code == core::ErrorCode::ParseError);
}

TEST_CASE("Only the carrier that generates the excitation advertises the growl channel") {
  const auto sourceFilter = synthesis::rendererCapabilities(synthesis::RendererCarrier::SourceFilter);
  CHECK(sourceFilter.supports(synthesis::RendererControl::Growl));
  CHECK(sourceFilter.supports(synthesis::RendererControl::Breathiness));
  const auto bank = synthesis::rendererCapabilities(synthesis::RendererCarrier::SampleBank);
  CHECK(!bank.supports(synthesis::RendererControl::Growl));
  CHECK(bank.supports(synthesis::RendererControl::Dynamics));

  synthesis::RendererControlRequest request;
  request.require(synthesis::RendererControl::Growl);
  CHECK(synthesis::validateRendererCapabilities(
            synthesis::RendererCarrier::SourceFilter, request).hasValue());
  const auto refused = synthesis::validateRendererCapabilities(
      synthesis::RendererCarrier::SampleBank, request);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("growl") != std::string::npos);
}

TEST_CASE("A bank that has no excitation refuses the curve instead of dropping it") {
  const auto fixture = makeVowelProject();
  auto project = withGrowl(fixture.project, fixture.region, 0.7F);
  const auto manifest = test::support::makeManifest({test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 45, voicebank::UnitKind::Sustain)});
  const auto bankRoot = test::support::temporaryDirectory("growl-bank-refusal");
  std::filesystem::create_directories(bankRoot / "audio");
  auto refused = rendering::RenderSnapshotFactory{}.create(
      project, manifest, fixture.track, rendering::PhraseSegment{.regionId = fixture.region}, 1U,
      rendering::RenderQuality::Preview, bankRoot, 48000U);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("growl") != std::string::npos);

  project = withGrowl(fixture.project, fixture.region, 0.0F);
  auto neutral = rendering::RenderSnapshotFactory{}.create(
      project, manifest, fixture.track, rendering::PhraseSegment{.regionId = fixture.region}, 1U,
      rendering::RenderQuality::Preview, bankRoot, 48000U);
  if (!neutral) CHECK(neutral.error().code != core::ErrorCode::Unsupported);
}

struct CarrierFixture final {
  application::ProjectFactory factory{8400U};
  domain::RegionId regionId{};
  domain::TrackId trackId{};
  application::EditorSession session;

  explicit CarrierFixture(bool procedural) : session(makeProject(procedural)) {}

  domain::Project makeProject(bool procedural) {
    procedural_ = procedural;
    auto project = factory.createProject("Growl editing");
    trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", time::Tick{0}, time::Tick{3840});
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{3840}, 45U, U"あ",
                                          domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    if (procedural) {
      project.findVocalTrack(trackId)->proceduralRecipe = domain::ProceduralRecipeReference{
          .resource = {domain::SingerResourceKind::Procedural, "growl-channel", "1.0.0",
                       std::string(64U, 'a')},
          .path = "recipe.json",
          .style = "neutral"};
    }
    return project;
  }

 private:
  bool procedural_{false};
};

TEST_CASE("A growl nudge is an undoable edit on the singer that can take it") {
  CarrierFixture fixture{true};
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.setPlayheadTick(time::Tick{480});
  CHECK_NEAR(controller.growlAtPlayhead(), 0.0, 1e-6);
  CHECK(controller.nudgeGrowl(3).hasValue());
  const auto region = [&fixture]() {
    return fixture.session.project().findRegion(fixture.regionId);
  };
  CHECK(region() != nullptr);
  if (region() == nullptr) return;
  CHECK(region()->growlAutomation.points().size() == 1U);
  CHECK(region()->growlAutomation.points().front().tick == time::Tick{480});
  CHECK_NEAR(region()->growlAutomation.points().front().amount, 0.3, 1e-6);
  CHECK_NEAR(controller.growlAtPlayhead(), 0.3, 1e-6);

  CHECK(controller.nudgeGrowl(2).hasValue());
  CHECK(region()->growlAutomation.points().size() == 1U);
  CHECK_NEAR(region()->growlAutomation.points().front().amount, 0.5, 1e-6);
  CHECK(controller.nudgeGrowl(100).hasValue());
  CHECK_NEAR(region()->growlAutomation.points().front().amount, domain::kMaximumGrowl, 1e-6);

  // Ten tenths back down land on neutral exactly, so the channel is cleared rather than left holding a
  // point that is neutral to seven decimal places.
  CHECK(controller.nudgeGrowl(-10).hasValue());
  CHECK_NEAR(controller.growlAtPlayhead(), 0.0, 1e-9);
  CHECK(region()->growlAutomation.points().empty());

  CHECK(fixture.session.undo());
  CHECK_NEAR(fixture.session.project().findRegion(fixture.regionId)
                 ->growlAutomation.points().front().amount,
             domain::kMaximumGrowl, 1e-6);
  CHECK(fixture.session.undo());
  CHECK_NEAR(fixture.session.project().findRegion(fixture.regionId)
                 ->growlAutomation.points().front().amount,
             0.5, 1e-6);

  // A downward nudge at the floor is not an edit and must not fill the undo history.
  CHECK(controller.resetGrowlCurve().hasValue());
  CHECK(controller.nudgeGrowl(-1).hasValue());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->growlAutomation.points().empty());
}

TEST_CASE("A singer without its own excitation refuses the nudge and keeps the stored curve") {
  CarrierFixture fixture{false};
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.setPlayheadTick(time::Tick{960});
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->growlAutomation.upsert({time::Tick{0}, 0.4F}).hasValue());
  const auto refused = controller.nudgeGrowl(1);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("growl") != std::string::npos);
  CHECK(refused.error().message.find("source-filter") != std::string::npos);
  CHECK(region->growlAutomation.points().size() == 1U);
  CHECK_NEAR(controller.growlAtPlayhead(), 0.4, 1e-6);

  CHECK(controller.resetGrowlCurve().hasValue());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->growlAutomation.points().empty());
}

TEST_CASE("A neutral growl nudge preserves expression elsewhere and undoes as one edit") {
  CarrierFixture fixture{true};
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region->growlAutomation.replacePoints({
      {time::Tick{480}, 0.1F}, {time::Tick{960}, 0.8F}}));
  const auto before = region->growlAutomation;
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.setPlayheadTick(time::Tick{480});
  CHECK(controller.nudgeGrowl(-1));
  const auto after = fixture.session.project().findRegion(fixture.regionId)->growlAutomation;
  CHECK(after.points().size() == 2U);
  CHECK_NEAR(after.valueAt(time::Tick{480}), 0.0, 1e-6);
  CHECK_NEAR(after.valueAt(time::Tick{960}), 0.8, 1e-6);
  CHECK_NEAR(after.valueAt(time::Tick{720}), 0.4, 1e-6);
  CHECK(fixture.session.undo());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->growlAutomation == before);
  CHECK(fixture.session.redo());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->growlAutomation == after);
}

TEST_CASE("Growl adds a subharmonic and stays finite and bounded at its maximum") {
  const auto fixture = makeVowelProject();
  const auto plain = renderVowel(fixture, fixture.project);
  const auto neutral = renderVowel(fixture, withGrowl(fixture.project, fixture.region, 0.0F));
  const auto half = renderVowel(fixture, withGrowl(fixture.project, fixture.region, 0.5F));
  const auto growling = renderVowel(fixture, withGrowl(fixture.project, fixture.region, 1.0F));
  CHECK(!plain.empty());
  CHECK(growling.size() == plain.size());
  CHECK(neutral == plain);
  CHECK(growling != plain);

  const auto plainWindow = analyseWindow(plain);
  const auto halfWindow = analyseWindow(half);
  const auto growlWindow = analyseWindow(growling);

  // The roughness itself: energy half a fundamental from each harmonic, which the plain render does not
  // have, rising with the curve.
  const auto plainRough = interHarmonicEnergy(plainWindow);
  const auto halfRough = interHarmonicEnergy(halfWindow);
  const auto growlRough = interHarmonicEnergy(growlWindow);
  const auto plainPeriod = correlationAtLag(plainWindow, kSampleRate / kFundamentalHz);
  const auto plainDoublePeriod =
      correlationAtLag(plainWindow, 2.0 * kSampleRate / kFundamentalHz);
  const auto growlPeriod = correlationAtLag(growlWindow, kSampleRate / kFundamentalHz);
  const auto growlDoublePeriod =
      correlationAtLag(growlWindow, 2.0 * kSampleRate / kFundamentalHz);
  CHECK(halfRough > plainRough * 1.5);
  CHECK(growlRough > halfRough);
  CHECK(growlRough > plainRough * 3.0);
  // The roughness also reaches the subharmonic itself: the modulation puts real energy at half the
  // fundamental, where the plain render has only the noise floor.
  CHECK(peakNear(growlWindow, kSubharmonicHz, 8.0) >
        peakNear(plainWindow, kSubharmonicHz, 8.0) * 5.0);

  // Which is period doubling: the note repeats better at two of its periods than at one, which the plain
  // render does not.
  CHECK(plainPeriod > plainDoublePeriod);
  CHECK(growlDoublePeriod > growlPeriod);

  // The note is still there: the fundamental has not been replaced by its own half.
  const auto plainFundamental = peakNear(plainWindow, kFundamentalHz, 8.0);
  const auto growlFundamental = peakNear(growlWindow, kFundamentalHz, 8.0);
  CHECK(growlFundamental > plainFundamental * 0.5);
  CHECK(growlFundamental < plainFundamental * 1.5);

  // And the contract for this channel: finite, and inside the renderer's own safety bound, at the extreme
  // setting rather than only at a comfortable one.
  for (const auto sample : growling) {
    CHECK(std::isfinite(sample));
    CHECK(std::abs(sample) <= 0.500001F);
  }
  CHECK(maximumMagnitude(growlWindow) < maximumMagnitude(plainWindow) * 3.0);
  CHECK(rootMeanSquare(growlWindow) < rootMeanSquare(plainWindow) * 3.0);
  // Roughness attenuates the periodic part rather than adding to it, so the phrase stays audible at the
  // channel's maximum instead of quietly disappearing.
  CHECK(rootMeanSquare(growlWindow) > rootMeanSquare(plainWindow) * 0.5);
}

}  // namespace
