#include "test_framework.hpp"

#include "seam/voice_design/articulated_stream.hpp"
#include "seam/voice_design/articulation_plan.hpp"
#include "seam/voice_design/frication_gesture_stream.hpp"
#include "seam/voice_design/phonation_source.hpp"
#include "seam/voice_design/recipe_resource.hpp"

#include "seam/synthesis/performance_compiler.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace seam;
using voice_design::AffricateBinding;
using voice_design::ArticulationGestureKind;
using voice_design::ArticulationPlan;
using voice_design::FricationBinding;
using voice_design::ApproximantBinding;

constexpr std::uint32_t kRate{48000U};
constexpr time::SampleFrame kNoteStart{0};
constexpr time::SampleFrame kNucleus{24000};   // 500 ms into a one-second note
constexpr time::SampleFrame kNoteEnd{48000};

// The unreleased pairing the affricate is built from: a low-frequency release spectrum for the
// stop and a high-frequency tail for the fricative.
FricationBinding frication(const std::string& phone, double centerHz) {
  return FricationBinding{phone, voice_design::FricationConfig{.seed = 7U,
                                                              .centerHz = centerHz,
                                                              .bandwidthHz = 3000.0,
                                                              .gain = 0.12}};
}

AffricateBinding affricate(const std::string& phone, double burstHz, double tailHz,
                           double burstMilliseconds = 10.0) {
  return AffricateBinding{phone,
                          voice_design::FricationConfig{
                              .seed = 11U, .centerHz = burstHz, .bandwidthHz = 2500.0, .gain = 0.12},
                          voice_design::FricationConfig{
                              .seed = 12U, .centerHz = tailHz, .bandwidthHz = 3000.0, .gain = 0.12},
                          burstMilliseconds};
}

// One syllable: an onset consonant followed by its vowel nucleus.
struct Syllable final {
  std::vector<domain::PhonemeToken> phones;
  std::vector<synthesis::PhonemeTimingAnchor> timing;
};

Syllable syllable(const std::string& onset, bool onsetVoiced, time::SampleFrame nucleusFrame,
                  time::SampleFrame noteEnd = kNoteEnd) {
  Syllable value;
  const domain::PhonemeKey consonant{domain::NoteId{5U}, 0U};
  const domain::PhonemeKey vowel{domain::NoteId{5U}, 1U};
  value.phones.push_back(domain::PhonemeToken{.key = consonant,
                                              .symbol = onset,
                                              .role = domain::PhonemeRole::Onset,
                                              .voiced = onsetVoiced});
  value.phones.push_back(domain::PhonemeToken{.key = vowel,
                                              .symbol = "a",
                                              .role = domain::PhonemeRole::Nucleus,
                                              .voiced = true});
  value.timing.push_back(synthesis::PhonemeTimingAnchor{.key = consonant,
                                                        .nucleusFrame = nucleusFrame,
                                                        .endFrame = nucleusFrame,
                                                        .explicitStartFrame = kNoteStart,
                                                        .syllableIndex = 0U,
                                                        .nucleusKey = vowel,
                                                        .voiced = onsetVoiced});
  value.timing.push_back(synthesis::PhonemeTimingAnchor{.key = vowel,
                                                        .nucleusFrame = nucleusFrame,
                                                        .endFrame = noteEnd,
                                                        .explicitStartFrame = nucleusFrame,
                                                        .syllableIndex = 0U,
                                                        .nucleusKey = vowel,
                                                        .endExplicit = true,
                                                        .voiced = true});
  return value;
}

core::Result<ArticulationPlan> planFor(const std::string& onset, bool onsetVoiced,
                                       time::SampleFrame nucleusFrame,
                                       std::span<const FricationBinding> frications,
                                       std::span<const AffricateBinding> affricates) {
  auto fixture = syllable(onset, onsetVoiced, nucleusFrame);
  return ArticulationPlan::compile(fixture.phones, fixture.timing, frications, kRate,
                                   synthesis::PhraseFrameRange{kNoteStart, kNoteEnd}, {}, {},
                                   affricates, {});
}

core::Result<ArticulationPlan> approximantPlan(const std::string& onset, bool onsetVoiced,
                                               time::SampleFrame nucleusFrame,
                                               std::span<const ApproximantBinding> approximants) {
  auto fixture = syllable(onset, onsetVoiced, nucleusFrame);
  return ArticulationPlan::compile(fixture.phones, fixture.timing, {}, kRate,
                                   synthesis::PhraseFrameRange{kNoteStart, kNoteEnd}, {}, {}, {}, approximants);
}

