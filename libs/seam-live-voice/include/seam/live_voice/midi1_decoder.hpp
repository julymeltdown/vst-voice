#pragma once

#include <array>
#include <cstdint>

namespace seam::live_voice {

enum class Midi1ActionType : std::uint8_t {
  None,
  NoteOn,
  NoteOff,
  PitchBend,
  Pressure,
  ControlChange,
  SustainPedal,
  AllNotesOff,
  AllSoundOff,
};

struct Midi1Action final {
  Midi1ActionType type{Midi1ActionType::None};
  std::uint8_t channel{0U};
  std::uint8_t data1{0U};
  std::uint8_t data2{0U};
  float normalized{0.0F};
};

class Midi1Decoder final {
 public:
  [[nodiscard]] static Midi1Action decode(
      std::array<std::uint8_t, 3> bytes) noexcept;
};

}
