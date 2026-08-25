#include "seam/phase12c/live_voice.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

namespace {

using seam::phase12c::EventType;
using seam::phase12c::LiveEvent;
using seam::phase12c::LiveStats;

constexpr std::uint32_t kBlockFrames = 64U;
constexpr std::size_t kEventCapacity = 64U;

struct WorkloadState final {
  std::uint64_t eventBlocks{0U};
  std::uint64_t resourcePublishes{0U};
  std::uint64_t resourceClears{0U};
  std::size_t maxActiveVoices{0U};
  double absoluteEnergy{0.0};
  float peak{0.0F};
  bool finite{true};
};

void append(std::array<LiveEvent, kEventCapacity>& events,
            std::size_t& count, LiveEvent event) noexcept {
  if (count < events.size()) events[count++] = event;
}

std::size_t scheduleEvents(
    std::uint64_t block,
    std::array<LiveEvent, kEventCapacity>& events) noexcept {
  std::size_t count = 0U;
  if (block % 128U == 0U) {
    append(events, count, LiveEvent{0U, EventType::NoteOn, 1, 0, 60, 0.85F, {}});
    append(events, count, LiveEvent{8U, EventType::NoteOn, 2, 0, 62, 0.80F, {}});
    append(events, count, LiveEvent{16U, EventType::Pressure, 2, 0, 62, 0.45F, {}});
    append(events, count, LiveEvent{24U, EventType::PitchBend, 2, 0, 62, 2.0F, {}});
    append(events, count, LiveEvent{32U, EventType::Timbre, 2, 0, 62, 0.65F, {}});
    append(events, count, LiveEvent{40U, EventType::NoteOff, 1, 0, 60, 0.0F, {}});
    append(events, count, LiveEvent{48U, EventType::NoteChoke, 2, 0, 62, 0.0F, {}});
    append(events, count,
           LiveEvent{56U, EventType::Midi1, -1, 0, 0, 0.0F, {0x80U, 64U, 0U}});
  }

  if (block % 512U == 0U) {
    for (std::int32_t index = 0; index < 33; ++index) {
      append(events, count,
             LiveEvent{0U, EventType::NoteOn, 100 + index, 1,
                       static_cast<std::int16_t>(48 + index % 24), 0.65F, {}});
    }
  }
  return count;
}

bool writeJson(const std::string& path, const std::string& profile,
               std::uint64_t requiredSeconds, std::uint64_t elapsedSeconds,
               std::uint64_t blocks, const WorkloadState& workload,
               const LiveStats& stats, bool pass) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) return false;
  output << std::setprecision(9)
         << "{\n"
         << "  \"profile\": \"" << profile << "\",\n"
         << "  \"requiredSeconds\": " << requiredSeconds << ",\n"
         << "  \"elapsedSeconds\": " << elapsedSeconds << ",\n"
         << "  \"blocks\": " << blocks << ",\n"
         << "  \"eventBlocks\": " << workload.eventBlocks << ",\n"
         << "  \"resourcePublishes\": " << workload.resourcePublishes << ",\n"
         << "  \"resourceClears\": " << workload.resourceClears << ",\n"
         << "  \"maxActiveVoices\": " << workload.maxActiveVoices << ",\n"
         << "  \"absoluteEnergy\": " << workload.absoluteEnergy << ",\n"
         << "  \"peak\": " << workload.peak << ",\n"
         << "  \"finite\": " << (workload.finite ? "true" : "false") << ",\n"
         << "  \"noteOns\": " << stats.noteOns << ",\n"
         << "  \"noteOffs\": " << stats.noteOffs << ",\n"
         << "  \"steals\": " << stats.steals << ",\n"
         << "  \"transitionHits\": " << stats.transitionHits << ",\n"
         << "  \"transitionFallbacks\": " << stats.transitionFallbacks << ",\n"
         << "  \"midiEvents\": " << stats.midiEvents << ",\n"
         << "  \"expressionEvents\": " << stats.expressionEvents << ",\n"
         << "  \"renderedFrames\": " << stats.renderedFrames << ",\n"
         << "  \"silentFramesNoResource\": " << stats.silentFramesNoResource << ",\n"
         << "  \"eventOverflows\": " << stats.eventOverflows << ",\n"
         << "  \"result\": \"" << (pass ? "PASS" : "FAIL") << "\"\n"
         << "}\n";
  output.flush();
  return output.good();
}

}

