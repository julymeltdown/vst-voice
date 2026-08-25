#include "seam/live_voice/diagnostics.hpp"

namespace seam::live_voice {

std::string_view liveDiagnosticName(LiveDiagnosticCode code) noexcept {
  switch (code) {
    case LiveDiagnosticCode::None: return "none";
    case LiveDiagnosticCode::VoicebankMissing: return "voicebank-missing";
    case LiveDiagnosticCode::VoicebankUntrusted: return "voicebank-untrusted";
    case LiveDiagnosticCode::ArticulationInventoryInvalid:
      return "articulation-inventory-invalid";
    case LiveDiagnosticCode::LiveResourcePublicationBusy:
      return "live-resource-publication-busy";
    case LiveDiagnosticCode::UnsupportedNoteExpression:
      return "unsupported-note-expression";
    case LiveDiagnosticCode::InvalidMidiEvent: return "invalid-midi-event";
  }
  return "unknown";
}

}
