#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/sha256.hpp"
#include "seam/voicebank/acoustic_analysis.hpp"
#include "seam/voicebank/wav.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace {

// A take that alternates sung vowel, unvoiced fricative, sung vowel. The plan
// asks for separate voicing states; this makes the boundary something the
// analysis has to find rather than something the fixture hands over.
std::vector<float> voicedUnvoicedVoiced(std::uint32_t sampleRate,
                                        std::size_t frames,
                                        std::size_t firstVoicedEnd,
                                        std::size_t unvoicedEnd) {
  std::vector<float> samples(frames, 0.0F);
  unsigned seed = 987654321U;
  auto noise = [&seed]() {
    seed = seed * 1103515245U + 12345U;
    return (static_cast<float>((seed >> 16U) & 0x7FFFU) / 16384.0F) - 1.0F;
  };
  for (std::size_t frame = 0U; frame < frames; ++frame) {
    const auto time = static_cast<double>(frame) / static_cast<double>(sampleRate);
    if (frame < firstVoicedEnd) {
      samples[frame] = 0.5F * static_cast<float>(
          std::sin(2.0 * std::numbers::pi * 200.0 * time));
    } else if (frame < unvoicedEnd) {
      samples[frame] = 0.35F * noise();
    } else {
      samples[frame] = 0.5F * static_cast<float>(
          std::sin(2.0 * std::numbers::pi * 240.0 * time));
    }
  }
  return samples;
}

constexpr std::uint32_t kRate = 48000U;
constexpr std::size_t kFrames = 48000U;
constexpr std::size_t kVoicedEnd = 19200U;
constexpr std::size_t kUnvoicedEnd = 28800U;

}  // namespace

// U15: the analysis is stored, versioned, and bound to the bytes it measured.
TEST_CASE("acoustic analysis records separate voicing states and binds its audio") {
  auto unit = seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 60,
      seam::voicebank::UnitKind::Sustain, kFrames);
  const auto samples = voicedUnvoicedVoiced(kRate, kFrames, kVoicedEnd, kUnvoicedEnd);
  const auto digest = seam::core::sha256Hex(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(samples.data()),
      samples.size() * sizeof(float)));

  const auto analysis = seam::voicebank::analyzeUnitAcoustics(
      samples, kRate, unit, digest, static_cast<seam::time::SampleFrame>(kFrames));
  CHECK(analysis);
  const auto& value = analysis.value();
  CHECK(value.unitId == unit.id);
  CHECK(value.audioSha256 == digest);
  CHECK(value.decodedFrames == static_cast<seam::time::SampleFrame>(kFrames));
  CHECK(value.currentAlgorithm());
  CHECK(value.spans.size() >= 3U);

  // The whole decoded extent is described, exactly once, in order.
  CHECK(value.spans.front().start == 0);
  CHECK(value.spans.back().end == static_cast<seam::time::SampleFrame>(kFrames));
  for (std::size_t index = 1U; index < value.spans.size(); ++index) {
    CHECK(value.spans[index].start == value.spans[index - 1U].end);
  }

  // Both states must actually be present, or the fixture proves nothing about
  // separation.
  const auto voicedSpans = static_cast<std::size_t>(std::count_if(
      value.spans.begin(), value.spans.end(),
      [](const auto& span) { return span.voiced; }));
  const auto unvoicedSpans = value.spans.size() - voicedSpans;
  CHECK(voicedSpans >= 2U);
  CHECK(unvoicedSpans >= 1U);

  // A voiced span carries a fundamental near the source it was measured from;
  // an unvoiced span carries none at all.
  for (const auto& span : value.spans) {
    if (span.voiced) {
      CHECK(span.f0Hz > 100.0);
      CHECK(span.f0Hz < 400.0);
    } else {
      CHECK(span.f0Hz == 0.0);
    }
  }

  // The fricative must be reported unvoiced somewhere in its middle, which is
  // the property the plan asks for and the one a per-frame analyser can get
  // wrong at the boundaries.
  const auto middleOfFricative = static_cast<seam::time::SampleFrame>(
      (kVoicedEnd + kUnvoicedEnd) / 2U);
  const auto voicedThere = seam::voicebank::acousticVoicedAt(value, middleOfFricative);
  CHECK(voicedThere.has_value());
  CHECK(voicedThere.value() == false);
  // And the first vowel must read voiced.
  const auto voicedEarly = seam::voicebank::acousticVoicedAt(
      value, static_cast<seam::time::SampleFrame>(kVoicedEnd / 2U));
  CHECK(voicedEarly.has_value());
  CHECK(voicedEarly.value() == true);

  // Round trip through the stored form preserves the measurement exactly.
  const auto encoded = seam::voicebank::encodeAcousticAnalysis(
      value, unit, digest, static_cast<seam::time::SampleFrame>(kFrames));
  CHECK(encoded);
  const auto decoded = seam::voicebank::decodeAcousticAnalysis(
      encoded.value(), unit, digest, static_cast<seam::time::SampleFrame>(kFrames));
  CHECK(decoded);
  CHECK(decoded.value() == value);

  // A sidecar path is derived from the ID digest, never from the ID itself, so an
  // ID is never interpreted as a path. Using an ID containing separators and
  // non-hex characters is what makes that observable: a raw interpolation would
  // have to show them through.
  const std::string traversalId = "ja.original.a4.k-a/../../etc/01";
  const auto path = seam::voicebank::acousticAnalysisSidecarPath(traversalId);
  CHECK(path == "analysis/" + seam::core::sha256Hex(traversalId) + ".json");
  CHECK(path.find('/') == 8U);  // only the "analysis/" separator
  CHECK(path.starts_with("analysis/"));
  CHECK(path.ends_with(".json"));
  CHECK(path.find("..") == std::string::npos);
  // Distinct IDs cannot collide onto one sidecar through a shared prefix.
  CHECK(seam::voicebank::acousticAnalysisSidecarPath("unit") !=
        seam::voicebank::acousticAnalysisSidecarPath("unit2"));
}

