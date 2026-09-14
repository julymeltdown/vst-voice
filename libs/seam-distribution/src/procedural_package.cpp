#include "seam/distribution/procedural_package.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voice_design/articulation_plan.hpp"
#include "seam/voice_design/voice_recipe.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <set>
#include <system_error>

namespace seam::distribution {
namespace {

using seam::formats::JsonValue;
using Object = JsonValue::Object;
using Array = JsonValue::Array;

constexpr std::string_view kManifestEntry = "manifest.json";

bool safeIdentity(std::string_view value) noexcept {
  if (value.empty() || value.size() > 128U) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return std::isalnum(character) != 0 || character == '.' || character == '-' ||
           character == '_';
  });
}

core::Result<std::vector<std::string>> stringArray(const JsonValue* value,
                                                   std::string_view field,
                                                   std::size_t maximum) {
  if (value == nullptr || !value->isArray() || value->asArray().empty() ||
      value->asArray().size() > maximum) {
    return core::failure<std::vector<std::string>>(core::ErrorCode::ParseError,
                                                   "Procedural manifest array is invalid",
                                                   std::string{field});
  }
  std::set<std::string> unique;
  std::vector<std::string> result;
  for (const auto& entry : value->asArray()) {
    if (!entry.isString() || entry.asString().empty() || entry.asString().size() > 64U)
      return core::failure<std::vector<std::string>>(core::ErrorCode::ParseError,
                                                     "Procedural manifest entry is invalid",
                                                     std::string{field});
    if (!unique.insert(entry.asString()).second)
      return core::failure<std::vector<std::string>>(
          core::ErrorCode::ParseError, "Procedural manifest repeats an entry",
          std::string{field});
    result.push_back(entry.asString());
  }
  return result;
}

core::Result<std::string> requiredString(const JsonValue& root, std::string_view field,
                                         std::size_t maximum) {
  const auto* value = root.find(field);
  if (value == nullptr || !value->isString() || value->asString().empty() ||
      value->asString().size() > maximum) {
    return core::failure<std::string>(core::ErrorCode::ParseError,
                                      "Procedural manifest string field is invalid",
                                      std::string{field});
  }
  return value->asString();
}

}  // namespace

core::Result<void> ProceduralSingerManifest::validate() const {
  if (!safeIdentity(id) || !safeIdentity(version))
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Procedural singer identity is empty or unsafe");
  if (displayName.empty() || displayName.size() > 256U)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Procedural singer display name is invalid");
  if (language.empty() || language.size() > 16U)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Procedural singer language is invalid");
  if (engineId.empty() || engineId.size() > 128U)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Procedural singer engine identity is invalid");
  if (engineRevision == 0U)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Procedural singer engine revision must be nonzero");
  if (styles.empty() || styles.size() > 64U)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Procedural singer must declare at least one style");
  if (std::any_of(styles.begin(), styles.end(), [](const std::string& style) {
        return style.empty() || style.size() > 64U;
      }))
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Procedural singer declares an invalid style");
  if (!isSafeSeambankPath(recipeEntry) ||
      recipeEntry == kManifestEntry)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Procedural recipe entry path is invalid");
  // A sha256 is 64 lowercase hex characters. A short or uppercase digest is a producer mistake,
  // not a value to normalise, because the digest is what binds the signed recipe.
  if (recipeSha256.size() != 64U ||
      !std::all_of(recipeSha256.begin(), recipeSha256.end(), [](unsigned char character) {
        return std::isdigit(character) != 0 || (character >= 'a' && character <= 'f');
      }))
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Procedural recipe digest must be lowercase SHA-256 hex");
  if (phones.empty() || phones.size() > 4096U)
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Procedural singer must declare its phone coverage");
  return core::success();
}

core::Result<std::string> ProceduralSingerManifestJsonCodec::encode(
    const ProceduralSingerManifest& manifest) const {
  const auto valid = manifest.validate();
  if (!valid) return core::Result<std::string>{valid.error()};
  Array styles;
  for (const auto& style : manifest.styles) styles.emplace_back(style);
  Array phones;
  for (const auto& phone : manifest.phones) phones.emplace_back(phone);
  return formats::stringifyJson(JsonValue{Object{
      {"formatId", JsonValue{std::string{ProceduralSingerManifest::kFormatId}}},
      {"schemaVersion", JsonValue{static_cast<std::int64_t>(ProceduralSingerManifest::kSchemaVersion)}},
      {"id", JsonValue{manifest.id}},
      {"version", JsonValue{manifest.version}},
      {"displayName", JsonValue{manifest.displayName}},
      {"language", JsonValue{manifest.language}},
      {"styles", JsonValue{std::move(styles)}},
      {"engineId", JsonValue{manifest.engineId}},
      {"engineRevision", JsonValue{static_cast<std::int64_t>(manifest.engineRevision)}},
      {"recipeEntry", JsonValue{manifest.recipeEntry}},
      {"recipeSha256", JsonValue{manifest.recipeSha256}},
      {"phones", JsonValue{std::move(phones)}},
  }}, true);
}

