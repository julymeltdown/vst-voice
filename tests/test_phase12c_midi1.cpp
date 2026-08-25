#include "test_framework.hpp"

#include "seam/live_voice/midi1_decoder.hpp"
#include "seam/live_voice/diagnostics.hpp"

TEST_CASE("MIDI 1 decoder maps note, expression, pedal and all-notes actions") {
  using seam::live_voice::Midi1ActionType;
  using seam::live_voice::Midi1Decoder;

  const auto noteOn = Midi1Decoder::decode({0x90U, 60U, 100U});
  CHECK(noteOn.type == Midi1ActionType::NoteOn);
  CHECK(noteOn.channel == 0U);
  CHECK(noteOn.normalized > 0.7F);

  const auto zeroVelocity = Midi1Decoder::decode({0x90U, 60U, 0U});
  CHECK(zeroVelocity.type == Midi1ActionType::NoteOff);

  const auto bend = Midi1Decoder::decode({0xE0U, 0U, 65U});
  CHECK(bend.type == Midi1ActionType::PitchBend);
  CHECK(bend.normalized > 0.0F);

  const auto pressure = Midi1Decoder::decode({0xD0U, 96U, 0U});
  CHECK(pressure.type == Midi1ActionType::Pressure);
  CHECK(pressure.normalized > 0.7F);

  const auto vibrato = Midi1Decoder::decode({0xB0U, 1U, 127U});
  CHECK(vibrato.type == Midi1ActionType::ControlChange);
  CHECK(vibrato.data1 == 1U);
  CHECK(vibrato.normalized == 1.0F);

  const auto pedal = Midi1Decoder::decode({0xB0U, 64U, 127U});
  CHECK(pedal.type == Midi1ActionType::SustainPedal);
  CHECK(pedal.normalized == 1.0F);

  const auto allNotes = Midi1Decoder::decode({0xB0U, 123U, 0U});
  CHECK(allNotes.type == Midi1ActionType::AllNotesOff);
}

TEST_CASE("MIDI 1 decoder rejects malformed data without allocation-facing state") {
  using seam::live_voice::Midi1ActionType;
  const auto invalidStatus =
      seam::live_voice::Midi1Decoder::decode({0xF8U, 0U, 0U});
  CHECK(invalidStatus.type == Midi1ActionType::None);
  const auto invalidData =
      seam::live_voice::Midi1Decoder::decode({0x90U, 0xFFU, 0xFFU});
  CHECK(invalidData.type == Midi1ActionType::None);
}

TEST_CASE("live diagnostics expose stable machine-readable names") {
  CHECK(seam::live_voice::liveDiagnosticName(
            seam::live_voice::LiveDiagnosticCode::VoicebankMissing) ==
        "voicebank-missing");
  CHECK(seam::live_voice::liveDiagnosticName(
            seam::live_voice::LiveDiagnosticCode::InvalidMidiEvent) ==
        "invalid-midi-event");
}
