// The formant channel moves the vocal tract's resonances and nothing else.
//
// A creator's saved curve has to be persisted, evaluated per frame, applied by the carrier that owns
// the resonances, and refused by a carrier that cannot apply it -- a curve that is dropped in silence
// is worse than one that is rejected by name. These cases check all four, and then check the acoustic
// result: with a shift up, the spectral envelope moves up while the fundamental stays exactly where the
// score put it, and a curve that is entirely neutral is bit-identical to no curve at all.
//
// The voice here is a procedural fixture, not a singer: the oracle measures the render, and no claim is
// made about how the shifted vowel sounds to a listener.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/application/editor_session.hpp"
#include "seam/domain/formant_automation.hpp"
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
#include <numbers>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace seam;

constexpr double kSampleRate = 48000.0;

voice_design::VoiceRecipe vowelRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "formant-channel";
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

// A low sustained note, so the spectrum samples the envelope densely enough for a band measurement to
// mean something.
VowelProject makeVowelProject() {
  application::ProjectFactory factory{7300U};
  auto project = factory.createProject("Formant channel");
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

domain::Project withFormantShift(domain::Project project, domain::RegionId region, float semitones) {
  auto* target = project.findRegion(region);
  CHECK(target != nullptr);
  const auto inserted =
      target->formantAutomation.upsert(domain::FormantAutomationPoint{time::Tick{0}, semitones});
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

// One window from the middle of the note, so the release and the onset fades are not measured.
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

// The energy-weighted centre of the envelope between two frequencies, which is what a formant shift is
// supposed to move.
double spectralCentroid(std::span<const float> window, double fromHz, double toHz, double stepHz) {
  double weighted = 0.0;
  double total = 0.0;
  for (double frequency = fromHz; frequency <= toHz; frequency += stepHz) {
    const auto magnitude = magnitudeAt(window, frequency);
    weighted += frequency * magnitude;
    total += magnitude;
  }
  CHECK(total > 0.0);
  return weighted / total;
}

// Autocorrelation over the plausible fundamental range. This is deliberately crude: it is only asked to
// tell whether the fundamental moved, not to certify a pitch.
double fundamentalHz(std::span<const float> window, double minimumHz, double maximumHz) {
  const auto minimumLag = static_cast<std::size_t>(kSampleRate / maximumHz);
  const auto maximumLag = std::min<std::size_t>(
      static_cast<std::size_t>(kSampleRate / minimumHz), window.size() / 2U);
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
  return kSampleRate / static_cast<double>(bestLag);
}

TEST_CASE("A formant curve is bounded, ordered and interpolated") {
  domain::FormantAutomation curve;
  CHECK(curve.points().empty());
  CHECK(curve.valueAt(time::Tick{0}) == 0.0F);
  CHECK(curve.upsert({time::Tick{0}, 0.0F}).hasValue());
  CHECK(curve.upsert({time::Tick{960}, 7.0F}).hasValue());
  CHECK_NEAR(curve.valueAt(time::Tick{480}), 3.5, 1e-6);
  CHECK_NEAR(curve.valueAt(time::Tick{-10}), 0.0, 1e-6);
  CHECK_NEAR(curve.valueAt(time::Tick{4000}), 7.0, 1e-6);
  CHECK(!curve.upsert({time::Tick{960}, 30.0F}).hasValue());
  CHECK(!curve.upsert({time::Tick{960}, std::numeric_limits<float>::quiet_NaN()}).hasValue());
  CHECK(!curve.replacePoints({{time::Tick{960}, 1.0F}, {time::Tick{480}, 2.0F}}).hasValue());
  CHECK(curve.erase(time::Tick{960}));
  CHECK(!curve.erase(time::Tick{960}));
  CHECK(curve.points().size() == 1U);
}

TEST_CASE("A formant curve survives a save and older documents still load") {
  const auto fixture = makeVowelProject();
  const auto project = withFormantShift(fixture.project, fixture.region, 7.0F);
  formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(project);
  CHECK(encoded.hasValue());
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded.hasValue());
  if (!decoded) return;
  const auto* region = decoded.value().findRegion(fixture.region);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->formantAutomation.points().size() == 1U);
  CHECK_NEAR(region->formantAutomation.points().front().semitones, 7.0, 1e-6);

  // A document written before the channel existed has no such field, and its meaning is an empty curve
  // rather than a refusal.
  auto older = formats::parseJson(encoded.value());
  CHECK(older.hasValue());
  if (!older) return;
  auto tree = older.value();
  tree.asObject()["schemaVersion"] = formats::JsonValue{std::int64_t{11}};
  auto* tracks = tree.find("vocalTracks");
  CHECK(tracks != nullptr && tracks->isArray());
  auto& firstTrack = tracks->asArray().front().asObject();
  auto& regions = firstTrack["regions"].asArray();
  regions.front().asObject().erase("formantAutomation");
  const auto reloaded = codec.decode(formats::stringifyJson(tree));
  CHECK(reloaded.hasValue());
  if (!reloaded) return;
  const auto* olderRegion = reloaded.value().findRegion(fixture.region);
  CHECK(olderRegion != nullptr);
  if (olderRegion != nullptr) CHECK(olderRegion->formantAutomation.points().empty());

  // A schema-12 document whose curve leaves the supported range is a parse error, not a clamped value.
  auto outOfRange = tree;
  outOfRange.asObject()["schemaVersion"] = formats::JsonValue{std::int64_t{12}};
  auto* outOfRangeTracks = outOfRange.find("vocalTracks");
  CHECK(outOfRangeTracks != nullptr);
  auto& outOfRangeRegions = outOfRangeTracks->asArray().front().asObject()["regions"].asArray();
  outOfRangeRegions.front().asObject()["formantAutomation"] = formats::JsonValue{
      formats::JsonValue::Array{formats::JsonValue{formats::JsonValue::Object{
          {"tick", formats::JsonValue{std::int64_t{0}}},
          {"semitones", formats::JsonValue{30.0}}}}}};
  const auto rejected = codec.decode(formats::stringifyJson(outOfRange));
  CHECK(!rejected.hasValue());
  CHECK(rejected.error().code == core::ErrorCode::ParseError);
}

