#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/project.hpp"
#include "seam/voicebank/catalog.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace seam::native_ui {

// Studio's return-to-song step after an engineering candidate has been packed, signed and installed
// as a sample Voicebank. The song editor could already offer the bank on its New Project screen, but
// only if the creator knew to find it there; this writes a song whose one vocal track is bound to
// that exact installed bank, so the editor opens straight into a song that sings with it.
struct InstalledBankSongRequest final {
  // Absolute path of a project file that must not exist yet. An existing project is never replaced,
  // even when a save panel already asked the user about replacing it.
  std::filesystem::path projectPath;
  // The installed bank the song must end up bound to, as installed by this session: exact ID,
  // version and content hash together with the directory the installation reported.
  std::string voicebankId;
  std::string voicebankVersion;
  std::string contentHash;
  std::filesystem::path installDirectory;
};

struct InstalledBankSongProject final {
  std::filesystem::path projectPath;
  std::string projectName;
  domain::VoicebankReference voicebank;
  std::string styleId;
  std::string projectSha256;
};

// Re-scans the given Voicebank roots, requires the bank installed by this session to still be present
// with the same content and to resolve as a trusted installation, then publishes a new 120 BPM 4/4
// 48 kHz stereo song project bound to it. The style is the one the editor's own resolution would
// pick, so the saved song reopens with the same selection. Nothing is written on refusal, and writing
// into an installation folder is refused because signed installations are immutable.
[[nodiscard]] core::Result<InstalledBankSongProject> createInstalledBankSongProject(
    const std::vector<voicebank::VoicebankSearchRoot>& roots,
    const InstalledBankSongRequest& request);

}  // namespace seam::native_ui
