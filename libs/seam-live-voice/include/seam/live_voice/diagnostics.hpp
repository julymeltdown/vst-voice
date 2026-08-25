#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace seam::live_voice {

enum class LiveDiagnosticCode : std::uint8_t {
  None,
  VoicebankMissing,
  VoicebankUntrusted,
  ArticulationInventoryInvalid,
  LiveResourcePublicationBusy,
  UnsupportedNoteExpression,
  InvalidMidiEvent,
};

struct LiveDiagnostic final {
  LiveDiagnosticCode code{LiveDiagnosticCode::None};
  std::uint64_t occurrenceCount{0U};
  std::string identity;
};

[[nodiscard]] std::string_view liveDiagnosticName(
    LiveDiagnosticCode code) noexcept;

}