TEST_CASE("Only the carrier that owns the resonances advertises the formant channel") {
  const auto sourceFilter = synthesis::rendererCapabilities(synthesis::RendererCarrier::SourceFilter);
  CHECK(sourceFilter.supports(synthesis::RendererControl::Formant));
  CHECK(sourceFilter.supports(synthesis::RendererControl::Dynamics));
  const auto bank = synthesis::rendererCapabilities(synthesis::RendererCarrier::SampleBank);
  CHECK(!bank.supports(synthesis::RendererControl::Formant));
  CHECK(bank.supports(synthesis::RendererControl::Dynamics));

  synthesis::RendererControlRequest request;
  request.require(synthesis::RendererControl::Formant);
  const auto accepted = synthesis::validateRendererCapabilities(
      synthesis::RendererCarrier::SourceFilter, request);
  CHECK(accepted.hasValue());
  const auto refused = synthesis::validateRendererCapabilities(
      synthesis::RendererCarrier::SampleBank, request);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("formant") != std::string::npos);
}

TEST_CASE("A bank that cannot move its resonances refuses the curve instead of dropping it") {
  const auto fixture = makeVowelProject();
  auto project = withFormantShift(fixture.project, fixture.region, 7.0F);
  const auto manifest = test::support::makeManifest({test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 45, voicebank::UnitKind::Sustain)});
  const auto bankRoot = test::support::temporaryDirectory("formant-bank-refusal");
  std::filesystem::create_directories(bankRoot / "audio");
  auto refused = rendering::RenderSnapshotFactory{}.create(
      project, manifest, fixture.track, rendering::PhraseSegment{.regionId = fixture.region}, 1U,
      rendering::RenderQuality::Preview, bankRoot, 48000U);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("formant") != std::string::npos);

  // The same region with a curve that asks for nothing is not a request, so it is not refused.
  project = withFormantShift(fixture.project, fixture.region, 0.0F);
  auto neutral = rendering::RenderSnapshotFactory{}.create(
      project, manifest, fixture.track, rendering::PhraseSegment{.regionId = fixture.region}, 1U,
      rendering::RenderQuality::Preview, bankRoot, 48000U);
  if (!neutral) CHECK(neutral.error().code != core::ErrorCode::Unsupported);
}

// A controller fixture whose single track is either a source-filter singer or a sample bank, which is
// the difference the formant channel's capability decision turns on.
struct CarrierFixture final {
  application::ProjectFactory factory{7400U};
  domain::RegionId regionId{};
  domain::TrackId trackId{};
  application::EditorSession session;

  explicit CarrierFixture(bool procedural) : session(makeProject(procedural)) {}
  [[nodiscard]] bool isProcedural() const noexcept { return procedural_; }

  domain::Project makeProject(bool procedural) {
    procedural_ = procedural;
    auto project = factory.createProject("Formant editing");
    trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", time::Tick{0}, time::Tick{3840});
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{3840}, 45U, U"あ",
                                          domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    if (procedural) {
      project.findVocalTrack(trackId)->proceduralRecipe = domain::ProceduralRecipeReference{
          .resource = {domain::SingerResourceKind::Procedural, "formant-channel", "1.0.0",
                       std::string(64U, 'a')},
          .path = "recipe.json",
          .style = "neutral"};
    }
    return project;
  }

 private:
  bool procedural_{false};
};

