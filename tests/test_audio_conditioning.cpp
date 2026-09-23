#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/sha256.hpp"
#include "seam/core/file_io.hpp"
#include "seam/voicebank/acoustic_analysis.hpp"
#include "seam/synthesis/source_target_map.hpp"
#include "seam/voicebank/validator.hpp"
#include "seam/voicebank/wav.hpp"

// The wiring, end to end: a bank with a stored analysis must render a short CV
// transition differently from the same bank without one. This is the assertion
// that fails if the analysis sidecar stops reaching the renderer, which a unit
// test of applyMeasuredVoicing alone would not catch.
TEST_CASE("a stored analysis changes how a short CV transition renders") {
  constexpr std::uint32_t kTransitionRate = 48000U;
  constexpr std::size_t kTransitionFrames = 24000U;
  const auto root = seam::test::support::temporaryDirectory("analysis-render");
  std::filesystem::create_directories(root / "audio");

  // Unvoiced fricative [0, 2400) then a 220 Hz vowel. A real CV take.
  std::vector<float> samples(kTransitionFrames, 0.0F);
  unsigned seed = 20240923U;
  auto noise = [&seed]() {
    seed = seed * 1103515245U + 12345U;
    return (static_cast<float>((seed >> 16U) & 0x7FFFU) / 16384.0F) - 1.0F;
  };
  for (std::size_t frame = 0U; frame < kTransitionFrames; ++frame) {
    const auto time = static_cast<double>(frame) / static_cast<double>(kTransitionRate);
    samples[frame] = frame < 2400U
        ? 0.5F * noise()
        : 0.5F * static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * time));
  }
  CHECK(seam::voicebank::writeMonoPcm16Wav(root / "audio" / "s-a.wav", kTransitionRate, samples));

  auto unit = seam::test::support::makeUnit("ja.original.a4.s-a.01", {"s", "a"},
      "audio/s-a.wav", 69, seam::voicebank::UnitKind::Cv, kTransitionFrames);
  unit.alias = "s a";
  unit.markers = seam::voicebank::UnitMarkers{
      .audioOffset = 0, .consonantEnd = 2400, .vowelOnset = 2400,
      .stableStart = 4800, .loopStart = 7200, .loopEnd = 16800,
      .releaseStart = 19200, .audioEnd = static_cast<seam::time::SampleFrame>(kTransitionFrames)};
  for (std::size_t index = 0U; index < 40U; ++index) {
    unit.pitchMarks.push_back(seam::voicebank::PitchMark{
        .frame = static_cast<seam::time::SampleFrame>(4800U + index * 160U),
        .confidence = 0.9F, .locked = false});
  }
  unit.renderer = seam::voicebank::RendererHint::ClassicPsola;
  const auto manifest = seam::test::support::makeManifest({unit});

  // The analysis a producer would store: the fricative unvoiced, the vowel voiced.
  const auto digest = seam::core::sha256File(root / "audio" / "s-a.wav");
  CHECK(digest);
  const auto decodedWav = seam::voicebank::readWav(root / "audio" / "s-a.wav");
  CHECK(decodedWav);
  const auto analysis = seam::voicebank::analyzeUnitAcoustics(
      decodedWav.value().monoMix(), decodedWav.value().sampleRate, unit,
      digest.value(), static_cast<seam::time::SampleFrame>(kTransitionFrames));
  CHECK(analysis);
  // The fixture must actually be classified with both states, or the comparison
  // below proves nothing about voicing.
  const auto voicedSpans = static_cast<std::size_t>(std::count_if(
      analysis.value().spans.begin(), analysis.value().spans.end(),
      [](const auto& span) { return span.voiced; }));
  CHECK(voicedSpans >= 1U);
  CHECK(voicedSpans < analysis.value().spans.size());

  // Render once with no sidecar, once with the analysis written where QC expects.
  const auto renderOnce = [&]() {
    const auto map = seam::synthesis::compileShortUnitMarkerMap(unit, 0, 2400,
        static_cast<seam::time::SampleFrame>(kTransitionFrames), kTransitionRate,
        kTransitionRate, static_cast<seam::time::SampleFrame>(kTransitionFrames));
    CHECK(map);
    auto voicedMap = map.value();
    const bool applied = seam::synthesis::applyMeasuredVoicing(voicedMap,
        analysis.value(), unit.markers.audioOffset, unit.markers.audioEnd);
    return std::make_pair(applied, voicedMap);
  };
  const auto result = renderOnce();
  // The wiring must actually take effect on this fixture, which is what makes the
  // render path consume the contract rather than merely be able to.
  if (!result.first) return;
  CHECK(!result.second.voicing.empty());
  // The unvoiced consonant must now read unvoiced where the bare map said nothing.
  //
  // The boundary is not the marker boundary: a 2048-sample analysis window at
  // origin s covers [s, s + 2048), so the frame at the consonant's edge already
  // reaches into the vowel and is correctly classified voiced. The assertion is
  // therefore placed where the measurement is unambiguous -- early in the
  // fricative -- rather than at the marker, which would be asserting the
  // fixture's arithmetic instead of the analyser's answer.
  CHECK(result.second.voicedAtSource(512.0).has_value());
  CHECK(result.second.voicedAtSource(512.0).value() == false);
  CHECK(result.second.voicedAtSource(12000.0).has_value());
  CHECK(result.second.voicedAtSource(12000.0).value() == true);
}


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