core::Result<ProceduralSingerManifest> ProceduralSingerManifestJsonCodec::decode(
    std::string_view json) const {
  auto parsed = formats::parseJson(json, formats::JsonParseLimits{
      .maximumInputBytes = 1024U * 1024U,
      .maximumDepth = 16U,
      .maximumNodes = 8192U,
      .maximumStringBytes = 64U * 1024U,
      .maximumCollectionEntries = 4096U,
  });
  if (!parsed) return core::Result<ProceduralSingerManifest>{parsed.error()};
  if (!parsed.value().isObject())
    return core::failure<ProceduralSingerManifest>(core::ErrorCode::ParseError,
                                                   "Procedural manifest root must be an object");
  const auto& root = parsed.value();
  const auto* formatId = root.find("formatId");
  const auto* schema = root.find("schemaVersion");
  if (formatId == nullptr || !formatId->isString() ||
      formatId->asString() != ProceduralSingerManifest::kFormatId) {
    return core::failure<ProceduralSingerManifest>(core::ErrorCode::Unsupported,
                                                   "Unsupported procedural manifest format");
  }
  if (schema == nullptr || !schema->isNumber() ||
      schema->asInt64() != ProceduralSingerManifest::kSchemaVersion) {
    return core::failure<ProceduralSingerManifest>(core::ErrorCode::Unsupported,
                                                   "Unsupported procedural manifest schema");
  }
  ProceduralSingerManifest manifest;
  auto id = requiredString(root, "id", 128U);
  auto version = requiredString(root, "version", 128U);
  auto displayName = requiredString(root, "displayName", 256U);
  auto language = requiredString(root, "language", 16U);
  auto engineId = requiredString(root, "engineId", 128U);
  auto recipeEntry = requiredString(root, "recipeEntry", 1024U);
  auto recipeSha256 = requiredString(root, "recipeSha256", 64U);
  if (!id) return core::Result<ProceduralSingerManifest>{id.error()};
  if (!version) return core::Result<ProceduralSingerManifest>{version.error()};
  if (!displayName) return core::Result<ProceduralSingerManifest>{displayName.error()};
  if (!language) return core::Result<ProceduralSingerManifest>{language.error()};
  if (!engineId) return core::Result<ProceduralSingerManifest>{engineId.error()};
  if (!recipeEntry) return core::Result<ProceduralSingerManifest>{recipeEntry.error()};
  if (!recipeSha256) return core::Result<ProceduralSingerManifest>{recipeSha256.error()};
  const auto* engineRevision = root.find("engineRevision");
  if (engineRevision == nullptr || !engineRevision->isInteger() ||
      engineRevision->asInt64() <= 0 ||
      engineRevision->asInt64() > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
    return core::failure<ProceduralSingerManifest>(core::ErrorCode::ParseError,
                                                   "Procedural engine revision is invalid");
  }
  auto styles = stringArray(root.find("styles"), "styles", 64U);
  auto phones = stringArray(root.find("phones"), "phones", 4096U);
  if (!styles) return core::Result<ProceduralSingerManifest>{styles.error()};
  if (!phones) return core::Result<ProceduralSingerManifest>{phones.error()};
  manifest.id = id.value();
  manifest.version = version.value();
  manifest.displayName = displayName.value();
  manifest.language = language.value();
  manifest.styles = std::move(styles).value();
  manifest.engineId = engineId.value();
  manifest.engineRevision = static_cast<std::uint32_t>(engineRevision->asInt64());
  manifest.recipeEntry = recipeEntry.value();
  manifest.recipeSha256 = recipeSha256.value();
  manifest.phones = std::move(phones).value();
  const auto valid = manifest.validate();
  if (!valid) return core::Result<ProceduralSingerManifest>{valid.error()};
  return manifest;
}