// The binding this contract is for: analysis of audio that has been replaced
// must not be accepted against the audio that is present now.
TEST_CASE("acoustic analysis refuses audio it was not measured from") {
  auto unit = seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 60,
      seam::voicebank::UnitKind::Sustain, kFrames);
  const auto original = seam::test::support::sineWave(kRate, 200.0, 1.0, 0.5F);
  CHECK(original.size() == kFrames);
  const auto originalDigest = seam::core::sha256Hex(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(original.data()),
      original.size() * sizeof(float)));
  const auto analysis = seam::voicebank::analyzeUnitAcoustics(
      original, kRate, unit, originalDigest,
      static_cast<seam::time::SampleFrame>(kFrames));
  CHECK(analysis);

  // Same shape, different bytes: a different take.
  const auto replacement = seam::test::support::sineWave(kRate, 150.0, 1.0, 0.5F);
  const auto replacementDigest = seam::core::sha256Hex(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(replacement.data()),
      replacement.size() * sizeof(float)));
  CHECK(replacementDigest != originalDigest);

  // Measurement against the wrong take is refused at encode and at decode.
  CHECK(!seam::voicebank::encodeAcousticAnalysis(analysis.value(), unit,
      replacementDigest, static_cast<seam::time::SampleFrame>(kFrames)));
  const auto encoded = seam::voicebank::encodeAcousticAnalysis(
      analysis.value(), unit, originalDigest,
      static_cast<seam::time::SampleFrame>(kFrames));
  CHECK(encoded);
  CHECK(!seam::voicebank::decodeAcousticAnalysis(encoded.value(), unit,
      replacementDigest, static_cast<seam::time::SampleFrame>(kFrames)));

  // A shorter recording cannot satisfy a record describing a longer one.
  CHECK(!seam::voicebank::decodeAcousticAnalysis(encoded.value(), unit,
      originalDigest, static_cast<seam::time::SampleFrame>(kFrames - 1U)));
  // Nor can a different unit claim the measurement.
  auto other = unit;
  other.id = "b";
  CHECK(!seam::voicebank::decodeAcousticAnalysis(encoded.value(), other,
      originalDigest, static_cast<seam::time::SampleFrame>(kFrames)));
}

// Regenerating derivatives after a change to the analyser is a checkable
// statement, not an intention: an older revision must be refused, not silently
// reinterpreted.
TEST_CASE("acoustic analysis refuses a record from a different algorithm revision") {
  auto unit = seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 60,
      seam::voicebank::UnitKind::Sustain, kFrames);
  const auto samples = seam::test::support::sineWave(kRate, 200.0, 1.0, 0.5F);
  CHECK(samples.size() == kFrames);
  const auto digest = seam::core::sha256Hex(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(samples.data()),
      samples.size() * sizeof(float)));
  const auto analysis = seam::voicebank::analyzeUnitAcoustics(
      samples, kRate, unit, digest, static_cast<seam::time::SampleFrame>(kFrames));
  CHECK(analysis);

  // A future revision of the analyser must invalidate what the old one recorded.
  auto stale = analysis.value();
  stale.algorithmVersion = "0";
  CHECK(!stale.currentAlgorithm());
  CHECK(!seam::voicebank::validateAcousticAnalysis(stale, unit, digest,
      static_cast<seam::time::SampleFrame>(kFrames)));
  CHECK(!seam::voicebank::encodeAcousticAnalysis(stale, unit, digest,
      static_cast<seam::time::SampleFrame>(kFrames)));

  // A different algorithm identity is likewise not interchangeable.
  auto foreign = analysis.value();
  foreign.algorithmId = "seam.pitch.something-else";
  CHECK(!foreign.currentAlgorithm());
  CHECK(!seam::voicebank::validateAcousticAnalysis(foreign, unit, digest,
      static_cast<seam::time::SampleFrame>(kFrames)));
}

