// A character performance snapshot is what a presentation can draw without owning the audio.
//
// The snapshot is built from the same bounded phrase result that is audible, keeps only normalized
// envelopes and cue spans, and refuses anything it cannot draw honestly: an expression envelope of
// the wrong length, cues that are unordered or outside the phrase, an empty or inverted span, a
// sample buffer shorter than the span it claims, or an analysis that was cancelled. The dock then
// reads one immutable model at a playhead, so stop, seek and loop are the caller's arithmetic
// instead of state that can drift away from what is sounding.
//
// The second half covers the dock itself: what the character is singing is a read model beside the
// operational state, so a warning stays a warning while a phrase plays, and a snapshot that does
// not validate is refused rather than replacing what the dock is already showing.
//
// Every identity here is synthetic, every cue is written by this test, and the phone-to-mouth map
// is an engineering default for the dock rather than approved artwork or a phonetic claim. Nothing
// here is listened to and no roadmap unit is accepted.
#include "test_framework.hpp"

#include "seam/character/performance.hpp"
#include "seam/core/error.hpp"
#include "seam/native_ui/character_presentation.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace seam;

constexpr std::string_view kDigest =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

// A constant signal is used wherever a test needs an exact envelope value: the RMS of a constant
// window is that constant, so normalization can be checked to the last bit instead of to a
// tolerance that would hide a real scaling mistake.
std::vector<float> constant(std::size_t frames, float amplitude) {
  return std::vector<float>(frames, amplitude);
}

character::CharacterPerformanceRequest makeRequest(
    std::span<const float> samples,
    std::span<const character::PerformanceCueInput> cues = {},
    std::span<const float> expression = {}, std::uint32_t sampleRate = 48000U) {
  character::CharacterPerformanceRequest request;
  request.resourceId = "seam-pilot-01";
  request.resourceVersion = "1.0.0";
  request.resourceContentHash = std::string(kDigest);
  request.style = "neutral";
  request.pronunciationIdentity = "ja-ipa-1";
  request.renderRevision = 7U;
  request.origin = 0;
  request.end = static_cast<time::SampleFrame>(samples.size());
  request.sampleRate = sampleRate;
  request.samples = samples;
  request.cues = cues;
  request.expressionEnvelope = expression;
  return request;
}

character::CharacterPerformanceSnapshot buildOrFail(
    const character::CharacterPerformanceRequest& request, std::uint32_t windowFrames = 240U) {
  auto built = character::buildCharacterPerformanceSnapshot(request, windowFrames);
  CHECK(built.hasValue());
  return std::move(built.value());
}

TEST_CASE("A snapshot is built from the phrase that is audible") {
  const auto samples = constant(960U, 0.5F);
  const auto request = makeRequest(samples);
  auto built = character::buildCharacterPerformanceSnapshot(request, 240U);
  CHECK(built.hasValue());
  const auto& snapshot = built.value();
  CHECK(snapshot.schemaVersion == 1);
  CHECK(snapshot.windowFrames == 240U);
  CHECK(snapshot.windowCount() == 4U);
  CHECK(snapshot.energy.size() == snapshot.expression.size());
  CHECK(snapshot.validate().hasValue());
  CHECK(snapshot.resourceId == "seam-pilot-01");
  CHECK(snapshot.resourceVersion == "1.0.0");
  CHECK(snapshot.resourceContentHash == kDigest);
  CHECK(snapshot.pronunciationIdentity == "ja-ipa-1");
  CHECK(snapshot.renderRevision == 7U);
  CHECK(snapshot.origin == 0);
  CHECK(snapshot.end == 960);
  CHECK(snapshot.cues.empty());
  bool audible = false;
  for (const auto value : snapshot.energy) {
    CHECK(value >= 0.0F);
    CHECK(value <= 1.0F);
    audible = audible || value > 0.0F;
  }
  CHECK(audible);
  // No expression envelope was supplied, so the snapshot says so and reports zeros instead of
  // inventing a curve the phrase never had.
  CHECK(!snapshot.expressionMeasured);
  for (const auto value : snapshot.expression) CHECK(value == 0.0F);
}

TEST_CASE("A silent phrase is honestly silent instead of dividing by an absent peak") {
  const auto samples = constant(480U, 0.0F);
  const auto request = makeRequest(samples);
  const auto snapshot = buildOrFail(request);
  CHECK(snapshot.windowCount() == 2U);
  CHECK(snapshot.validate().hasValue());
  for (const auto value : snapshot.energy) CHECK(value == 0.0F);
}

TEST_CASE("Energy is normalized to the peak of its own phrase") {
  auto samples = constant(240U, 0.25F);
  const auto loud = constant(240U, 1.0F);
  samples.insert(samples.end(), loud.begin(), loud.end());
  const auto request = makeRequest(samples);
  const auto snapshot = buildOrFail(request);
  CHECK(snapshot.windowCount() == 2U);
  CHECK_NEAR(snapshot.energy[0], 0.25, 1e-6);
  CHECK_NEAR(snapshot.energy[1], 1.0, 1e-6);
}

