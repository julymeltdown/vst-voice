#include "test_framework.hpp"
#include "test_support.hpp"

#include "seam/core/sha256.hpp"
#include "seam/core/file_io.hpp"
#include "seam/voicebank/acoustic_analysis.hpp"
#include "seam/voicebank/content_identity.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/synthesis/source_target_map.hpp"
#include "seam/voicebank/validator.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/synthesis/classic_psola.hpp"
#include "seam/synthesis/renderer_capabilities.hpp"
#include "seam/synthesis/renderer_dispatcher.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/voicebank/pitch_marks.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>




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


TEST_CASE("classic PSOLA reaches the target pitch at integer down-ratios") {
  constexpr std::uint32_t sampleRate = 48000U;
  constexpr std::size_t frames = 24000U;
  constexpr std::int32_t sourceMidi = 69;  // A4 = 440 Hz
  const auto sourceHz = 440.0;

  // A harmonic-rich source, so the measurement is not an artefact of a bare sine.
  std::vector<float> samples(frames, 0.0F);
  for (std::size_t index = 0U; index < frames; ++index) {
    const auto time = static_cast<double>(index) / static_cast<double>(sampleRate);
    samples[index] = 0.30F * static_cast<float>(
                         std::sin(2.0 * std::numbers::pi * sourceHz * time)) +
                    0.15F * static_cast<float>(
                         std::sin(2.0 * std::numbers::pi * 2.0 * sourceHz * time)) +
                    0.08F * static_cast<float>(
                         std::sin(2.0 * std::numbers::pi * 3.0 * sourceHz * time));
  }
  const seam::voicebank::AudioBuffer source{
      .sampleRate = sampleRate, .channels = 1, .interleaved = samples};
  auto unit = seam::test::support::makeUnit("a-psola", {"a"}, "audio/a.wav",
      sourceMidi, seam::voicebank::UnitKind::Sustain, frames);
  unit.renderer = seam::voicebank::RendererHint::ClassicPsola;
  // Marks from the product's own analyser, as an installed bank has. Hand-placed
  // marks at a fixed spacing do not align with the waveform actual period and
  // drift in phase, which is a fixture artefact rather than a renderer fault.
  const auto generated = seam::voicebank::generatePitchMarks(samples, sampleRate,
      unit.markers.audioOffset, unit.markers.audioEnd);
  CHECK(generated);
  unit.pitchMarks = generated.value();
  CHECK(unit.validate());



  // Ratios 2.000 and 4.000 both failed before the fix; 1.888 and 3.364 passed, so
  // the non-integer cases are the control that shows the assertion discriminates.
  const std::array<std::int32_t, 4> targets{57, 58, 45, 48};
  for (const auto target : targets) {
    const auto expected = 440.0 * std::pow(2.0, (target - 69) / 12.0);
    const auto rendered = seam::synthesis::ClassicPsolaRenderer{}.render(
        unit, source, sampleRate, frames, target,
        seam::synthesis::PsolaRenderParameters{.sourcePitchResidual = 0.0F});
    CHECK(rendered);
    const auto pitch = seam::voicebank::analyzePitch(rendered.value().samples, sampleRate);
    CHECK(pitch);
    const auto measured = seam::voicebank::medianVoicedPitch(pitch.value());
    // The tolerance is generous because the analyser quantises to its hop grid;
    // it is far tighter than the 1200-cent error this test exists to catch.
    const auto cents = 1200.0 * std::log2(measured / expected);
    CHECK_NEAR(cents, 0.0, 25.0);
    // And the source pitch must not be what came out. Without this, a renderer
    // that ignores the target entirely would satisfy the check above whenever the
    // target happens to equal the source.
    const auto toSource = 1200.0 * std::log2(measured / sourceHz);
    CHECK(std::abs(toSource) > 100.0);
  }
}

// U16 scenario 3: "Renderer failures preserve required intent or fail truthfully;
// discarded experiments cannot become hidden fallback paths."
//
// The dangerous outcome is a required control disappearing behind a Raw fallback:
// the render succeeds, audio is produced, and nobody learns that the vibrato or
// formant that was asked for was dropped. Fallback is explicitly permitted here,
// which is the case where that could happen.

