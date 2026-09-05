#include "seam/domain/voice_style_selection.hpp"

#include "seam/domain/note.hpp"

namespace seam::domain {

core::Result<void> VoiceStyleSelection::validate() const {
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
