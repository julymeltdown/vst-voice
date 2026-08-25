#include "seam/phase12c/live_voice.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  using namespace seam::phase12c;

  const std::string output = argc > 1 ? argv[1] : "phase12c-matrix.json";
  constexpr std::array<std::uint32_t, 6> sampleRates{
      44100U, 48000U, 88200U, 96000U, 176400U, 192000U};
  constexpr std::array<std::uint32_t, 7> bufferFrames{
      16U, 32U, 64U, 128U, 256U, 512U, 1024U};
  constexpr std::array<std::uint32_t, 4> channelCounts{1U, 2U, 4U, 8U};
  std::uint32_t cases = 0U;
  std::uint32_t failures = 0U;
  double totalEnergy = 0.0;

  for (const auto sampleRate : sampleRates) {
    for (const auto frames : bufferFrames) {
      for (const auto channels : channelCounts) {
        for (const auto highBend : {false, true}) {
          LiveVoiceEngine engine;
          engine.configure(sampleRate, channels);
          static_cast<void>(engine.publishResource(makeEmbeddedHumanResource()));

          std::vector<std::vector<float>> memory(
              channels, std::vector<float>(frames));
          std::array<float*, 8> outputs{};
          for (std::size_t channel = 0U;
               channel < static_cast<std::size_t>(channels); ++channel) {
            outputs[channel] = memory[channel].data();
          }
          std::array<LiveEvent, 2> events{
              LiveEvent{0U, EventType::NoteOn, 1, 0, 60, 0.8F, {}},
              LiveEvent{frames / 2U, EventType::PitchBend, 1, 0, 60,
                        highBend ? 7.0F : 2.0F, {}}};
          engine.process(events, outputs.data(), channels, frames);

          bool finite = true;
          for (const auto& channel : memory) {
            for (const auto sample : channel) {
              finite = finite && std::isfinite(sample);
              totalEnergy += std::abs(static_cast<double>(sample));
            }
          }
          if (!finite) ++failures;
          ++cases;
        }
      }
    }
  }

  std::ofstream file(output);
  file << "{\n"
       << "  \"cases\": " << cases << ",\n"
       << "  \"expected\": 336,\n"
       << "  \"failures\": " << failures << ",\n"
       << "  \"finite\": " << (failures == 0U ? "true" : "false") << ",\n"
       << "  \"result\": \""
       << (cases == 336U && failures == 0U ? "PASS" : "FAIL") << "\"\n"
       << "}\n";
  std::cout << "cases=" << cases << " fail=" << failures
            << " energy=" << totalEnergy << '\n';
  return cases == 336U && failures == 0U ? 0 : 1;
}