// The record must not be able to contradict itself, and the decoder is a boundary
// where a hand-edited or hostile sidecar arrives.
TEST_CASE("acoustic analysis rejects self-contradictory and malformed records") {
  auto unit = seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 60,
      seam::voicebank::UnitKind::Sustain, kFrames);
  const auto samples = seam::test::support::sineWave(kRate, 200.0, 1.0, 0.5F);
  CHECK(samples.size() == kFrames);
  const auto digest = seam::core::sha256Hex(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(samples.data()),
      samples.size() * sizeof(float)));
  const auto frames = static_cast<seam::time::SampleFrame>(kFrames);
  const auto analysis = seam::voicebank::analyzeUnitAcoustics(
      samples, kRate, unit, digest, frames);
  CHECK(analysis);

  const auto rejected = [&](seam::voicebank::AcousticAnalysis candidate) {
    return !seam::voicebank::validateAcousticAnalysis(candidate, unit, digest, frames);
  };

  // A voiced span with no fundamental is a contradiction.
  auto noFundamental = analysis.value();
  noFundamental.spans.front().voiced = true;
  noFundamental.spans.front().f0Hz = 0.0;
  CHECK(rejected(noFundamental));

  // An unvoiced span carrying a fundamental is the same contradiction reversed.
  auto unvoicedWithFundamental = analysis.value();
  unvoicedWithFundamental.spans.front().voiced = false;
  unvoicedWithFundamental.spans.front().f0Hz = 220.0;
  CHECK(rejected(unvoicedWithFundamental));

  // A gap in coverage means part of the audio is undescribed.
  auto gapped = analysis.value();
  gapped.spans.front().end -= 1;
  CHECK(rejected(gapped));

  // Overlapping spans describe the same sample twice.
  auto overlapping = analysis.value();
  overlapping.spans.front().end += 1;
  CHECK(rejected(overlapping));

  // Out-of-range confidence is not a measurement.
  auto badConfidence = analysis.value();
  badConfidence.spans.front().confidence = 1.5;
  CHECK(rejected(badConfidence));

  // Coverage that stops short of the decoded extent is incomplete.
  auto short_ = analysis.value();
  short_.spans.pop_back();
  CHECK(rejected(short_));

  // An empty record describes nothing and must not be accepted.
  auto empty = analysis.value();
  empty.spans.clear();
  CHECK(rejected(empty));

  // The decoder is a boundary: malformed input fails rather than half-loading.
  const auto encoded = seam::voicebank::encodeAcousticAnalysis(
      analysis.value(), unit, digest, frames);
  CHECK(encoded);
  CHECK(!seam::voicebank::decodeAcousticAnalysis("", unit, digest, frames));
  CHECK(!seam::voicebank::decodeAcousticAnalysis("{", unit, digest, frames));
  CHECK(!seam::voicebank::decodeAcousticAnalysis("[]", unit, digest, frames));
  CHECK(!seam::voicebank::decodeAcousticAnalysis(
      R"({"formatId":"com.project-seam.acoustic-analysis","schemaVersion":99})",
      unit, digest, frames));
  CHECK(!seam::voicebank::decodeAcousticAnalysis(
      R"({"formatId":"com.project-seam.other","schemaVersion":1})",
      unit, digest, frames));
  // A record whose spans are not numbers must fail rather than coerce.
  auto tampered = encoded.value();
  const auto at = tampered.find("\"f0Hz\": ");
  CHECK(at != std::string::npos);
  tampered.replace(at, 9U, "\"f0Hz\": \"loud\"");
  CHECK(!seam::voicebank::decodeAcousticAnalysis(tampered, unit, digest, frames));
}

