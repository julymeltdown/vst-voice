// The breathiness channel rebalances the excitation, and nothing else.
//
// A saved curve has to be persisted, evaluated per frame, applied by the carrier that generates the
// excitation, and refused by a carrier that does not -- the same four obligations the formant channel
// carries one layer further down the voice, and a curve that disappears in silence is worse than one
// that is refused by name. These cases check all four, and then the acoustic result: more breathiness
// means less periodic energy without moving the fundamental, the melody stays where the score put it,
// a partly breathy setting sits between the two endpoints, and a curve that is entirely neutral is
// bit-identical to no curve at all.
//
// The voice here is a procedural fixture, not a singer: the oracle measures the render, and no claim is
// made about how the breathy vowel sounds to a listener.
#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/application/editor_session.hpp"
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
#include <span>
#include <string>
#include <vector>

namespace {

using namespace seam;

constexpr double kSampleRate = 48000.0;

voice_design::VoiceRecipe vowelRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "breathiness-channel";
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

// A low sustained note, so the fundamental has many periods inside the analysis window.
VowelProject makeVowelProject() {
  application::ProjectFactory factory{7500U};
  auto project = factory.createProject("Breathiness channel");
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

// One window from the middle of the note, so the release and the onset fade are not measured.
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

// How periodic the window is, and where its period sits. The autocorrelation peak is the measurement
// that answers both questions at once: a balance toward aperiodic energy lowers the peak while leaving
// the lag, and therefore the fundamental, where it was. This is deliberately crude; it is asked to tell
// whether the excitation changed character, not to certify a pitch.
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

TEST_CASE("A breathiness curve is bounded, ordered and interpolated") {
  domain::BreathinessAutomation curve;
  CHECK(curve.points().empty());
  CHECK(curve.valueAt(time::Tick{0}) == 0.0F);
  CHECK(curve.upsert({time::Tick{0}, 0.0F}).hasValue());
  CHECK(curve.upsert({time::Tick{960}, 0.6F}).hasValue());
  CHECK_NEAR(curve.valueAt(time::Tick{480}), 0.3, 1e-6);
  CHECK_NEAR(curve.valueAt(time::Tick{-10}), 0.0, 1e-6);
  CHECK_NEAR(curve.valueAt(time::Tick{4000}), 0.6, 1e-6);
  // The channel is a share, so its bound is one and a value below zero is a mistake rather than taste.
  CHECK(!curve.upsert({time::Tick{960}, 1.5F}).hasValue());
  CHECK(!curve.upsert({time::Tick{960}, -0.1F}).hasValue());
  CHECK(!curve.upsert({time::Tick{960}, std::numeric_limits<float>::quiet_NaN()}).hasValue());
  CHECK(!curve.replacePoints({{time::Tick{960}, 0.2F}, {time::Tick{480}, 0.4F}}).hasValue());
  CHECK(curve.erase(time::Tick{960}));
  CHECK(!curve.erase(time::Tick{960}));
  CHECK(curve.points().size() == 1U);
}

TEST_CASE("A breathiness curve survives a save and older documents still load") {
  const auto fixture = makeVowelProject();
  const auto project = withBreathiness(fixture.project, fixture.region, 0.6F);
  formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(project);
  CHECK(encoded.hasValue());
  const auto decoded = codec.decode(encoded.value());
  CHECK(decoded.hasValue());
  if (!decoded) return;
  const auto* region = decoded.value().findRegion(fixture.region);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->breathinessAutomation.points().size() == 1U);
  CHECK_NEAR(region->breathinessAutomation.points().front().amount, 0.6, 1e-6);

  // A document written before the channel existed has no such field, and its meaning is an empty curve
  // rather than a refusal.
  auto older = formats::parseJson(encoded.value());
  CHECK(older.hasValue());
  if (!older) return;
  auto tree = older.value();
  tree.asObject()["schemaVersion"] = formats::JsonValue{std::int64_t{12}};
  auto* tracks = tree.find("vocalTracks");
  CHECK(tracks != nullptr && tracks->isArray());
  auto& firstTrack = tracks->asArray().front().asObject();
  auto& regions = firstTrack["regions"].asArray();
  regions.front().asObject().erase("breathinessAutomation");
  const auto reloaded = codec.decode(formats::stringifyJson(tree));
  CHECK(reloaded.hasValue());
  if (!reloaded) return;
  const auto* olderRegion = reloaded.value().findRegion(fixture.region);
  CHECK(olderRegion != nullptr);
  if (olderRegion != nullptr) CHECK(olderRegion->breathinessAutomation.points().empty());

  // A schema-13 document whose curve leaves the supported range is a parse error, not a clamped value.
  auto outOfRange = tree;
  outOfRange.asObject()["schemaVersion"] = formats::JsonValue{std::int64_t{13}};
  auto* outOfRangeTracks = outOfRange.find("vocalTracks");
  CHECK(outOfRangeTracks != nullptr);
  auto& outOfRangeRegions = outOfRangeTracks->asArray().front().asObject()["regions"].asArray();
  outOfRangeRegions.front().asObject()["breathinessAutomation"] = formats::JsonValue{
      formats::JsonValue::Array{formats::JsonValue{formats::JsonValue::Object{
          {"tick", formats::JsonValue{std::int64_t{0}}},
          {"amount", formats::JsonValue{1.5}}}}}};
  const auto rejected = codec.decode(formats::stringifyJson(outOfRange));
  CHECK(!rejected.hasValue());
  CHECK(rejected.error().code == core::ErrorCode::ParseError);
}

TEST_CASE("Only the carrier that generates the excitation advertises the breathiness channel") {
  const auto sourceFilter = synthesis::rendererCapabilities(synthesis::RendererCarrier::SourceFilter);
  CHECK(sourceFilter.supports(synthesis::RendererControl::Breathiness));
  CHECK(sourceFilter.supports(synthesis::RendererControl::Formant));
  const auto bank = synthesis::rendererCapabilities(synthesis::RendererCarrier::SampleBank);
  CHECK(!bank.supports(synthesis::RendererControl::Breathiness));
  CHECK(bank.supports(synthesis::RendererControl::Dynamics));

  synthesis::RendererControlRequest request;
  request.require(synthesis::RendererControl::Breathiness);
  const auto accepted = synthesis::validateRendererCapabilities(
      synthesis::RendererCarrier::SourceFilter, request);
  CHECK(accepted.hasValue());
  const auto refused = synthesis::validateRendererCapabilities(
      synthesis::RendererCarrier::SampleBank, request);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("breathiness") != std::string::npos);
}

