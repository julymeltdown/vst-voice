#include "seam/candidate_packaging/candidate_package.hpp"

#include "seam/core/sha256.hpp"
#include "private_stage.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <map>
#include <set>
#include <string_view>
#if defined(__APPLE__) || defined(__linux__)
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace seam::candidate_packaging {
namespace {
#if defined(__APPLE__) || defined(__linux__)
namespace production = voicebank_production;
constexpr std::string_view kReceipt = "install-receipt.json";
constexpr std::uint64_t kReceiptLimit = 1024U * 1024U;

bool digestText(std::string_view text) {
  return text.size() == 64U && std::all_of(text.begin(), text.end(), [](char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
  });
}
std::string digestHex(const std::array<std::byte, 32>& bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (const auto byte : bytes) {
    const auto value = std::to_integer<unsigned>(byte);
    result += digits[value >> 4U]; result += digits[value & 15U];
  }
  return result;
}
core::Result<void> refuse(std::string message, std::string context = {}) {
  return core::failure(core::ErrorCode::Conflict, std::move(message), std::move(context));
}

struct Handle final {
  int value{-1};
  explicit Handle(int fd) : value(fd) {}
  ~Handle() { if (value >= 0) ::close(value); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
};

bool same(const struct stat& a, const struct stat& b) {
#if defined(__APPLE__)
  const auto am = a.st_mtimespec, bm = b.st_mtimespec, ac = a.st_ctimespec, bc = b.st_ctimespec;
#else
  const auto am = a.st_mtim, bm = b.st_mtim, ac = a.st_ctim, bc = b.st_ctim;
#endif
  return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_mode == b.st_mode && a.st_size == b.st_size &&
      am.tv_sec == bm.tv_sec && am.tv_nsec == bm.tv_nsec && ac.tv_sec == bc.tv_sec && ac.tv_nsec == bc.tv_nsec;
}
// Every container/descriptor read uses one captured private copy. The caller's
// pin applies to the bytes copied from the held descriptor, not a later reopen.
struct PackageSnapshot final {
  std::unique_ptr<detail::PrivateStage> stage;
  std::unique_ptr<Handle> original;
  std::filesystem::path source, path;
  struct stat sourceStamp{}, snapshotStamp{};

  bool unchanged() const {
    struct stat held{}, named{}, copied{};
    return stage->unchanged() && ::fstat(original->value, &held) == 0 &&
        ::lstat(source.c_str(), &named) == 0 && ::lstat(path.c_str(), &copied) == 0 &&
        same(sourceStamp, held) && same(sourceStamp, named) && same(snapshotStamp, copied);
  }
  static core::Result<std::unique_ptr<PackageSnapshot>> capture(
      const std::filesystem::path& source, std::string_view pin,
      std::uint64_t limit, std::stop_token stop) {
    using Output = std::unique_ptr<PackageSnapshot>;
    auto result = std::make_unique<PackageSnapshot>();
    result->source = source;
    struct stat named{};
    if (::lstat(source.c_str(), &named) != 0 || !S_ISREG(named.st_mode))
      return core::failure<Output>(core::ErrorCode::Conflict, "Package must be a regular file without a final symlink");
    result->original = std::make_unique<Handle>(::open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    if (result->original->value < 0 || ::fstat(result->original->value, &result->sourceStamp) != 0 ||
        !same(named, result->sourceStamp) || named.st_size < 0 || static_cast<std::uint64_t>(named.st_size) > limit)
      return core::failure<Output>(core::ErrorCode::Conflict, "Package changed or exceeds the snapshot size bound");
    std::error_code error;
    const auto temporary = std::filesystem::temp_directory_path(error);
    if (error) return core::failure<Output>(core::ErrorCode::IoError, "Cannot locate package snapshot directory");
    const auto parent = std::filesystem::canonical(temporary, error);
    if (error) return core::failure<Output>(core::ErrorCode::IoError, "Cannot resolve package snapshot directory");
    auto stage = detail::PrivateStage::create(parent);
    if (!stage) return core::Result<Output>{stage.error()};
    result->stage = std::move(stage.value());
    result->path = result->stage->path / "captured.seambank";
    Handle output{::open(result->path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600)};
    if (output.value < 0) return core::failure<Output>(core::ErrorCode::IoError, "Cannot create package snapshot");
    core::Sha256 hash;
    std::uint64_t total = 0;
    std::array<std::byte, 65536> buffer{};
    while (true) {
      if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Package capture cancelled");
      const auto count = ::read(result->original->value, buffer.data(), buffer.size());
      if (count < 0 && errno == EINTR) continue;
      if (count < 0) return core::failure<Output>(core::ErrorCode::IoError, "Cannot read package snapshot source");
      if (count == 0) break;
      total += static_cast<std::uint64_t>(count);
      if (total > limit || total > static_cast<std::uint64_t>(named.st_size))
        return core::failure<Output>(core::ErrorCode::Conflict, "Package grew during capture");
      std::size_t written = 0;
      while (written < static_cast<std::size_t>(count)) {
        const auto bytes = ::write(output.value, buffer.data() + written, static_cast<std::size_t>(count) - written);
        if (bytes < 0 && errno == EINTR) continue;
        if (bytes <= 0) return core::failure<Output>(core::ErrorCode::IoError, "Cannot write package snapshot (including insufficient disk space)");
        written += static_cast<std::size_t>(bytes);
      }
      hash.update(std::span{buffer.data(), static_cast<std::size_t>(count)});
    }
    if (total != static_cast<std::uint64_t>(named.st_size) || hash.hexDigest() != pin ||
        ::fstat(output.value, &result->snapshotStamp) != 0 || !result->unchanged())
      return core::failure<Output>(core::ErrorCode::Conflict, "Package capture differs from pinned bytes or changed during capture");
    return result;
  }
};

struct ExpectedFile { std::uint64_t size; std::string sha; };
struct CheckedFile { std::uint64_t size; std::string sha; struct stat stamp; };
struct Tree final {
  std::map<std::string, ExpectedFile, std::less<>> expected;
  std::set<std::string, std::less<>> directories;
  std::map<std::string, CheckedFile, std::less<>> checked;
  std::map<std::string, struct stat, std::less<>> directoryStamps;
  std::string receipt;
  std::stop_token stop;

  core::Result<void> file(int parent, const std::string& name, const std::string& relative, const struct stat& named) {
    const auto expectedFile = expected.find(relative);
    const bool isReceipt = relative == kReceipt;
    if (!isReceipt && expectedFile == expected.end()) return refuse("Installed candidate contains an unlisted file", relative);
    Handle fd{::openat(parent, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
    struct stat before{}, after{}, namedAfter{};
    if (fd.value < 0 || ::fstat(fd.value, &before) != 0 || !S_ISREG(before.st_mode) || !same(named, before))
      return refuse("Installed candidate file changed or cannot be opened safely", relative);
    const auto limit = isReceipt ? kReceiptLimit : expectedFile->second.size;
    if (before.st_size < 0 || static_cast<std::uint64_t>(before.st_size) > limit ||
        (!isReceipt && static_cast<std::uint64_t>(before.st_size) != limit))
      return refuse("Installed candidate file size differs or exceeds its bound", relative);
    core::Sha256 hash;
    std::uint64_t total = 0;
    std::array<std::byte, 65536> buffer{};
    while (true) {
      if (stop.stop_requested()) return refuse("Installed candidate verification cancelled");
      const auto count = ::read(fd.value, buffer.data(), buffer.size());
      if (count < 0 && errno == EINTR) continue;
      if (count < 0) return refuse("Cannot read installed candidate file", relative);
      if (count == 0) break;
      total += static_cast<std::uint64_t>(count);
      if (total > limit) return refuse("Installed candidate file grew while reading", relative);
      hash.update(std::span{buffer.data(), static_cast<std::size_t>(count)});
      if (isReceipt) receipt.append(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(count));
    }
    if (::fstat(fd.value, &after) != 0 || ::fstatat(parent, name.c_str(), &namedAfter, AT_SYMLINK_NOFOLLOW) != 0 ||
        !same(before, after) || !same(before, namedAfter) || total != static_cast<std::uint64_t>(before.st_size))
      return refuse("Installed candidate file changed while reading", relative);
    const auto sha = hash.hexDigest();
    if (!isReceipt && sha != expectedFile->second.sha) return refuse("Installed candidate file differs from signed bytes", relative);
    checked.emplace(relative, CheckedFile{total, sha, before});
    return core::success();
  }

  core::Result<void> walk(int directory, const std::string& prefix, bool recheck) {
    struct stat before{}, after{};
    if (::fstat(directory, &before) != 0) return refuse("Cannot inspect installed candidate directory", prefix);
    if (recheck) {
      const auto original = directoryStamps.find(prefix);
      if (original == directoryStamps.end() || !same(original->second, before))
        return refuse("Installed directory changed after its first inspection", prefix);
    } else directoryStamps.emplace(prefix, before);
    // openat(.) gives enumeration its own offset; dup would share the root's offset
    // and silently skip the final closure pass.
    const int listing = ::openat(directory, ".", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    DIR* raw = listing < 0 ? nullptr : ::fdopendir(listing);
    if (!raw) { if (listing >= 0) ::close(listing); return refuse("Cannot enumerate installed candidate", prefix); }
    struct Listing { DIR* value; ~Listing() { ::closedir(value); } } owned{raw};
    std::size_t visited = 0;
    while (true) {
      errno = 0;
      const auto* entry = ::readdir(raw);
      if (!entry) { if (errno != 0) return refuse("Cannot read installed directory entries", prefix); break; }
      const std::string name{entry->d_name};
      if (name == "." || name == "..") continue;
      if (stop.stop_requested()) return refuse("Installed candidate verification cancelled");
      if (++visited > expected.size() + directories.size() + 1U) return refuse("Installed directory exceeds its entry bound", prefix);
      const auto relative = prefix.empty() ? name : prefix + "/" + name;
      struct stat named{};
      if (::fstatat(directory, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0) return refuse("Cannot inspect installed entry", relative);
      if (S_ISDIR(named.st_mode)) {
        if (!directories.contains(relative)) return refuse("Installed candidate contains an unlisted directory", relative);
        Handle child{::openat(directory, name.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW)};
        struct stat opened{}, namedAfter{};
        if (child.value < 0 || ::fstat(child.value, &opened) != 0 || !same(named, opened))
          return refuse("Installed directory changed while opening", relative);
        const auto result = walk(child.value, relative, recheck);
        if (!result) return result;
        if (::fstatat(directory, name.c_str(), &namedAfter, AT_SYMLINK_NOFOLLOW) != 0 || !same(named, namedAfter))
          return refuse("Installed directory changed during verification", relative);
      } else if (S_ISREG(named.st_mode)) {
        if (recheck) {
          const auto found = checked.find(relative);
          if (found == checked.end() || !same(found->second.stamp, named)) return refuse("Installed file changed after verification", relative);
        } else {
          const auto result = file(directory, name, relative, named);
          if (!result) return result;
        }
      } else return refuse("Installed candidate contains a link or special file", relative);
    }
    if (::fstat(directory, &after) != 0 || !same(before, after)) return refuse("Installed directory changed during enumeration", prefix);
    return core::success();
  }
};

bool stringIs(const formats::JsonValue& value, std::string_view key, std::string_view expected) {
  const auto* field = value.find(key);
  return field && field->isString() && field->asString() == expected;
}
#endif
}  // namespace

core::Result<ModelCandidateInstallProbe> probeModelCandidateInstallation(
    const std::filesystem::path& packagePath, std::string_view expectedPackageDigest,
    std::string_view expectedCandidateSha256, const distribution::VerifySeambankOptions& options,
    std::stop_token stop) {
  using Output = ModelCandidateInstallProbe;
#if defined(__APPLE__) || defined(__linux__)
  if (!digestText(expectedPackageDigest) || !digestText(expectedCandidateSha256) ||
      !options.requireTrustedSigner || options.trustedPublicKeys.empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Model probe requires captured package/candidate digests and explicit trust");
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Model probe cancelled");
  auto captured = PackageSnapshot::capture(packagePath, expectedPackageDigest, options.limits.maximumArchiveBytes, stop);
  if (!captured) return core::Result<Output>{captured.error()};
  const auto& snapshot = *captured.value();
  const auto verified = verifyResourceCandidatePackage(snapshot.path, options);
  if (!verified) return core::Result<Output>{verified.error()};
  const auto& value = verified.value();
  if (value.container.packageDigest != expectedPackageDigest || value.candidateSha256 != expectedCandidateSha256 ||
      value.descriptor.kind != production::ResourceCandidateKind::Model)
    return core::failure<Output>(core::ErrorCode::Conflict, "Model probe requires the captured typed model package");
  const auto destination = snapshot.stage->path / "model-install-probe";
  struct stat state{};
  if (::lstat(destination.c_str(), &state) == 0 || errno != ENOENT)
    return core::failure<Output>(core::ErrorCode::Conflict, "Model probe destination is not absent");
  InstallCandidateOptions install;
  install.verification = options;
  install.expectedPackageDigest = std::string{expectedPackageDigest};
  const auto attempt = installResourceCandidatePackage(snapshot.path, destination, install, stop);
  if (attempt || attempt.error().code != core::ErrorCode::Unsupported || attempt.error().context != kModelInstallUnsupported)
    return core::failure<Output>(core::ErrorCode::Conflict, "Model probe did not observe the required model installation refusal");
  if (::lstat(destination.c_str(), &state) == 0 || errno != ENOENT)
    return core::failure<Output>(core::ErrorCode::Conflict, "Model installation refusal created a destination");
  const auto digest = core::sha256File(snapshot.path, options.limits.maximumArchiveBytes, stop);
  if (!digest || digest.value() != expectedPackageDigest || !snapshot.unchanged())
    return core::failure<Output>(core::ErrorCode::Conflict, "Model package changed during the refusal probe");
  return Output{value.descriptor, value.candidateSha256, value.container.packageDigest,
      value.container.signerKeyId, value.container.entries.size()};
#else
  (void)packagePath; (void)expectedPackageDigest; (void)expectedCandidateSha256; (void)options; (void)stop;
  return core::failure<Output>(core::ErrorCode::Unsupported, "Model candidate probing is not implemented on this platform");
#endif
}

core::Result<VerifiedInstalledCandidate> verifyInstalledResourceCandidate(
    const std::filesystem::path& packagePath, std::string_view expectedPackageDigest,
    std::string_view expectedCandidateSha256, const std::filesystem::path& installedDirectory,
    const distribution::VerifySeambankOptions& options, std::stop_token stop) {
  using Output = VerifiedInstalledCandidate;
#if defined(__APPLE__) || defined(__linux__)
  if (!digestText(expectedPackageDigest) || !digestText(expectedCandidateSha256) ||
      !options.requireTrustedSigner || options.trustedPublicKeys.empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Installed verification requires captured package/candidate digests and explicit trust");
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Installed candidate verification cancelled");
  auto captured = PackageSnapshot::capture(packagePath, expectedPackageDigest, options.limits.maximumArchiveBytes, stop);
  if (!captured) return core::Result<Output>{captured.error()};
  const auto& snapshot = *captured.value();
  const auto package = verifyResourceCandidatePackage(snapshot.path, options);
  if (!package) return core::Result<Output>{package.error()};
  const auto& value = package.value();
  const auto& descriptor = value.descriptor;
  if (value.container.packageDigest != expectedPackageDigest || value.candidateSha256 != expectedCandidateSha256)
    return core::failure<Output>(core::ErrorCode::Conflict, "Installed verification package or candidate differs from captured identity");
  if (descriptor.kind == production::ResourceCandidateKind::Model)
    return core::failure<Output>(core::ErrorCode::Unsupported, "Model candidates have no installed-resource verifier; this build installs no models");
  if (!installedDirectory.is_absolute() || installedDirectory.filename().empty() || installedDirectory != installedDirectory.lexically_normal())
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Installed directory must be normalized and absolute");
  struct stat named{}, opened{}, finalNamed{};
  if (::lstat(installedDirectory.c_str(), &named) != 0 || !S_ISDIR(named.st_mode))
    return core::failure<Output>(core::ErrorCode::Conflict, "Installed directory must be a real directory");
  Handle root{::open(installedDirectory.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW)};
  if (root.value < 0 || ::fstat(root.value, &opened) != 0 || !same(named, opened))
    return core::failure<Output>(core::ErrorCode::Conflict, "Installed directory changed while opening");
  Tree tree;
  tree.stop = stop;
  for (const auto& entry : value.container.entries) {
    if (entry.path == kReceipt) return core::failure<Output>(core::ErrorCode::Conflict, "Signed candidate cannot supply an install receipt");
    tree.expected.emplace(entry.path, ExpectedFile{entry.payloadSize, digestHex(entry.sha256)});
    for (auto parent = std::filesystem::path{entry.path}.parent_path(); !parent.empty(); parent = parent.parent_path())
      tree.directories.emplace(parent.generic_string());
  }
  auto checked = tree.walk(root.value, {}, false);
  if (!checked) return core::Result<Output>{checked.error()};
  if (tree.checked.size() != tree.expected.size() + 1U || !tree.checked.contains(std::string{kReceipt}))
    return core::failure<Output>(core::ErrorCode::Conflict, "Installed candidate has missing signed files or receipt");
  const auto receipt = formats::parseJson(tree.receipt);
  if (!receipt || !receipt.value().isObject())
    return core::failure<Output>(core::ErrorCode::ParseError, "Installed receipt is not a JSON object");
  const auto& record = receipt.value();
  std::string contentHash = descriptor.contentSha256;
  const bool recipe = descriptor.kind == production::ResourceCandidateKind::Recipe;
  if (recipe) {
    const auto manifest = distribution::verifyProceduralPackage(snapshot.path, options);
    if (!manifest || manifest.value().container.packageDigest != expectedPackageDigest)
      return core::failure<Output>(core::ErrorCode::Conflict, "Recipe package changed during installed verification");
    const auto manifestBytes = distribution::readSignedContainerEntry(value.container, snapshot.path, "manifest.json", 32U * 1024U * 1024U);
    const auto recipeBytes = distribution::readSignedContainerEntry(value.container, snapshot.path, manifest.value().manifest.recipeEntry, 16U * 1024U * 1024U);
    if (!manifestBytes || !recipeBytes) return core::failure<Output>(core::ErrorCode::Conflict, "Cannot read signed recipe identity");
    contentHash = distribution::proceduralInstalledContentHash(
        std::string_view{reinterpret_cast<const char*>(manifestBytes.value().data()), manifestBytes.value().size()}, recipeBytes.value());
    const auto& declared = manifest.value().manifest;
    const auto* revision = record.find("engineRevision");
    if (!stringIs(record, "resourceFamily", "procedural-singer") || !stringIs(record, "recipeEntry", declared.recipeEntry) ||
        !stringIs(record, "recipeSha256", declared.recipeSha256) || !stringIs(record, "engineId", declared.engineId) ||
        !revision || !revision->isInteger() || revision->asInt64() != declared.engineRevision)
      return core::failure<Output>(core::ErrorCode::Conflict, "Installed recipe receipt differs from the signed recipe/runtime identity");
  }
  const std::set<std::string, std::less<>> fields = recipe
      ? std::set<std::string, std::less<>>{"schemaVersion", "resourceFamily", "id", "version", "contentHash", "recipeEntry", "recipeSha256", "engineId", "engineRevision", "packageDigest", "signerKeyId", "signatureValid", "signerTrusted"}
      : std::set<std::string, std::less<>>{"schemaVersion", "voicebankId", "voicebankVersion", "contentHash", "packageDigest", "signerKeyId", "signatureValid", "signerTrusted"};
  if (record.asObject().size() != fields.size() || std::any_of(record.asObject().begin(), record.asObject().end(),
      [&](const auto& field) { return !fields.contains(field.first); }))
    return core::failure<Output>(core::ErrorCode::Conflict, "Installed receipt fields differ from its family schema");
  const auto* schema = record.find("schemaVersion");
  const auto* signature = record.find("signatureValid");
  const auto* trusted = record.find("signerTrusted");
  // These flags must agree with the independently verified package; they never
  // establish trust themselves. Missing or false flags also cannot enter a trusted catalogue.
  if (!schema || !schema->isInteger() || schema->asInt64() != (recipe ? 1 : 2) ||
      !stringIs(record, recipe ? "id" : "voicebankId", descriptor.resourceId) ||
      !stringIs(record, recipe ? "version" : "voicebankVersion", descriptor.resourceVersion) ||
      !stringIs(record, "contentHash", contentHash) || !stringIs(record, "packageDigest", expectedPackageDigest) ||
      !stringIs(record, "signerKeyId", value.container.signerKeyId) || !signature || !signature->isBool() || !signature->asBool() ||
      !trusted || !trusted->isBool() || !trusted->asBool())
    return core::failure<Output>(core::ErrorCode::Conflict, "Installed receipt differs from the verified package and content identity");
  const auto finalPackageDigest = core::sha256File(snapshot.path, options.limits.maximumArchiveBytes, stop);
  if (!finalPackageDigest || finalPackageDigest.value() != expectedPackageDigest || !snapshot.unchanged())
    return core::failure<Output>(core::ErrorCode::Conflict, "Package changed during installed verification");
  checked = tree.walk(root.value, {}, true);
  if (!checked) return core::Result<Output>{checked.error()};
  if (::lstat(installedDirectory.c_str(), &finalNamed) != 0 || !same(opened, finalNamed))
    return core::failure<Output>(core::ErrorCode::Conflict, "Installed root changed during verification");
  core::Sha256 treeHash;
  treeHash.update("seam.installed-candidate-tree.v1\n");
  for (const auto& [path, file] : tree.checked) {
    treeHash.update(path); treeHash.update(std::string_view{"\0", 1});
    treeHash.update(std::to_string(file.size)); treeHash.update(std::string_view{"\0", 1});
    treeHash.update(file.sha); treeHash.update("\n");
  }
  return Output{descriptor.kind, descriptor.resourceKind, descriptor.resourceId, descriptor.resourceVersion,
      value.candidateSha256, value.container.packageDigest, descriptor.contentSha256, contentHash,
      value.container.signerKeyId, tree.checked.at(std::string{kReceipt}).sha, treeHash.hexDigest(), tree.checked.size()};
#else
  (void)packagePath; (void)expectedPackageDigest; (void)expectedCandidateSha256; (void)installedDirectory; (void)options; (void)stop;
  return core::failure<Output>(core::ErrorCode::Unsupported, "Installed candidate verification is not implemented on this platform");
#endif
}
}  // namespace seam::candidate_packaging