TEST_CASE("A partial final window still covers its own span") {
  const auto samples = constant(500U, 0.5F);
  const auto request = makeRequest(samples);
  const auto snapshot = buildOrFail(request);
  CHECK(snapshot.windowFrames == 240U);
  CHECK(snapshot.windowCount() == 3U);
  CHECK(snapshot.validate().hasValue());
  CHECK_NEAR(snapshot.energy[2], 1.0, 1e-6);
}

TEST_CASE("A cue maps to the mouth the dock can draw and the playhead picks the cue") {
  CHECK(character::mouthShapeForCue(character::CueKind::Silence, "pau") ==
        character::MouthShape::Closed);
  CHECK(character::mouthShapeForCue(character::CueKind::Closure, "k") ==
        character::MouthShape::Closed);
  CHECK(character::mouthShapeForCue(character::CueKind::Nasal, "n") ==
        character::MouthShape::Nasal);
  CHECK(character::mouthShapeForCue(character::CueKind::Consonant, "s") ==
        character::MouthShape::Narrow);
  CHECK(character::mouthShapeForCue(character::CueKind::Vowel, "a") ==
        character::MouthShape::Open);
  CHECK(character::mouthShapeForCue(character::CueKind::Vowel, "i") ==
        character::MouthShape::Wide);
  CHECK(character::mouthShapeForCue(character::CueKind::Vowel, "o") ==
        character::MouthShape::Round);
  CHECK(character::mouthShapeName(character::MouthShape::Nasal) == "nasal");

  const std::array<character::PerformanceCueInput, 3> cues{{
      {"k", character::CueKind::Closure, 0, 120},
      {"a", character::CueKind::Vowel, 120, 480},
      {"n", character::CueKind::Nasal, 480, 720},
  }};
  const auto samples = constant(960U, 0.5F);
  const auto request = makeRequest(samples, cues);
  const auto snapshot = buildOrFail(request);
  CHECK(snapshot.cues.size() == 3U);
  CHECK(snapshot.cues[1].phone == "a");
  CHECK(snapshot.cues[1].mouth == character::MouthShape::Open);
  CHECK(snapshot.cues[2].mouth == character::MouthShape::Nasal);

  const auto closure = character::characterPerformanceFrameAt(snapshot, 0);
  CHECK(closure.performing);
  CHECK(closure.mouth == character::MouthShape::Closed);
  const auto vowel = character::characterPerformanceFrameAt(snapshot, 300);
  CHECK(vowel.performing);
  CHECK(vowel.mouth == character::MouthShape::Open);
  CHECK(vowel.energy > 0.0F);
  const auto nasal = character::characterPerformanceFrameAt(snapshot, 600);
  CHECK(nasal.performing);
  CHECK(nasal.mouth == character::MouthShape::Nasal);
  // The last cue ends at 720 and the phrase ends at 960: a playhead between cues is performing with
  // a closed mouth rather than being reported as outside the phrase.
  const auto between = character::characterPerformanceFrameAt(snapshot, 800);
  CHECK(between.performing);
  CHECK(between.mouth == character::MouthShape::Closed);
}

TEST_CASE("A playhead outside the phrase is closed and not performing") {
  const std::array<character::PerformanceCueInput, 1> cues{{
      {"a", character::CueKind::Vowel, 120, 480},
  }};
  const auto samples = constant(960U, 0.5F);
  const auto request = makeRequest(samples, cues);
  const auto snapshot = buildOrFail(request);
  // The span is half-open: its own end frame belongs to whatever comes next, not to this phrase.
  for (const auto playhead : {time::SampleFrame{-1}, time::SampleFrame{960}, time::SampleFrame{5000}}) {
    const auto frame = character::characterPerformanceFrameAt(snapshot, playhead);
    CHECK(!frame.performing);
    CHECK(frame.mouth == character::MouthShape::Closed);
    CHECK(frame.energy == 0.0F);
    CHECK(frame.expression == 0.0F);
  }
}

TEST_CASE("Stop, seek and loop read one immutable model") {
  const auto samples = constant(960U, 0.5F);
  const auto request = makeRequest(samples);
  const auto snapshot = buildOrFail(request);
  const auto first = character::characterPerformanceFrameAt(snapshot, 300);
  const auto elsewhere = character::characterPerformanceFrameAt(snapshot, 900);
  const auto repeated = character::characterPerformanceFrameAt(snapshot, 300);
  CHECK(first == repeated);
  CHECK(elsewhere.performing);
  CHECK(first == character::characterPerformanceFrameAt(snapshot, 300));
}