namespace {

// The declared recipe must be exactly the bytes the package carries and a recipe this build can
// decode. A digest that matches undecodable bytes is still refused: it would be a package that
// verifies and then cannot be admitted.
core::Result<void> checkRecipeBytes(const std::vector<std::byte>& bytes,
                                    const ProceduralSingerManifest& manifest) {
  const auto digest = core::sha256Hex(bytes);
  if (digest != manifest.recipeSha256)
    return core::failure(core::ErrorCode::Conflict,
                         "Procedural recipe bytes do not match the manifest digest",
                         manifest.recipeEntry);
  const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  auto recipe = voice_design::decodeVoiceRecipe(text);
  if (!recipe) return core::Result<void>{recipe.error()};
  if (recipe.value().engineId != manifest.engineId)
    return core::failure(core::ErrorCode::Conflict,
                         "Procedural recipe engine does not match the manifest",
                         recipe.value().engineId);
  return core::success();
}

}  // namespace

core::Result<ProceduralPackageInfo> packProceduralPackage(
    const std::filesystem::path& sourceDirectory,
    const std::filesystem::path& outputPackage,
    const SigningKeyPair& signingKey,
    const PackProceduralPackageOptions& options) {
  const auto manifestPath = sourceDirectory / kManifestEntry;
  auto manifestText = core::readTextFileLimited(manifestPath, 1024U * 1024U);
  if (!manifestText) return core::Result<ProceduralPackageInfo>{manifestText.error()};
  ProceduralSingerManifestJsonCodec codec;
  auto manifest = codec.decode(manifestText.value());
  if (!manifest) return core::Result<ProceduralPackageInfo>{manifest.error()};
  const auto recipePath = sourceDirectory / manifest.value().recipeEntry;
  auto recipeBytes = core::readFileBytesLimited(recipePath, 16U * 1024U * 1024U);
  if (!recipeBytes) return core::Result<ProceduralPackageInfo>{recipeBytes.error()};
  const auto recipeChecked = checkRecipeBytes(recipeBytes.value(), manifest.value());
  if (!recipeChecked) return core::Result<ProceduralPackageInfo>{recipeChecked.error()};
  auto packed = packSignedContainer(sourceDirectory, outputPackage, signingKey,
      PackSignedContainerOptions{.limits = options.limits, .rootManifest = std::string{kManifestEntry}});
  if (!packed) return core::Result<ProceduralPackageInfo>{packed.error()};
  return verifyProceduralPackage(outputPackage, VerifySeambankOptions{
      .limits = options.limits,
      .trustedPublicKeys = {signingKey.publicKey},
      .requireTrustedSigner = true});
}

core::Result<ProceduralPackageInfo> verifyProceduralPackage(
    const std::filesystem::path& packagePath,
    const VerifySeambankOptions& options) {
  auto container = verifySignedContainer(packagePath, options);
  if (!container) return core::Result<ProceduralPackageInfo>{container.error()};
  auto manifestBytes = readSignedContainerEntry(container.value(), packagePath, kManifestEntry,
                                                1024U * 1024U);
  if (!manifestBytes) return core::Result<ProceduralPackageInfo>{manifestBytes.error()};
  const std::string manifestText(reinterpret_cast<const char*>(manifestBytes.value().data()),
                                 manifestBytes.value().size());
  ProceduralSingerManifestJsonCodec codec;
  auto manifest = codec.decode(manifestText);
  if (!manifest) return core::Result<ProceduralPackageInfo>{manifest.error()};
  // The signed container proves the bytes; this proves the bytes are the declared recipe.
  const auto recipeEntry = std::find_if(container.value().entries.begin(),
      container.value().entries.end(),
      [&manifest](const auto& entry) { return entry.path == manifest.value().recipeEntry; });
  if (recipeEntry == container.value().entries.end()) {
    return core::failure<ProceduralPackageInfo>(core::ErrorCode::NotFound,
                                                "Procedural package lacks its declared recipe",
                                                manifest.value().recipeEntry);
  }
  return ProceduralPackageInfo{std::move(container.value()), std::move(manifest.value())};
}

core::Result<std::vector<std::byte>> readProceduralRecipe(
    const ProceduralPackageInfo& package) {
  auto bytes = readSignedContainerEntry(package.container, package.container.packagePath,
                                        package.manifest.recipeEntry, 16U * 1024U * 1024U);
  if (!bytes) return core::Result<std::vector<std::byte>>{bytes.error()};
  const auto checked = checkRecipeBytes(bytes.value(), package.manifest);
  if (!checked) return core::Result<std::vector<std::byte>>{checked.error()};
  return bytes;
}

