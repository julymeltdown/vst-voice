#pragma once

#include "seam/native_ui/design/shell_workspace.hpp"
#include "seam/native_ui/voice_designer_session.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace seam::native_ui::design {

// What the VOICE workspace needs from its host. The Voice Designer session lives in the host (the
// standalone editor creates it on first use, with the installed-singer roots protected); a host
// without one (the CLAP plug-in) leaves designer empty and VOICE says so and offers the browser.
// File, naming and seed choices are the host's native dialogs, the same ones the Voicebank Studio
// uses; audition audio plays through the host's audition output, or VOICE says it cannot.
struct ShellVoiceHost final {
  std::function<VoiceDesignerSession*()> designer;
  std::string unavailable{"Voice design runs in the standalone Project SEAM app."};
  // Save As (save = true) or Open. An empty optional is a cancelled dialog.
  std::function<core::Result<std::optional<std::filesystem::path>>(bool save,
                                                                    const std::filesystem::path&)>
      choosePath;
  // The unsaved-changes prompt before New or Open replaces a dirty recipe; true discards.
  std::function<core::Result<bool>()> confirmDiscard;
  // Phone and style for a duplicated pose (frication = false) or a new frication source.
  std::function<core::Result<std::optional<std::pair<std::string, std::string>>>(bool frication)>
      choosePoseIdentity;
  // An exact decimal seed, starting from the current one.
  std::function<core::Result<std::optional<std::string>>(const std::string& current,
                                                         bool frication)>
      chooseSeed;
  // Audition playback through the host's audio output. Empty when the host cannot play.
  std::function<core::Result<void>(std::shared_ptr<const voicebank::AudioBuffer>)> play;
  std::function<void()> stop;
  // The measured peak (0..1) of the audition block the output device last played, or nothing when
  // no audition plays. The host polls its audition output here, so a finished audition ends.
  std::function<std::optional<float>()> level;
  std::string playUnavailable{"This host has no audition output"};
};

// The VOICE workspace (docs/design/SEAM_UI_REDESIGN_CODE_PLAN_2026-09-25.md §7.2): the host's Voice
// Designer session as a SOURCE / RESONANCE / NOISE module rack with a signal rail, an OUTPUT
// strip and the singer listening to the audition. Every edit is an existing session command, so
// validation, undo and preview invalidation are the session's own.
class VoiceWorkspace : public ShellWorkspace {
public:
  virtual void setHost(ShellVoiceHost host) = 0;
  // The look's portrait artwork for the listening singer; null draws the ring alone.
  virtual void setPortrait(std::shared_ptr<const paint::Image> portrait) = 0;
  // The pose the hero holds while an audition plays; empty keeps the portrait above. The shell passes
  // the package's listening-state asset when it has one and nothing when it does not, so the hero
  // never invents a pose the artwork does not declare.
  virtual void setListeningPortrait(std::shared_ptr<const paint::Image> listening) = 0;
  // The measured peak of the audition block the host's output device last played, or nothing when
  // nothing plays. The shell reads it so the protagonist's state is the same one VOICE is showing.
  [[nodiscard]] virtual std::optional<float> auditionLevel() const noexcept = 0;
  // Owner-thread step before each painted VOICE frame: collects the session's finished file and
  // audition work and starts a requested audition once it is rendered. Returns true while work
  // or playback is in progress, so the shell keeps repainting.
  virtual bool poll() = 0;
  // Designer undo and redo while VOICE is shown (the editor's history is not on screen).
  virtual core::Result<void> undo(bool redo) = 0;
  // Status line for the last command (its error, or what it did); empty when there is none.
  [[nodiscard]] virtual const std::string& message() const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<VoiceWorkspace> makeVoiceWorkspace();

}  // namespace seam::native_ui::design