TEST_CASE("A bank that has no excitation refuses the curve instead of dropping it") {
  const auto fixture = makeVowelProject();
  auto project = withBreathiness(fixture.project, fixture.region, 0.6F);
  const auto manifest = test::support::makeManifest({test::support::makeUnit(
      "a", {"a"}, "audio/a.wav", 45, voicebank::UnitKind::Sustain)});
  const auto bankRoot = test::support::temporaryDirectory("breathiness-bank-refusal");
  std::filesystem::create_directories(bankRoot / "audio");
  auto refused = rendering::RenderSnapshotFactory{}.create(
      project, manifest, fixture.track, rendering::PhraseSegment{.regionId = fixture.region}, 1U,
      rendering::RenderQuality::Preview, bankRoot, 48000U);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("breathiness") != std::string::npos);

  // The same region with a curve that asks for nothing is not a request, so it is not refused.
  project = withBreathiness(fixture.project, fixture.region, 0.0F);
  auto neutral = rendering::RenderSnapshotFactory{}.create(
      project, manifest, fixture.track, rendering::PhraseSegment{.regionId = fixture.region}, 1U,
      rendering::RenderQuality::Preview, bankRoot, 48000U);
  if (!neutral) CHECK(neutral.error().code != core::ErrorCode::Unsupported);
}

