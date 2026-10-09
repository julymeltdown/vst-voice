#include "seam/native_ui/installed_singer_song.hpp"

#include "seam/application/project_factory.hpp"
#include "seam/authoring/project_document.hpp"
#include "seam/authoring/project_lifecycle.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/project_json.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

namespace seam::native_ui {
namespace {

using Output = InstalledSingerSongProject;

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

core::Result<InstalledSingerSongProject> createInstalledSingerSongProject(
    const distribution::InstalledProceduralSinger& installed,
    const std::vector<distribution::ProceduralSearchRoot>& roots,
    const InstalledSingerSongRequest& request) {
  if (installed.id.empty() || installed.installDirectory.empty() ||
      installed.renderIdentity.contentHash.empty())
    return core::failure<Output>(core::ErrorCode::InvalidState,
        "Install the published singer before creating a song with it");
  if (request.renderableEngineId.empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "The song editor's procedural engine is required to check the singer can sing");
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
          "Save the song project outside the installed singer folders; signed installations are never modified",
          root.path.string());
  }

  // Resolve the singer the way the editor's picker will: the installation this session made must
  // still be present with the same content, be trusted, and be renderable by the editor's engine.
  auto scanned = distribution::ProceduralCatalogue{}.scanDetailed(roots);
  if (!scanned) return core::Result<Output>{scanned.error()};
  const auto installDirectory = comparablePath(installed.installDirectory);
  const auto match = std::find_if(scanned.value().candidates.begin(), scanned.value().candidates.end(),
      [&](const distribution::ProceduralCandidate& candidate) {
        return candidate.renderIdentity == installed.renderIdentity &&
               comparablePath(candidate.resourceRoot) == installDirectory;
      });
  if (match == scanned.value().candidates.end())
    return core::failure<Output>(core::ErrorCode::NotFound,
        "The installed singer is no longer in the singer folder; install it again before creating a song",
        installed.installDirectory.string());
  if (match->manifest.id != installed.id || match->manifest.version != installed.version ||
      match->contentHash != installed.contentHash)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "The installed singer changed after installation; install the published package again",
        installed.installDirectory.string());
  distribution::ProceduralResolveOptions options;
  options.requireTrustedInstalled = true;
  options.allowDevelopmentFixtures = false;
  options.renderableEngineId = request.renderableEngineId;
  options.renderableEngineRevision = request.renderableEngineRevision;
  const auto resolution = distribution::resolveProceduralSinger(
      match->renderIdentity, std::vector<distribution::ProceduralCandidate>{*match}, options);
  if (!resolution.resolved())
    return core::failure<Output>(core::ErrorCode::Conflict,
        "The song editor cannot sing with this installed singer: " +
            (resolution.diagnostic.empty()
                 ? std::string{distribution::proceduralResolveStatusName(resolution.status)}
                 : resolution.diagnostic));
  const auto& styles = match->manifest.styles;
  if (styles.empty())
    return core::failure<Output>(core::ErrorCode::InvalidState,
        "The installed singer declares no singing style");
  const auto preferred = std::find(styles.begin(), styles.end(), request.preferredStyle);
  const domain::ProceduralRecipeReference singer{
      .resource = match->renderIdentity,
      .path = (match->resourceRoot / match->manifest.recipeEntry).string(),
      .style = preferred != styles.end() ? *preferred : styles.front(),
      .installation = distribution::proceduralInstallationReference(*match)};

  application::ProjectFactory seed{1U};
  auto initial = seed.createProject("Song");
  authoring::ProjectDocument document{std::move(initial),
                                      application::ProjectFactory{seed.nextIdValue()}};
  const auto created = authoring::ProjectLifecycleService{}.createNew(
      document, authoring::NewProjectRequest{.name = projectName,
                                             .tempoBpm = 120.0,
                                             .sampleRate = 48000U,
                                             .outputChannels = 2U,
                                             .initialProceduralSinger = singer});
  if (!created) return core::Result<Output>{created.error()};
  const formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(document.session().project());
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
  // project that would not reopen with this exact singer is removed instead of handed over.
  const auto reread = core::readTextFileLimited(destination, 64ULL * 1024ULL * 1024ULL);
  auto decoded = reread ? codec.decode(reread.value())
                        : core::Result<domain::Project>{reread.error()};
  const bool bound = decoded && decoded.value().vocalTracks().size() == 1U &&
                     decoded.value().vocalTracks().front().proceduralRecipe == singer &&
                     reread.value() == encoded.value();
  if (!bound) {
    std::error_code removeError;
    std::filesystem::remove(destination, removeError);
    return core::failure<Output>(core::ErrorCode::Internal,
        "The written song project did not reopen bound to the installed singer and was removed",
        decoded ? destination.string() : decoded.error().message);
  }
  return Output{.projectPath = destination,
                .projectName = std::move(projectName),
                .singer = singer,
                .projectSha256 = core::sha256Hex(encoded.value())};
}

std::string suggestedSongProjectFileName(std::string_view singerDisplayName) {
  std::string stem;
  stem.reserve(singerDisplayName.size());
  for (const char character : singerDisplayName) {
    const auto byte = static_cast<unsigned char>(character);
    const bool reserved = character == '/' || character == '\\' || character == ':' ||
                          character == '*' || character == '?' || character == '"' ||
                          character == '<' || character == '>' || character == '|';
    stem.push_back(byte < 32U || byte == 127U || reserved ? '-' : character);
  }
  stem = trimmed(std::move(stem));
  while (!stem.empty() && stem.front() == '.') stem.erase(stem.begin());
  constexpr std::size_t kMaximumStemBytes = 96U;
  if (stem.size() > kMaximumStemBytes) {
    std::size_t cut = kMaximumStemBytes;
    while (cut > 0U && (static_cast<unsigned char>(stem[cut]) & 0xC0U) == 0x80U) --cut;
    stem.resize(cut);
  }
  while (!stem.empty() && (stem.back() == '.' || stem.back() == ' ')) stem.pop_back();
  if (stem.empty()) stem = "New";
  return stem + " Song.seam";
}

}  // namespace seam::native_ui