namespace {

std::atomic<std::uint64_t> gProceduralInstallCounter{0U};

bool safeComponent(std::string_view value) noexcept {
  if (value.empty() || value.front() == '.') return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return std::isalnum(character) != 0 || character == '.' || character == '-' ||
           character == '_';
  });
}

// The installed resource identity binds the manifest and the recipe, so a song that resolves an
// installed procedural singer can tell that the resource it used has not changed.
std::string proceduralContentHash(std::string_view manifestBytes,
                                  const std::vector<std::byte>& recipeBytes) {
  core::Sha256 hash;
  hash.update(std::as_bytes(std::span{manifestBytes.data(), manifestBytes.size()}));
  hash.update(recipeBytes);
  return core::sha256Hex(hash.digest());
}

}  // namespace

core::Result<InstalledProceduralSinger> installProceduralPackage(
    const std::filesystem::path& packagePath,
    const std::filesystem::path& installRoot,
    const InstallProceduralOptions& options) {
  if (!options.verification.requireTrustedSigner ||
      options.verification.trustedPublicKeys.empty()) {
    return core::failure<InstalledProceduralSinger>(
        core::ErrorCode::InvalidArgument,
        "Procedural singer installation requires an explicit trusted public key");
  }
  auto package = verifyProceduralPackage(packagePath, options.verification);
  if (!package) return core::Result<InstalledProceduralSinger>{package.error()};
  auto recipeBytes = readProceduralRecipe(package.value());
  if (!recipeBytes) return core::Result<InstalledProceduralSinger>{recipeBytes.error()};
  // The recipe was decoded by admission; this is the manifest-identity binding used by the receipt.
  auto manifestBytes = readSignedContainerEntry(package.value().container, packagePath,
                                                kManifestEntry, 1024U * 1024U);
  if (!manifestBytes) return core::Result<InstalledProceduralSinger>{manifestBytes.error()};
  const auto& manifest = package.value().manifest;
  if (!safeComponent(manifest.id) || !safeComponent(manifest.version)) {
    return core::failure<InstalledProceduralSinger>(
        core::ErrorCode::Unsupported,
        "Procedural singer identity is unsafe for installation");
  }
  std::error_code error;
  if (std::filesystem::exists(installRoot, error)) {
    const auto status = std::filesystem::symlink_status(installRoot, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
      return core::failure<InstalledProceduralSinger>(
          core::ErrorCode::Conflict, "Procedural install root must be a real directory",
          installRoot.string());
    }
  } else {
    std::filesystem::create_directories(installRoot, error);
    if (error) {
      return core::failure<InstalledProceduralSinger>(
          core::ErrorCode::IoError, "Unable to create procedural install root",
          error.message());
    }
  }
  const auto canonicalRoot = std::filesystem::canonical(installRoot, error);
  if (error) {
    return core::failure<InstalledProceduralSinger>(
        core::ErrorCode::IoError, "Unable to canonicalize procedural install root",
        error.message());
  }
  const auto productRoot = canonicalRoot / manifest.id;
  if (std::filesystem::exists(productRoot, error)) {
    const auto status = std::filesystem::symlink_status(productRoot, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
      return core::failure<InstalledProceduralSinger>(
          core::ErrorCode::Conflict, "Procedural product install path is unsafe",
          productRoot.string());
    }
  } else {
    std::filesystem::create_directory(productRoot, error);
    if (error) {
      return core::failure<InstalledProceduralSinger>(
          core::ErrorCode::IoError, "Unable to create procedural product directory",
          error.message());
    }
  }
  const auto target = productRoot / manifest.version;
  if (std::filesystem::exists(target) && !options.replaceExisting) {
    return core::failure<InstalledProceduralSinger>(
        core::ErrorCode::Conflict, "Procedural singer version is already installed",
        target.string());
  }
  const auto token = gProceduralInstallCounter.fetch_add(1U, std::memory_order_relaxed);
  const auto staging = canonicalRoot / (".staging-" + manifest.id + "-" + manifest.version +
                                        "-" + std::to_string(token));
  const auto backup = canonicalRoot / (".backup-" + manifest.id + "-" + manifest.version + "-" +
                                       std::to_string(token));
  std::filesystem::remove_all(staging, error);
  std::filesystem::create_directories(staging, error);
  if (error) {
    return core::failure<InstalledProceduralSinger>(
        core::ErrorCode::IoError, "Unable to create procedural staging directory",
        error.message());
  }
  // Every entry is written from the verified container, so the installed bytes are the signed ones.
  for (const auto& entry : package.value().container.entries) {
    auto bytes = readSignedContainerEntry(package.value().container, packagePath, entry.path,
                                          options.verification.limits.maximumEntryBytes);
    if (!bytes) {
      std::filesystem::remove_all(staging, error);
      return core::Result<InstalledProceduralSinger>{bytes.error()};
    }
    const auto destination = staging / std::filesystem::path{entry.path};
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
      std::filesystem::remove_all(staging, error);
      return core::failure<InstalledProceduralSinger>(
          core::ErrorCode::IoError, "Unable to create installed asset directory", entry.path);
    }
    auto written = core::durableAtomicWrite(destination, bytes.value());
    if (!written) {
      std::filesystem::remove_all(staging, error);
      return core::Result<InstalledProceduralSinger>{written.error()};
    }
  }
  // The installed manifest and recipe must still be the exact signed ones before a receipt exists.
  ProceduralSingerManifestJsonCodec codec;
  auto installedText = core::readTextFileLimited(staging / kManifestEntry, 1024U * 1024U);
  if (!installedText) {
    std::filesystem::remove_all(staging, error);
    return core::Result<InstalledProceduralSinger>{installedText.error()};
  }
  auto installedManifest = codec.decode(installedText.value());
  if (!installedManifest || installedManifest.value() != manifest) {
    std::filesystem::remove_all(staging, error);
    return core::failure<InstalledProceduralSinger>(
        core::ErrorCode::Conflict,
        "Installed procedural manifest differs from the signed manifest");
  }
  const auto contentHash = proceduralContentHash(installedText.value(), recipeBytes.value());
  formats::JsonValue::Object receipt;
  receipt.emplace("schemaVersion", static_cast<std::int64_t>(1));
  receipt.emplace("resourceFamily", std::string{"procedural-singer"});
  receipt.emplace("id", manifest.id);
  receipt.emplace("version", manifest.version);
  receipt.emplace("contentHash", contentHash);
  receipt.emplace("recipeEntry", manifest.recipeEntry);
  receipt.emplace("recipeSha256", manifest.recipeSha256);
  receipt.emplace("engineId", manifest.engineId);
  receipt.emplace("engineRevision", static_cast<std::int64_t>(manifest.engineRevision));
  receipt.emplace("packageDigest", package.value().container.packageDigest);
  receipt.emplace("signerKeyId", package.value().container.signerKeyId);
  receipt.emplace("signatureValid", package.value().container.signatureValid);
  receipt.emplace("signerTrusted", package.value().container.signerTrusted);
  auto receiptText = formats::stringifyJson(formats::JsonValue{std::move(receipt)}, true) + "\n";
  auto receiptWritten = core::durableAtomicWriteText(staging / "install-receipt.json", receiptText);
  if (!receiptWritten) {
    std::filesystem::remove_all(staging, error);
    return core::Result<InstalledProceduralSinger>{receiptWritten.error()};
  }
  auto finalDigest = core::sha256File(packagePath, options.verification.limits.maximumArchiveBytes);
  if (!finalDigest || finalDigest.value() != package.value().container.packageDigest) {
    std::filesystem::remove_all(staging, error);
    return core::failure<InstalledProceduralSinger>(
        core::ErrorCode::Conflict, "Procedural package changed during installation");
  }
  bool movedExisting = false;
  if (std::filesystem::exists(target)) {
    const auto targetStatus = std::filesystem::symlink_status(target, error);
    if (error || std::filesystem::is_symlink(targetStatus) ||
        !std::filesystem::is_directory(targetStatus)) {
      std::filesystem::remove_all(staging, error);
      return core::failure<InstalledProceduralSinger>(
          core::ErrorCode::Conflict, "Existing procedural installation target is unsafe",
          target.string());
    }
    std::filesystem::remove_all(backup, error);
    std::filesystem::rename(target, backup, error);
    if (error) {
      std::filesystem::remove_all(staging, error);
      return core::failure<InstalledProceduralSinger>(
          core::ErrorCode::IoError,
          "Unable to stage the existing procedural singer for replacement", error.message());
    }
    movedExisting = true;
  }
  std::filesystem::rename(staging, target, error);
  if (error) {
    if (movedExisting) {
      std::error_code rollbackError;
      std::filesystem::rename(backup, target, rollbackError);
    }
    std::filesystem::remove_all(staging, error);
    return core::failure<InstalledProceduralSinger>(
        core::ErrorCode::IoError, "Unable to publish the installed procedural singer",
        error.message());
  }
  if (movedExisting) std::filesystem::remove_all(backup, error);
  return InstalledProceduralSinger{
      .id = manifest.id,
      .version = manifest.version,
      .contentHash = contentHash,
      .packageDigest = package.value().container.packageDigest,
      .signerKeyId = package.value().container.signerKeyId,
      .installDirectory = target,
  };
}

}  // namespace seam::distribution