TEST_CASE("A formant nudge is an undoable edit on the singer that can take it") {
  CarrierFixture fixture{true};
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.setPlayheadTick(time::Tick{480});
  CHECK_NEAR(controller.formantShiftAtPlayhead(), 0.0, 1e-6);
  CHECK(controller.nudgeFormantShift(2).hasValue());
  const auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->formantAutomation.points().size() == 1U);
  CHECK(region->formantAutomation.points().front().tick == time::Tick{480});
  CHECK_NEAR(region->formantAutomation.points().front().semitones, 2.0, 1e-6);
  CHECK_NEAR(controller.formantShiftAtPlayhead(), 2.0, 1e-6);

  // A second nudge at the same playhead replaces its point rather than accumulating points.
  CHECK(controller.nudgeFormantShift(3).hasValue());
  CHECK(region->formantAutomation.points().size() == 1U);
  CHECK_NEAR(region->formantAutomation.points().front().semitones, 5.0, 1e-6);

  // The shift is bounded by the channel's own range, not by how often the menu item was used.
  CHECK(controller.nudgeFormantShift(100).hasValue());
  CHECK_NEAR(region->formantAutomation.points().front().semitones,
             domain::kMaximumFormantShiftSemitones, 1e-6);

  // Every one of those is an ordinary undoable edit.
  CHECK(fixture.session.undo());
  CHECK_NEAR(fixture.session.project().findRegion(fixture.regionId)
                 ->formantAutomation.points().front().semitones,
             5.0, 1e-6);
  CHECK(fixture.session.undo());
  CHECK_NEAR(fixture.session.project().findRegion(fixture.regionId)
                 ->formantAutomation.points().front().semitones,
             2.0, 1e-6);
  CHECK(fixture.session.undo());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->formantAutomation.points().empty());

  // And resetting a curve is a real edit too, not a silent mutation.
  CHECK(controller.nudgeFormantShift(1).hasValue());
  CHECK(!fixture.session.project().findRegion(fixture.regionId)->formantAutomation.points().empty());
  CHECK(controller.resetFormantCurve().hasValue());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->formantAutomation.points().empty());
  CHECK(fixture.session.undo());
  CHECK(!fixture.session.project().findRegion(fixture.regionId)->formantAutomation.points().empty());
}

TEST_CASE("A singer without resonances refuses the nudge and keeps the stored curve") {
  CarrierFixture fixture{false};
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.setPlayheadTick(time::Tick{960});
  // A curve that arrived with the document is not destroyed by a refused edit.
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->formantAutomation.upsert({time::Tick{0}, 5.0F}).hasValue());
  const auto refused = controller.nudgeFormantShift(1);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("formant") != std::string::npos);
  CHECK(refused.error().message.find("source-filter") != std::string::npos);
  CHECK(region->formantAutomation.points().size() == 1U);
  CHECK_NEAR(region->formantAutomation.points().front().semitones, 5.0, 1e-6);
  CHECK_NEAR(controller.formantShiftAtPlayhead(), 5.0, 1e-6);

  // Clearing is always allowed: it is the remedy, not the request.
  CHECK(controller.resetFormantCurve().hasValue());
  CHECK(region->formantAutomation.points().empty());
}

TEST_CASE("A shift moves the envelope and leaves the fundamental alone") {
  const auto fixture = makeVowelProject();
  const auto plain = renderVowel(fixture, fixture.project);
  const auto neutral = renderVowel(fixture, withFormantShift(fixture.project, fixture.region, 0.0F));
  const auto shifted = renderVowel(fixture, withFormantShift(fixture.project, fixture.region, 7.0F));
  CHECK(!plain.empty());
  CHECK(shifted.size() == plain.size());
  // A curve that asks for nothing is exactly the render that had no curve at all.
  CHECK(neutral == plain);
  CHECK(shifted != plain);

  const auto plainWindow = analyseWindow(plain);
  const auto shiftedWindow = analyseWindow(shifted);
  const auto plainCentroid = spectralCentroid(plainWindow, 300.0, 4000.0, 40.0);
  const auto shiftedCentroid = spectralCentroid(shiftedWindow, 300.0, 4000.0, 40.0);
  // Seven semitones is a ratio of about 1.5, and the envelope has to follow it upward.
  CHECK(shiftedCentroid > plainCentroid * 1.2);

  // The tract moved; the excitation did not. A100 Hz source stays a 100 Hz source.
  const auto plainF0 = fundamentalHz(plainWindow, 60.0, 400.0);
  const auto shiftedF0 = fundamentalHz(shiftedWindow, 60.0, 400.0);
  CHECK_NEAR(plainF0, 110.0, 40.0);
  CHECK_NEAR(shiftedF0, plainF0, 2.0);
}

}  // namespace