// A one-note Japanese syllable whose onset is a voiced approximant, compiled through the real
// score compiler the way a song would be.
struct GlideFixture final {
  domain::Project project{domain::ProjectId{11U}, "Glide fixture"};
  domain::VocalRegion region;
  std::vector<domain::PhonemeToken> phones;
};

GlideFixture glideFixture(const std::string& onsetSymbol) {
  GlideFixture fixture;
  fixture.region = domain::VocalRegion{
      .id = domain::RegionId{13U},
      .name = "Note",
      .durationTick = time::Tick{960},
      .lyrics = {{domain::LyricTokenId{14U}, U"や", domain::Language::Japanese}},
      .notes = {{.id = domain::NoteId{15U},
                 .durationTick = time::Tick{960},
                 .midiKey = 67U,
                 .lyricTokenId = domain::LyricTokenId{14U}}}};
  fixture.phones = {
      // The onset states where it starts and the vowel states where its nucleus begins, exactly
      // as an authored syllable does; the glide then fills the span before that nucleus.
      {.key = {domain::NoteId{15U}, 0U},
       .symbol = onsetSymbol,
       .role = domain::PhonemeRole::Onset,
       .voiced = true,
       .timing = {.startOffset = time::Microseconds{0}}},
      {.key = {domain::NoteId{15U}, 1U},
       .symbol = "a",
       .role = domain::PhonemeRole::Nucleus,
       .voiced = true,
       .timing = {.startOffset = time::Microseconds{100000}}}};
  return fixture;
}

// The energy in the 500-1000 Hz region relative to the 100-500 Hz region of one rendered window.
// This recipe's glide pose puts its energy at a 250 Hz first resonance and its vowel at a 800 Hz
// one, so this balance follows the vocal-tract pose. A zero-crossing count would not: at this
// pitch it follows the excitation's own harmonic spacing instead.
double lowBandBalance(std::span<const float> samples, time::SampleFrame begin, time::SampleFrame end) {
  constexpr double kPi = 3.14159265358979323846;
  double first = 0.0;
  double second = 0.0;
  const auto width = static_cast<std::size_t>(end - begin);
  for (std::uint32_t hz = 100U; hz <= 1000U; hz += 50U) {
    double real = 0.0;
    double imaginary = 0.0;
    for (std::size_t index = 0U; index < width; ++index) {
      const auto hann = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(index) /
                                             static_cast<double>(width - 1U));
      const auto angle = 2.0 * kPi * static_cast<double>(hz) * static_cast<double>(index) /
                         static_cast<double>(kRate);
      const auto value = static_cast<double>(samples[static_cast<std::size_t>(begin) + index]) * hann;
      real += value * std::cos(angle);
      imaginary += value * std::sin(angle);
    }
    const auto energy = (real * real + imaginary * imaginary) / static_cast<double>(width * width);
    if (hz < 500U) first += energy;
    else second += energy;
  }
  return second / first;
}

voice_design::VoiceRecipe glideRecipe(double transitionMilliseconds = 60.0) {
  voice_design::VoiceRecipe recipe;
  recipe.id = "approximant-context-test";
  recipe.seed = 31U;
  recipe.phonation.aspiration = 0.0;
  // A palatal glide into a low vowel moves the spectrum down, which makes the transition
  // measurable rather than merely scheduled.
  recipe.poses = {{"a", "neutral", 0.0, {{800.0, 90.0, 0.0}, {1250.0, 110.0, -3.0}, {2800.0, 160.0, -6.0}}},
                  {"y", "neutral", 0.0, {{250.0, 70.0, 0.0}, {2200.0, 120.0, -3.0}, {3000.0, 170.0, -6.0}}}};
  recipe.approximants = {{"y", "neutral", transitionMilliseconds}};
  return recipe;
}

}  // namespace

