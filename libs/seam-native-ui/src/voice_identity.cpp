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
  if (input.character == nullptr) return false;
  const auto active = input.activeResource.value_or(domain::SingerResourceIdentity{
      .kind = domain::SingerResourceKind::Sample,
      .id = input.reference.id,
      .version = input.reference.version,
      .contentHash = input.reference.contentHash});
  if (input.character->resourceIdentity.has_value())
    return *input.character->resourceIdentity == active;
  return exactCard(input) && !input.card->characterId.empty() &&
         input.character->id == input.card->characterId &&
         input.character->version == input.card->characterVersion &&
         active.kind == domain::SingerResourceKind::Sample &&
         input.character->voicebankId == active.id;
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

bool currentRenderConfirmed(const RenderStatusView& status) noexcept {
  if (status.state != RenderStatusState::Ready || status.audibleAudioStale) return false;
  if (!status.hasAudibleAudio) return true;
  return status.audibleRevision == status.requestedRevision &&
         status.audibleQuality == status.requestedQuality;
}

}

VoiceIdentityView resolveVoiceIdentity(const VoiceIdentityInput& input) noexcept {
  VoiceIdentityView result;
  const auto activeKind = input.activeResource.has_value()
      ? input.activeResource->kind : domain::SingerResourceKind::Sample;
  const bool sampleRoute = activeKind == domain::SingerResourceKind::Sample;
  const bool cardMatches = sampleRoute && exactCard(input);
  const bool alternativeSelected = !sampleRoute && input.activeResource.has_value() &&
      !input.activeResource->id.empty() && !input.activeResource->version.empty() &&
      !input.activeResource->contentHash.empty();
  const auto family = activeKind == domain::SingerResourceKind::Procedural
      ? std::string_view{"Procedural singer"}
      : activeKind == domain::SingerResourceKind::Neural
          ? std::string_view{"Neural singer"}
          : std::string_view{"Sample singer"};
  if (cardMatches) {
    result.name = input.card->displayName;
    result.identity = input.reference.id + " " + input.reference.version;
  } else if (alternativeSelected) {
    result.name = std::string{family} + ": " + input.activeResource->id;
    result.identity = std::string{family} + " " + input.activeResource->id + " " +
        input.activeResource->version + " #" + input.activeResource->contentHash.substr(0U, 12U);
  } else {
    result.name = sampleRoute ? "Voicebank unavailable" : std::string{family} + " not selected";
    result.identity = sampleRoute
        ? (input.reference.id.empty() ? "No sample singer selected"
                                      : input.reference.id + " " + input.reference.version)
        : std::string{family} + " not selected";
  }
  result.recovery = cardMatches ? "" : alternativeSelected
      ? (currentRenderConfirmed(input.renderStatus)
            ? "" : "Selected resource; render to confirm availability")
      : sampleRoute ? "Choose or relink a trusted voicebank"
                    : "Select a complete singer resource identity";
  if (input.character != nullptr && input.character->resourceIdentity.has_value() &&
      !matchingCharacter(input)) {
    result.recovery = "Loaded character package does not match the selected singer resource identity";
  } else if (cardMatches && input.character != nullptr && !matchingCharacter(input)) {
    result.recovery = "Loaded character package does not match the selected voicebank";
  }
  if (hasError(input.diagnostics)) result.state = VoiceIdentityState::Error;
  else if (input.renderStatus.state == RenderStatusState::Failed)
    result.state = VoiceIdentityState::Error;
  else if (!cardMatches && !alternativeSelected)
    result.state = sampleRoute ? VoiceIdentityState::Missing : VoiceIdentityState::Selected;
  else if (hasWarning(input.diagnostics) ||
           input.renderStatus.state == RenderStatusState::Stale) {
    result.state = VoiceIdentityState::Warning;
  } else if (input.renderStatus.state == RenderStatusState::Queued ||
             input.renderStatus.state == RenderStatusState::Rendering) {
    result.state = VoiceIdentityState::Rendering;
  // A completed render is a presentation dwell state only while transport is
  // idle.  Once the user starts playback, the character must return to its
  // focused/performing state instead of remaining on the completion pose.
  } else if (input.completeDwell && !input.focused) {
    result.state = VoiceIdentityState::Complete;
  } else if (alternativeSelected && !currentRenderConfirmed(input.renderStatus)) {
    result.state = VoiceIdentityState::Selected;
  } else {
    result.state = VoiceIdentityState::Ready;
  }
  result.characterActive = matchingCharacter(input) &&
                           result.state != VoiceIdentityState::Missing &&
                           result.state != VoiceIdentityState::Error;
  if (result.characterActive) {
    result.accentPrimary = input.character->accentPrimary;
    result.accentSecondary = input.character->accentSecondary;
    result.hasPerformanceAssets = input.character->hasPerformance;
  }
  return result;
}

std::string_view voiceIdentityStateName(VoiceIdentityState state) noexcept {
  switch (state) {
    case VoiceIdentityState::Missing: return "Missing";
    case VoiceIdentityState::Selected: return "Selected";
    case VoiceIdentityState::Ready: return "Ready";
    case VoiceIdentityState::Rendering: return "Rendering";
    case VoiceIdentityState::Complete: return "Complete";
    case VoiceIdentityState::Warning: return "Warning";
    case VoiceIdentityState::Error: return "Error";
  }
  return "Missing";
}

}