TEST_CASE("a required control either applies or fails rather than falling back silently") {
  constexpr std::uint32_t rate = 48000U;
  constexpr std::size_t frames = 24000U;
  const auto samples = seam::test::support::sineWave(rate, 440.0, 0.5, 0.4F);
  CHECK(samples.size() == frames);
  const seam::voicebank::AudioBuffer source{
      .sampleRate = rate, .channels = 1, .interleaved = samples};
  auto unit = seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 69,
      seam::voicebank::UnitKind::Sustain, frames);
  unit.renderer = seam::voicebank::RendererHint::ClassicPsola;
  const auto generated = seam::voicebank::generatePitchMarks(samples, rate,
      unit.markers.audioOffset, unit.markers.audioEnd);
  CHECK(generated);
  unit.pitchMarks = generated.value();
  CHECK(unit.validate());

  const seam::synthesis::UnitRendererDispatcher dispatcher;
  for (const auto control : {seam::synthesis::RendererControl::Formant,
                             seam::synthesis::RendererControl::Gender,
                             seam::synthesis::RendererControl::Growl,
                             seam::synthesis::RendererControl::Vibrato}) {
    seam::synthesis::RendererDispatchParameters parameters{};
    parameters.controls.require(control);
    parameters.allowRawFallback = true;
    const auto rendered = dispatcher.render(unit, source, rate, frames, 69, parameters);
    const auto name = std::string{seam::synthesis::rendererControlName(control)};
    if (!rendered) {
      // Refusing is truthful, as long as the message names the control so the
      // caller can act on it.
      CHECK(rendered.error().message.find(name) != std::string::npos);
      continue;
    }
    // Accepting is truthful only when what actually ran supports the control, or
    // when a diagnostic tells the caller it was not applied.
    const auto actualSupports =
        seam::synthesis::rendererCapabilities(rendered.value().actual).supports(control);
    CHECK(actualSupports || !rendered.value().diagnostic.empty());
    // A silent fallback that drops a required control is the failure this exists
    // to catch, so state it directly.
    if (rendered.value().usedFallback && !actualSupports) {
      CHECK(!rendered.value().diagnostic.empty());
    }
  }
}

