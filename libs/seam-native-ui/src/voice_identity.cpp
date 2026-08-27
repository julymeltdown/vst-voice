#include "seam/native_ui/voice_identity.hpp"

namespace seam::native_ui {
namespace {

bool exactCard(const VoiceIdentityInput& input) noexcept {
  return input.card != nullptr && input.card->id == input.reference.id &&
         input.card->version == input.reference.version &&
         input.card->contentHash == input.reference.contentHash &&
         input.card->selectable;
}

bool matchingCharacter(const VoiceIdentityInput& input) noexcept {
  if (!exactCard(input) || input.character == nullptr) return false;
  return !input.card->characterId.empty() &&
         input.character->id == input.card->characterId &&
         input.character->version == input.card->characterVersion &&
         input.character->voicebankId == input.reference.id;
}

bool hasError(std::span<const authoring::Diagnostic> diagnostics) noexcept {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.severity == authoring::DiagnosticSeverity::Critical ||
        (diagnostic.severity == authoring::DiagnosticSeverity::Error &&
         diagnostic.code != "BANK_MISSING")) return true;
  }
  return false;
}

bool hasWarning(std::span<const authoring::Diagnostic> diagnostics) noexcept {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.severity == authoring::DiagnosticSeverity::Warning) return true;
  }
  return false;
}

}

VoiceIdentityView resolveVoiceIdentity(const VoiceIdentityInput& input) noexcept {
  VoiceIdentityView result;
  const auto cardMatches = exactCard(input);
  result.name = cardMatches ? input.card->displayName : "Voicebank unavailable";
  result.identity = input.reference.id.empty()
                        ? "No voicebank selected"
                        : input.reference.id + " " + input.reference.version;
  result.recovery = cardMatches ? "" : "Choose or relink a trusted voicebank";
  if (hasError(input.diagnostics)) result.state = VoiceIdentityState::Error;
  else if (!cardMatches) result.state = VoiceIdentityState::Missing;
  else if (hasWarning(input.diagnostics) ||
           input.renderStatus.state == RenderStatusState::Stale) {
    result.state = VoiceIdentityState::Warning;
  } else if (input.renderStatus.state == RenderStatusState::Queued ||
             input.renderStatus.state == RenderStatusState::Rendering) {
    result.state = VoiceIdentityState::Rendering;
  } else if (input.completeDwell) {
    result.state = VoiceIdentityState::Complete;
  } else {
    result.state = VoiceIdentityState::Ready;
  }
  result.characterActive = matchingCharacter(input) &&
                           result.state != VoiceIdentityState::Missing &&
                           result.state != VoiceIdentityState::Error;
  if (result.characterActive) {
    result.accentPrimary = input.character->accentPrimary;
    result.accentSecondary = input.character->accentSecondary;
  }
  return result;
}

std::string_view voiceIdentityStateName(VoiceIdentityState state) noexcept {
  switch (state) {
    case VoiceIdentityState::Missing: return "Missing";
    case VoiceIdentityState::Ready: return "Ready";
    case VoiceIdentityState::Rendering: return "Rendering";
    case VoiceIdentityState::Complete: return "Complete";
    case VoiceIdentityState::Warning: return "Warning";
    case VoiceIdentityState::Error: return "Error";
  }
  return "Missing";
}

}
