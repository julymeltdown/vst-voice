#include "test_framework.hpp"

#include "seam/voice_design/articulated_stream.hpp"
#include "seam/voice_design/articulation_plan.hpp"
#include "seam/voice_design/frication_gesture_stream.hpp"
#include "seam/voice_design/phonation_source.hpp"
#include "seam/voice_design/recipe_resource.hpp"

#include "seam/synthesis/performance_compiler.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"

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

// One note that ends in a consonant: the vowel owns the note up to the coda's start, and the
// coda owns the tail. Its placement comes from the compiler's resolved start, not from a score
// boundary, which is what makes a vowel-to-coda unit a real gesture pair.
Syllable codaSyllable(const std::string& coda, time::SampleFrame codaStart, bool codaVoiced,
                      time::SampleFrame noteEnd = kNoteEnd) {
  Syllable value;
  const domain::PhonemeKey vowel{domain::NoteId{7U}, 0U};
  const domain::PhonemeKey tail{domain::NoteId{7U}, 1U};
  value.phones.push_back(domain::PhonemeToken{.key = vowel, .symbol = "a",
                                              .role = domain::PhonemeRole::Nucleus, .voiced = true});
  value.phones.push_back(domain::PhonemeToken{.key = tail, .symbol = coda,
                                              .role = domain::PhonemeRole::Coda, .voiced = codaVoiced});
  value.timing.push_back(synthesis::PhonemeTimingAnchor{.key = vowel,
                                                        .nucleusFrame = kNoteStart,
                                                        .endFrame = codaStart,
                                                        .explicitStartFrame = kNoteStart,
                                                        .syllableIndex = 0U,
                                                        .nucleusKey = vowel,
                                                        .endExplicit = true,
                                                        .voiced = true});
  value.timing.push_back(synthesis::PhonemeTimingAnchor{.key = tail,
                                                        .nucleusFrame = kNoteStart,
                                                        .endFrame = noteEnd,
                                                        .syllableIndex = 0U,
                                                        .nucleusKey = vowel,
                                                        .endExplicit = true,
                                                        .voiced = codaVoiced,
                                                        .inferredStartFrame = codaStart});
  return value;
}

core::Result<ArticulationPlan> codaPlan(const std::string& coda, bool codaVoiced,
                                        time::SampleFrame codaStart,
                                        std::span<const FricationBinding> frications) {
  auto fixture = codaSyllable(coda, codaStart, codaVoiced);
  return ArticulationPlan::compile(fixture.phones, fixture.timing, frications, kRate,
                                   synthesis::PhraseFrameRange{kNoteStart, kNoteEnd}, {}, {}, {}, {});
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

voice_design::VoiceRecipe codaRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "coda-context-test";
  recipe.seed = 43U;
  recipe.phonation.aspiration = 0.0;
  recipe.poses = {{"a", "neutral", 0.0, {{800.0, 90.0, 0.0}, {1250.0, 110.0, -3.0}, {2800.0, 160.0, -6.0}}}};
  recipe.frications = {{"s", "neutral", voice_design::FricationConfig{.seed = 44U, .centerHz = 5500.0,
                                                                     .bandwidthHz = 3000.0, .gain = 0.12}}};
  return recipe;
}

// One note whose explicit phone hint is a vowel followed by a coda. The tokens therefore come
// from the real phonemizer, which is where an ordinary consonant's place in the syllable is
// decided.
struct CodaRenderFixture final {
  domain::Project project{domain::ProjectId{21U}, "Coda render fixture"};
  domain::VocalRegion region;
  domain::NoteId noteId{domain::NoteId{25U}};
  std::vector<domain::PhonemeToken> phones;
};