// U15 scenario 1: "Voiced-to-fricative material retains separate voicing states;
// marks never bridge unvoiced spans."
//
// The analysis and the pitch marks are two readings of the same frames, and until
// they shared one partition they disagreed: spans anchored each frame to its
// window origin while mark generation accepted any sample a voiced window covered.
// Measured on these engineering fixtures before the shared partition: the
// noise-to-voice boundary 1408 samples early, 14 of 190 marks inside the noise of
// the voiced/noise/voiced take, and 8 marks in spans the analysis of the same
// audio called unvoiced. These are synthetic sines and noise, not singing; they
// establish the mechanism, not a voice-quality result.
TEST_CASE("voicing spans and generated pitch marks agree on voiced-to-fricative material") {
  struct Fixture {
    const char* name;
    std::size_t firstVoicedEnd;  // voiced [0, firstVoicedEnd)
    std::size_t unvoicedEnd;     // noise [firstVoicedEnd, unvoicedEnd), voiced after
  };
  // Voiced/noise/voiced, noise-then-vowel (CV) and vowel-then-noise (VC).
  const std::array<Fixture, 3> fixtures{{
      {"voiced-noise-voiced", kVoicedEnd, kUnvoicedEnd},
      {"noise-then-vowel", 0U, 9600U},
      {"vowel-then-noise", 28800U, kFrames},
  }};
  const auto frameSize = static_cast<std::int64_t>(seam::voicebank::kProducerFrameSize);
  const auto hop = static_cast<std::int64_t>(seam::voicebank::kProducerHopSize);
  // A 2048-sample window cannot place a boundary more precisely than the point at
  // which the detector changes its mind, which on these fixtures is two hops from
  // the true edge. Origin anchoring missed by 1408, which this rejects.
  const auto boundaryTolerance = frameSize / 4 + hop / 2;
  for (const auto& fixture : fixtures) {
    const auto samples = voicedUnvoicedVoiced(kRate, kFrames, fixture.firstVoicedEnd,
                                              fixture.unvoicedEnd);
    auto unit = seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 60,
        seam::voicebank::UnitKind::Sustain, kFrames);
    const auto digest = seam::core::sha256Hex(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(samples.data()), samples.size() * sizeof(float)));
    const auto frames = static_cast<seam::time::SampleFrame>(kFrames);
    const auto analysis = seam::voicebank::analyzeUnitAcoustics(samples, kRate, unit,
        digest, frames, seam::voicebank::AcousticAnalysisLimits{
            .maximumSpans = 4096U, .pitch = seam::voicebank::producerPitchLimits(kFrames)});
    CHECK(analysis);
    const auto& spans = analysis.value().spans;

    // Separate states: every true edge is matched by a span boundary of the right
    // kind within the tolerance, and nothing else changes state.
    std::vector<std::pair<std::int64_t, bool>> trueEdges;  // position, voiced after
    if (fixture.firstVoicedEnd > 0U) trueEdges.emplace_back(fixture.firstVoicedEnd, false);
    if (fixture.unvoicedEnd < kFrames) trueEdges.emplace_back(fixture.unvoicedEnd, true);
    CHECK(spans.size() == trueEdges.size() + 1U);
    for (std::size_t index = 1U; index < spans.size() && index - 1U < trueEdges.size(); ++index) {
      const auto [edge, voicedAfter] = trueEdges[index - 1U];
      CHECK(spans[index].voiced == voicedAfter);
      CHECK(std::abs(spans[index].start - edge) <= boundaryTolerance);
    }

    const auto marks = seam::voicebank::generatePitchMarks(samples, kRate, 0, frames,
        seam::voicebank::producerPitchMarkConfig(), {},
        seam::voicebank::producerPitchLimits(kFrames));
    CHECK(marks);
    std::size_t marksInVoicedAudio = 0U;
    for (const auto& mark : marks.value()) {
      // Never inside a span the analysis of the same audio measured as unvoiced.
      const auto voiced = seam::voicebank::acousticVoicedAt(analysis.value(), mark.frame);
      CHECK(voiced.has_value());
      CHECK(voiced.value_or(false));
      // And never deeper into the noise than the analysis itself can resolve.
      const auto inNoise = mark.frame >= static_cast<std::int64_t>(fixture.firstVoicedEnd) &&
                           mark.frame < static_cast<std::int64_t>(fixture.unvoicedEnd);
      if (inNoise) {
        const auto depth = std::min(
            mark.frame - static_cast<std::int64_t>(fixture.firstVoicedEnd),
            static_cast<std::int64_t>(fixture.unvoicedEnd) - mark.frame);
        const auto reachesEdge =
            (fixture.firstVoicedEnd > 0U &&
             mark.frame - static_cast<std::int64_t>(fixture.firstVoicedEnd) <= boundaryTolerance) ||
            (fixture.unvoicedEnd < kFrames &&
             static_cast<std::int64_t>(fixture.unvoicedEnd) - mark.frame <= boundaryTolerance);
        CHECK(depth >= 0);
        CHECK(reachesEdge);
      } else {
        ++marksInVoicedAudio;
      }
    }
    // The voiced audio keeps its marks: at least 90 percent of the glottal periods
    // the sines contain (200 Hz before the noise, 240 Hz after).
    const auto voicedPeriods =
        static_cast<double>(fixture.firstVoicedEnd) * 200.0 / kRate +
        static_cast<double>(kFrames - fixture.unvoicedEnd) * 240.0 / kRate;
    CHECK(static_cast<double>(marksInVoicedAudio) >= 0.9 * voicedPeriods);
  }
}