// A controller fixture whose single track is either a source-filter singer or a sample bank, which is
// the difference the breathiness channel's capability decision turns on.
struct CarrierFixture final {
  application::ProjectFactory factory{7600U};
  domain::RegionId regionId{};
  domain::TrackId trackId{};
  application::EditorSession session;

  explicit CarrierFixture(bool procedural) : session(makeProject(procedural)) {}
  [[nodiscard]] bool isProcedural() const noexcept { return procedural_; }

  domain::Project makeProject(bool procedural) {
    procedural_ = procedural;
    auto project = factory.createProject("Breathiness editing");
    trackId = factory.addVocalTrack(project, "Singer");
    regionId = factory.addRegion(project, trackId, "Phrase", time::Tick{0}, time::Tick{3840});
    auto [lyric, note] = factory.makeNote(time::Tick{0}, time::Tick{3840}, 45U, U"あ",
                                          domain::Language::Japanese);
    auto* region = project.findRegion(regionId);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
    if (procedural) {
      project.findVocalTrack(trackId)->proceduralRecipe = domain::ProceduralRecipeReference{
          .resource = {domain::SingerResourceKind::Procedural, "breathiness-channel", "1.0.0",
                       std::string(64U, 'a')},
          .path = "recipe.json",
          .style = "neutral"};
    }
    return project;
  }

 private:
  bool procedural_{false};
};

TEST_CASE("A breathiness nudge is an undoable edit on the singer that can take it") {
  CarrierFixture fixture{true};
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.setPlayheadTick(time::Tick{480});
  CHECK_NEAR(controller.breathinessAtPlayhead(), 0.0, 1e-6);
  CHECK(controller.nudgeBreathiness(2).hasValue());
  const auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->breathinessAutomation.points().size() == 1U);
  CHECK(region->breathinessAutomation.points().front().tick == time::Tick{480});
  CHECK_NEAR(region->breathinessAutomation.points().front().amount, 0.2, 1e-6);
  CHECK_NEAR(controller.breathinessAtPlayhead(), 0.2, 1e-6);

  // A second nudge at the same playhead replaces its point rather than accumulating points.
  CHECK(controller.nudgeBreathiness(3).hasValue());
  CHECK(region->breathinessAutomation.points().size() == 1U);
  CHECK_NEAR(region->breathinessAutomation.points().front().amount, 0.5, 1e-6);

  // The balance is bounded by the channel's own range, not by how often the menu item was used.
  CHECK(controller.nudgeBreathiness(100).hasValue());
  CHECK_NEAR(region->breathinessAutomation.points().front().amount,
             domain::kMaximumBreathiness, 1e-6);

  // Every one of those is an ordinary undoable edit.
  CHECK(fixture.session.undo());
  CHECK_NEAR(fixture.session.project().findRegion(fixture.regionId)
                 ->breathinessAutomation.points().front().amount,
             0.5, 1e-6);
  CHECK(fixture.session.undo());
  CHECK_NEAR(fixture.session.project().findRegion(fixture.regionId)
                 ->breathinessAutomation.points().front().amount,
             0.2, 1e-6);
  CHECK(fixture.session.undo());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->breathinessAutomation.points().empty());

  // And resetting a curve is a real edit too, not a silent mutation.
  CHECK(controller.nudgeBreathiness(1).hasValue());
  CHECK(!fixture.session.project().findRegion(fixture.regionId)
             ->breathinessAutomation.points().empty());
  CHECK(controller.resetBreathinessCurve().hasValue());
  CHECK(fixture.session.project().findRegion(fixture.regionId)->breathinessAutomation.points().empty());
  CHECK(fixture.session.undo());
  CHECK(!fixture.session.project().findRegion(fixture.regionId)
             ->breathinessAutomation.points().empty());

  // Ten steps up and ten steps back down land on the neutral value exactly, so the curve is cleared
  // rather than left holding a point that is neutral to seven decimal places.
  CHECK(controller.resetBreathinessCurve().hasValue());
  CHECK(controller.nudgeBreathiness(10).hasValue());
  CHECK(controller.nudgeBreathiness(-10).hasValue());
  const auto* cleared = fixture.session.project().findRegion(fixture.regionId);
  CHECK_NEAR(controller.breathinessAtPlayhead(), 0.0, 1e-9);
  CHECK(cleared->breathinessAutomation.points().empty());
}