TEST_CASE("an affricate is one gesture whose release continues into its tail") {
  const std::vector<AffricateBinding> affricates{affricate("ts", 4500.0, 5500.0)};
  const auto plan = planFor("ts", false, kNucleus, {}, affricates);
  CHECK(plan);
  if (!plan) return;
  CHECK(plan.value().gestures().size() == 2U);
  const auto& gesture = plan.value().gestures().front();
  CHECK(gesture.kind == ArticulationGestureKind::Affricate);
  CHECK(gesture.phone == "ts");
  CHECK(gesture.span.start == kNoteStart);
  CHECK(gesture.span.end == kNucleus);
  CHECK(gesture.affricate.has_value());
  CHECK(!gesture.plosive);
  CHECK(!gesture.frication);
  if (!gesture.affricate) return;
  const auto span = kNucleus - kNoteStart;
  const auto burst = static_cast<time::SampleFrame>(std::llround(10.0 * kRate / 1000.0));
  const auto minimumTail = static_cast<time::SampleFrame>(
      std::llround(ArticulationPlan::kMinimumAffricateTailMilliseconds * kRate / 1000.0));
  CHECK(gesture.affricate->release.burstFrames == burst);
  // The tail is the larger half once the note has room, and the closure takes the rest.
  const auto afterBurst = span - burst;
  const auto expectedTail = std::min<time::SampleFrame>(afterBurst - 1,
      std::max<time::SampleFrame>(minimumTail, afterBurst / 2));
  CHECK(gesture.affricate->tailFrames == expectedTail);
  CHECK(gesture.affricate->release.closureFrames == afterBurst - expectedTail);
  CHECK(gesture.affricate->release.closureFrames + gesture.affricate->release.burstFrames +
            gesture.affricate->tailFrames ==
        span);
  CHECK(gesture.affricate->release.burst.centerHz == 4500.0);
  CHECK(gesture.affricate->tail.centerHz == 5500.0);
  // The vowel keeps its own ordered span; the affricate does not absorb it.
  CHECK(plan.value().gestures().back().kind == ArticulationGestureKind::OralVowel);
  CHECK(plan.value().gestures().back().span.start == kNucleus);
}

TEST_CASE("an affricate needs room for its closure, release and tail") {
  const std::vector<AffricateBinding> affricates{affricate("ch", 3000.0, 4500.0, 12.0)};
  // 30 ms of onset is shorter than the 12 ms release plus the 20 ms minimum tail.
  const auto shortNote = planFor("ch", false, 1440, {}, affricates);
  CHECK(!shortNote);
  if (!shortNote) {
    CHECK(shortNote.error().code == core::ErrorCode::Unsupported);
    CHECK(shortNote.error().message.find("ch") != std::string::npos);
    CHECK(shortNote.error().message.find("ms") != std::string::npos);
  }
  // The same phone in a note with room is admitted, which is what makes the refusal actionable
  // rather than a blanket unsupported phone.
  CHECK(planFor("ch", false, kNucleus, {}, affricates));
}

TEST_CASE("a voiced affricate is refused rather than rendered as unvoiced noise") {
  const std::vector<AffricateBinding> affricates{affricate("ts", 4500.0, 5500.0)};
  const auto voiced = planFor("ts", true, kNucleus, {}, affricates);
  CHECK(!voiced);
  if (voiced) return;
  CHECK(voiced.error().code == core::ErrorCode::Unsupported);
  CHECK(voiced.error().message.find("voiced affricate") != std::string::npos);
  // An unbound unvoiced phone is still refused with the unsupported-phone message rather than
  // silently becoming an affricate.
  const auto unbound = planFor("ts", false, kNucleus, {}, {});
  CHECK(!unbound);
}

TEST_CASE("frication, plosive and affricate stay distinct gestures") {
  const std::vector<FricationBinding> frications{frication("s", 5500.0)};
  const std::vector<voice_design::PlosiveBinding> stops{
      {"t", voice_design::FricationConfig{.seed = 9U, .centerHz = 4500.0, .bandwidthHz = 3000.0, .gain = 0.12}, 10.0}};
  const std::vector<AffricateBinding> affricates{affricate("ts", 4500.0, 5500.0)};
  auto fricationPlan = syllable("s", false, kNucleus);
  const auto s = ArticulationPlan::compile(fricationPlan.phones, fricationPlan.timing, frications,
                                           kRate, synthesis::PhraseFrameRange{kNoteStart, kNoteEnd});
  CHECK(s);
  if (s) CHECK(s.value().gestures().front().kind == ArticulationGestureKind::Frication);
  auto stopPlan = syllable("t", false, kNucleus);
  const auto t = ArticulationPlan::compile(stopPlan.phones, stopPlan.timing, frications, kRate,
                                           synthesis::PhraseFrameRange{kNoteStart, kNoteEnd}, {}, stops);
  CHECK(t);
  if (t) CHECK(t.value().gestures().front().kind == ArticulationGestureKind::Plosive);
  const auto ts = planFor("ts", false, kNucleus, frications, affricates);
  CHECK(ts);
  if (ts) CHECK(ts.value().gestures().front().kind == ArticulationGestureKind::Affricate);

  // A binding the build does not admit is refused instead of being treated as a frication.
  const std::vector<AffricateBinding> unsupported{affricate("dz", 3000.0, 4500.0)};
  CHECK(!planFor("dz", false, kNucleus, {}, unsupported));
}

