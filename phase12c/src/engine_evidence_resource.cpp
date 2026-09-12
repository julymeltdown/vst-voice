#include "seam/phase12c/canonical_evidence.hpp"
#include "seam/live_voice/live_resources.hpp"
#include "seam/voicebank/catalog.hpp"

namespace seam::phase12c {
// Legacy linked-engine soak only. Never link this helper into the CLAP host.
core::Result<std::shared_ptr<const LiveVoicebankResource>>
loadCanonicalVoicebankResource(const std::filesystem::path& bank) {
  voicebank::VoicebankCatalog catalog;
  const auto scanned = catalog.scan({voicebank::VoicebankSearchRoot{
      .path = bank, .kind = voicebank::VoicebankRootKind::Development}});
  if (!scanned || scanned.value().size() != 1U) {
    return core::failure<std::shared_ptr<const LiveVoicebankResource>>(
        core::ErrorCode::NotFound, "Engine fixture requires exactly one bank");
  }
  return live_voice::buildTrustedResource(scanned.value().front());
}
}