// A span index must answer honestly about frames it does not describe.
// The plan requires renderers and producer QC to consume the SAME analysis
// contract, not two configurations that happen to agree today. This pins that
// the producer constants, the analysis defaults and the work budget describe one
// configuration, so a change to any single copy fails here instead of silently
// making a stored analysis describe a different measurement than a fresh one.
TEST_CASE("producer analysis configuration is single-sourced") {
  const auto pitch = seam::voicebank::producerPitchConfig();
  CHECK(pitch.frameSize == seam::voicebank::kProducerFrameSize);
  CHECK(pitch.hopSize == seam::voicebank::kProducerHopSize);
  CHECK(pitch.minimumHz == seam::voicebank::kProducerMinimumHz);
  CHECK(pitch.maximumHz == seam::voicebank::kProducerMaximumHz);
  CHECK(pitch.voicingThreshold == seam::voicebank::kProducerVoicingThreshold);
  // The producer analyses the spectrum; a direct-correlation variant would answer
  // differently and could not be compared against a stored record.
  CHECK(pitch.correlationMethod == seam::voicebank::PitchCorrelationMethod::Fft);
  // The mark generator must run the same analysis, not merely a similar one.
  const auto markConfig = seam::voicebank::producerPitchMarkConfig();
  CHECK(markConfig.pitch.frameSize == pitch.frameSize);
  CHECK(markConfig.pitch.hopSize == pitch.hopSize);
  CHECK(markConfig.pitch.minimumHz == pitch.minimumHz);
  CHECK(markConfig.pitch.maximumHz == pitch.maximumHz);
  CHECK(markConfig.pitch.voicingThreshold == pitch.voicingThreshold);
  CHECK(markConfig.pitch.correlationMethod == pitch.correlationMethod);
  // The budget must scale with the take and admit exactly one pass over it, so a
  // caller cannot exceed it by analysing the same audio twice.
  const auto shortWork = seam::voicebank::producerAnalysisWork(4096U);
  const auto longWork = seam::voicebank::producerAnalysisWork(4096U * 4U);
  CHECK(longWork > shortWork);
  const auto limits = seam::voicebank::producerPitchLimits(48000U);
  CHECK(limits.maximumTransformButterflies ==
        seam::voicebank::producerAnalysisWork(48000U));
  CHECK(limits.maximumFrames > 0U);

  // And the configuration must actually be able to measure the fixture, so this
  // test cannot pass against constants that no longer work at all.
  auto unit = seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 69,
      seam::voicebank::UnitKind::Sustain, 24000U);
  const auto tone = seam::test::support::sineWave(48000U, 220.0, 0.5, 0.5F);
  CHECK(tone.size() == 24000U);
  const auto digest = seam::core::sha256Hex(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(tone.data()), tone.size() * sizeof(float)));
  const auto analysis = seam::voicebank::analyzeUnitAcoustics(
      tone, 48000U, unit, digest, static_cast<seam::time::SampleFrame>(tone.size()),
      seam::voicebank::producerPitchConfig(),
      seam::voicebank::AcousticAnalysisLimits{.maximumSpans = 4096U,
                                              .pitch = limits});
  CHECK(analysis);
  CHECK(!analysis.value().spans.empty());
}

// A span index must answer honestly about frames it does not describe.

TEST_CASE("acoustic analysis reports unknown voicing outside its analysed range") {
  auto unit = seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 60,
      seam::voicebank::UnitKind::Sustain, kFrames);
  const auto samples = seam::test::support::sineWave(kRate, 200.0, 1.0, 0.5F);
  CHECK(samples.size() == kFrames);
  const auto digest = seam::core::sha256Hex(std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(samples.data()),
      samples.size() * sizeof(float)));
  const auto frames = static_cast<seam::time::SampleFrame>(kFrames);
  const auto analysis = seam::voicebank::analyzeUnitAcoustics(
      samples, kRate, unit, digest, frames);
  CHECK(analysis);

  // Inside the coverage the answer exists.
  CHECK(seam::voicebank::acousticVoicedAt(analysis.value(), 0).has_value());
  CHECK(seam::voicebank::acousticVoicedAt(analysis.value(), frames - 1).has_value());
  // At and beyond the exclusive end it does not, and guessing would be wrong.
  CHECK(!seam::voicebank::acousticVoicedAt(analysis.value(), frames).has_value());
  CHECK(!seam::voicebank::acousticVoicedAt(analysis.value(), frames + 1000).has_value());
  CHECK(!seam::voicebank::acousticVoicedAt(analysis.value(), -1).has_value());
  // querying an empty record answers nothing rather than a default.
  seam::voicebank::AcousticAnalysis empty;
  CHECK(!seam::voicebank::acousticVoicedAt(empty, 0).has_value());
  CHECK(empty.spanAt(0) == nullptr);
}
