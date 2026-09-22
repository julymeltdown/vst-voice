#include "seam/domain/voice_style_selection.hpp"

#include "seam/domain/note.hpp"
#include <cmath>

namespace seam::domain {

core::Result<void> VoiceStyleSelection::validate() const {
  if (blend && (origin != VoiceStyleOrigin::Explicit || styleId.empty() ||
      blend->targetStyleId.empty() || blend->targetStyleId == styleId ||
      blend->targetStyleId.size() > 1024U || !fromUtf8(blend->targetStyleId) ||
      !std::isfinite(blend->amount) || blend->amount < 0.0F || blend->amount > 1.0F)) {
    return core::failure(core::ErrorCode::InvariantViolation,
        "Style blend requires two distinct explicit styles and a finite amount between zero and one");
  }
  switch (origin) {
    case VoiceStyleOrigin::Unselected:
    case VoiceStyleOrigin::LegacyNeedsExactBankResolution:
      if (styleId.empty()) return core::success();
      break;
    case VoiceStyleOrigin::Explicit:
    case VoiceStyleOrigin::SoleDeclaredStyle:
    case VoiceStyleOrigin::LegacyManifestFirst:
      if (!styleId.empty() && styleId.size() <= 1024U && fromUtf8(styleId)) {
        return core::success();
      }
      break;
  }
  return core::failure(core::ErrorCode::InvariantViolation,
                       "Voice style selection has invalid identity or provenance");
}

}
