#include "test_framework.hpp"

#include "seam/voice_design/articulated_stream.hpp"
#include "seam/voice_design/articulation_plan.hpp"
#include "seam/voice_design/frication_gesture_stream.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace {

using namespace seam;
using voice_design::AffricateBinding;
using voice_design::ArticulationGestureKind;
using voice_design::ArticulationPlan;
using voice_design::FricationBinding;

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
                                   affricates);
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
