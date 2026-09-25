#pragma once

#include "seam/authoring/diagnostic.hpp"
#include "seam/authoring/voicebank_browser.hpp"
#include "seam/character/character.hpp"
#include "seam/domain/project.hpp"
#include "seam/native_ui/render_status_panel.hpp"

#include <span>
#include <optional>
#include <string>

namespace seam::native_ui {

enum class VoiceIdentityState { Missing, Selected, Ready, Rendering, Complete, Warning, Error };

struct VoiceIdentityInput final {
  domain::VoicebankReference reference;
  // The track's actual selected carrier identity. When absent, preserve the legacy sample-bank path.
  std::optional<domain::SingerResourceIdentity> activeResource;
  const authoring::VoicebankCard* card{nullptr};
  struct CharacterBinding final {
    std::string id;
    std::string version;
    std::string voicebankId;
    std::optional<domain::SingerResourceIdentity> resourceIdentity;
    std::string accentPrimary;
    std::string accentSecondary;
    bool hasPerformance{false};
  };
  const CharacterBinding* character{nullptr};
  RenderStatusView renderStatus;
  std::span<const authoring::Diagnostic> diagnostics;
  bool focused{false};
  bool completeDwell{false};
};

struct VoiceIdentityView final {
  VoiceIdentityState state{VoiceIdentityState::Missing};
  std::string name;
  std::string identity;
  std::string recovery;
  bool characterActive{false};
  bool hasPerformanceAssets{false};
  std::string accentPrimary;
  std::string accentSecondary;
};

[[nodiscard]] VoiceIdentityView resolveVoiceIdentity(
    const VoiceIdentityInput& input) noexcept;
[[nodiscard]] std::string_view voiceIdentityStateName(
    VoiceIdentityState state) noexcept;

}