// The QC half of scenario 1: a stored mark inside a measured unvoiced span is
// reported against the stored analysis renderers read. Marks generated from the
// same audio pass, because they share the analysis partition.
TEST_CASE("bank QC reports stored pitch marks inside measured unvoiced spans") {
  const auto root = seam::test::support::temporaryDirectory("analysis-unvoiced-marks");
  std::filesystem::create_directories(root / "audio");
  const auto audioPath = root / "audio" / "a.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(audioPath, kRate,
      voicedUnvoicedVoiced(kRate, kFrames, kVoicedEnd, kUnvoicedEnd)));
  const auto decoded = seam::voicebank::readWav(audioPath);
  CHECK(decoded);
  const auto mono = decoded.value().monoMix();
  const auto digest = seam::core::sha256File(audioPath);
  CHECK(digest);
  const auto frames = static_cast<seam::time::SampleFrame>(mono.size());

  auto unit = seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 55,
      seam::voicebank::UnitKind::Sustain, kFrames);
  const auto analysis = seam::voicebank::analyzeUnitAcoustics(mono, kRate, unit,
      digest.value(), frames);
  CHECK(analysis);
  const auto encoded = seam::voicebank::encodeAcousticAnalysis(analysis.value(), unit,
      digest.value(), frames);
  CHECK(encoded);
  std::filesystem::create_directories(root / "analysis");
  CHECK(seam::core::durableAtomicWriteText(
      root / seam::voicebank::acousticAnalysisSidecarPath(unit.id), encoded.value()));

  const auto generated = seam::voicebank::generatePitchMarks(mono, kRate, 0, frames,
      seam::voicebank::producerPitchMarkConfig(), {},
      seam::voicebank::producerPitchLimits(mono.size()));
  CHECK(generated);
  const auto findings = [&](const seam::voicebank::Unit& candidate,
                            seam::voicebank::IssueSeverity severity) {
    const auto report = seam::voicebank::BankValidator{}.validate(
        seam::test::support::makeManifest({candidate}), root);
    return std::count_if(report.issues.begin(), report.issues.end(), [&](const auto& issue) {
      return issue.code == seam::voicebank::IssueCode::PitchMarksUnvoiced &&
             issue.severity == severity;
    });
  };

  // Marks generated from this audio agree with its analysis.
  unit.pitchMarks = generated.value();
  CHECK(findings(unit, seam::voicebank::IssueSeverity::Error) == 0);
  CHECK(findings(unit, seam::voicebank::IssueSeverity::Warning) == 0);

  // One unlocked mark in the middle of the noise is an error.
  const auto middleOfNoise = static_cast<seam::time::SampleFrame>((kVoicedEnd + kUnvoicedEnd) / 2U);
  CHECK(!seam::voicebank::acousticVoicedAt(analysis.value(), middleOfNoise).value_or(true));
  auto bridged = unit;
  bridged.pitchMarks.push_back(seam::voicebank::PitchMark{.frame = middleOfNoise, .confidence = 0.9F});
  std::sort(bridged.pitchMarks.begin(), bridged.pitchMarks.end(),
      [](const auto& left, const auto& right) { return left.frame < right.frame; });
  CHECK(findings(bridged, seam::voicebank::IssueSeverity::Error) == 1);

  // The same mark, locked by a reviewer, is surfaced but not overruled.
  for (auto& mark : bridged.pitchMarks) {
    if (mark.frame == middleOfNoise) mark.locked = true;
  }
  CHECK(findings(bridged, seam::voicebank::IssueSeverity::Error) == 0);
  CHECK(findings(bridged, seam::voicebank::IssueSeverity::Warning) == 1);
}

// The bank content identity (catalogue, installer, candidate publication) covers
// stored analyses the same way it covers alignments, and keeps legacy identities.
TEST_CASE("bank content identity covers stored acoustic analyses and keeps legacy identities") {
  const auto root = seam::test::support::temporaryDirectory("analysis-content-identity");
  std::filesystem::create_directories(root / "audio");
  const auto audioPath = root / "audio" / "a.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(audioPath, kRate,
      seam::test::support::sineWave(kRate, 200.0, 1.0, 0.5F)));
  const auto unit = seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 55,
      seam::voicebank::UnitKind::Sustain, kFrames);
  const auto manifest = seam::test::support::makeManifest({unit});
  const auto legacy = seam::voicebank::computeVoicebankContentHash(manifest, root);
  CHECK(legacy);

  // An empty analysis directory describes nothing and changes nothing.
  std::filesystem::create_directories(root / "analysis");
  CHECK(seam::voicebank::computeVoicebankContentHash(manifest, root).value() == legacy.value());

  const auto decoded = seam::voicebank::readWav(audioPath);
  CHECK(decoded);
  const auto digest = seam::core::sha256File(audioPath);
  CHECK(digest);
  const auto frames = static_cast<seam::time::SampleFrame>(decoded.value().frameCount());
  const auto analysis = seam::voicebank::analyzeUnitAcoustics(decoded.value().monoMix(),
      kRate, unit, digest.value(), frames);
  CHECK(analysis);
  const auto sidecar = root / seam::voicebank::acousticAnalysisSidecarPath(unit.id);
  const auto write = [&](const seam::voicebank::AcousticAnalysis& value) {
    const auto encoded = seam::voicebank::encodeAcousticAnalysis(value, unit, digest.value(), frames);
    CHECK(encoded);
    CHECK(seam::core::durableAtomicWriteText(sidecar, encoded.value()));
    const auto hashed = seam::voicebank::computeVoicebankContentHash(manifest, root);
    CHECK(hashed);
    return hashed.value();
  };
  const auto stored = write(analysis.value());
  CHECK(stored != legacy.value());
  auto regenerated = analysis.value();
  regenerated.spans.front().confidence *= 0.5;
  CHECK(write(regenerated) != stored);

  // Removing the record restores the legacy identity exactly.
  CHECK(std::filesystem::remove(sidecar));
  CHECK(seam::voicebank::computeVoicebankContentHash(manifest, root).value() == legacy.value());

  // A linked analysis directory is refused, as a linked alignment directory is.
  std::filesystem::remove(root / "analysis");
  std::filesystem::create_directory_symlink(root / "audio", root / "analysis");
  CHECK(!seam::voicebank::computeVoicebankContentHash(manifest, root));
}

