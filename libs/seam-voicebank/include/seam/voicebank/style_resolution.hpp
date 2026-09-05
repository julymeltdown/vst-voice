#pragma once

#include "seam/domain/voice_style_selection.hpp"
#include "seam/voicebank/catalog.hpp"

namespace seam::voicebank {

enum class VoiceStyleStatus { Resolved, BankUnresolved, NeedsChoice, MissingStyle };

struct VoiceStyleResolution final {
  VoiceStyleStatus status{VoiceStyleStatus::BankUnresolved};
  domain::VoiceStyleSelection selection;
};

[[nodiscard]] core::Result<VoiceStyleResolution> resolveVoiceStyle(
    const domain::VoicebankReference& reference,
    const domain::VoiceStyleSelection& selection,
    const VoicebankResolution& bank);

}
