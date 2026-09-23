#include "test_framework.hpp"

#include "seam/rendering/sample_rate_converter.hpp"
#include "seam/core/resample.hpp"

#include <array>
#include <cmath>
#include <algorithm>
#include <numbers>
#include <span>
#include <vector>

// Aliasing regression for every rate-conversion path.
//
// Three separate interpolators in this repository downsampled by linear
// interpolation between neighbouring samples. Nothing removed energy above the
// target Nyquist frequency, so it folded back into the band: a 30 kHz tone
// resampled 96k -> 48k came back at ratio 1.0000, and a 10 kHz tone resampled
// 48k -> 16k came back at full amplitude as a 6 kHz tone. These cases measure
// the frequency response instead of asserting a number, because the defect was
// not a wrong value -- it was that nothing attenuated anything.
TEST_CASE("sample rate conversion rejects energy above the target nyquist") {
  // A tone is built at the source rate and carried across; its surviving level
  // is compared against the passband so the assertion is about relative
  // rejection, not an absolute amplitude.
  const auto gainDecibels = [](std::uint32_t sourceRate, std::uint32_t targetRate,
                               double hz) {
    const std::size_t frames = static_cast<std::size_t>(sourceRate) * 4U;
    std::vector<float> source(frames, 0.0F);
    for (std::size_t frame = 0U; frame < frames; ++frame) {
      source[frame] = static_cast<float>(
          0.5 * std::sin(2.0 * std::numbers::pi * hz *
                         static_cast<double>(frame) / static_cast<double>(sourceRate)));
    }
    const auto converted = seam::rendering::SampleRateConverter::convert(
        source, sourceRate, targetRate, seam::rendering::SampleRateQuality::Preview);
    if (!converted || converted.value().empty()) return -999.0;
    // Skip the edges: the kernel is truncated there by design and that is not
    // what this measures.
    const auto skip = converted.value().size() / 20U;
    double total = 0.0;
    for (auto index = skip; index < converted.value().size() - skip; ++index) {
      total += static_cast<double>(converted.value()[index]) *
               static_cast<double>(converted.value()[index]);
    }
    total /= static_cast<double>(converted.value().size() - 2U * skip);
    // A 0.5-amplitude sine has mean power 0.125.
    return 10.0 * std::log10(total / 0.125 + 1.0e-30);
  };

  struct Case {
    const char* name;
    std::uint32_t sourceRate;
    std::uint32_t targetRate;
    // Guard band available between the target Nyquist and the source Nyquist, as
    // a fraction of the target Nyquist. Where this is tiny the filter has almost
    // no room to complete its transition, so the achievable rejection is lower
    // and the assertion below says so rather than pretending otherwise.
    double guardFraction;
  };
  const std::array<Case, 4> cases{{
      {"48000->16000", 48000U, 16000U, 2.0},
      {"96000->48000", 96000U, 48000U, 1.0},
      {"48000->32000", 48000U, 32000U, 0.5},
      {"48000->44100", 48000U, 44100U, 0.088},
  }};
  for (const auto& item : cases) {
    const auto nyquist = static_cast<double>(item.targetRate) / 2.0;
    const auto sourceNyquist = static_cast<double>(item.sourceRate) / 2.0;
    // The passband must be preserved, or a converter that simply muted
    // everything would satisfy the rejection check below.
    CHECK(gainDecibels(item.sourceRate, item.targetRate, nyquist * 0.5) > -0.5);
    CHECK(gainDecibels(item.sourceRate, item.targetRate, nyquist * 0.8) > -0.5);

    // Probe inside the guard band, never above the source Nyquist: a tone at or
    // above the source Nyquist is not representable in the input at all, so
    // measuring there would be measuring input aliasing rather than the filter.
    const auto probe = std::min(nyquist * 1.25, sourceNyquist * 0.98);
    const auto required = item.guardFraction > 0.2 ? -40.0 : -10.0;
    CHECK(gainDecibels(item.sourceRate, item.targetRate, probe) < required);
  }

  // The shared kernel is what all three call sites now use, so it is checked
  // directly as well: a ratio below 1 must place the cutoff at the lower Nyquist.
  CHECK(seam::core::resampleKernelHalfWidthSamples(1.0) == 16);
  CHECK(seam::core::resampleKernelHalfWidthSamples(0.5) == 32);
  CHECK(seam::core::resampleKernelHalfWidthSamples(0.25) == 64);
}