namespace seam::distribution {
namespace {

constexpr std::size_t kMaximumProceduralCandidates = 4096U;

bool isRealRegularFile(const std::filesystem::path& path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  return !error && !std::filesystem::is_symlink(status) &&
         std::filesystem::is_regular_file(status);
}

bool isRealDirectory(const std::filesystem::path& path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  return !error && !std::filesystem::is_symlink(status) &&
         std::filesystem::is_directory(status);
}

struct ProceduralReceipt final {
  bool present{false};
  bool signatureValid{false};
  bool signerTrusted{false};
  std::string id;
  std::string version;
  std::string contentHash;
  std::string packageDigest;
  std::string signerKeyId;
};

ProceduralReceipt loadProceduralReceipt(const std::filesystem::path& resourceRoot) {
  ProceduralReceipt result;
  const auto path = resourceRoot / "install-receipt.json";
  if (!isRealRegularFile(path)) return result;
  auto text = core::readTextFileLimited(path, 1024U * 1024U);
  if (!text) return result;
  auto parsed = formats::parseJson(text.value());
  if (!parsed || !parsed.value().isObject()) return result;
  result.present = true;
  const auto readString = [&parsed](std::string_view key) -> std::string {
    const auto* value = parsed.value().find(key);
    return value != nullptr && value->isString() ? value->asString() : std::string{};
  };
  const auto readBool = [&parsed](std::string_view key) -> bool {
    const auto* value = parsed.value().find(key);
    return value != nullptr && value->isBool() && value->asBool();
  };
  // A receipt that is not this family is not a receipt for this resource.
  if (readString("resourceFamily") != "procedural-singer") return ProceduralReceipt{};
  result.id = readString("id");
  result.version = readString("version");
  result.contentHash = readString("contentHash");
  result.packageDigest = readString("packageDigest");
  result.signerKeyId = readString("signerKeyId");
  result.signatureValid = readBool("signatureValid");
  result.signerTrusted = readBool("signerTrusted");
  return result;
}

}  // namespace

core::Result<std::vector<ProceduralCandidate>> ProceduralCatalogue::scan(
    const std::vector<ProceduralSearchRoot>& roots) const {
  std::vector<ProceduralCandidate> result;
  for (const auto& root : roots) {
    if (!isRealDirectory(root.path)) continue;
    std::error_code error;
    for (std::filesystem::directory_iterator product(root.path, error), end;
         !error && product != end; product.increment(error)) {
      if (!isRealDirectory(product->path())) continue;
      for (std::filesystem::directory_iterator version(product->path(), error), endVersion;
           !error && version != endVersion; version.increment(error)) {
        const auto resourceRoot = version->path();
        const auto name = resourceRoot.filename().string();
        // Staging and backup directories are installation machinery, not installed resources.
        if (name.starts_with(".staging-") || name.starts_with(".backup-")) continue;
        if (!isRealDirectory(resourceRoot)) continue;
        const auto manifestPath = resourceRoot / "manifest.json";
        if (!isRealRegularFile(manifestPath)) continue;
        auto text = core::readTextFileLimited(manifestPath, 1024U * 1024U);
        if (!text) continue;
        ProceduralSingerManifestJsonCodec codec;
        auto manifest = codec.decode(text.value());
        if (!manifest) continue;
        const auto recipePath = resourceRoot / manifest.value().recipeEntry;
        if (!isRealRegularFile(recipePath)) continue;
        auto recipeBytes = core::readFileBytesLimited(recipePath, 16U * 1024U * 1024U);
        if (!recipeBytes) continue;
        // The content hash is recomputed from the installed bytes. A receipt that disagrees with
        // them describes a different resource than the one on disk.
        const auto contentHash = proceduralContentHash(text.value(), recipeBytes.value());
        const auto receipt = loadProceduralReceipt(resourceRoot);
        const auto matches = receipt.present && receipt.id == manifest.value().id &&
                             receipt.version == manifest.value().version &&
                             receipt.contentHash == contentHash;
        ProceduralTrust trust = ProceduralTrust::DevelopmentFixture;
        if (root.kind == ProceduralRootKind::Installed) {
          trust = matches && receipt.signatureValid && receipt.signerTrusted
                      ? ProceduralTrust::TrustedInstalled
                      : ProceduralTrust::UntrustedInstalled;
        }
        result.push_back(ProceduralCandidate{
            .manifest = std::move(manifest).value(),
            .resourceRoot = resourceRoot,
            .contentHash = contentHash,
            .trust = trust,
            .packageDigest = receipt.packageDigest,
            .signerKeyId = receipt.signerKeyId,
        });
        if (result.size() > kMaximumProceduralCandidates) {
          return core::failure<std::vector<ProceduralCandidate>>(
              core::ErrorCode::Unsupported,
              "Procedural catalogue exceeds the supported candidate count");
        }
      }
    }
  }
  const auto trustRank = [](ProceduralTrust trust) noexcept {
    switch (trust) {
      case ProceduralTrust::TrustedInstalled: return 0;
      case ProceduralTrust::DevelopmentFixture: return 1;
      case ProceduralTrust::UntrustedInstalled: return 2;
    }
    return 3;
  };
  std::stable_sort(result.begin(), result.end(), [&](const auto& lhs, const auto& rhs) {
    if (lhs.manifest.id != rhs.manifest.id) return lhs.manifest.id < rhs.manifest.id;
    if (lhs.manifest.version != rhs.manifest.version)
      return lhs.manifest.version < rhs.manifest.version;
    if (lhs.contentHash != rhs.contentHash) return lhs.contentHash < rhs.contentHash;
    const auto lhsRank = trustRank(lhs.trust);
    const auto rhsRank = trustRank(rhs.trust);
    if (lhsRank != rhsRank) return lhsRank < rhsRank;
    return lhs.resourceRoot.generic_string() < rhs.resourceRoot.generic_string();
  });
  result.erase(std::unique(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.manifest.id == rhs.manifest.id &&
           lhs.manifest.version == rhs.manifest.version &&
           lhs.contentHash == rhs.contentHash && lhs.resourceRoot == rhs.resourceRoot;
  }), result.end());
  return result;
}

