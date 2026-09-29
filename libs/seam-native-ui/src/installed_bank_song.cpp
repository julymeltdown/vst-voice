#include "seam/native_ui/installed_bank_song.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/authoring/project_document.hpp"
#include "seam/authoring/project_lifecycle.hpp"
#include "seam/authoring/voicebank_session.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/project_json.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

namespace seam::native_ui {
namespace {

using Output = InstalledBankSongProject;

std::filesystem::path comparablePath(const std::filesystem::path& path) {
  std::error_code error;
  auto canonical = std::filesystem::weakly_canonical(path, error);
  return error ? path.lexically_normal() : canonical.lexically_normal();
}

bool contains(const std::filesystem::path& root, const std::filesystem::path& path) {
  const auto relative = path.lexically_relative(root);
  return !relative.empty() && *relative.begin() != "..";
}

std::string trimmed(std::string value) {
  const auto space = [](unsigned char character) {
    return character == ' ' || character == '\t' || character == '\r' || character == '\n';
  };
  while (!value.empty() && space(static_cast<unsigned char>(value.back()))) value.pop_back();
  std::size_t first = 0U;
  while (first < value.size() && space(static_cast<unsigned char>(value[first]))) ++first;
  return value.substr(first);
}

}  // namespace

core::Result<InstalledBankSongProject> createInstalledBankSongProject(
    const std::vector<voicebank::VoicebankSearchRoot>& roots,
    const InstalledBankSongRequest& request) {
  if (request.voicebankId.empty() || request.voicebankVersion.empty() ||
      request.contentHash.size() != 64U || request.installDirectory.empty())
    return core::failure<Output>(core::ErrorCode::InvalidState,
        "Install the signed sample bank before creating a song with it");
  const auto& path = request.projectPath;
  if (path.empty() || !path.is_absolute() || path.filename().empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Choose an absolute location for the new song project");
  if (path.extension() != ".seam")
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "A song project file name must end in .seam");
  auto projectName = trimmed(path.stem().string());
  if (projectName.empty() || projectName.front() == '.')
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Name the song project with a visible file name");
  std::error_code statusError;
  const auto status = std::filesystem::symlink_status(path, statusError);
  if (!statusError && std::filesystem::exists(status))
    return core::failure<Output>(core::ErrorCode::Conflict,
        "A file already exists there. Studio never replaces an existing project; choose a new name",
        path.string());
  const auto parent = comparablePath(path.parent_path());
  if (!std::filesystem::is_directory(parent, statusError))
    return core::failure<Output>(core::ErrorCode::NotFound,
        "The folder for the new song project does not exist", path.parent_path().string());
  const auto destination = parent / path.filename();
  for (const auto& root : roots) {
    if (root.path.empty()) continue;
    const auto protectedRoot = comparablePath(root.path);
    if (destination == protectedRoot || contains(protectedRoot, destination))
      return core::failure<Output>(core::ErrorCode::InvalidArgument,
          "Save the song project outside the installed bank folders; signed installations are never modified",
          root.path.string());
  }

  // Resolve the bank the way the editor's picker will: the installation this session made must still
  // be present with the same content, and must resolve as a trusted installation for its exact ID,
  // version and content hash. A development fixture is never a substitute for the installed bank.
  const voicebank::VoicebankCatalog catalog;
  auto scanned = catalog.scan(roots);
  if (!scanned) return core::Result<Output>{scanned.error()};
  const auto installDirectory = comparablePath(request.installDirectory);
  const auto match = std::find_if(scanned.value().begin(), scanned.value().end(),
      [&](const voicebank::VoicebankCandidate& candidate) {
        return candidate.manifest.id == request.voicebankId &&
               candidate.manifest.version == request.voicebankVersion &&
               candidate.contentHash == request.contentHash &&
               comparablePath(candidate.bankRoot) == installDirectory;
      });
  if (match == scanned.value().end())
    return core::failure<Output>(core::ErrorCode::NotFound,
        "The installed sample bank is no longer in the bank folder; install it again before creating a song",
        request.installDirectory.string());
  const domain::VoicebankReference reference{.id = request.voicebankId,
                                             .version = request.voicebankVersion,
                                             .contentHash = request.contentHash};
  voicebank::VoicebankResolveOptions options;
  options.requireTrustedInstalled = true;
  options.allowDevelopmentFixtures = false;
  const auto resolution = catalog.resolve(reference,
      std::vector<voicebank::VoicebankCandidate>{*match}, options);
  if (!resolution.resolved())
    return core::failure<Output>(core::ErrorCode::Conflict,
        "The song editor cannot sing with this installed bank: " +
            (resolution.diagnostic.empty()
                 ? std::string{voicebank::voicebankResolveStatusName(resolution.status)}
                 : resolution.diagnostic),
        request.installDirectory.string());
  if (match->manifest.styles.size() != 1U)
    return core::failure<Output>(core::ErrorCode::Unsupported,
        "A song hand-off needs a bank that declares exactly one style; choose a style in the editor instead",
        std::to_string(match->manifest.styles.size()));

  // The project is created through the same lifecycle the editor uses, with the installed bank as the
  // initial selection, so the style is the one the editor's own resolution would record.
  authoring::VoicebankSession banks{roots, false};
  auto refreshed = banks.refresh();
  if (!refreshed) return core::Result<Output>{refreshed.error()};
  application::ProjectFactory seed{1U};
  auto initial = seed.createProject("Song");
  authoring::ProjectDocument document{std::move(initial),
                                      application::ProjectFactory{seed.nextIdValue()}};
  const auto created = authoring::ProjectLifecycleService{&banks}.createNew(
      document, authoring::NewProjectRequest{.name = projectName,
                                             .tempoBpm = 120.0,
                                             .sampleRate = 48000U,
                                             .outputChannels = 2U,
                                             .initialVoicebank = *match});
  if (!created) return core::Result<Output>{created.error()};
  const auto& project = document.session().project();
  if (project.vocalTracks().size() != 1U || project.vocalTracks().front().voicebank != reference ||
      project.vocalTracks().front().proceduralRecipe.has_value())
    return core::failure<Output>(core::ErrorCode::Internal,
        "The new song was not bound to the installed sample bank");
  const auto& style = project.vocalTracks().front().styleSelection;
  const auto styleValid = style.validate();
  if (!styleValid || style.styleId.empty() ||
      std::find(match->manifest.styles.begin(), match->manifest.styles.end(), style.styleId) ==
          match->manifest.styles.end())
    return core::failure<Output>(core::ErrorCode::Conflict,
        "The installed bank did not resolve an exact declared style for the new song",
        match->manifest.displayName);

  const formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(project);
  if (!encoded) return core::Result<Output>{encoded.error()};
  const auto written = core::durableAtomicWriteTextNew(destination, encoded.value());
  if (!written) {
    if (written.error().code == core::ErrorCode::Conflict)
      return core::failure<Output>(core::ErrorCode::Conflict,
          "A file appeared at the song project location. Studio never replaces an existing project; choose a new name",
          destination.string());
    return core::Result<Output>{written.error()};
  }

  // The file this call created is read back and decoded before it is offered to the editor, so a
  // project that would not reopen with this exact bank is removed instead of handed over.
  const auto reread = core::readTextFileLimited(destination, 64ULL * 1024ULL * 1024ULL);
  auto decoded = reread ? codec.decode(reread.value())
                        : core::Result<domain::Project>{reread.error()};
  const bool bound = decoded && decoded.value().vocalTracks().size() == 1U &&
                     decoded.value().vocalTracks().front().voicebank == reference &&
                     decoded.value().vocalTracks().front().styleSelection == style &&
                     reread.value() == encoded.value();
  if (!bound) {
    std::error_code removeError;
    std::filesystem::remove(destination, removeError);
    return core::failure<Output>(core::ErrorCode::Internal,
        "The written song project did not reopen bound to the installed sample bank and was removed",
        decoded ? destination.string() : decoded.error().message);
  }
  return Output{.projectPath = destination,
                .projectName = std::move(projectName),
                .voicebank = reference,
                .styleId = style.styleId,
                .projectSha256 = core::sha256Hex(encoded.value())};
}

}  // namespace seam::native_ui