// U15 "store F0/confidence/voicing spans ... bound to exact audio": the producer
// path that writes the records. Candidate publication runs it over the staged
// bank before QC and before the content identity is computed.
TEST_CASE("storing a bank's analyses binds each unit to the bytes present and regenerates them") {
  const auto root = seam::test::support::temporaryDirectory("analysis-store");
  std::filesystem::create_directories(root / "audio");
  const auto shared = root / "audio" / "vuv.wav";
  CHECK(seam::voicebank::writeMonoPcm16Wav(shared, kRate,
      voicedUnvoicedVoiced(kRate, kFrames, kVoicedEnd, kUnvoicedEnd)));
  // One frame more than a single producer pass admits (4096 analysis frames).
  const std::size_t longFrames = seam::voicebank::kProducerFrameSize +
                                 4096U * seam::voicebank::kProducerHopSize;
  std::vector<float> longTake(longFrames, 0.0F);
  for (std::size_t frame = 0U; frame < longFrames; ++frame) {
    longTake[frame] = 0.4F * static_cast<float>(std::sin(
        2.0 * std::numbers::pi * 220.0 * static_cast<double>(frame) / kRate));
  }
  CHECK(seam::voicebank::writeMonoPcm16Wav(root / "audio" / "long.wav", kRate, longTake));
  const auto a = seam::test::support::makeUnit("a", {"a"}, "audio/vuv.wav", 55,
      seam::voicebank::UnitKind::Sustain, kFrames);
  const auto b = seam::test::support::makeUnit("b", {"a"}, "audio/vuv.wav", 55,
      seam::voicebank::UnitKind::Sustain, kFrames);
  const auto longUnit = seam::test::support::makeUnit("long", {"a"}, "audio/long.wav", 57,
      seam::voicebank::UnitKind::Sustain, longFrames);
  const auto manifest = seam::test::support::makeManifest({a, b, longUnit});

  const auto stored = seam::voicebank::storeBankAcousticAnalyses(manifest, root);
  CHECK(stored);
  CHECK(stored.value().written == 2U);
  CHECK((stored.value().unmeasuredUnits == std::vector<std::string>{"long"}));
  CHECK(!std::filesystem::exists(root / seam::voicebank::acousticAnalysisSidecarPath("long")));

  // Each record decodes against the bytes present and equals a fresh measurement
  // of the decoded file, under the current algorithm.
  const auto decodedWav = seam::voicebank::readWav(shared);
  CHECK(decodedWav);
  const auto mono = decodedWav.value().monoMix();
  const auto digest = seam::core::sha256File(shared);
  CHECK(digest);
  for (const auto& unit : {a, b}) {
    const auto text = seam::core::readTextFileLimited(
        root / seam::voicebank::acousticAnalysisSidecarPath(unit.id), 512U * 1024U);
    CHECK(text);
    const auto decoded = seam::voicebank::decodeAcousticAnalysis(text.value(), unit,
        digest.value(), static_cast<seam::time::SampleFrame>(kFrames));
    CHECK(decoded);
    CHECK(decoded.value().currentAlgorithm());
    const auto fresh = seam::voicebank::analyzeUnitAcoustics(mono, kRate, unit, digest.value(),
        static_cast<seam::time::SampleFrame>(kFrames));
    CHECK(fresh);
    CHECK(decoded.value() == fresh.value());
  }

  const auto reportHas = [&](seam::voicebank::IssueCode code) {
    const auto report = seam::voicebank::BankValidator{}.validate(manifest, root);
    return std::any_of(report.issues.begin(), report.issues.end(),
        [code](const auto& issue) { return issue.code == code; });
  };
  CHECK(!reportHas(seam::voicebank::IssueCode::AcousticAnalysisStale));
  CHECK(!reportHas(seam::voicebank::IssueCode::AcousticAnalysisMismatch));

  // Replacing the recording leaves records QC refuses; storing again regenerates
  // them from the bytes now present.
  CHECK(seam::voicebank::writeMonoPcm16Wav(shared, kRate,
      seam::test::support::sineWave(kRate, 150.0, 1.0, 0.5F)));
  CHECK(reportHas(seam::voicebank::IssueCode::AcousticAnalysisStale));
  CHECK(seam::voicebank::storeBankAcousticAnalyses(manifest, root));
  CHECK(!reportHas(seam::voicebank::IssueCode::AcousticAnalysisStale));

  // A record left for a unit that cannot be measured is removed rather than kept
  // describing audio it never measured.
  CHECK(std::filesystem::copy_file(root / seam::voicebank::acousticAnalysisSidecarPath("a"),
      root / seam::voicebank::acousticAnalysisSidecarPath("long")));
  CHECK(seam::voicebank::storeBankAcousticAnalyses(manifest, root));
  CHECK(!std::filesystem::exists(root / seam::voicebank::acousticAnalysisSidecarPath("long")));

  // Cancellation is reported, never presented as a completed store.
  std::stop_source cancelled;
  cancelled.request_stop();
  CHECK(!seam::voicebank::storeBankAcousticAnalyses(manifest, root, cancelled.get_token()));
}

