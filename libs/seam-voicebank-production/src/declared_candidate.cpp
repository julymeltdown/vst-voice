#include "seam/voicebank_production/resource_candidate.hpp"
#include "candidate_publication_internal.hpp"

#include "seam/core/exclusive_file_lock.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"

#include <algorithm>
#include <atomic>
#include <set>
#include <system_error>
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace seam::voicebank_production {
namespace {
using namespace candidate_publication_internal;

std::atomic<std::uint64_t> gDeclaredSequence{0U};
constexpr std::uint64_t kMaximumPayloadFileBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumPayloadBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;

struct OwnedStage final {
  std::filesystem::path path;
  DirectoryIdentity parent, stage;
  ~OwnedStage() { cleanupOwnedDirectory(path, parent, stage); }
};

core::Result<std::set<std::string, std::less<>>> payloadFiles(const std::filesystem::path& root) {
  using Output = std::set<std::string, std::less<>>;
  Output files;
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator iterator{root, error}, end; !error && iterator != end;
       iterator.increment(error)) {
    const auto status = iterator->symlink_status(error);
    if (error) break;
    const auto relative = iterator->path().lexically_relative(root).generic_string();
    if (std::filesystem::is_symlink(status) || (!std::filesystem::is_directory(status) && !std::filesystem::is_regular_file(status)))
      return core::failure<Output>(core::ErrorCode::Conflict, "Candidate payload contains a link or special file", relative);
    if (std::filesystem::is_regular_file(status)) files.insert(relative);
    if (files.size() > 65536U) return core::failure<Output>(core::ErrorCode::Unsupported, "Candidate payload has too many files");
  }
  if (error) return core::failure<Output>(core::ErrorCode::IoError, "Cannot enumerate candidate payload", error.message());
  return files;
}
}  // namespace