TEST_CASE("the aperiodic lane renders the affricate's closure, release and tail") {
  const std::vector<AffricateBinding> affricates{affricate("ts", 4500.0, 5500.0)};
  const auto plan = planFor("ts", false, kNucleus, {}, affricates);
  CHECK(plan);
  if (!plan) return;
  const auto& gesture = plan.value().gestures().front();
  CHECK(gesture.affricate.has_value());
  if (!gesture.affricate) return;
  auto stream = voice_design::FricationGestureStream::create(plan.value());
  CHECK(stream);
  if (!stream) return;
  const synthesis::PhraseFrameRange context = plan.value().context();
  const auto whole = stream.value().renderOwned(context);
  CHECK(whole);
  if (!whole) return;
  const auto closureEnd = gesture.span.start + static_cast<time::SampleFrame>(gesture.affricate->release.closureFrames);
  const auto burstEnd = closureEnd + static_cast<time::SampleFrame>(gesture.affricate->release.burstFrames);
  const auto quiet = [&](time::SampleFrame begin, time::SampleFrame end) {
    return std::all_of(whole.value().samples.begin() + begin, whole.value().samples.begin() + end,
                       [](float sample) { return sample == 0.0F; });
  };
  const auto loud = [&](time::SampleFrame begin, time::SampleFrame end) {
    return std::any_of(whole.value().samples.begin() + begin, whole.value().samples.begin() + end,
                       [](float sample) { return sample != 0.0F; });
  };
  CHECK(quiet(gesture.span.start, closureEnd));
  CHECK(loud(closureEnd, burstEnd));
  CHECK(loud(burstEnd, gesture.span.end));
  // The aperiodic lane owns only this gesture; the vowel's frames stay silent there.
  CHECK(quiet(kNucleus, kNoteEnd));

  // The same owned range from the same full context is identical whether it is rendered in one
  // window or two, which is what keeps a chunked render equal to a whole one.
  auto second = voice_design::FricationGestureStream::create(plan.value());
  CHECK(second);
  if (!second) return;
  const auto prefix = second.value().renderOwned({context.start, burstEnd});
  CHECK(prefix);
  const auto suffix = second.value().renderOwned({burstEnd, context.end});
  CHECK(suffix);
  if (!prefix || !suffix) return;
  std::vector<float> joined = prefix.value().samples;
  joined.insert(joined.end(), suffix.value().samples.begin(), suffix.value().samples.end());
  CHECK(joined == whole.value().samples);
}

TEST_CASE("a voiced approximant is a tonal gesture with a bounded transition") {
  const std::vector<ApproximantBinding> approximants{ApproximantBinding{"y", 60.0}};
  const auto plan = approximantPlan("y", true, kNucleus, approximants);
  CHECK(plan);
  if (!plan) return;
  CHECK(plan.value().gestures().size() == 2U);
  const auto& gesture = plan.value().gestures().front();
  CHECK(gesture.kind == ArticulationGestureKind::Approximant);
  CHECK(voice_design::isVoicedGesture(gesture.kind));
  CHECK(!voice_design::isAperiodicGesture(gesture.kind));
  CHECK(gesture.span.start == kNoteStart);
  CHECK(gesture.span.end == kNucleus);
  // 60 ms of transition, well inside a 500 ms onset.
  CHECK(gesture.transitionFrames == static_cast<std::uint32_t>(std::llround(60.0 * kRate / 1000.0)));
  CHECK(!gesture.affricate);
  CHECK(!gesture.frication);
  CHECK(plan.value().gestures().back().kind == ArticulationGestureKind::OralVowel);
  CHECK(plan.value().gestures().back().span.start == kNucleus);

  // The transition can never outlast the note it belongs to.
  const std::vector<ApproximantBinding> longTransition{ApproximantBinding{"y", 200.0}};
  const auto clamped = approximantPlan("y", true, 1440, longTransition);
  CHECK(clamped);
  if (clamped) CHECK(clamped.value().gestures().front().transitionFrames == 1440U);

  // A voiced approximant binding is not a way to voice an unvoiced token, and a symbol this
  // build does not admit is refused instead of being treated as a vowel.
  const auto voiceless = approximantPlan("y", false, kNucleus, approximants);
  CHECK(!voiceless);
  if (!voiceless) CHECK(voiceless.error().code == core::ErrorCode::InvalidArgument);
  const std::vector<ApproximantBinding> unsupported{ApproximantBinding{"l", 40.0}};
  CHECK(!approximantPlan("l", true, kNucleus, unsupported));
}

