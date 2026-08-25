#include "seam/live_voice/live_resources.hpp"
#include "seam/live_voice/voice_engine.hpp"
#include "seam/voicebank/catalog.hpp"
#include "seam/voicebank/wav.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

#ifndef SEAM_SOURCE_PRODUCTION_VOICEBANK
#error SEAM_SOURCE_PRODUCTION_VOICEBANK is required for the live demo
#endif

namespace {

std::filesystem::path outputRoot(int argc, char** argv) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (std::string_view{argv[index]} == "--output") {
      return std::filesystem::path{argv[index + 1]};
    }
  }
  return "out/phase12c-live";
}

}

int main(int argc, char** argv) {
  const auto output = outputRoot(argc, argv);
  std::error_code error;
  std::filesystem::create_directories(output, error);
  if (error) return 1;

  seam::voicebank::VoicebankCatalog catalog;
  const auto scanned = catalog.scan({seam::voicebank::VoicebankSearchRoot{
      .path = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK},
      .kind = seam::voicebank::VoicebankRootKind::Development,
  }});
  if (!scanned || scanned.value().empty()) return 2;
  auto resources = seam::live_voice::LiveResourceBuilder{}.build(
      scanned.value().front());
  if (!resources) return 3;

  seam::live_voice::VoiceEngine engine;
  engine.configure(48000U, 2U);
  if (!engine.publishResources(resources.value())) return 4;

  constexpr std::uint32_t frames = 48000U;
  std::vector<float> left(frames, 0.0F);
  std::vector<float> right(frames, 0.0F);
  float* outputs[2] = {left.data(), right.data()};
  std::array<seam::live_voice::LiveEvent, 40> events{};
  events[0] = seam::live_voice::LiveEvent{
      .sampleOffset = 0U,
      .type = seam::live_voice::EventType::NoteOn,
      .noteId = 1,
      .key = 60,
      .value = 0.85F,
  };
  events[1] = seam::live_voice::LiveEvent{
      .sampleOffset = 8000U,
      .type = seam::live_voice::EventType::NoteOn,
      .noteId = 2,
      .key = 64,
      .value = 0.8F,
  };
  events[2] = seam::live_voice::LiveEvent{
      .sampleOffset = 12000U,
      .type = seam::live_voice::EventType::PitchBend,
      .noteId = 2,
      .key = 64,
      .value = 2.0F,
  };
  events[3] = seam::live_voice::LiveEvent{
      .sampleOffset = 16000U,
      .type = seam::live_voice::EventType::Pressure,
      .noteId = 2,
      .key = 64,
      .value = 0.7F,
  };
  for (std::size_t index = 0U; index < 33U; ++index) {
    events[4U + index] = seam::live_voice::LiveEvent{
        .sampleOffset = 20000U,
        .type = seam::live_voice::EventType::NoteOn,
        .noteId = static_cast<std::int32_t>(10U + index),
        .channel = 1,
        .key = static_cast<std::int16_t>(48U + index % 24U),
        .value = 0.6F,
    };
  }
  events[37] = seam::live_voice::LiveEvent{
      .sampleOffset = 36000U,
      .type = seam::live_voice::EventType::NoteOff,
      .noteId = 1,
      .key = 60,
  };
  events[38] = seam::live_voice::LiveEvent{
      .sampleOffset = 40000U,
      .type = seam::live_voice::EventType::NoteChoke,
      .noteId = 2,
      .key = 64,
  };
  events[39] = seam::live_voice::LiveEvent{
      .sampleOffset = 44000U,
      .type = seam::live_voice::EventType::Midi1,
      .midi = {0xB0U, 123U, 0U},
  };
  engine.process(events, outputs, 2U, frames);

  double energy = 0.0;
  bool finite = true;
  std::vector<float> interleaved;
  interleaved.resize(static_cast<std::size_t>(frames) * 2U);
  for (std::uint32_t frame = 0U; frame < frames; ++frame) {
    finite = finite && std::isfinite(left[frame]) && std::isfinite(right[frame]);
    energy += std::abs(static_cast<double>(left[frame])) +
              std::abs(static_cast<double>(right[frame]));
    interleaved[static_cast<std::size_t>(frame) * 2U] = left[frame];
    interleaved[static_cast<std::size_t>(frame) * 2U + 1U] = right[frame];
  }
  if (!finite || energy <= 1.0) return 5;
  const auto wav = seam::voicebank::writePcm16Wav(
      output / "phase12c-live-articulation.wav", 48000U, 2U, interleaved);
  if (!wav) return 6;
  const auto stats = engine.stats();
  std::ofstream summary(output / "phase12c-live-articulation-summary.json");
  summary << "{\n"
          << "  \"voicebankId\": \"" << resources.value()->identity.id
          << "\",\n"
          << "  \"voicebankVersion\": \""
          << resources.value()->identity.version << "\",\n"
          << "  \"voicebankContentHash\": \""
          << resources.value()->identity.contentHash << "\",\n"
          << "  \"unitCount\": " << resources.value()->units.size() << ",\n"
          << "  \"energy\": " << energy << ",\n"
          << "  \"noteOns\": " << stats.noteOns << ",\n"
          << "  \"steals\": " << stats.steals << ",\n"
          << "  \"transitionFallbacks\": " << stats.transitionFallbacks << ",\n"
          << "  \"eventOverflows\": " << stats.eventOverflows << ",\n"
          << "  \"renderedFrames\": " << stats.renderedFrames << ",\n"
          << "  \"finite\": " << (finite ? "true" : "false") << ",\n"
          << "  \"result\": \"PASS\"\n"
          << "}\n";
  if (!summary) return 7;
  std::cout << "PHASE12C_LIVE_DEMO=PASS energy=" << energy
            << " steals=" << stats.steals << '\n';
  return 0;
}
