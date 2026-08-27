#include "seam/authoring/voicebank_browser.hpp"

#include <algorithm>

namespace seam::authoring {
namespace {

std::string languageName(domain::Language language) {
  switch (language) {
    case domain::Language::Japanese: return "Japanese";
    case domain::Language::Korean: return "Korean";
    case domain::Language::English: return "English";
    case domain::Language::Unspecified: return "Unspecified";
  }
  return "Unspecified";
}

int trustRank(voicebank::VoicebankTrust trust) noexcept {
  switch (trust) {
    case voicebank::VoicebankTrust::TrustedInstalled: return 0;
    case voicebank::VoicebankTrust::DevelopmentFixture: return 1;
    case voicebank::VoicebankTrust::UntrustedInstalled: return 2;
  }
  return 3;
}

}  // namespace

void VoicebankBrowserModel::rebuild(
    std::span<const voicebank::VoicebankCandidate> candidates) {
  cards_.clear();
  cards_.reserve(candidates.size());
  for (const auto& candidate : candidates) {
    const auto inventory =
        voicebank::VoicebankCoverageAnalyzer::inventory(candidate.manifest);
    const auto installed =
        candidate.trust != voicebank::VoicebankTrust::DevelopmentFixture;
    const auto selectable =
        candidate.trust == voicebank::VoicebankTrust::TrustedInstalled ||
        (candidate.trust == voicebank::VoicebankTrust::DevelopmentFixture &&
         allowDevelopmentFixtures_);
    std::vector<std::string> diagnostics;
    if (candidate.trust == voicebank::VoicebankTrust::UntrustedInstalled) {
      diagnostics.push_back(
          "Installed voicebank is visible but cannot be selected because its "
          "receipt or signer trust could not be verified");
    } else if (candidate.trust == voicebank::VoicebankTrust::DevelopmentFixture &&
               !allowDevelopmentFixtures_) {
      diagnostics.push_back(
          "Development voicebank requires an explicit development override");
    }
    cards_.push_back(VoicebankCard{
        .id = candidate.manifest.id,
        .version = candidate.manifest.version,
        .displayName = candidate.manifest.displayName,
        .language = languageName(candidate.manifest.language),
        .styles = candidate.manifest.styles,
        .contentHash = candidate.contentHash,
        .contentHashAbbreviation = candidate.contentHash.substr(
            0U, std::min<std::size_t>(12U, candidate.contentHash.size())),
        .trust = candidate.trust,
        .trustLabel = std::string{voicebank::voicebankTrustName(candidate.trust)},
        .signerKeyId = candidate.signerKeyId,
        .installed = installed,
        .selectable = selectable,
        .characterAvailable = !candidate.manifest.characterId.empty() &&
                              !candidate.manifest.characterVersion.empty(),
        .characterId = candidate.manifest.characterId,
        .characterVersion = candidate.manifest.characterVersion,
        .enabledUnitCount = inventory.enabledUnitCount,
        .disabledUnitCount = inventory.disabledUnitCount,
        .rootPitchLayers = inventory.rootPitchLayers,
        .hasSustain = inventory.hasSustain,
        .hasRelease = inventory.hasRelease,
        .hasBreath = inventory.hasBreath,
        .diagnostics = std::move(diagnostics),
    });
  }
  std::stable_sort(cards_.begin(), cards_.end(), [](const auto& lhs,
                                                    const auto& rhs) {
    const auto lhsRank = trustRank(lhs.trust);
    const auto rhsRank = trustRank(rhs.trust);
    if (lhsRank != rhsRank) return lhsRank < rhsRank;
    if (lhs.displayName != rhs.displayName) {
      return lhs.displayName < rhs.displayName;
    }
    if (lhs.version != rhs.version) return lhs.version < rhs.version;
    return lhs.contentHash < rhs.contentHash;
  });
}

}  // namespace seam::authoring
