#include "seam/candidate_packaging/candidate_package.hpp"
#include "private_stage.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/content_identity.hpp"
#include "seam/voicebank/manifest_json.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <map>
#include <system_error>
#if !defined(_WIN32)
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

namespace seam::candidate_packaging {
namespace {
namespace production = voicebank_production;
constexpr std::uint64_t kMaximumDescriptorBytes = 32ULL * 1024ULL * 1024ULL;
std::string hex(const std::array<std::byte, 32U>& digest) {
  static constexpr std::string_view kDigits = "0123456789abcdef";
  std::string output;
  for (const auto value : digest) {
    const auto byte = std::to_integer<unsigned>(value);
    output.push_back(kDigits[(byte >> 4U) & 0x0fU]);
    output.push_back(kDigits[byte & 0x0fU]);
  }
  return output;
}

bool isDigest(std::string_view value) {
  return value.size() == 64U && std::all_of(value.begin(), value.end(),
      [](char character) { return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'); });
}

bool absentPath(const std::filesystem::path& path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  return error == std::errc::no_such_file_or_directory || (!error && status.type() == std::filesystem::file_type::not_found);
}

core::Result<void> cancelled(std::stop_token stop) {
  return stop.stop_requested() ? core::failure(core::ErrorCode::Conflict, "Candidate packaging cancelled; nothing was published")
                               : core::success();
}

std::string languageTag(domain::Language language) {
  switch (language) {
    case domain::Language::Japanese: return "ja";
    case domain::Language::English: return "en";
    case domain::Language::Korean: return "ko";
    default: return "und";
  }
}

const production::ResourceCandidateFile* payloadWithRole(const production::ResourceCandidateDescriptor& descriptor,
                                                         std::string_view role) {
  const auto found = std::find_if(descriptor.payload.begin(), descriptor.payload.end(),
      [&](const auto& file) { return file.role == role; });
  return found == descriptor.payload.end() ? nullptr : &*found;
}

const production::ResourceCandidateExternalDependency* dependencyOfKind(
    const production::ResourceCandidateDescriptor& descriptor, std::string_view kind) {
  const auto found = std::find_if(descriptor.externalDependencies.begin(), descriptor.externalDependencies.end(),
      [&](const auto& value) { return value.kind == kind; });
  return found == descriptor.externalDependencies.end() ? nullptr : &*found;
}

// The sample content identity is re-derived from the signed bytes exactly as an installed bank derives it,
// so verification never repeats a descriptor claim it has not checked. The copy is private and discarded.
core::Result<std::string> signedSampleContentHash(const distribution::SignedContainerInfo& container,
                                                  const std::filesystem::path& packagePath,
                                                  const distribution::VerifySeambankOptions& options,
                                                  const voicebank::Manifest& manifest) {
  using Output = std::string;
  std::error_code error;
  const auto base = std::filesystem::temp_directory_path(error);
  if (error) return core::failure<Output>(core::ErrorCode::IoError, "Cannot locate a temporary directory", error.message());
  const auto resolvedBase = std::filesystem::canonical(base, error);
  if (error) return core::failure<Output>(core::ErrorCode::IoError, "Cannot resolve temporary directory", error.message());
  auto owned = detail::PrivateStage::create(resolvedBase);
  if (!owned) return core::Result<Output>{owned.error()};
  const auto& directory = owned.value()->path;
  for (const auto& entry : container.entries) {
    if (entry.path == production::kResourceCandidateDescriptorPath) continue;
    const auto bytes = distribution::readSignedContainerEntry(container, packagePath, entry.path, options.limits.maximumEntryBytes);
    if (!bytes) return core::Result<Output>{bytes.error()};
    // Entry paths were validated as safe relative paths by container verification.
    const auto target = directory / std::filesystem::path{entry.path};
    std::filesystem::create_directories(target.parent_path(), error);
    if (error) return core::failure<Output>(core::ErrorCode::IoError, "Cannot prepare content verification", error.message());
    std::ofstream stream{target, std::ios::binary | std::ios::trunc};
    stream.write(reinterpret_cast<const char*>(bytes.value().data()), static_cast<std::streamsize>(bytes.value().size()));
    stream.close();
    if (!stream) return core::failure<Output>(core::ErrorCode::IoError, "Cannot prepare content verification", entry.path);
  }
  if (!owned.value()->unchanged()) return core::failure<Output>(core::ErrorCode::Conflict, "Content verification directory changed");
  return voicebank::computeVoicebankContentHash(manifest, directory);
}

core::Result<void> checkSampleFamily(const distribution::SignedContainerInfo& container,
                                     const std::filesystem::path& packagePath, const distribution::VerifySeambankOptions& options,
                                     const production::ResourceCandidateDescriptor& descriptor) {
  const auto bank = distribution::verifySeambank(packagePath, options);
  if (!bank) return core::Result<void>{bank.error()};
  if (bank.value().packageDigest != container.packageDigest)
    return core::failure(core::ErrorCode::Conflict, "Package changed while it was being verified", packagePath.string());
  const auto& manifest = bank.value().manifest;
  const std::optional<production::ResourceCandidateCharacter> character = manifest.characterId.empty()
      ? std::nullopt : std::optional{production::ResourceCandidateCharacter{manifest.characterId, manifest.characterVersion}};
  if (manifest.id != descriptor.resourceId || manifest.version != descriptor.resourceVersion ||
      manifest.displayName != descriptor.displayName ||
      descriptor.languages != std::vector<std::string>{languageTag(manifest.language)} ||
      manifest.styles != descriptor.styles || character != descriptor.character)
    return core::failure(core::ErrorCode::Conflict,
        "Signed sample manifest differs from the candidate's declared identity or applicability");
  const auto content = signedSampleContentHash(container, packagePath, options, manifest);
  if (!content) return core::Result<void>{content.error()};
  if (content.value() != descriptor.contentSha256)
    return core::failure(core::ErrorCode::Conflict, "Signed sample content differs from the candidate's declared content identity");
  return core::success();
}

core::Result<void> checkRecipeFamily(const std::filesystem::path& packagePath, const distribution::VerifySeambankOptions& options,
                                     const production::ResourceCandidateDescriptor& descriptor, std::string_view packageDigest) {
  const auto singer = distribution::verifyProceduralPackage(packagePath, options);
  if (!singer) return core::Result<void>{singer.error()};
  if (singer.value().container.packageDigest != packageDigest)
    return core::failure(core::ErrorCode::Conflict, "Recipe package changed during verification");
  const auto& manifest = singer.value().manifest;
  const auto* recipe = payloadWithRole(descriptor, "recipe");
  const auto* engine = dependencyOfKind(descriptor, "render-engine");
  if (!recipe || !engine || manifest.id != descriptor.resourceId || manifest.version != descriptor.resourceVersion ||
      manifest.displayName != descriptor.displayName || descriptor.languages != std::vector<std::string>{manifest.language} ||
      manifest.styles != descriptor.styles || recipe->path != manifest.recipeEntry ||
      manifest.recipeSha256 != descriptor.contentSha256 || engine->id != manifest.engineId ||
      engine->revision != std::to_string(manifest.engineRevision))
    return core::failure(core::ErrorCode::Conflict,
        "Signed procedural manifest differs from the recipe candidate's declared identity or dependencies");
  const auto decoded = distribution::readProceduralRecipe(singer.value());
  return decoded ? core::success() : core::Result<void>{decoded.error()};
}

core::Result<void> checkModelManifest(std::string_view text, const production::ResourceCandidateDescriptor& descriptor) {
  const auto parsed = formats::parseJson(text);
  const auto* modelId = parsed && parsed.value().isObject() ? parsed.value().find("modelId") : nullptr;
  const auto* modelVersion = parsed && parsed.value().isObject() ? parsed.value().find("modelVersion") : nullptr;
  if (!modelId || !modelId->isString() || !modelVersion || !modelVersion->isString() ||
      modelId->asString() != descriptor.resourceId || modelVersion->asString() != descriptor.resourceVersion)
    return core::failure(core::ErrorCode::Conflict, "Model manifest must name the candidate's modelId and modelVersion");
  return core::success();
}
}  // namespace

core::Result<production::PublishedResourceCandidate> publishRecipeCandidate(
    const synthesis::ProceduralSingerResource& recipe, const distribution::PublishProceduralSingerOptions& options,
    const std::filesystem::path& destination, std::stop_token stop) {
  using Output = production::PublishedResourceCandidate;
  if (!destination.is_absolute() || destination.filename().empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Recipe candidate destination must be absolute");
  auto owned = detail::PrivateStage::create(destination.parent_path());
  if (!owned) return core::Result<Output>{owned.error()};
  const auto payload = owned.value()->path / "payload";
  const auto manifest = distribution::writeProceduralSingerSource(recipe, payload, options, stop);
  if (!manifest) return core::Result<Output>{manifest.error()};
  production::DeclaredResourceCandidateRequest request;
  request.kind = production::ResourceCandidateKind::Recipe;
  request.resourceId = manifest.value().id;
  request.resourceVersion = manifest.value().version;
  request.displayName = manifest.value().displayName;
  request.languages = {manifest.value().language};
  request.styles = manifest.value().styles;
  request.externalDependencies = {{"render-engine", manifest.value().engineId, std::to_string(manifest.value().engineRevision)}};
  request.roles = {{"manifest.json", "manifest"}, {manifest.value().recipeEntry, "recipe"}};
  request.contentSha256 = manifest.value().recipeSha256;
  return production::publishDeclaredResourceCandidate(payload, request, destination, stop);
}

core::Result<production::PublishedResourceCandidate> publishModelCandidate(
    const std::filesystem::path& payloadDirectory, const ModelCandidateDeclaration& declaration,
    const std::filesystem::path& destination, std::stop_token stop) {
  using Output = production::PublishedResourceCandidate;
  const auto manifestText = core::readTextFileLimited(payloadDirectory / "manifest.json", 1024U * 1024U);
  if (!manifestText) return core::Result<Output>{manifestText.error()};
  const auto parsed = formats::parseJson(manifestText.value());
  const auto* modelId = parsed && parsed.value().isObject() ? parsed.value().find("modelId") : nullptr;
  const auto* modelVersion = parsed && parsed.value().isObject() ? parsed.value().find("modelVersion") : nullptr;
  if (!modelId || !modelId->isString() || !modelVersion || !modelVersion->isString())
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Model manifest must name modelId and modelVersion");
  production::DeclaredResourceCandidateRequest request;
  request.kind = production::ResourceCandidateKind::Model;
  request.resourceId = modelId->asString();
  request.resourceVersion = modelVersion->asString();
  request.displayName = declaration.displayName;
  request.languages = declaration.languages;
  request.styles = declaration.styles;
  request.externalDependencies = {{"neural-runtime", declaration.runtimeId, declaration.runtimeRevision}};
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator iterator{payloadDirectory, error}, end; !error && iterator != end;
       iterator.increment(error)) {
    if (!iterator->is_regular_file(error) || iterator->is_symlink(error)) continue;
    const auto relative = iterator->path().lexically_relative(payloadDirectory).generic_string();
    request.roles.emplace(relative, relative == "manifest.json" ? "manifest"
        : relative.starts_with("graphs/") ? "model-graph" : "model-data");
  }
  if (error) return core::failure<Output>(core::ErrorCode::IoError, "Cannot enumerate model payload", error.message());
  return production::publishDeclaredResourceCandidate(payloadDirectory, request, destination, stop);
}

core::Result<VerifiedCandidatePackage> verifyResourceCandidatePackage(
    const std::filesystem::path& packagePath, const distribution::VerifySeambankOptions& options) {
  using Output = VerifiedCandidatePackage;
  auto container = distribution::verifySignedContainer(packagePath, options);
  if (!container) return core::Result<Output>{container.error()};
  const auto descriptorBytes = distribution::readSignedContainerEntry(container.value(), packagePath,
      production::kResourceCandidateDescriptorPath, kMaximumDescriptorBytes);
  if (!descriptorBytes)
    return core::failure<Output>(core::ErrorCode::NotFound, "Package carries no resource candidate descriptor",
                                 descriptorBytes.error().message);
  const std::string text(reinterpret_cast<const char*>(descriptorBytes.value().data()), descriptorBytes.value().size());
  auto descriptor = production::decodeResourceCandidateDescriptor(text);
  if (!descriptor) return core::Result<Output>{descriptor.error()};
  const auto& declared = descriptor.value();
  if (!declared.declaresDependencySet())
    return core::failure<Output>(core::ErrorCode::Unsupported,
        "Legacy schema-1/2 candidates declare no dependency set and cannot be verified as typed packages");
  // The signed entries must be exactly the declared dependency set plus the descriptor itself.
  std::map<std::string, const production::ResourceCandidateFile*, std::less<>> expected;
  for (const auto* list : {&declared.payload, &declared.evidence})
    for (const auto& file : *list) expected.emplace(file.path, &file);
  if (container.value().entries.size() != expected.size() + 1U)
    return core::failure<Output>(core::ErrorCode::Conflict, "Package entries differ from the candidate's declared dependency set");
  for (const auto& entry : container.value().entries) {
    if (entry.path == production::kResourceCandidateDescriptorPath) continue;
    const auto found = expected.find(entry.path);
    if (found == expected.end() || found->second->bytes != entry.payloadSize || found->second->sha256 != hex(entry.sha256))
      return core::failure<Output>(core::ErrorCode::Conflict, "Package entry differs from the candidate's declared dependency set",
                                   entry.path);
  }
  core::Result<void> family = core::success();
  switch (declared.kind) {
    case production::ResourceCandidateKind::Sample: family = checkSampleFamily(container.value(), packagePath, options, declared); break;
    case production::ResourceCandidateKind::Recipe: family = checkRecipeFamily(packagePath, options, declared, container.value().packageDigest); break;
    case production::ResourceCandidateKind::Model: {
      const auto manifest = distribution::readSignedContainerEntry(container.value(), packagePath, declared.rootManifest, 1024U * 1024U);
      if (!manifest) return core::Result<Output>{manifest.error()};
      family = checkModelManifest(std::string_view{reinterpret_cast<const char*>(manifest.value().data()), manifest.value().size()},
                                  declared);
      break;
    }
  }
  if (!family) return core::Result<Output>{family.error()};
  return VerifiedCandidatePackage{std::move(container.value()), std::move(descriptor.value()), core::sha256Hex(text)};
}

core::Result<PackagedResourceCandidate> packageResourceCandidate(
    const std::filesystem::path& candidateRoot, const std::filesystem::path& outputPackage,
    const distribution::SigningKeyPair& signingKey, const PackageCandidateOptions& options, std::stop_token stop) {
  using Output = PackagedResourceCandidate;
  auto checked = cancelled(stop);
  if (!checked) return core::Result<Output>{checked.error()};
  const auto verified = production::verifyResourceCandidateDirectory(candidateRoot, stop);
  if (!verified) return core::Result<Output>{verified.error()};
  const auto& descriptor = verified.value().descriptor;
  if (!descriptor.declaresDependencySet())
    return core::failure<Output>(core::ErrorCode::Unsupported,
        "Legacy schema-1/2 candidates declare no dependency set; publish a schema-3 candidate from the same generation");
  if (!isDigest(options.expectedCandidateSha256) || verified.value().candidateSha256 != options.expectedCandidateSha256)
    return core::failure<Output>(core::ErrorCode::Conflict, "Candidate differs from the captured candidate digest");
  if (!outputPackage.is_absolute() || outputPackage != outputPackage.lexically_normal() || outputPackage.filename().empty() ||
      outputPackage.filename().string().front() == '.')
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Package destination must be a normalized absolute path");
  std::error_code error;
  const auto parentStatus = std::filesystem::symlink_status(outputPackage.parent_path(), error);
  if (error || !std::filesystem::is_directory(parentStatus) || std::filesystem::is_symlink(parentStatus))
    return core::failure<Output>(core::ErrorCode::Conflict, "Package destination must be in a real directory");
  const auto canonicalCandidate = std::filesystem::canonical(candidateRoot, error);
  if (error) return core::failure<Output>(core::ErrorCode::IoError, "Cannot resolve candidate root", error.message());
  const auto canonicalParent = std::filesystem::canonical(outputPackage.parent_path(), error);
  if (error) return core::failure<Output>(core::ErrorCode::IoError, "Cannot resolve candidate or package paths", error.message());
  const auto inside = (canonicalParent / outputPackage.filename()).lexically_relative(canonicalCandidate);
  if (!inside.empty() && *inside.begin() != "..")
    return core::failure<Output>(core::ErrorCode::Conflict, "A package cannot be written inside the candidate it signs");
  if (!absentPath(outputPackage))
    return core::failure<Output>(core::ErrorCode::Conflict, "Package destination already exists; packages are never replaced");
  auto owned = detail::PrivateStage::create(canonicalParent);
  if (!owned) return core::Result<Output>{owned.error()};
  const auto temporary = owned.value()->path / "package";
  core::Result<void> packed = core::success();
  switch (descriptor.kind) {
    case production::ResourceCandidateKind::Sample: {
      const auto result = distribution::packSeambank(candidateRoot, temporary, signingKey, {.limits = options.limits});
      if (!result) packed = core::Result<void>{result.error()};
      break;
    }
    case production::ResourceCandidateKind::Recipe: {
      const auto result = distribution::packProceduralPackage(candidateRoot, temporary, signingKey, {.limits = options.limits});
      if (!result) packed = core::Result<void>{result.error()};
      break;
    }
    case production::ResourceCandidateKind::Model: {
      const auto result = distribution::packSignedContainer(candidateRoot, temporary, signingKey,
          {.limits = options.limits, .rootManifest = descriptor.rootManifest});
      if (!result) packed = core::Result<void>{result.error()};
      break;
    }
  }
  if (!packed) return core::Result<Output>{packed.error()};
  const auto package = verifyResourceCandidatePackage(temporary, {.limits = options.limits,
      .trustedPublicKeys = {signingKey.publicKey}, .requireTrustedSigner = true});
  if (!package) return core::Result<Output>{package.error()};
  if (package.value().candidateSha256 != verified.value().candidateSha256)
    return core::failure<Output>(core::ErrorCode::Conflict, "Candidate changed while it was being packaged");
  checked = options.faultInjector ? options.faultInjector(CandidatePackagingStage::BeforePublish) : core::success();
  if (!checked) return core::Result<Output>{checked.error()};
  checked = cancelled(stop);
  if (!checked) return core::Result<Output>{checked.error()};
  const auto currentParent = std::filesystem::canonical(outputPackage.parent_path(), error);
  if (error || currentParent != canonicalParent || !owned.value()->unchanged())
    return core::failure<Output>(core::ErrorCode::Conflict, "Package parent or staging directory changed before publication");
  const auto finalPackage = verifyResourceCandidatePackage(temporary, {.limits = options.limits,
      .trustedPublicKeys = {signingKey.publicKey}, .requireTrustedSigner = true});
  if (!finalPackage || finalPackage.value().container.packageDigest != package.value().container.packageDigest)
    return core::failure<Output>(core::ErrorCode::Conflict, "Staged package changed before publication");
  checked = cancelled(stop);
  if (!checked) return core::Result<Output>{checked.error()};
  checked = owned.value()->publishFile("package", canonicalParent / outputPackage.filename());
  if (!checked) return core::Result<Output>{checked.error()};
  Output output{outputPackage, descriptor.kind, descriptor.resourceId, descriptor.resourceVersion,
      package.value().container.packageDigest, package.value().container.signerKeyId, verified.value().candidateSha256,
      descriptor.manifestSha256, descriptor.contentSha256, package.value().container.entries.size(), true, {}, descriptor.resourceKind};
  checked = options.faultInjector ? options.faultInjector(CandidatePackagingStage::AfterPublishBeforeSync) : core::success();
  if (checked) checked = owned.value()->syncAfterPublication();
  if (!checked) {
    output.durabilityConfirmed = false;
    output.diagnostic = "Package is committed but its directory entry may not be durable yet; verify it before retrying. " +
        checked.error().message;
  }
  return output;
}

core::Result<InstalledResourceCandidate> installResourceCandidatePackage(
    const std::filesystem::path& packagePath, const std::filesystem::path& installRoot,
    const InstallCandidateOptions& options, std::stop_token stop) {
  using Output = InstalledResourceCandidate;
  if (!isDigest(options.expectedPackageDigest))
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Installing a candidate requires the package digest its packaging step reported");
  if (!options.verification.requireTrustedSigner || options.verification.trustedPublicKeys.empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Candidate installation requires an explicit trusted public key");
  auto checked = cancelled(stop);
  if (!checked) return core::Result<Output>{checked.error()};
  const auto verified = verifyResourceCandidatePackage(packagePath, options.verification);
  if (!verified) return core::Result<Output>{verified.error()};
  if (verified.value().container.packageDigest != options.expectedPackageDigest)
    return core::failure<Output>(core::ErrorCode::Conflict, "Candidate package differs from the captured package digest");
  const auto& descriptor = verified.value().descriptor;
  Output output;
  output.kind = descriptor.kind;
  output.resourceKind = descriptor.resourceKind;
  output.resourceId = descriptor.resourceId;
  output.resourceVersion = descriptor.resourceVersion;
  output.candidateSha256 = verified.value().candidateSha256;
  switch (descriptor.kind) {
    case production::ResourceCandidateKind::Sample: {
      distribution::InstallSeambankOptions install;
      install.verification = options.verification;
      install.replaceExisting = options.replaceExisting;
      install.expectedPackageDigest = options.expectedPackageDigest;
      install.expectedContentHash = descriptor.contentSha256;
      install.faultInjector = options.faultInjector;
      const auto installed = distribution::installSeambank(packagePath, installRoot, install, stop);
      if (!installed) return core::Result<Output>{installed.error()};
      output.contentHash = installed.value().contentHash;
      output.packageDigest = installed.value().packageDigest;
      output.signerKeyId = installed.value().signerKeyId;
      output.installDirectory = installed.value().installDirectory;
      output.replacedExisting = installed.value().replacedExisting;
      output.durabilityConfirmed = installed.value().durabilityConfirmed;
      output.diagnostic = installed.value().diagnostic;
      break;
    }
    case production::ResourceCandidateKind::Recipe: {
      distribution::InstallProceduralOptions install;
      install.verification = options.verification;
      install.replaceExisting = options.replaceExisting;
      install.expectedPackageDigest = options.expectedPackageDigest;
      install.faultInjector = options.faultInjector;
      const auto installed = distribution::installProceduralPackage(packagePath, installRoot, install, stop);
      if (!installed) return core::Result<Output>{installed.error()};
      output.contentHash = installed.value().contentHash;
      output.packageDigest = installed.value().packageDigest;
      output.signerKeyId = installed.value().signerKeyId;
      output.installDirectory = installed.value().installDirectory;
      output.replacedExisting = installed.value().replacedExisting;
      output.durabilityConfirmed = installed.value().durabilityConfirmed;
      output.diagnostic = installed.value().diagnostic;
      break;
    }
    case production::ResourceCandidateKind::Model:
      return core::failure<Output>(core::ErrorCode::Unsupported,
          "Model candidates are a packaging contract only; this build installs no model resources");
  }
  // This postcommit read is a disclosure, never a prepublication failure. The atomic
  // installer already pinned the signed package and verified all entries before rename.
  const auto installedDescriptor = core::sha256File(output.installDirectory / production::kResourceCandidateDescriptorPath,
                                                    kMaximumDescriptorBytes);
  if (!installedDescriptor || installedDescriptor.value() != output.candidateSha256) {
    output.descriptorReconfirmed = false;
    output.diagnostic += " Installed candidate descriptor could not be reconfirmed after commit; inspect the installed directory before use.";
  }
  return output;
}

}  // namespace seam::candidate_packaging
