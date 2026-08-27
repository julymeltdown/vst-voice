#pragma once

#include "seam/authoring/diagnostic.hpp"
#include "seam/authoring/voicebank_browser.hpp"
#include "seam/character/character.hpp"
#include "seam/domain/project.hpp"
#include "seam/native_ui/render_status_panel.hpp"

#include <span>
#include <string>

namespace seam::native_ui {

enum class VoiceIdentityState { Missing, Ready, Rendering, Complete, Warning, Error };

struct VoiceIdentityInput final {
  domain::VoicebankReference reference;
  const authoring::VoicebankCard* card{nullptr};
  struct CharacterBinding final {
    std::string id;
    std::string version;
    std::string voicebankId;
    std::string accentPrimary;
    std::string accentSecondary;
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
  std::string accentPrimary;
  std::string accentSecondary;
};

[[nodiscard]] VoiceIdentityView resolveVoiceIdentity(
    const VoiceIdentityInput& input) noexcept;
[[nodiscard]] std::string_view voiceIdentityStateName(
    VoiceIdentityState state) noexcept;

}
