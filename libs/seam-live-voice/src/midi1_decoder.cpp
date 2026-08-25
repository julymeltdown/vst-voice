#include "seam/live_voice/midi1_decoder.hpp"

namespace seam::live_voice {

Midi1Action Midi1Decoder::decode(
    std::array<std::uint8_t, 3> bytes) noexcept {
  const auto status = bytes[0];
  const auto high = static_cast<std::uint8_t>(status & 0xF0U);
  const auto channel = static_cast<std::uint8_t>(status & 0x0FU);
  if (status < 0x80U || status >= 0xF0U || bytes[1] > 127U || bytes[2] > 127U) {
    return {};
  }
  const auto data1 = bytes[1];
  const auto data2 = bytes[2];
  switch (high) {
    case 0x80U:
      return {Midi1ActionType::NoteOff, channel, data1, data2,
              static_cast<float>(data2) / 127.0F};
    case 0x90U:
      return {data2 == 0U ? Midi1ActionType::NoteOff : Midi1ActionType::NoteOn,
              channel, data1, data2, static_cast<float>(data2) / 127.0F};
    case 0xB0U:
      if (data1 == 64U) {
        return {Midi1ActionType::SustainPedal, channel, data1, data2,
                static_cast<float>(data2) / 127.0F};
      }
      if (data1 == 123U) {
        return {Midi1ActionType::AllNotesOff, channel, data1, data2, 0.0F};
      }
      if (data1 == 120U) {
        return {Midi1ActionType::AllSoundOff, channel, data1, data2, 0.0F};
      }
      return {Midi1ActionType::ControlChange, channel, data1, data2,
              static_cast<float>(data2) / 127.0F};
    case 0xD0U:
      return {Midi1ActionType::Pressure, channel, data1, data2,
              static_cast<float>(data1) / 127.0F};
    case 0xE0U: {
      const auto value = static_cast<std::uint16_t>(data1) |
                         (static_cast<std::uint16_t>(data2) << 7U);
      return {Midi1ActionType::PitchBend, channel, data1, data2,
              static_cast<float>(static_cast<int>(value) - 8192) / 8192.0F};
    }
    default:
      return {};
  }
}

}