ProceduralResolution resolveProceduralSinger(
    const domain::SingerResourceIdentity& reference,
    const std::vector<ProceduralCandidate>& candidates,
    const ProceduralResolveOptions& options) {
  ProceduralResolution result;
  if (reference.id.empty() || reference.version.empty() ||
      reference.kind != domain::SingerResourceKind::Procedural) {
    result.status = ProceduralResolveStatus::InvalidReference;
    result.diagnostic = "A procedural selection needs a procedural identity with an id and version";
    return result;
  }
  std::vector<const ProceduralCandidate*> idMatches;
  std::vector<const ProceduralCandidate*> versionMatches;
  for (const auto& candidate : candidates) {
    if (candidate.manifest.id != reference.id) continue;
    idMatches.push_back(&candidate);
    result.availableVersions.push_back(candidate.manifest.version);
    if (candidate.manifest.version == reference.version) versionMatches.push_back(&candidate);
  }
  std::sort(result.availableVersions.begin(), result.availableVersions.end());
  result.availableVersions.erase(std::unique(result.availableVersions.begin(),
                                             result.availableVersions.end()),
                                 result.availableVersions.end());
  if (idMatches.empty()) {
    result.status = ProceduralResolveStatus::Missing;
    result.diagnostic = "Procedural singer is not installed: " + reference.id;
    return result;
  }
  if (versionMatches.empty()) {
    result.status = ProceduralResolveStatus::VersionMismatch;
    result.diagnostic = "Procedural singer version is unavailable: " + reference.id + " " +
                        reference.version;
    return result;
  }
  if (reference.contentHash.empty()) {
    result.status = ProceduralResolveStatus::ContentHashMissing;
    result.diagnostic =
        "Project procedural selection has no content hash; an explicit rebind is required";
    return result;
  }
  std::vector<const ProceduralCandidate*> contentMatches;
  for (const auto* candidate : versionMatches)
    if (candidate->contentHash == reference.contentHash) contentMatches.push_back(candidate);
  if (contentMatches.empty()) {
    result.status = ProceduralResolveStatus::ContentMismatch;
    result.expectedContentHash = reference.contentHash;
    for (const auto* candidate : versionMatches)
      result.actualContentHashes.push_back(candidate->contentHash);
    std::sort(result.actualContentHashes.begin(), result.actualContentHashes.end());
    result.actualContentHashes.erase(
        std::unique(result.actualContentHashes.begin(), result.actualContentHashes.end()),
        result.actualContentHashes.end());
    result.diagnostic = "Procedural singer content does not match the saved project state";
    return result;
  }
  // Compatibility is checked before trust: a resource that cannot be rendered here is not playable
  // whatever its signature says, and reporting it as untrusted would name the wrong reason.
  if (!options.renderableEngineId.empty()) {
    const auto renderable = std::find_if(contentMatches.begin(), contentMatches.end(),
        [&options](const ProceduralCandidate* candidate) {
          return candidate->manifest.engineId == options.renderableEngineId &&
                 candidate->manifest.engineRevision == options.renderableEngineRevision;
        });
    if (renderable == contentMatches.end()) {
      const auto& declared = contentMatches.front()->manifest;
      result.status = ProceduralResolveStatus::IncompatibleEngine;
      result.diagnostic = "Procedural singer needs engine " + declared.engineId + " revision " +
          std::to_string(declared.engineRevision) + ", but this build renders " +
          options.renderableEngineId + " revision " +
          std::to_string(options.renderableEngineRevision);
      return result;
    }
  }
  const auto acceptable = [&options](const ProceduralCandidate* candidate) noexcept {
    if (candidate->trust == ProceduralTrust::TrustedInstalled) return true;
    if (candidate->trust == ProceduralTrust::DevelopmentFixture)
      return options.allowDevelopmentFixtures;
    return !options.requireTrustedInstalled;
  };
  const auto selected = std::find_if(contentMatches.begin(), contentMatches.end(), acceptable);
  if (selected == contentMatches.end()) {
    result.status = ProceduralResolveStatus::Untrusted;
    result.diagnostic =
        "Matching procedural singer content exists, but its trust policy is not accepted";
    return result;
  }
  result.status = ProceduralResolveStatus::Resolved;
  result.candidate = **selected;
  result.diagnostic = std::string{"Resolved "} + (*selected)->manifest.displayName + " (" +
                      std::string{proceduralTrustName((*selected)->trust)} + ")";
  return result;
}