// The streaming converter feeds the backing-media render path, where an
// imported 96 kHz file rendered into a 48 kHz project is ordinary. It must
// reject the same content as the whole-buffer form.
TEST_CASE("streaming sample rate conversion rejects energy above the target nyquist") {
  constexpr std::uint32_t kSourceRate = 96000U;
  constexpr std::uint32_t kTargetRate = 48000U;
  constexpr std::size_t kFrames = 96000U;
  const auto render = [](double hz) {
    std::vector<float> chunk(kFrames, 0.0F);
    for (std::size_t frame = 0U; frame < kFrames; ++frame) {
      chunk[frame] = static_cast<float>(
          0.5 * std::sin(2.0 * std::numbers::pi * hz *
                         static_cast<double>(frame) / static_cast<double>(kSourceRate)));
    }
    seam::rendering::StreamingSampleRateConverter converter{
        kSourceRate, kTargetRate, seam::rendering::SampleRateQuality::Final};
    std::vector<float> output;
    // Feed in uneven chunks so a boundary assumption would show up.
    std::size_t offset = 0U;
    const std::size_t sizes[] = {1000U, 4096U, 97U};
    std::size_t step = 0U;
    while (offset < chunk.size()) {
      const auto count = std::min(sizes[step % 3U], chunk.size() - offset);
      const auto produced = converter.append(
          std::span<const float>{chunk.data() + offset, count});
      if (!produced) return -999.0;
      output.insert(output.end(), produced.value().begin(), produced.value().end());
      offset += count;
      ++step;
    }
    const auto tail = converter.finish();
    if (!tail) return -999.0;
    output.insert(output.end(), tail.value().begin(), tail.value().end());
    if (output.empty()) return -999.0;
    const auto skip = output.size() / 20U;
    if (output.size() <= 2U * skip) return -999.0;
    double total = 0.0;
    for (auto index = skip; index < output.size() - skip; ++index) {
      total += static_cast<double>(output[index]) * static_cast<double>(output[index]);
    }
    total /= static_cast<double>(output.size() - 2U * skip);
    return 10.0 * std::log10(total / 0.125 + 1.0e-30);
  };
  CHECK(render(12000.0) > -0.5);     // passband
  CHECK(render(30000.0) < -40.0);    // above the 24 kHz target Nyquist
}


TEST_CASE("sample rate converter preserves bounded impulse placement") {

  const std::array<float, 4> impulse{0.0F, 1.0F, 0.0F, 0.0F};
  const auto converted = seam::rendering::SampleRateConverter::convert(
      impulse, 44100U, 48000U, seam::rendering::SampleRateQuality::Final);
  CHECK(converted);
  CHECK(converted.value().size() == 4U);
  for (const auto sample : converted.value()) CHECK(std::isfinite(sample));
  CHECK(converted.value()[1] > 0.8F);
}