TEST_CASE("An expression envelope of the wrong length is refused, not stretched") {
  const auto samples = constant(960U, 0.5F);
  const std::array<float, 3> shortEnvelope{0.1F, 0.2F, 0.3F};
  const auto request = makeRequest(samples, {}, shortEnvelope);
  auto built = character::buildCharacterPerformanceSnapshot(request, 240U);
  CHECK(!built.hasValue());
  CHECK(built.error().code == core::ErrorCode::InvalidArgument);
  CHECK(!built.error().message.empty());
}

TEST_CASE("A supplied expression envelope is copied window for window and clamped") {
  const auto samples = constant(960U, 0.5F);
  const std::array<float, 4> envelope{0.1F, 0.5F, 1.5F, -1.0F};
  const auto request = makeRequest(samples, {}, envelope);
  const auto snapshot = buildOrFail(request);
  CHECK(snapshot.expressionMeasured);
  CHECK_NEAR(snapshot.expression[0], 0.1, 1e-6);
  CHECK_NEAR(snapshot.expression[1], 0.5, 1e-6);
  CHECK_NEAR(snapshot.expression[2], 1.0, 1e-6);
  CHECK_NEAR(snapshot.expression[3], 0.0, 1e-6);
  CHECK(snapshot.validate().hasValue());
}

TEST_CASE("A cue that is unordered or outside the phrase is refused") {
  const auto samples = constant(960U, 0.5F);
  const std::array<character::PerformanceCueInput, 2> unordered{{
      {"a", character::CueKind::Vowel, 120, 480},
      {"k", character::CueKind::Closure, 0, 120},
  }};
  auto built = character::buildCharacterPerformanceSnapshot(makeRequest(samples, unordered), 240U);
  CHECK(!built.hasValue());
  CHECK(built.error().code == core::ErrorCode::InvalidArgument);

  const std::array<character::PerformanceCueInput, 1> past{{
      {"a", character::CueKind::Vowel, 0, 1000},
  }};
  built = character::buildCharacterPerformanceSnapshot(makeRequest(samples, past), 240U);
  CHECK(!built.hasValue());
  CHECK(built.error().code == core::ErrorCode::InvalidArgument);

  const std::array<character::PerformanceCueInput, 1> emptySpan{{
      {"a", character::CueKind::Vowel, 300, 300},
  }};
  built = character::buildCharacterPerformanceSnapshot(makeRequest(samples, emptySpan), 240U);
  CHECK(!built.hasValue());
  CHECK(built.error().code == core::ErrorCode::InvalidArgument);
}

TEST_CASE("An empty span, an inverted span and a short buffer are refused") {
  const auto samples = constant(480U, 0.5F);
  auto request = makeRequest(samples);
  request.end = 0;
  auto built = character::buildCharacterPerformanceSnapshot(request, 240U);
  CHECK(!built.hasValue());
  CHECK(built.error().code == core::ErrorCode::InvalidArgument);

  request = makeRequest(samples);
  request.origin = 100;
  request.end = 100;
  built = character::buildCharacterPerformanceSnapshot(request, 240U);
  CHECK(!built.hasValue());
  CHECK(built.error().code == core::ErrorCode::InvalidArgument);

  // The phrase claims 960 frames but only 480 were rendered: the snapshot refuses to describe audio
  // it was not given instead of zero-filling the rest.
  request = makeRequest(samples);
  request.end = 960;
  built = character::buildCharacterPerformanceSnapshot(request, 240U);
  CHECK(!built.hasValue());
  CHECK(built.error().code == core::ErrorCode::InvalidArgument);
}

TEST_CASE("Incomplete identity, a malformed digest and an unbounded window are refused") {
  const auto samples = constant(480U, 0.5F);
  auto request = makeRequest(samples);
  request.resourceContentHash = "not-a-digest";
  auto built = character::buildCharacterPerformanceSnapshot(request, 240U);
  CHECK(!built.hasValue());
  CHECK(built.error().code == core::ErrorCode::InvalidArgument);

  request = makeRequest(samples);
  request.pronunciationIdentity.clear();
  built = character::buildCharacterPerformanceSnapshot(request, 240U);
  CHECK(!built.hasValue());
  CHECK(built.error().code == core::ErrorCode::InvalidArgument);

  built = character::buildCharacterPerformanceSnapshot(makeRequest(samples), 48001U);
  CHECK(!built.hasValue());
  CHECK(built.error().code == core::ErrorCode::InvalidArgument);

  built = character::buildCharacterPerformanceSnapshot(makeRequest(samples), 0U);
  CHECK(!built.hasValue());
  CHECK(built.error().code == core::ErrorCode::InvalidArgument);
}