std::vector<ProceduralSearchRoot> defaultProceduralSearchRoots() {
  std::vector<ProceduralSearchRoot> result;
#ifdef _WIN32
  if (const auto* local = std::getenv("LOCALAPPDATA"); local != nullptr) {
    result.push_back({std::filesystem::path{local} / "ProjectSEAM" / "Singers",
                      ProceduralRootKind::Installed});
  }
#elif defined(__APPLE__)
  if (const auto* home = std::getenv("HOME"); home != nullptr) {
    result.push_back({std::filesystem::path{home} / "Library" / "Application Support" /
                          "ProjectSEAM" / "Singers",
                      ProceduralRootKind::Installed});
  }
#else
  if (const auto* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr) {
    result.push_back({std::filesystem::path{xdg} / "project-seam" / "singers",
                      ProceduralRootKind::Installed});
  } else if (const auto* home = std::getenv("HOME"); home != nullptr) {
    result.push_back({std::filesystem::path{home} / ".local" / "share" / "project-seam" /
                          "singers",
                      ProceduralRootKind::Installed});
  }
#endif
  return result;
}

std::string_view proceduralTrustName(ProceduralTrust trust) noexcept {
  switch (trust) {
    case ProceduralTrust::TrustedInstalled: return "trusted-installed";
    case ProceduralTrust::UntrustedInstalled: return "untrusted-installed";
    case ProceduralTrust::DevelopmentFixture: return "development-fixture";
  }
  return "unknown";
}

std::string_view proceduralResolveStatusName(ProceduralResolveStatus status) noexcept {
  switch (status) {
    case ProceduralResolveStatus::Resolved: return "resolved";
    case ProceduralResolveStatus::Missing: return "missing";
    case ProceduralResolveStatus::VersionMismatch: return "version-mismatch";
    case ProceduralResolveStatus::ContentHashMissing: return "content-hash-missing";
    case ProceduralResolveStatus::ContentMismatch: return "content-mismatch";
    case ProceduralResolveStatus::Untrusted: return "untrusted";
    case ProceduralResolveStatus::IncompatibleEngine: return "incompatible-engine";
    case ProceduralResolveStatus::UnsafeEntry: return "unsafe-entry";
    case ProceduralResolveStatus::InvalidReference: return "invalid-reference";
  }
  return "unknown";
}

}  // namespace seam::distribution