// U16 scenario 1: "Annotated CV/VC material transposes without source-pitched
// voiced edges." The sustain was retargeted; the recorded attack and release
// were copied at the source pitch, so a note rendered at 220 Hz from a 440 Hz
// take began and ended at 440 Hz. Engineering fixture: a noise onset, then a
// harmonic 440 Hz vowel, marks from the product's own generator.
TEST_CASE("classic PSOLA retargets the voiced attack and release as well as the sustain") {
  constexpr std::uint32_t rate = 48000U;
  constexpr std::size_t frames = 24000U;
  constexpr std::size_t consonantEnd = 3600U;
  std::vector<float> samples(frames, 0.0F);
  unsigned seed = 424242U;
  for (std::size_t index = 0U; index < frames; ++index) {
    const auto time = static_cast<double>(index) / static_cast<double>(rate);
    if (index < consonantEnd) {
      seed = seed * 1103515245U + 12345U;
      samples[index] = 0.3F * ((static_cast<float>((seed >> 16U) & 0x7FFFU) / 16384.0F) - 1.0F);
      continue;
    }
    samples[index] = 0.30F * static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * time)) +
                     0.15F * static_cast<float>(std::sin(2.0 * std::numbers::pi * 880.0 * time)) +
                     0.08F * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1320.0 * time));
  }
  const seam::voicebank::AudioBuffer source{.sampleRate = rate, .channels = 1, .interleaved = samples};
  auto unit = seam::test::support::makeUnit("ka-psola", {"k", "a"}, "audio/ka.wav", 69,
      seam::voicebank::UnitKind::Cv, frames);
  unit.markers = seam::voicebank::UnitMarkers{
      .audioOffset = 0, .consonantEnd = static_cast<seam::time::SampleFrame>(consonantEnd),
      .vowelOnset = static_cast<seam::time::SampleFrame>(consonantEnd), .stableStart = 8400,
      .loopStart = 10800, .loopEnd = 15600, .releaseStart = 18000,
      .audioEnd = static_cast<seam::time::SampleFrame>(frames)};
  unit.renderer = seam::voicebank::RendererHint::ClassicPsola;
  const auto marks = seam::voicebank::generatePitchMarks(samples, rate, 0,
      static_cast<seam::time::SampleFrame>(frames), seam::voicebank::producerPitchMarkConfig());
  CHECK(marks);
  unit.pitchMarks = marks.value();
  CHECK(unit.validate());

  const auto regionPitch = [&](const std::vector<float>& rendered, std::size_t begin, std::size_t end) {
    const auto pitch = seam::voicebank::analyzePitch(
        std::span<const float>(rendered).subspan(begin, end - begin), rate);
    return pitch ? seam::voicebank::medianVoicedPitch(pitch.value()) : 0.0;
  };
  for (const std::int32_t target : {57, 64, 76}) {
    const auto expected = 440.0 * std::pow(2.0, (target - 69) / 12.0);
    const auto rendered = seam::synthesis::ClassicPsolaRenderer{}.render(unit, source, rate,
        static_cast<seam::time::SampleFrame>(frames), target,
        seam::synthesis::PsolaRenderParameters{.sourcePitchResidual = 0.0F});
    CHECK(rendered);
    const auto& output = rendered.value().samples;
    CHECK(output.size() == frames);
    // Attack: from the vowel onset to the start of the sustain. Release: from the
    // release marker to the end, less the final fade.
    const auto attack = regionPitch(output, consonantEnd + 512U, 8400U);
    const auto release = regionPitch(output, 18000U, frames - 1024U);
    CHECK(attack > 0.0);
    CHECK(release > 0.0);
    CHECK_NEAR(1200.0 * std::log2(attack / expected), 0.0, 25.0);
    CHECK_NEAR(1200.0 * std::log2(release / expected), 0.0, 25.0);
    // The unvoiced onset keeps its recorded samples: after gain and DC removal it
    // still correlates with the source almost perfectly.
    double dot = 0.0, left = 0.0, right = 0.0;
    for (std::size_t index = 256U; index < consonantEnd - 512U; ++index) {
      dot += static_cast<double>(output[index]) * samples[index];
      left += static_cast<double>(output[index]) * output[index];
      right += static_cast<double>(samples[index]) * samples[index];
    }
    CHECK(dot / std::sqrt(left * right) > 0.99);
  }

  // Scenario 2's pitch jump, on an edge: a +700-cent step in the pitch curve at
  // the release marker is followed there, while the attack keeps the target.
  const seam::synthesis::PitchCurve jump{std::vector<seam::synthesis::PitchPoint>{
      {.frame = 0, .cents = 0.0F}, {.frame = 17999, .cents = 0.0F},
      {.frame = 18000, .cents = 700.0F}}};
  const auto jumped = seam::synthesis::ClassicPsolaRenderer{}.render(unit, source, rate,
      static_cast<seam::time::SampleFrame>(frames), 57,
      seam::synthesis::PsolaRenderParameters{.sourcePitchResidual = 0.0F, .pitchCurve = jump});
  CHECK(jumped);
  const auto jumpAttack = regionPitch(jumped.value().samples, consonantEnd + 512U, 8400U);
  const auto jumpRelease = regionPitch(jumped.value().samples, 18000U + 512U, frames - 1024U);
  CHECK_NEAR(1200.0 * std::log2(jumpAttack / 220.0), 0.0, 25.0);
  CHECK_NEAR(1200.0 * std::log2(jumpRelease / 220.0), 700.0, 25.0);

  // Duration mapping is untouched: the vowel starts where it started in the
  // source, measured by the same analysis on both signals.
  const auto voicedStart = [&](const std::vector<float>& audio) -> std::int64_t {
    const auto probe = seam::test::support::makeUnit("probe", {"a"}, "audio/probe.wav", 69,
        seam::voicebank::UnitKind::Sustain, frames);
    const auto digest = seam::core::sha256Hex(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(audio.data()), audio.size() * sizeof(float)));
    const auto measured = seam::voicebank::analyzeUnitAcoustics(audio, rate, probe, digest,
        static_cast<seam::time::SampleFrame>(frames));
    if (!measured) return -1;
    for (const auto& span : measured.value().spans) {
      if (span.voiced) return span.start;
    }
    return -1;
  };
  const auto sourceStart = voicedStart(samples);
  const auto renderedStart = voicedStart(jumped.value().samples);
  CHECK(sourceStart > 0);
  CHECK(std::abs(renderedStart - sourceStart) <= static_cast<std::int64_t>(seam::voicebank::kProducerHopSize));
}

