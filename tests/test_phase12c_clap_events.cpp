#include "test_framework.hpp"

#include "seam/clap_editor/editor_runtime.hpp"

#include <array>
#include <cmath>
#include <filesystem>

TEST_CASE("canonical CLAP live adapter dispatches expression and MIDI events") {
  seam::live_voice::VoiceEngine::resetCallPathEvidence();
  seam::live_voice::VoiceEngine instrument;
  CHECK(instrument.publishVoicebankResource(
      seam::phase12c::makeEmbeddedHumanResource()));
  instrument.noteOn(1, 60, 0.9F);
  instrument.dispatchLiveEvent(seam::live_voice::LiveEvent{
      .type = seam::live_voice::EventType::PitchBend,
      .noteId = 1,
      .key = 60,
      .value = 2.0F,
  });
  instrument.dispatchLiveEvent(seam::live_voice::LiveEvent{
      .type = seam::live_voice::EventType::Pressure,
      .noteId = 1,
      .key = 60,
      .value = 0.7F,
  });
  instrument.dispatchLiveEvent(seam::live_voice::LiveEvent{
      .type = seam::live_voice::EventType::Midi1,
      .midi = {0x90U, 64U, 100U},
  });
  std::array<float, 256> left{};
  std::array<float, 256> right{};
  float* outputs[2] = {left.data(), right.data()};
  instrument.renderLiveRange(outputs, 2U, 0U, 256U);
  double energy = 0.0;
  for (const auto sample : left) {
    CHECK(std::isfinite(sample));
    energy += std::abs(static_cast<double>(sample));
  }
  CHECK(energy > 1.0);
  CHECK(instrument.activeVoiceCount() == 2U);
  CHECK(seam::live_voice::VoiceEngine::callPathEvidence() > 0U);

  instrument.reset();
  std::array<seam::live_voice::LiveEvent, 1025> storm{};
  for (std::size_t index = 0U; index < storm.size(); ++index) {
    storm[index] = seam::live_voice::LiveEvent{
        .type = seam::live_voice::EventType::NoteOn,
        .noteId = static_cast<std::int32_t>(index),
        .key = static_cast<std::int16_t>(48U + (index % 24U)),
        .value = 0.7F,
    };
  }
  instrument.process(storm, outputs, 2U, 64U);
  CHECK(instrument.stats().eventOverflows >= 1U);
  CHECK(instrument.activeVoiceCount() <= seam::phase12c::kMaxVoices);

  seam::clap_editor::EditorRuntime missingBank(
      std::nullopt, std::filesystem::path{"assets/character-01"}, {});
  missingBank.noteOn(77, 60, 0.8F);
  for (int frame = 0; frame < 128; ++frame) {
    CHECK(missingBank.renderLiveSample() == 0.0F);
  }
}
