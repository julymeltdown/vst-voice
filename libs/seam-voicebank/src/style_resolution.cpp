#include "seam/voicebank/style_resolution.hpp"

#include <algorithm>

namespace seam::voicebank {

core::Result<VoiceStyleResolution> resolveVoiceStyle(
    const domain::VoicebankReference& reference,
    const domain::VoiceStyleSelection& selection,
    const VoicebankResolution& bank) {
  const auto valid = selection.validate();
  if (!valid) return core::Result<VoiceStyleResolution>{valid.error()};
  VoiceStyleResolution result{VoiceStyleStatus::BankUnresolved, selection};
  const auto validHash = reference.contentHash.size() == 64U &&
      std::all_of(reference.contentHash.begin(), reference.contentHash.end(), [](char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
               (value >= 'A' && value <= 'F');
      });
  if (!bank.resolved() || !validHash || reference.id.empty() || reference.version.empty()) {
    return result;
  }
  const auto& candidate = *bank.candidate;
  if (candidate.trust != VoicebankTrust::TrustedInstalled ||
      candidate.manifest.id != reference.id || candidate.manifest.version != reference.version ||
      candidate.contentHash != reference.contentHash) {
    return result;
  }
  const auto manifestValidation = candidate.manifest.validate();
  if (!manifestValidation) return core::Result<VoiceStyleResolution>{manifestValidation.error()};
  const auto& styles = candidate.manifest.styles;
  if (selection.origin == domain::VoiceStyleOrigin::LegacyNeedsExactBankResolution) {
    result.selection = {domain::VoiceStyleOrigin::LegacyManifestFirst, styles.front()};
  } else if (selection.origin == domain::VoiceStyleOrigin::Unselected) {
    if (styles.size() != 1U) {
      result.status = VoiceStyleStatus::NeedsChoice;
      return result;
    }
    result.selection = {domain::VoiceStyleOrigin::SoleDeclaredStyle, styles.front()};
  }
  const auto selectedValidation = result.selection.validate();
  if (!selectedValidation) return core::Result<VoiceStyleResolution>{selectedValidation.error()};
  result.status = std::find(styles.begin(), styles.end(), result.selection.styleId) != styles.end()
                      ? VoiceStyleStatus::Resolved : VoiceStyleStatus::MissingStyle;
  return result;
}

}