// U15 scenario 3, second half: "old manifests migrate without fabricated
// measurements or automatic approval". A legacy bank has no stored analysis and
// may carry marks from the earlier generator, which ran into fricatives. Migration
// is measuring the audio present: nothing is inferred before that, the manifest
// is not rewritten, and a legacy defect is surfaced for review rather than
// silently repaired or hidden. Nothing in this path grants a review state.
TEST_CASE("a legacy bank migrates by measurement without rewriting or inferring anything") {
  const auto root = seam::test::support::temporaryDirectory("analysis-legacy-migration");
  std::filesystem::create_directories(root / "audio");
  CHECK(seam::voicebank::writeMonoPcm16Wav(root / "audio" / "a.wav", kRate,
      voicedUnvoicedVoiced(kRate, kFrames, kVoicedEnd, kUnvoicedEnd)));
  auto unit = seam::test::support::makeUnit("a", {"a"}, "audio/a.wav", 55,
      seam::voicebank::UnitKind::Sustain, kFrames);
  // Marks as the earlier generator left them: one 200 Hz period apart, running
  // 1400 samples past the end of the voiced audio into the noise.
  for (seam::time::SampleFrame frame = 120; frame < static_cast<seam::time::SampleFrame>(kVoicedEnd) + 1400;
       frame += 240) {
    unit.pitchMarks.push_back(seam::voicebank::PitchMark{.frame = frame, .confidence = 0.9F});
  }
  const auto manifest = seam::test::support::makeManifest({unit});
  const auto manifestJson = seam::voicebank::ManifestJsonCodec{}.encode(manifest);
  CHECK(manifestJson);
  CHECK(seam::core::durableAtomicWriteText(root / "manifest.json", manifestJson.value()));
  const auto manifestBefore = seam::core::sha256File(root / "manifest.json");
  CHECK(manifestBefore);
  const auto count = [&](seam::voicebank::IssueCode code) {
    const auto report = seam::voicebank::BankValidator{}.validate(manifest, root);
    return std::count_if(report.issues.begin(), report.issues.end(),
        [code](const auto& issue) { return issue.code == code; });
  };

  // Before migration nothing is inferred: absence of a record is not a finding,
  // and the marks are not judged against a measurement that does not exist.
  CHECK(count(seam::voicebank::IssueCode::AcousticAnalysisStale) == 0);
  CHECK(count(seam::voicebank::IssueCode::PitchMarksUnvoiced) == 0);

  const auto stored = seam::voicebank::storeBankAcousticAnalyses(manifest, root);
  CHECK(stored);
  CHECK(stored.value().written == 1U);
  // The stored description is untouched: marks are not moved, dropped or added.
  CHECK(seam::core::sha256File(root / "manifest.json").value() == manifestBefore.value());
  // The record is a measurement of the file, identical to measuring it afresh.
  const auto digest = seam::core::sha256File(root / "audio" / "a.wav");
  CHECK(digest);
  const auto decodedWav = seam::voicebank::readWav(root / "audio" / "a.wav");
  CHECK(decodedWav);
  const auto fresh = seam::voicebank::analyzeUnitAcoustics(decodedWav.value().monoMix(), kRate,
      unit, digest.value(), static_cast<seam::time::SampleFrame>(kFrames));
  CHECK(fresh);
  const auto text = seam::core::readTextFileLimited(
      root / seam::voicebank::acousticAnalysisSidecarPath(unit.id), 512U * 1024U);
  CHECK(text);
  const auto recorded = seam::voicebank::decodeAcousticAnalysis(text.value(), unit, digest.value(),
      static_cast<seam::time::SampleFrame>(kFrames));
  CHECK(recorded);
  CHECK(recorded.value() == fresh.value());
  // Now the legacy marks are judged against the measurement, and the ones in the
  // noise are reported for regeneration.
  CHECK(count(seam::voicebank::IssueCode::PitchMarksUnvoiced) == 1);
}