CodaRenderFixture codaRenderFixture() {
  CodaRenderFixture fixture;
  fixture.region = domain::VocalRegion{
      .id = domain::RegionId{23U},
      .name = "Note",
      .durationTick = time::Tick{960},
      .lyrics = {{domain::LyricTokenId{24U}, U"\u3042", domain::Language::Japanese}},
      .notes = {{.id = fixture.noteId,
                 .durationTick = time::Tick{960},
                 .midiKey = 60U,
                 .lyricTokenId = domain::LyricTokenId{24U},
                 .phoneticHint = "a s"}}};
  const auto pronunciation = phonemizer::JapaneseKanaPhonemizer{}.phonemize(fixture.region);
  fixture.phones = pronunciation.tokensForNote(fixture.noteId);
  return fixture;
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

// One note whose explicit phone hint is a palatalized consonant and its vowel. The hint is the
// inventory's own spelling, so the phone keeps its identity instead of becoming k plus y.
struct PalatalizedRenderFixture final {
  domain::Project project{domain::ProjectId{41U}, "Palatalized render fixture"};
  domain::VocalRegion region;
  domain::NoteId noteId{domain::NoteId{45U}};
  std::vector<domain::PhonemeToken> phones;
};

PalatalizedRenderFixture palatalizedRenderFixture(const char* hint) {
  PalatalizedRenderFixture fixture;
  fixture.region = domain::VocalRegion{
      .id = domain::RegionId{43U},
      .name = "Note",
      .durationTick = time::Tick{960},
      .lyrics = {{domain::LyricTokenId{44U}, U"\u3042", domain::Language::Japanese}},
      .notes = {{.id = fixture.noteId,
                 .durationTick = time::Tick{960},
                 .midiKey = 60U,
                 .lyricTokenId = domain::LyricTokenId{44U},
                 .phoneticHint = hint}}};
  const auto pronunciation = phonemizer::JapaneseKanaPhonemizer{}.phonemize(fixture.region);
  fixture.phones = pronunciation.tokensForNote(fixture.noteId);
  return fixture;
}

// The base release plus the palatal pose that release moves out of, which is what makes a
// palatalized consonant different from its base rather than the same sound under a new label.
voice_design::VoiceRecipe palatalizedRecipe() {
  voice_design::VoiceRecipe recipe;
  recipe.id = "palatalized-context-test";
  recipe.seed = 61U;
  recipe.phonation.aspiration = 0.0;
  recipe.poses = {{"a", "neutral", 0.0, {{800.0, 90.0, 0.0}, {1250.0, 110.0, -3.0}, {2800.0, 160.0, -6.0}}},
                  {"ky", "neutral", 0.0, {{250.0, 70.0, 0.0}, {2200.0, 120.0, -3.0}, {3000.0, 170.0, -6.0}}}};
  recipe.plosives = {{"k", "neutral", {.seed = 62U, .centerHz = 2500.0, .bandwidthHz = 2200.0, .gain = 0.12}, 10.0}};
  recipe.palatalized = {{"ky", "neutral", "k"}};
  return recipe;
}

// The energy in the 200-500 Hz region relative to the 500-1000 Hz region of one rendered window.
// The palatal pose holds its first resonance at 250 Hz and the vowel holds its own at 800 Hz, so
// this balance says which of the two the tract is at. A directional pose check, not an
// intelligibility or quality claim.
double lowResonanceBalance(std::span<const float> samples, time::SampleFrame begin, time::SampleFrame end) {
  constexpr double kPi = 3.14159265358979323846;
  const auto width = static_cast<std::size_t>(end - begin);
  if (width < 2U) return 0.0;
  double low = 0.0;
  double high = 0.0;
  for (std::uint32_t hz = 200U; hz <= 1000U; hz += 50U) {
    double real = 0.0;
    double imaginary = 0.0;
    for (std::size_t index = 0U; index < width; ++index) {
      const auto hann = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(index) / static_cast<double>(width - 1U));
      const auto angle = 2.0 * kPi * static_cast<double>(hz) * static_cast<double>(index) / static_cast<double>(kRate);
      const auto value = static_cast<double>(samples[static_cast<std::size_t>(begin) + index]) * hann;
      real += value * std::cos(angle);
      imaginary += value * std::sin(angle);
    }
    const auto energy = (real * real + imaginary * imaginary) / static_cast<double>(width * width);
    if (hz <= 500U) low += energy;
    else high += energy;
  }
  return low / (high + 1e-15);
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

TEST_CASE("a palatalized consonant renders its own pose rather than its base consonant") {
  using namespace seam;
  const auto palatalized = palatalizedRenderFixture("ky a");
  const auto plain = palatalizedRenderFixture("k a");
  // The hint keeps the phone's identity: the inventory's key names a unit a bank has to contain,
  // so a palatalized syllable is not rewritten into a consonant the song can already ask for.
  CHECK(palatalized.phones.size() == 2U);
  CHECK(palatalized.phones.front().symbol == "ky");
  CHECK(plain.phones.front().symbol == "k");
  if (palatalized.phones.size() != 2U || plain.phones.size() != 2U) return;
  const auto resource = voice_design::freezeVoiceRecipeResource(palatalizedRecipe());
  CHECK(resource);
  if (!resource) return;
  const auto palatalizedPerformance = synthesis::compileScorePerformance(
      palatalized.project, palatalized.region, kRate, palatalized.phones,
      synthesis::PhonemeTimingPolicy::ProceduralInNote);
  const auto plainPerformance = synthesis::compileScorePerformance(
      plain.project, plain.region, kRate, plain.phones,
      synthesis::PhonemeTimingPolicy::ProceduralInNote);
  CHECK(palatalizedPerformance);
  CHECK(plainPerformance);
  if (!palatalizedPerformance || !plainPerformance) return;
  const auto palatalizedPlan = voice_design::ArticulationPlan::compileRecipe(
      resource.value(), palatalizedPerformance.value(), palatalized.phones, "neutral");
  const auto plainPlan = voice_design::ArticulationPlan::compileRecipe(
      resource.value(), plainPerformance.value(), plain.phones, "neutral");
  CHECK(palatalizedPlan);
  CHECK(plainPlan);
  if (!palatalizedPlan || !plainPlan) return;
  const auto& onset = palatalizedPlan.value().gestures().front();
  CHECK(onset.kind == ArticulationGestureKind::Plosive);
  CHECK(onset.phone == "ky");
  CHECK(onset.posePhone.has_value());
  if (onset.posePhone) CHECK(*onset.posePhone == "ky");
  // The release is the base consonant's own burst; the palatalization is the resonance the release
  // moves out of, not a second noise source invented for the symbol.
  CHECK(onset.plosive.has_value());
  if (onset.plosive) CHECK(onset.plosive->burst.centerHz == 2500.0);
  CHECK(!plainPlan.value().gestures().front().posePhone.has_value());
  // A frozen recipe that does not declare the palatalized phone cannot render this plan: the base
  // consonant under a new name is exactly what the plan may not become.
  auto withoutPalatalized = palatalizedRecipe();
  withoutPalatalized.palatalized.clear();
  const auto baseOnly = voice_design::freezeVoiceRecipeResource(withoutPalatalized);
  CHECK(baseOnly);
  if (!baseOnly) return;
  const auto relabelled = voice_design::ArticulatedStream::create(
      baseOnly.value(), palatalizedPerformance.value(), palatalizedPlan.value(), "neutral");
  CHECK(!relabelled);
  if (!relabelled) CHECK(relabelled.error().message.find("Palatalized") != std::string::npos);
  auto palatalizedStream = voice_design::ArticulatedStream::createFromRecipe(
      resource.value(), palatalizedPerformance.value(), palatalized.phones, "neutral", 257U);
  auto plainStream = voice_design::ArticulatedStream::createFromRecipe(
      resource.value(), plainPerformance.value(), plain.phones, "neutral", 257U);
  CHECK(palatalizedStream);
  CHECK(plainStream);
  if (!palatalizedStream || !plainStream) return;
  const synthesis::PhraseFrameRange palatalizedRange{palatalizedPlan.value().context().start,
                                                    palatalizedPlan.value().context().end};
  const synthesis::PhraseFrameRange plainRange{plainPlan.value().context().start,
                                               plainPlan.value().context().end};
  const auto palatalizedAudio = palatalizedStream.value().renderOwned(palatalizedRange);
  const auto plainAudio = plainStream.value().renderOwned(plainRange);
  CHECK(palatalizedAudio);
  CHECK(plainAudio);
  if (!palatalizedAudio || !plainAudio) return;
  const auto nucleus = palatalizedPlan.value().gestures().back().span.start;
  const auto window = static_cast<time::SampleFrame>(kRate / 100U);
  // The vowel leaves the palatal pose instead of already being at its own, so its opening frames
  // hold the palatal first resonance and then settle onto the vowel. Directional, and no
  // intelligibility claim follows.
  const auto onsetPalatalized = lowResonanceBalance(palatalizedAudio.value().samples, nucleus, nucleus + window);
  const auto onsetPlain = lowResonanceBalance(plainAudio.value().samples, nucleus, nucleus + window);
  CHECK(onsetPalatalized > onsetPlain);
  const auto settled = nucleus + static_cast<time::SampleFrame>(kRate / 20U);
  const auto settledPalatalized = lowResonanceBalance(palatalizedAudio.value().samples, settled, settled + window);
  const auto settledPlain = lowResonanceBalance(plainAudio.value().samples, settled, settled + window);
  CHECK(std::abs(settledPalatalized - settledPlain) < 0.01);
  CHECK(std::any_of(palatalizedAudio.value().samples.begin(),
                    palatalizedAudio.value().samples.begin() + nucleus,
                    [](float sample) { return sample != 0.0F; }));
  CHECK(std::any_of(palatalizedAudio.value().samples.begin() + nucleus,
                    palatalizedAudio.value().samples.end(),
                    [](float sample) { return sample != 0.0F; }));
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
TEST_CASE("a consonant after the vowel is a coda gesture that owns the tail") {
  const std::vector<FricationBinding> frications{frication("s", 5500.0)};
  const auto codaStart = kNucleus;
  const auto plan = codaPlan("s", false, codaStart, frications);
  CHECK(plan);
  if (!plan) return;
  CHECK(plan.value().gestures().size() == 2U);
  const auto& vowel = plan.value().gestures().front();
  const auto& coda = plan.value().gestures().back();
  CHECK(vowel.kind == ArticulationGestureKind::OralVowel);
  CHECK(vowel.span.start == kNoteStart);
  CHECK(vowel.span.end == codaStart);
  CHECK(coda.kind == ArticulationGestureKind::Frication);
  CHECK(coda.phone == "s");
  CHECK(coda.span.start == codaStart);
  CHECK(coda.span.end == kNoteEnd);
  CHECK(!voice_design::isVoicedGesture(coda.kind));
  CHECK(coda.frication.has_value());
  // The two gestures stay ordered and do not overlap: the coda begins where the vowel ends.
  CHECK(vowel.span.end == coda.span.start);
  // A coda whose start was never resolved is refused instead of being placed at an invented
  // frame, and a coda that would begin before its own nucleus is a conflict.
  auto unresolved = codaSyllable("s", codaStart, false);
  unresolved.timing.back().inferredStartFrame.reset();
  const auto refused = ArticulationPlan::compile(unresolved.phones, unresolved.timing, frications, kRate,
      synthesis::PhraseFrameRange{kNoteStart, kNoteEnd}, {}, {}, {}, {});
  CHECK(!refused);
  if (!refused) CHECK(refused.error().message.find("resolved start") != std::string::npos);
  auto crossing = codaSyllable("s", codaStart, false);
  crossing.timing.back().inferredStartFrame = kNoteStart;
  CHECK(!ArticulationPlan::compile(crossing.phones, crossing.timing, frications, kRate,
      synthesis::PhraseFrameRange{kNoteStart, kNoteEnd}, {}, {}, {}, {}));
}

TEST_CASE("a vowel-to-coda unit renders its vowel and then its coda") {
  auto fixture = codaRenderFixture();
  CHECK(fixture.phones.size() == 2U);
  if (fixture.phones.size() != 2U) return;
  // The hint decided the roles, so the second token is a coda rather than a nameless consonant.
  CHECK(fixture.phones.front().role == domain::PhonemeRole::Nucleus);
  CHECK(fixture.phones.back().role == domain::PhonemeRole::Coda);
  CHECK(fixture.phones.back().symbol == "s");
  CHECK(!fixture.phones.back().voiced);
  const auto performance = synthesis::compileScorePerformance(fixture.project, fixture.region, kRate,
      fixture.phones, synthesis::PhonemeTimingPolicy::ProceduralInNote);
  CHECK(performance);
  if (!performance) return;
  const auto& anchors = performance.value().phonemeTiming();
  CHECK(anchors.size() == 2U);
  if (anchors.size() != 2U) return;
  // The compiler reserved the coda's own tail instead of leaving its start unresolved.
  CHECK(anchors.back().inferredStartFrame.has_value());
  if (!anchors.back().inferredStartFrame) return;
  const auto codaStart = *anchors.back().inferredStartFrame;
  CHECK(codaStart > anchors.front().nucleusFrame);
  CHECK(anchors.front().endFrame == codaStart);
  CHECK(!performance.value().notes().empty());
  if (performance.value().notes().empty()) return;
  CHECK(anchors.back().endFrame == performance.value().notes().back().endFrame);
  const auto resource = voice_design::freezeVoiceRecipeResource(codaRecipe());
  CHECK(resource);
  if (!resource) return;
  auto stream = voice_design::ArticulatedStream::createFromRecipe(resource.value(), performance.value(),
      fixture.phones, "neutral", 257U);
  CHECK(stream);
  if (!stream) return;
  const auto plan = ArticulationPlan::compileRecipe(resource.value(), performance.value(), fixture.phones, "neutral");
  CHECK(plan);
  if (!plan) return;
  CHECK(plan.value().gestures().size() == 2U);
  if (plan.value().gestures().size() != 2U) return;
  CHECK(plan.value().gestures().front().kind == ArticulationGestureKind::OralVowel);
  CHECK(plan.value().gestures().back().kind == ArticulationGestureKind::Frication);
  CHECK(plan.value().gestures().front().span.end == codaStart);
  CHECK(plan.value().gestures().back().span.start == codaStart);
  const synthesis::PhraseFrameRange whole{plan.value().context().start, plan.value().context().end};
  const auto audio = stream.value().renderOwned(whole);
  CHECK(audio);
  if (!audio) return;
  const auto samples = audio.value().samples;
  const auto nonzero = [&](time::SampleFrame begin, time::SampleFrame end) {
    return std::any_of(samples.begin() + begin, samples.begin() + end,
                       [](float sample) { return sample != 0.0F; });
  };
  // Both halves of the unit are audible: the vowel owns the span before the coda, and the
  // coda is not silence.
  CHECK(nonzero(anchors.front().nucleusFrame, codaStart));
  CHECK(nonzero(codaStart, whole.end));
  // The same owned range from the same context is identical in one window or two.
  auto second = voice_design::ArticulatedStream::createFromRecipe(resource.value(), performance.value(),
      fixture.phones, "neutral", 111U);
  CHECK(second);
  if (!second) return;
  const auto split = codaStart;
  const auto prefix = second.value().renderOwned({whole.start, split});
  const auto suffix = second.value().renderOwned({split, whole.end});
  CHECK(prefix);
  CHECK(suffix);
  if (!prefix || !suffix) return;
  std::vector<float> joined = prefix.value().samples;
  joined.insert(joined.end(), suffix.value().samples.begin(), suffix.value().samples.end());
  CHECK(joined == samples);
}