TEST_CASE("A build that was already cancelled is refused") {
  const auto samples = constant(480U, 0.5F);
  std::stop_source source;
  source.request_stop();
  auto built = character::buildCharacterPerformanceSnapshot(makeRequest(samples), 240U,
      source.get_token());
  CHECK(!built.hasValue());
  CHECK(built.error().code == core::ErrorCode::Conflict);
}

TEST_CASE("A tampered snapshot does not validate") {
  const auto samples = constant(480U, 0.5F);
  const auto snapshot = buildOrFail(makeRequest(samples));
  CHECK(snapshot.validate().hasValue());

  auto inflated = snapshot;
  inflated.energy[0] = 1.5F;
  CHECK(!inflated.validate().hasValue());
  CHECK(inflated.validate().error().code == core::ErrorCode::InvalidArgument);

  auto negative = snapshot;
  negative.expression[0] = -0.25F;
  CHECK(!negative.validate().hasValue());

  auto truncated = snapshot;
  truncated.energy.pop_back();
  CHECK(!truncated.validate().hasValue());

  auto mismatched = snapshot;
  mismatched.expression.clear();
  CHECK(!mismatched.validate().hasValue());

  auto unnamed = snapshot;
  unnamed.resourceVersion.clear();
  CHECK(!unnamed.validate().hasValue());

  auto digestless = snapshot;
  digestless.resourceContentHash = "0123";
  CHECK(!digestless.validate().hasValue());

  auto inverted = snapshot;
  inverted.end = inverted.origin;
  CHECK(!inverted.validate().hasValue());

  auto unsupported = snapshot;
  unsupported.schemaVersion = 2;
  CHECK(!unsupported.validate().hasValue());
  CHECK(unsupported.validate().error().code == core::ErrorCode::Unsupported);

  auto unordered = snapshot;
  unordered.cues.push_back(character::PerformanceCue{"a", character::CueKind::Vowel,
      character::MouthShape::Open, 400, 200});
  CHECK(!unordered.validate().hasValue());
}

TEST_CASE("The dock's performance frame is separate from its operational state") {
  native_ui::CharacterPresentation presentation;
  CHECK(!presentation.hasPerformanceSnapshot());
  const auto idle = presentation.performanceFrameAt(0);
  CHECK(!idle.performing);
  CHECK(idle.mouth == character::MouthShape::Closed);
  CHECK(idle.energy == 0.0F);
  CHECK(presentation.state() == character::State::Neutral);

  const std::array<character::PerformanceCueInput, 1> cues{{
      {"a", character::CueKind::Vowel, 120, 480},
  }};
  const auto samples = constant(960U, 0.5F);
  auto snapshot = buildOrFail(makeRequest(samples, cues));
  presentation.setState(character::State::Warning);
  const auto accepted = presentation.setPerformanceSnapshot(std::move(snapshot));
  CHECK(accepted.hasValue());
  CHECK(presentation.hasPerformanceSnapshot());
  CHECK(presentation.performanceSnapshot() != nullptr);
  CHECK(presentation.performanceSnapshot()->renderRevision == 7U);

  const auto vowel = presentation.performanceFrameAt(300);
  CHECK(vowel.performing);
  CHECK(vowel.mouth == character::MouthShape::Open);
  CHECK(vowel.energy > 0.0F);
  // A phrase playing does not make the dock Complete: the warning is still the warning.
  CHECK(presentation.state() == character::State::Warning);
  const auto outside = presentation.performanceFrameAt(5000);
  CHECK(!outside.performing);
  CHECK(outside.mouth == character::MouthShape::Closed);
  CHECK(presentation.state() == character::State::Warning);

  presentation.clearPerformanceSnapshot();
  CHECK(!presentation.hasPerformanceSnapshot());
  CHECK(presentation.performanceSnapshot() == nullptr);
  const auto cleared = presentation.performanceFrameAt(300);
  CHECK(!cleared.performing);
  CHECK(cleared.mouth == character::MouthShape::Closed);
  CHECK(cleared.energy == 0.0F);
  CHECK(presentation.state() == character::State::Warning);
}

TEST_CASE("A snapshot the dock cannot draw is refused instead of replacing what it shows") {
  const auto samples = constant(960U, 0.5F);
  native_ui::CharacterPresentation presentation;
  CHECK(presentation.setPerformanceSnapshot(buildOrFail(makeRequest(samples))).hasValue());
  CHECK(presentation.performanceSnapshot()->energy[0] > 0.0F);

  auto tampered = buildOrFail(makeRequest(samples));
  tampered.energy[0] = 2.0F;
  const auto refused = presentation.setPerformanceSnapshot(std::move(tampered));
  CHECK(!refused.hasValue());
  CHECK(refused.error().code == core::ErrorCode::InvalidArgument);
  CHECK(presentation.hasPerformanceSnapshot());
  CHECK(presentation.performanceSnapshot()->energy[0] <= 1.0F);
  CHECK(presentation.performanceFrameAt(300).performing);
}

}  // namespace