core::Result<PublishedResourceCandidate> publishDeclaredResourceCandidate(
    const std::filesystem::path& payloadDirectory, const DeclaredResourceCandidateRequest& request,
    const std::filesystem::path& destination, std::stop_token stop) {
  using Output = PublishedResourceCandidate;
  if (request.kind == ResourceCandidateKind::Sample)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Sample candidates are built only from a reviewed producer generation");
  auto checked = cancelled(stop);
  if (!checked) return core::Result<Output>{checked.error()};
  if (!destination.is_absolute() || destination != destination.lexically_normal() || destination.filename().empty() ||
      destination.filename().string().front() == '.')
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Candidate destination must be a normalized absolute path");
  const auto payload = realDirectory(payloadDirectory);
  if (!payload) return core::Result<Output>{payload.error()};
  const auto parent = realDirectory(destination.parent_path());
  if (!parent) return core::Result<Output>{parent.error()};
  const auto finalPath = parent.value() / destination.filename();
  const auto inside = finalPath.lexically_relative(payload.value());
  if (!inside.empty() && *inside.begin() != "..")
    return core::failure<Output>(core::ErrorCode::Conflict, "Candidates must publish outside their payload directory");
  const auto present = payloadFiles(payload.value());
  if (!present) return core::Result<Output>{present.error()};
  if (present.value().size() != request.roles.size() ||
      !std::equal(present.value().begin(), present.value().end(), request.roles.begin(),
                  [](const auto& path, const auto& role) { return path == role.first; }))
    return core::failure<Output>(core::ErrorCode::Conflict, "Declared payload roles must name exactly the files in the payload");
  core::ExclusiveFileLock destinationLock;
  checked = destinationLock.acquire(parent.value() / ".seam-candidate-writer.lock");
  if (!checked) return core::Result<Output>{checked.error()};
  const auto parentIdentity = captureDirectoryIdentity(parent.value());
  if (!parentIdentity) return core::Result<Output>{parentIdentity.error()};
  checked = absentDestination(finalPath);
  if (!checked) return core::Result<Output>{checked.error()};
#if defined(_WIN32)
  const auto processId = std::uint64_t{0U};
#else
  const auto processId = static_cast<std::uint64_t>(::getpid());
#endif
  const auto stage = parent.value() /
      (".seam-candidate-" + std::to_string(processId) + "-declared-" + std::to_string(gDeclaredSequence.fetch_add(1U)));
  std::error_code error;
  if (!std::filesystem::create_directory(stage, error) || error)
    return core::failure<Output>(core::ErrorCode::Conflict, "Cannot create private candidate staging directory", stage.string());
  const auto stageIdentity = captureDirectoryIdentity(stage);
  if (!stageIdentity) return core::Result<Output>{stageIdentity.error()};
  OwnedStage cleanup{stage, parentIdentity.value(), stageIdentity.value()};
  std::vector<ResourceCandidateFile> files;
  std::set<std::filesystem::path> directories;
  std::uint64_t total = 0U;
  for (const auto& [path, role] : request.roles) {
    checked = cancelled(stop);
    if (!checked) return core::Result<Output>{checked.error()};
    if (!isPackageableCandidatePath(path) || path == kResourceCandidateDescriptorPath)
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "Candidate payload path cannot be packaged", path);
    const auto bytes = core::readFileBytesLimited(payload.value() / path,
        std::min(kMaximumPayloadFileBytes, kMaximumPayloadBytes - total));
    if (!bytes) return core::Result<Output>{bytes.error()};
    total += static_cast<std::uint64_t>(bytes.value().size());
    for (auto directory = (stage / path).parent_path(); directory != stage; directory = directory.parent_path())
      directories.insert(directory);
    checked = core::durableAtomicWriteNew(stage / path, bytes.value());
    if (!checked) return core::Result<Output>{checked.error()};
    files.push_back({path, role, core::sha256Hex(bytes.value()), static_cast<std::uint64_t>(bytes.value().size())});
  }
  const auto manifest = std::find_if(files.begin(), files.end(), [&](const auto& file) { return file.path == request.rootManifest; });
  if (manifest == files.end())
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Declared payload lacks its root manifest");
  ResourceCandidateDescriptor descriptor;
  descriptor.kind = request.kind;
  descriptor.status = std::string{kDeclaredCandidateStatus};
  descriptor.resourceId = request.resourceId;
  descriptor.resourceVersion = request.resourceVersion;
  descriptor.displayName = request.displayName;
  descriptor.languages = request.languages;
  descriptor.styles = request.styles;
  descriptor.rootManifest = request.rootManifest;
  descriptor.manifestSha256 = manifest->sha256;
  descriptor.contentSha256 = request.kind == ResourceCandidateKind::Model ? modelCandidateContentSha256(files) : request.contentSha256;
  descriptor.payload = std::move(files);
  descriptor.externalDependencies = request.externalDependencies;
  const auto encoded = encodeResourceCandidateDescriptor(descriptor);
  if (!encoded) return core::Result<Output>{encoded.error()};
  checked = core::durableAtomicWriteTextNew(stage / std::string{kResourceCandidateDescriptorPath}, encoded.value());
  if (!checked) return core::Result<Output>{checked.error()};
  std::vector<std::filesystem::path> ordered{directories.begin(), directories.end()};
  std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) { return left.native().size() > right.native().size(); });
  ordered.push_back(stage);
  for (const auto& directory : ordered) {
    checked = syncDirectory(directory);
    if (!checked) return core::Result<Output>{checked.error()};
  }
  const auto staged = verifyResourceCandidateDirectory(stage, stop);
  if (!staged) return core::Result<Output>{staged.error()};
  if (staged.value().candidateSha256 != core::sha256Hex(encoded.value()))
    return core::failure<Output>(core::ErrorCode::Conflict, "Candidate staged descriptor changed before publication");
  checked = cancelled(stop);
  if (!checked) return core::Result<Output>{checked.error()};
  checked = publishNewDirectory(stage, finalPath, parentIdentity.value(), stageIdentity.value());
  if (!checked) return core::Result<Output>{checked.error()};
  Output output{finalPath, core::sha256Hex(encoded.value()), descriptor.manifestSha256, descriptor.contentSha256, true, {}};
  checked = syncDirectory(parent.value());
  if (!checked) {
    output.durabilityConfirmed = false;
    output.diagnostic = "Candidate is committed but its parent directory entry may not be durable yet; inspect it before retrying. " +
        checked.error().message;
  }
  return output;
}

}  // namespace seam::voicebank_production