TEST_CASE("A singer without its own excitation refuses the nudge and keeps the stored curve") {
  CarrierFixture fixture{false};
  native_ui::NativeEditorController controller{fixture.session, fixture.factory, fixture.regionId, {}};
  controller.setPlayheadTick(time::Tick{960});
  // A curve that arrived with the document is not destroyed by a refused edit.
  auto* region = fixture.session.project().findRegion(fixture.regionId);
  CHECK(region != nullptr);
  if (region == nullptr) return;
  CHECK(region->breathinessAutomation.upsert({time::Tick{0}, 0.5F}).hasValue());
  const auto refused = controller.nudgeBreathiness(1);
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::Unsupported);
  CHECK(refused.error().message.find("breathiness") != std::string::npos);
  CHECK(refused.error().message.find("source-filter") != std::string::npos);
  CHECK(region->breathinessAutomation.points().size() == 1U);
  CHECK_NEAR(region->breathinessAutomation.points().front().amount, 0.5, 1e-6);
  CHECK_NEAR(controller.breathinessAtPlayhead(), 0.5, 1e-6);

  // Clearing is always allowed: it is the remedy, not the request.
  CHECK(controller.resetBreathinessCurve().hasValue());
  CHECK(region->breathinessAutomation.points().empty());
}

TEST_CASE("Breathiness rebalances the excitation and leaves the melody alone") {
  const auto fixture = makeVowelProject();
  const auto plain = renderVowel(fixture, fixture.project);
  const auto neutral = renderVowel(fixture, withBreathiness(fixture.project, fixture.region, 0.0F));
  const auto half = renderVowel(fixture, withBreathiness(fixture.project, fixture.region, 0.5F));
  const auto breathy = renderVowel(fixture, withBreathiness(fixture.project, fixture.region, 1.0F));
  CHECK(!plain.empty());
  CHECK(breathy.size() == plain.size());
  // A curve that asks for nothing is exactly the render that had no curve at all.
  CHECK(neutral == plain);
  CHECK(breathy != plain);

  const auto plainWindow = analyseWindow(plain);
  const auto halfWindow = analyseWindow(half);
  const auto breathyWindow = analyseWindow(breathy);
  const auto plainEvidence = periodicity(plainWindow, 60.0, 400.0);
  const auto halfEvidence = periodicity(halfWindow, 60.0, 400.0);
  const auto breathyEvidence = periodicity(breathyWindow, 60.0, 400.0);

  // The source moved its energy from the periodic part to the aperiodic part, and it kept moving in
  // that direction as the curve rose.
  CHECK(plainEvidence.coherence > halfEvidence.coherence);
  CHECK(halfEvidence.coherence > breathyEvidence.coherence);
  CHECK(plainEvidence.coherence - breathyEvidence.coherence > 0.15);
  // And it is still a voiced sound, not a whisper: the balance has a floor, and the floor is high
  // enough that the fundamental is still recovered from the phrase rather than from a coincidence.
  CHECK(breathyEvidence.coherence > 0.5);
  // The source changed character; the melody did not move.
  CHECK_NEAR(plainEvidence.fundamentalHz, 110.0, 40.0);
  CHECK_NEAR(breathyEvidence.fundamentalHz, plainEvidence.fundamentalHz, 2.0);
  // The phrase is still there at a comparable level: this is a balance, not a gain.
  const auto plainLevel = rootMeanSquare(plainWindow);
  const auto breathyLevel = rootMeanSquare(breathyWindow);
  CHECK(plainLevel > 0.0);
  CHECK(breathyLevel > plainLevel * 0.4);
  CHECK(breathyLevel < plainLevel * 2.0);
}

}  // namespace