// The streaming converter feeds the backing-media render path, which supplies
// audio in chunk-sized pieces. Two invariants matter there and neither was
// asserted before: an unchanged rate must be a bit-exact pass-through, so a
// project that never resamples cannot change; and the output length must not
// depend on how the input happened to be chopped, or a render's duration would
// depend on internal buffering rather than on the source.
TEST_CASE("rate conversion is identity at a matching rate and chunk independent") {
  constexpr std::size_t kFrames = 24000U;
  std::vector<float> source(kFrames, 0.0F);
  for (std::size_t frame = 0U; frame < kFrames; ++frame) {
    source[frame] = static_cast<float>(
        0.4 * std::sin(2.0 * std::numbers::pi * 440.0 *
                       static_cast<double>(frame) / 48000.0));
  }

  // Matching rate: bit-exact, not merely close. Clamping a Final-quality pass
  // through a filter would be a silent change to every 48 kHz project.
  const auto identical = seam::rendering::SampleRateConverter::convert(
      source, 48000U, 48000U, seam::rendering::SampleRateQuality::Final);
  CHECK(identical);
  CHECK(identical.value().size() == source.size());
  for (std::size_t index = 0U; index < source.size(); ++index) {
    CHECK(identical.value()[index] == source[index]);
  }

  // Length is the rounded duration at the target rate, for every conversion.
  struct Case { std::uint32_t sourceRate; std::uint32_t targetRate; };
  for (const auto& item : std::array<Case, 3>{{{48000U, 44100U}, {44100U, 48000U}, {96000U, 48000U}}}) {
    std::vector<float> atSource(
        static_cast<std::size_t>(static_cast<std::uint64_t>(kFrames) * item.sourceRate / 48000U), 0.0F);
    for (std::size_t frame = 0U; frame < atSource.size(); ++frame) {
      atSource[frame] = static_cast<float>(
          0.4 * std::sin(2.0 * std::numbers::pi * 440.0 *
                         static_cast<double>(frame) / static_cast<double>(item.sourceRate)));
    }
    const auto whole = seam::rendering::SampleRateConverter::convert(
        atSource, item.sourceRate, item.targetRate, seam::rendering::SampleRateQuality::Preview);
    CHECK(whole);
    const auto expected = static_cast<std::size_t>(std::llround(
        static_cast<double>(atSource.size()) * item.targetRate / item.sourceRate));
    CHECK(whole.value().size() == expected);

    // Same length regardless of chunking, and matching the whole-buffer form.
    for (const std::size_t chunk : {1U, 7U, 1024U}) {
      seam::rendering::StreamingSampleRateConverter converter{
          item.sourceRate, item.targetRate, seam::rendering::SampleRateQuality::Preview};
      std::size_t produced = 0U;
      for (std::size_t offset = 0U; offset < atSource.size(); offset += chunk) {
        const auto count = std::min(chunk, atSource.size() - offset);
        const auto part = converter.append(
            std::span<const float>{atSource.data() + offset, count});
        CHECK(part);
        produced += part.value().size();
      }
      const auto tail = converter.finish();
      CHECK(tail);
      produced += tail.value().size();
      CHECK(produced == expected);
    }
  }
}


TEST_CASE("sample rate converter rejects invalid rates") {
  const auto converted = seam::rendering::SampleRateConverter::convert(
      std::array<float, 1>{0.0F}, 1000U, 48000U);
  CHECK(!converted);
  CHECK(converted.error().code == seam::core::ErrorCode::InvalidArgument);
}

TEST_CASE("streaming sample rate converter matches bounded whole-buffer conversion") {
  const std::vector<float> source{0.0F, 0.25F, 0.5F, 0.75F, 1.0F,
                                  0.5F, 0.0F, -0.5F, -1.0F};
  const auto expected = seam::rendering::SampleRateConverter::convert(
      source, 44100U, 48000U, seam::rendering::SampleRateQuality::Final);
  CHECK(expected);
  seam::rendering::StreamingSampleRateConverter streaming{
      44100U, 48000U, seam::rendering::SampleRateQuality::Final};
  std::vector<float> actual;
  const auto first = streaming.append(
      std::span<const float>{source.data(), 3U});
  CHECK(first);
  actual.insert(actual.end(), first.value().begin(), first.value().end());
  const auto second = streaming.append(
      std::span<const float>{source.data() + 3U, source.size() - 3U});
  CHECK(second);
  actual.insert(actual.end(), second.value().begin(), second.value().end());
  const auto tail = streaming.finish();
  CHECK(tail);
  actual.insert(actual.end(), tail.value().begin(), tail.value().end());
  CHECK(actual.size() == expected.value().size());
  for (std::size_t index = 0U; index < actual.size(); ++index) {
    CHECK_NEAR(actual[index], expected.value()[index], 1.0e-5);
  }
  CHECK(!streaming.finish().value().size());
}