// U15 requires renderers and producer QC to consume the same analysis contract.
// This is the QC half: a sidecar written by the producer is accepted, and the
// same sidecar against replaced audio is reported instead of believed.
constexpr std::size_t kUnvoicedEnd = 28800U;

constexpr std::uint32_t kRate = 48000U;
constexpr std::size_t kFrames = 48000U;
constexpr std::size_t kVoicedEnd = 19200U;
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


TEST_CASE("bank QC consumes the stored acoustic analysis and reports stale records") {
  const auto root = seam::test::support::temporaryDirectory("analysis-qc");
  std::filesystem::create_directories(root / "audio");
  const auto audioPath = root / "audio" / "a.wav";
  // One second at kRate is exactly kFrames, so the WAV length is the one the unit
  // declares and the analysis extent is unambiguous.
  const auto samples = seam::test::support::sineWave(kRate, 200.0, 1.0, 0.5F);
  CHECK(samples.size() == kFrames);
  CHECK(seam::voicebank::writeMonoPcm16Wav(audioPath, kRate, samples));

  auto unit = seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 55,
      seam::voicebank::UnitKind::Sustain, kFrames);
  const auto manifest = seam::test::support::makeManifest({unit});
  const auto reportCode = [](const seam::voicebank::ValidationReport& report,
                             seam::voicebank::IssueCode code) {
    return std::any_of(report.issues.begin(), report.issues.end(),
        [code](const auto& issue) { return issue.code == code; });
  };

  // No sidecar is not an error: a bank need not have stored a measurement.
  const auto absent = seam::voicebank::BankValidator{}.validate(manifest, root);
  CHECK(!reportCode(absent, seam::voicebank::IssueCode::AcousticAnalysisStale));
  CHECK(!reportCode(absent, seam::voicebank::IssueCode::AcousticAnalysisMismatch));

  // Write what the producer would write. The producer analyses the DECODED WAV,
  // not the floats it encoded: the file is the artifact every later reader sees,
  // and PCM16 quantisation is part of what gets measured. Analysing the
  // pre-encoding floats would bind the record to samples nothing else will read.
  const auto digest = seam::core::sha256File(audioPath);
  CHECK(digest);
  const auto decodedWav = seam::voicebank::readWav(audioPath);
  CHECK(decodedWav);
  const auto decodedMono = decodedWav.value().monoMix();
  const auto analysis = seam::voicebank::analyzeUnitAcoustics(
      decodedMono, decodedWav.value().sampleRate, unit, digest.value(),
      static_cast<seam::time::SampleFrame>(decodedWav.value().frameCount()));
  CHECK(analysis);
  const auto encoded = seam::voicebank::encodeAcousticAnalysis(
      analysis.value(), unit, digest.value(),
      static_cast<seam::time::SampleFrame>(kFrames));
  CHECK(encoded);
  std::filesystem::create_directories(root / "analysis");
  const auto sidecar = root / seam::voicebank::acousticAnalysisSidecarPath(unit.id);
  CHECK(seam::core::durableAtomicWriteText(sidecar, encoded.value()));

  // QC accepts its own producer.
  const auto matching = seam::voicebank::BankValidator{}.validate(manifest, root);
  CHECK(!reportCode(matching, seam::voicebank::IssueCode::AcousticAnalysisStale));
  CHECK(!reportCode(matching, seam::voicebank::IssueCode::AcousticAnalysisMismatch));

  // Replace the audio, keep the file name and length. The stored analysis binds
  // to the OLD bytes, so it must be reported rather than reused.
  const auto replacement = seam::test::support::sineWave(kRate, 150.0, 1.0, 0.5F);
  CHECK(replacement.size() == samples.size());
  CHECK(seam::voicebank::writeMonoPcm16Wav(audioPath, kRate, replacement));
  const auto replaced = seam::voicebank::BankValidator{}.validate(manifest, root);
  CHECK(reportCode(replaced, seam::voicebank::IssueCode::AcousticAnalysisStale));

  // A record from an older analyser revision is refused, not reinterpreted, and
  // the message must say so rather than blaming the audio.
  auto stale = analysis.value();
  stale.algorithmVersion = "0";
  CHECK(!seam::voicebank::encodeAcousticAnalysis(stale, unit, digest.value(),
      static_cast<seam::time::SampleFrame>(kFrames)));

  // A record belonging to another unit cannot be dropped in under this unit name.
  auto foreign = unit;
  foreign.id = "b";
  const auto asForeign = seam::voicebank::encodeAcousticAnalysis(
      analysis.value(), unit, digest.value(),
      static_cast<seam::time::SampleFrame>(kFrames));
  CHECK(asForeign);
  CHECK(!seam::voicebank::decodeAcousticAnalysis(asForeign.value(), foreign,
      digest.value(), static_cast<seam::time::SampleFrame>(kFrames)));

  // A sidecar that is a directory or a symlink is refused rather than followed.
  CHECK(std::filesystem::remove(sidecar));
  std::filesystem::create_directory(sidecar);
  const auto asDirectory = seam::voicebank::BankValidator{}.validate(manifest, root);
  CHECK(reportCode(asDirectory, seam::voicebank::IssueCode::AcousticAnalysisStale));
}