int main(int argc, char** argv) {
  using namespace seam::phase12c;

  std::string profile = "smoke";
  std::string outputPath = "phase12c-soak.json";
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--profile" && index + 1 < argc) {
      profile = argv[++index];
    } else if (argument == "--output" && index + 1 < argc) {
      outputPath = argv[++index];
    }
  }

  const std::uint64_t requiredSeconds = profile == "full" ? 7200U : 5U;
  const auto requiredDuration = std::chrono::seconds{
      static_cast<std::chrono::seconds::rep>(requiredSeconds)};
  auto resource = makeEmbeddedHumanResource();
  LiveVoiceEngine engine;
  if (!engine.publishResource(resource)) return 1;

  std::array<float, kBlockFrames> left{};
  std::array<float, kBlockFrames> right{};
  float* outputs[2] = {left.data(), right.data()};
  std::array<LiveEvent, kEventCapacity> events{};
  WorkloadState workload;
  const auto start = std::chrono::steady_clock::now();
  std::uint64_t blocks = 0U;

  while (std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::steady_clock::now() - start)
             .count() < requiredDuration.count()) {
    const auto eventCount = scheduleEvents(blocks, events);
    workload.eventBlocks += eventCount == 0U ? 0U : 1U;

    if (blocks != 0U && blocks % 4096U == 0U) {
      engine.clearResource();
      ++workload.resourceClears;
      engine.process({}, outputs, 2U, kBlockFrames);
      if (!engine.publishResource(resource)) {
        workload.finite = false;
        break;
      }
      ++workload.resourcePublishes;
    }

    engine.process(std::span<const LiveEvent>{events.data(), eventCount},
                   outputs, 2U, kBlockFrames);
    for (std::size_t frame = 0U; frame < kBlockFrames; ++frame) {
      const auto leftSample = left[frame];
      const auto rightSample = right[frame];
      workload.finite = workload.finite && std::isfinite(leftSample) &&
                        std::isfinite(rightSample);
      workload.absoluteEnergy += std::abs(static_cast<double>(leftSample)) +
                                 std::abs(static_cast<double>(rightSample));
      workload.peak = std::max(workload.peak,
                               std::max(std::abs(leftSample), std::abs(rightSample)));
    }
    workload.maxActiveVoices =
        std::max(workload.maxActiveVoices, engine.activeVoiceCount());
    ++blocks;
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - start);
  const auto stats = engine.stats();
  const bool workloadPass =
      workload.finite && elapsed.count() >= requiredDuration.count() &&
      workload.absoluteEnergy > 1.0 && blocks > 0U &&
      workload.maxActiveVoices <= kMaxVoices && stats.noteOns > 0U &&
      stats.noteOffs > 0U && stats.transitionHits > 0U &&
      stats.midiEvents > 0U && stats.expressionEvents > 0U &&
      stats.steals > 0U && stats.renderedFrames > 0U &&
      stats.eventOverflows == 0U;
  const bool evidenceWritten =
      writeJson(outputPath, profile, requiredSeconds,
                static_cast<std::uint64_t>(elapsed.count()), blocks, workload,
                stats, workloadPass);
  const bool pass = workloadPass && evidenceWritten;
  std::cout << "profile=" << profile << " elapsed=" << elapsed.count()
            << " blocks=" << blocks << " noteOns=" << stats.noteOns
            << " noteOffs=" << stats.noteOffs
            << " transitions=" << stats.transitionHits
            << " midi=" << stats.midiEvents << " steals=" << stats.steals
            << " result=" << (pass ? "PASS" : "FAIL") << '\n';
  return pass ? 0 : 1;
}