TEST_CASE("the approximant transition moves the spectrum into its vowel") {
  auto fixture = glideFixture("y");
  const auto performance = synthesis::compileScorePerformance(fixture.project, fixture.region,
                                                             kRate, fixture.phones);
  CHECK(performance);
  if (!performance) return;
  const auto resource = voice_design::freezeVoiceRecipeResource(glideRecipe());
  CHECK(resource);
  if (!resource) return;
  auto stream = voice_design::ArticulatedStream::createFromRecipe(
      resource.value(), performance.value(), fixture.phones, "neutral", 257U);
  if (!stream) std::cerr << "stream error: " << stream.error().message << std::endl;
  CHECK(stream);
  if (!stream) return;
  const auto context = stream.value().position();
  static_cast<void>(context);
  const auto plan = voice_design::ArticulationPlan::compileRecipe(
      resource.value(), performance.value(), fixture.phones, "neutral");
  CHECK(plan);
  if (!plan) return;
  const auto span = plan.value().gestures().front().span;
  CHECK(plan.value().gestures().front().kind == ArticulationGestureKind::Approximant);
  const synthesis::PhraseFrameRange whole{plan.value().context().start, plan.value().context().end};
  const auto audio = stream.value().renderOwned(whole);
  CHECK(audio);
  if (!audio) return;
  const auto samples = audio.value().samples;
  const auto nonzero = [&](time::SampleFrame begin, time::SampleFrame end) {
    return std::any_of(samples.begin() + begin, samples.begin() + end,
                       [](float sample) { return sample != 0.0F; });
  };
  // A glide is voiced throughout, unlike a plosive closure.
  CHECK(nonzero(span.start, span.end));
  CHECK(nonzero(span.end, whole.end));
  // The declared transition is what moves the tract into its vowel: this recipe's glide pose holds
  // its energy at a 250 Hz first resonance and the vowel holds it at 800 Hz, so the low-band
  // balance rises across the transition. This is a directional pose check, not an intelligibility
  // or quality claim.
  const auto transitionFrames = static_cast<time::SampleFrame>(plan.value().gestures().front().transitionFrames);
  CHECK(transitionFrames > 0);
  CHECK(transitionFrames < span.end - span.start);
  const auto tailFrames = static_cast<time::SampleFrame>(kRate / 40U);
  const auto steady = lowBandBalance(samples, span.start, span.end - transitionFrames);
  const auto tail = lowBandBalance(samples, span.end - tailFrames, span.end);
  CHECK(tail > steady * 10.0);

  // The declared duration is the cause, not the gesture's mere existence: five milliseconds of
  // declaration leaves the same window in the glide's own pose.
  const auto shortResource = voice_design::freezeVoiceRecipeResource(glideRecipe(5.0));
  CHECK(shortResource);
  if (!shortResource) return;
  auto shortStream = voice_design::ArticulatedStream::createFromRecipe(
      shortResource.value(), performance.value(), fixture.phones, "neutral", 257U);
  CHECK(shortStream);
  if (!shortStream) return;
  const auto shortAudio = shortStream.value().renderOwned(whole);
  CHECK(shortAudio);
  if (!shortAudio) return;
  const auto shortTail = lowBandBalance(shortAudio.value().samples, span.end - tailFrames, span.end);
  CHECK(shortTail < tail / 10.0);

  // The same owned range from the same full context is identical in one window or two.
  auto second = voice_design::ArticulatedStream::createFromRecipe(
      resource.value(), performance.value(), fixture.phones, "neutral", 129U);
  CHECK(second);
  if (!second) return;
  const auto split = span.start + (span.end - span.start) / 2;
  const auto prefix = second.value().renderOwned({whole.start, split});
  CHECK(prefix);
  const auto suffix = second.value().renderOwned({split, whole.end});
  CHECK(suffix);
  if (!prefix || !suffix) return;
  std::vector<float> joined = prefix.value().samples;
  joined.insert(joined.end(), suffix.value().samples.begin(), suffix.value().samples.end());
  CHECK(joined == samples);

  // A plan whose transition differs from the frozen recipe cannot be rendered.
  const std::vector<ApproximantBinding> other{ApproximantBinding{"y", 20.0}};
  const auto stale = voice_design::ArticulationPlan::compileRecipe(
      resource.value(), performance.value(), fixture.phones, "neutral");
  CHECK(stale);
  const auto manual = approximantPlan("y", true, plan.value().gestures().front().span.end,
                                      other);
  CHECK(manual);
  if (manual) {
    CHECK(!voice_design::ArticulatedStream::create(resource.value(), performance.value(),
                                                   manual.value(), "neutral"));
  }
}
