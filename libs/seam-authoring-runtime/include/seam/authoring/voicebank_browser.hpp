#pragma once

#include "seam/voicebank/catalog.hpp"
#include "seam/voicebank/coverage.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace seam::authoring {

struct VoicebankCard final {
  std::string id;
  std::string version;
  std::string displayName;
  std::string language;
  std::vector<std::string> styles;
  std::string contentHash;
  std::string contentHashAbbreviation;
  voicebank::VoicebankTrust trust{voicebank::VoicebankTrust::UntrustedInstalled};
  std::string trustLabel;
  std::string signerKeyId;
  bool installed{false};
  bool selectable{false};
  bool characterAvailable{false};
  std::string characterId;
  std::string characterVersion;
  std::size_t enabledUnitCount{0U};
  std::size_t disabledUnitCount{0U};
  std::vector<std::int32_t> rootPitchLayers;
  bool hasSustain{false};
  bool hasRelease{false};
  bool hasBreath{false};
  std::vector<std::string> diagnostics;
};

class VoicebankBrowserModel final {
public:
  explicit VoicebankBrowserModel(bool allowDevelopmentFixtures = false)
      : allowDevelopmentFixtures_(allowDevelopmentFixtures) {}

  void rebuild(std::span<const voicebank::VoicebankCandidate> candidates);
  [[nodiscard]] const std::vector<VoicebankCard>& cards() const noexcept {
    return cards_;
  }

private:
  bool allowDevelopmentFixtures_{false};
  std::vector<VoicebankCard> cards_;
};

}  // namespace seam::authoring
