#include "seam/distribution/procedural_package.hpp"
#include "seam/core/environment.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voice_design/articulation_plan.hpp"
#include "seam/voice_design/voice_recipe.hpp"
#include "seam/voice_design/recipe_resource.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <set>
#include <system_error>

namespace seam::distribution {
namespace {

using seam::formats::JsonValue;
using Object = JsonValue::Object;
using Array = JsonValue::Array;

constexpr std::string_view kManifestEntry = "manifest.json";
// The one recipe filename this family publishes, matching the shipped fixtures and the default the
// manifest codec already carries.
constexpr std::string_view kRecipeEntry = "recipe.json";

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

// The declared digest is over the recipe's *canonical* encoding, which is exactly the identity a
// project stores and the renderer validates. Comparing it to the raw file bytes instead would accept
// a package whose recipe re-encodes to a different identity than the one the manifest promised.
core::Result<void> checkRecipeBytes(const std::vector<std::byte>& bytes,
                                    const ProceduralSingerManifest& manifest) {
  const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  auto recipe = voice_design::decodeVoiceRecipe(text);
  if (!recipe) return core::Result<void>{recipe.error()};
  auto canonical = voice_design::encodeVoiceRecipe(recipe.value());
  if (!canonical) return core::Result<void>{canonical.error()};
  if (core::sha256Hex(canonical.value()) != manifest.recipeSha256)
    return core::failure(core::ErrorCode::Conflict,
                         "Procedural recipe does not match the manifest digest",
                         manifest.recipeEntry);
  if (recipe.value().engineId != manifest.engineId)
    return core::failure(core::ErrorCode::Conflict,
                         "Procedural recipe engine does not match the manifest",
                         recipe.value().engineId);
  return core::success();
}

// The identity the renderer validates, derived exactly as the voice-design layer derives it: the id
// comes from the recipe, the version is its schema version, and the digest is over its canonical
// encoding. Deriving it independently here would be a second definition of the same identity, so
// this decodes, re-encodes and hashes the same way instead of trusting a stored value.
core::Result<domain::SingerResourceIdentity> proceduralRenderIdentity(
    const std::vector<std::byte>& bytes) {
  using Output = domain::SingerResourceIdentity;
  const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  auto recipe = voice_design::decodeVoiceRecipe(text);
  if (!recipe) return core::Result<Output>{recipe.error()};
  auto canonical = voice_design::encodeVoiceRecipe(recipe.value());
  if (!canonical) return core::Result<Output>{canonical.error()};
  return Output{domain::SingerResourceKind::Procedural, recipe.value().id,
                std::to_string(voice_design::voiceRecipeSchemaVersion(recipe.value())),
                core::sha256Hex(canonical.value())};
}

// Every style the recipe declares, across all its articulation families. A singer that offers a
// style the recipe does not carry would render that style by falling back to another one, so the
// offered set is read from the recipe rather than asked for.
std::vector<std::string> recipeStyles(const voice_design::VoiceRecipe& recipe) {
  std::set<std::string> unique;
  const auto observe = [&unique](const std::string& style) {
    if (!style.empty()) unique.insert(style);
  };
  for (const auto& pose : recipe.poses) observe(pose.style);
  for (const auto& pose : recipe.frications) observe(pose.style);
  for (const auto& pose : recipe.plosives) observe(pose.style);
  for (const auto& pose : recipe.affricates) observe(pose.style);
  for (const auto& pose : recipe.voicedAffricates) observe(pose.style);
  for (const auto& pose : recipe.approximants) observe(pose.style);
  for (const auto& pose : recipe.palatalized) observe(pose.style);
  for (const auto& pose : recipe.closures) observe(pose.style);
  for (const auto& pose : recipe.breaths) observe(pose.style);
  return {unique.begin(), unique.end()};
}

// Every phone the recipe declares a renderable model for. This is coverage *by declaration*, the
// same sense the shipped manifest uses: the recipe names the phone, not a measured result. A phone
// the recipe omits is absent here, so a singer cannot advertise one it would refuse to render.
std::vector<std::string> recipePhones(const voice_design::VoiceRecipe& recipe) {
  std::set<std::string> unique;
  const auto observe = [&unique](const std::string& phone) {
    if (!phone.empty()) unique.insert(phone);
  };
  for (const auto& pose : recipe.poses) observe(pose.phone);
  for (const auto& pose : recipe.frications) observe(pose.phone);
  for (const auto& pose : recipe.plosives) observe(pose.phone);
  for (const auto& pose : recipe.affricates) observe(pose.phone);
  for (const auto& pose : recipe.voicedAffricates) observe(pose.phone);
  for (const auto& pose : recipe.approximants) observe(pose.phone);
  for (const auto& pose : recipe.palatalized) observe(pose.phone);
  for (const auto& pose : recipe.closures) observe(pose.phone);
  for (const auto& pose : recipe.breaths) observe(pose.phone);
  return {unique.begin(), unique.end()};
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

core::Result<ProceduralPackageInfo> publishProceduralSingerFromRecipe(
    const synthesis::ProceduralSingerResource& resource,
    const std::filesystem::path& stagingDirectory,
    const std::filesystem::path& outputPackage,
    const SigningKeyPair& signingKey,
    const PublishProceduralSingerOptions& options, std::stop_token stop) {
  using Output = ProceduralPackageInfo;
  if (stop.stop_requested())
    return core::failure<Output>(core::ErrorCode::Conflict, "Procedural publication cancelled");
  if (options.version.empty() || options.version.size() > 128U)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "Procedural publication requires a bounded version");
  if (options.language.empty() || options.language.size() > 16U)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "Procedural publication requires a bounded language tag");
  // The recipe is decoded from the caller's resource rather than trusted as a structure, so the
  // manifest is derived from exactly the bytes that will be signed.
  auto decoded = voice_design::decodeVoiceRecipeResource(resource, stop);
  if (!decoded) return core::Result<Output>{decoded.error()};
  const auto recipe = decoded.value();
  const auto canonical = voice_design::encodeVoiceRecipe(recipe);
  if (!canonical) return core::Result<Output>{canonical.error()};

  const auto declaredStyles = recipeStyles(recipe);
  if (declaredStyles.empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "Procedural recipe declares no styles");
  std::vector<std::string> styles = declaredStyles;
  if (!options.styles.empty()) {
    for (const auto& style : options.styles) {
      if (std::find(declaredStyles.begin(), declaredStyles.end(), style) == declaredStyles.end())
        return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                     "Requested style is not declared by the recipe", style);
    }
    std::set<std::string> requested(options.styles.begin(), options.styles.end());
    if (requested.size() != options.styles.size())
      return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                   "Requested styles repeat an entry");
    styles = options.styles;
  }
  const auto phones = recipePhones(recipe);
  if (phones.empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "Procedural recipe declares no phones");

  ProceduralSingerManifest manifest;
  manifest.id = recipe.id;
  manifest.version = options.version;
  manifest.displayName = options.displayName.empty() ? recipe.id : options.displayName;
  manifest.language = options.language;
  manifest.styles = styles;
  manifest.engineId = recipe.engineId;
  manifest.engineRevision = voice_design::kSourceFilterEngineRevision;
  manifest.recipeEntry = std::string{kRecipeEntry};
  manifest.recipeSha256 = core::sha256Hex(canonical.value());
  manifest.phones = phones;
  const auto valid = manifest.validate();
  if (!valid) return core::Result<Output>{valid.error()};
  // The renderer must accept this engine/revision pair, because a manifest that promises a revision
  // no renderer implements would install a singer that cannot be selected.
  if (manifest.engineId != std::string{voice_design::kSourceFilterEngineId})
    return core::failure<Output>(core::ErrorCode::Unsupported,
                                 "Procedural recipe engine is not the source-filter engine",
                                 manifest.engineId);

  std::error_code error;
  if (std::filesystem::exists(stagingDirectory, error) || error)
    return core::failure<Output>(core::ErrorCode::Conflict,
                                 "Procedural staging directory already exists or is unreadable",
                                 stagingDirectory.string());
  if (!std::filesystem::create_directories(stagingDirectory, error) || error)
    return core::failure<Output>(core::ErrorCode::IoError,
                                 "Unable to create the procedural staging directory",
                                 error.message());
  const auto stagedRecipe = stagingDirectory / std::string{kRecipeEntry};
  const auto recipeWritten = core::durableAtomicWriteTextNew(stagedRecipe, canonical.value());
  if (!recipeWritten) return core::Result<Output>{recipeWritten.error()};
  ProceduralSingerManifestJsonCodec codec;
  const auto manifestText = codec.encode(manifest);
  if (!manifestText) return core::Result<Output>{manifestText.error()};
  const auto manifestWritten =
      core::durableAtomicWriteTextNew(stagingDirectory / std::string{kManifestEntry},
                                      manifestText.value());
  if (!manifestWritten) return core::Result<Output>{manifestWritten.error()};
  if (stop.stop_requested())
    return core::failure<Output>(core::ErrorCode::Conflict, "Procedural publication cancelled");
  return packProceduralPackage(stagingDirectory, outputPackage, signingKey, options.packing);
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
  if (options.expectedPackageDigest &&
      package.value().container.packageDigest != *options.expectedPackageDigest) {
    return core::failure<InstalledProceduralSinger>(
        core::ErrorCode::Conflict,
        "Procedural singer package differs from the caller's captured publication digest");
  }
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
  // The installed recipe is already verified, so this derivation cannot fail for a resource that
  // reached this point; a failure still refuses installation rather than publishing an unrenderable
  // singer.
  const auto renderIdentity = proceduralRenderIdentity(recipeBytes.value());
  if (!renderIdentity) {
    std::filesystem::remove_all(staging, error);
    return core::Result<InstalledProceduralSinger>{renderIdentity.error()};
  }
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
      .renderIdentity = renderIdentity.value(),
      .packageDigest = package.value().container.packageDigest,
      .signerKeyId = package.value().container.signerKeyId,
      .installDirectory = target,
  };
}

}  // namespace seam::distribution

namespace seam::distribution {
namespace {

constexpr std::size_t kMaximumProceduralCandidates = 4096U;
constexpr std::size_t kMaximumProceduralFoldersVisited = 8192U;
constexpr std::size_t kMaximumProceduralCatalogueIssues = 64U;

void recordCatalogueIssue(ProceduralCatalogueScan& scan,
                          const std::filesystem::path& root,
                          const std::filesystem::path& packagePath,
                          std::string detail) {
  if (scan.issues.size() < kMaximumProceduralCatalogueIssues) {
    scan.issues.push_back(ProceduralCatalogueIssue{
        .root = root, .packagePath = packagePath, .detail = std::move(detail)});
  } else if (scan.omittedIssueCount < std::numeric_limits<std::size_t>::max()) {
    ++scan.omittedIssueCount;
  }
}

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
  auto detailed = scanDetailed(roots);
  if (!detailed) return core::Result<std::vector<ProceduralCandidate>>{detailed.error()};
  return std::move(detailed).value().candidates;
}

core::Result<ProceduralCatalogueScan> ProceduralCatalogue::scanDetailed(
    const std::vector<ProceduralSearchRoot>& roots) const {
  ProceduralCatalogueScan result;
  const auto loadCandidate = [](const std::filesystem::path& resourceRoot,
                                ProceduralRootKind rootKind)
      -> core::Result<ProceduralCandidate> {
    const auto manifestPath = resourceRoot / "manifest.json";
    if (!isRealRegularFile(manifestPath))
      return core::failure<ProceduralCandidate>(
          core::ErrorCode::NotFound, "Package has no safe regular manifest.json file.");
    auto text = core::readTextFileLimited(manifestPath, 1024U * 1024U);
    if (!text)
      return core::failure<ProceduralCandidate>(
          text.error().code, "Cannot read manifest.json: " + text.error().message,
          text.error().context);
    ProceduralSingerManifestJsonCodec codec;
    auto manifest = codec.decode(text.value());
    if (!manifest)
      return core::failure<ProceduralCandidate>(
          manifest.error().code, "Invalid manifest.json: " + manifest.error().message,
          manifest.error().context);
    const auto recipePath = resourceRoot / manifest.value().recipeEntry;
    if (!isRealRegularFile(recipePath))
      return core::failure<ProceduralCandidate>(
          core::ErrorCode::NotFound,
          "Package recipe is missing or is not a safe regular file.");
    auto recipeBytes = core::readFileBytesLimited(recipePath, 16U * 1024U * 1024U);
    if (!recipeBytes)
      return core::failure<ProceduralCandidate>(
          recipeBytes.error().code,
          "Cannot read package recipe: " + recipeBytes.error().message,
          recipeBytes.error().context);
    const auto renderIdentity = proceduralRenderIdentity(recipeBytes.value());
    if (!renderIdentity)
      return core::failure<ProceduralCandidate>(
          renderIdentity.error().code,
          "Invalid package recipe: " + renderIdentity.error().message,
          renderIdentity.error().context);
    const auto contentHash = proceduralContentHash(text.value(), recipeBytes.value());
    const auto receipt = loadProceduralReceipt(resourceRoot);
    const auto matches = receipt.present && receipt.id == manifest.value().id &&
                         receipt.version == manifest.value().version &&
                         receipt.contentHash == contentHash;
    auto trust = ProceduralTrust::DevelopmentFixture;
    if (rootKind == ProceduralRootKind::Installed) {
      trust = matches && receipt.signatureValid && receipt.signerTrusted
                  ? ProceduralTrust::TrustedInstalled
                  : ProceduralTrust::UntrustedInstalled;
    }
    return ProceduralCandidate{
        .manifest = std::move(manifest).value(),
        .resourceRoot = resourceRoot,
        .contentHash = contentHash,
        .renderIdentity = renderIdentity.value(),
        .trust = trust,
        .packageDigest = receipt.packageDigest,
        .signerKeyId = receipt.signerKeyId,
    };
  };
  std::size_t visitedPackageFolders = 0U;
  bool scanStoppedAtLimit = false;
  for (const auto& root : roots) {
    if (scanStoppedAtLimit) break;
    std::error_code rootStatusError;
    const auto rootStatus = std::filesystem::symlink_status(root.path, rootStatusError);
    if (rootStatusError) {
      if (rootStatusError != std::errc::no_such_file_or_directory) {
        recordCatalogueIssue(result, root.path, root.path,
                             "Cannot inspect singer catalogue root: " +
                                 rootStatusError.message());
      }
      continue;
    }
    if (rootStatus.type() == std::filesystem::file_type::not_found) continue;
    if (std::filesystem::is_symlink(rootStatus) ||
        !std::filesystem::is_directory(rootStatus)) {
      recordCatalogueIssue(result, root.path, root.path,
                           "Singer catalogue root is not a safe real directory.");
      continue;
    }
    std::error_code productError;
    std::filesystem::directory_iterator product(root.path, productError), endProduct;
    if (productError) {
      recordCatalogueIssue(result, root.path, root.path,
                           "Cannot enumerate installed singer folders: " +
                               productError.message());
      continue;
    }
    while (!productError && product != endProduct) {
      const auto productPath = product->path();
      if (isRealDirectory(productPath)) {
        std::error_code versionError;
        std::filesystem::directory_iterator version(productPath, versionError), endVersion;
        if (versionError) {
          recordCatalogueIssue(result, root.path, productPath,
                               "Cannot enumerate package versions: " +
                                   versionError.message());
        }
        while (!versionError && version != endVersion) {
          const auto versionPath = version->path();
          if (isRealDirectory(versionPath)) {
            if (visitedPackageFolders >= kMaximumProceduralFoldersVisited) {
              recordCatalogueIssue(
                  result, root.path, productPath,
                  "Catalogue scan stopped after the supported 8192 package-folder limit.");
              scanStoppedAtLimit = true;
              result.scanLimitReached = true;
              break;
            }
            ++visitedPackageFolders;
            std::error_code canonicalError;
            const auto resourceRoot =
                std::filesystem::canonical(versionPath, canonicalError);
            if (canonicalError) {
              recordCatalogueIssue(result, root.path, versionPath,
                                   "Cannot resolve package folder: " +
                                       canonicalError.message());
            } else {
              const auto folderName = resourceRoot.filename().string();
              const bool isInstallWorkingDirectory =
                  folderName.starts_with(".staging-") || folderName.starts_with(".backup-");
              if (!isInstallWorkingDirectory) {
                auto candidate = loadCandidate(resourceRoot, root.kind);
                if (!candidate) {
                  auto detail = candidate.error().message;
                  if (!candidate.error().context.empty())
                    detail += " (" + candidate.error().context + ")";
                  recordCatalogueIssue(result, root.path, resourceRoot,
                                       std::move(detail));
                } else {
                  result.candidates.push_back(std::move(candidate).value());
                  if (result.candidates.size() > kMaximumProceduralCandidates) {
                    return core::failure<ProceduralCatalogueScan>(
                        core::ErrorCode::Unsupported,
                        "Procedural catalogue exceeds the supported candidate count");
                  }
                }
              }
            }
          }
          version.increment(versionError);
          if (versionError) {
            recordCatalogueIssue(result, root.path, productPath,
                                 "Cannot continue enumerating package versions: " +
                                     versionError.message());
          }
        }
      }
      if (scanStoppedAtLimit) break;
      product.increment(productError);
      if (productError) {
        recordCatalogueIssue(result, root.path, root.path,
                             "Cannot continue enumerating installed singer folders: " +
                                 productError.message());
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
  std::stable_sort(result.candidates.begin(), result.candidates.end(),
      [&](const auto& lhs, const auto& rhs) {
        if (lhs.manifest.id != rhs.manifest.id) return lhs.manifest.id < rhs.manifest.id;
        if (lhs.manifest.version != rhs.manifest.version)
          return lhs.manifest.version < rhs.manifest.version;
        if (lhs.contentHash != rhs.contentHash) return lhs.contentHash < rhs.contentHash;
        const auto lhsRank = trustRank(lhs.trust);
        const auto rhsRank = trustRank(rhs.trust);
        if (lhsRank != rhsRank) return lhsRank < rhsRank;
        return lhs.resourceRoot.generic_string() < rhs.resourceRoot.generic_string();
      });
  result.candidates.erase(std::unique(result.candidates.begin(), result.candidates.end(),
      [](const auto& lhs, const auto& rhs) {
        return lhs.manifest.id == rhs.manifest.id &&
               lhs.manifest.version == rhs.manifest.version &&
               lhs.contentHash == rhs.contentHash && lhs.resourceRoot == rhs.resourceRoot;
      }), result.candidates.end());
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
    // A project records the renderer's identity, so resolution compares against that identity. The
    // manifest's id and release version are distribution facts and do not appear in a saved song.
    if (candidate.renderIdentity.id != reference.id) continue;
    idMatches.push_back(&candidate);
    result.availableVersions.push_back(candidate.renderIdentity.version);
    if (candidate.renderIdentity.version == reference.version) versionMatches.push_back(&candidate);
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
    if (candidate->renderIdentity.contentHash == reference.contentHash)
      contentMatches.push_back(candidate);
  if (contentMatches.empty()) {
    result.status = ProceduralResolveStatus::ContentMismatch;
    result.expectedContentHash = reference.contentHash;
    for (const auto* candidate : versionMatches)
      result.actualContentHashes.push_back(candidate->renderIdentity.contentHash);
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
                 (options.renderableEngineRevision == 0U ||
                  candidate->manifest.engineRevision == options.renderableEngineRevision);
        });
    if (renderable == contentMatches.end()) {
      const auto& declared = contentMatches.front()->manifest;
      result.status = ProceduralResolveStatus::IncompatibleEngine;
      result.diagnostic = "Procedural singer needs engine " + declared.engineId + " revision " +
          std::to_string(declared.engineRevision) + ", but this build renders " +
          options.renderableEngineId +
          (options.renderableEngineRevision == 0U
               ? std::string{}
               : " revision " + std::to_string(options.renderableEngineRevision));
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
  if (const auto local = core::environmentVariable("LOCALAPPDATA"); local) {
    result.push_back({std::filesystem::path{*local} / "ProjectSEAM" / "Singers",
                      ProceduralRootKind::Installed});
  }
#elif defined(__APPLE__)
  if (const auto home = core::environmentVariable("HOME"); home) {
    result.push_back({std::filesystem::path{*home} / "Library" / "Application Support" /
                          "ProjectSEAM" / "Singers",
                      ProceduralRootKind::Installed});
  }
#else
  if (const auto xdg = core::environmentVariable("XDG_DATA_HOME"); xdg) {
    result.push_back({std::filesystem::path{*xdg} / "project-seam" / "singers",
                      ProceduralRootKind::Installed});
  } else if (const auto home = core::environmentVariable("HOME"); home) {
    result.push_back({std::filesystem::path{*home} / ".local" / "share" / "project-seam" /
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

namespace {

// Whether candidate lies inside root, comparing canonical paths so a symlinked or differently
// spelled destination cannot escape the check. A destination that does not exist yet is resolved
// through its nearest existing ancestor.
bool isInsideRoot(const std::filesystem::path& candidate, const std::filesystem::path& root) {
  std::error_code error;
  const auto resolvedRoot = std::filesystem::weakly_canonical(root, error);
  if (error) return false;
  const auto resolvedCandidate = std::filesystem::weakly_canonical(candidate, error);
  if (error) return false;
  const auto rootText = resolvedRoot.generic_string();
  const auto candidateText = resolvedCandidate.generic_string();
  if (candidateText == rootText) return true;
  if (candidateText.size() <= rootText.size()) return false;
  return candidateText.compare(0, rootText.size(), rootText) == 0 &&
         candidateText[rootText.size()] == '/';
}

}  // namespace

core::Result<std::filesystem::path> copyInstalledSingerToDraft(
    const ProceduralCandidate& candidate,
    const std::filesystem::path& destination,
    const CopyInstalledProceduralOptions& options) {
  using Output = std::filesystem::path;
  const auto identityValid = candidate.renderIdentity.validate();
  if (!identityValid)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "A copied singer needs a valid render identity",
                                 candidate.manifest.id);
  if (destination.empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "A draft destination path is required");
  // The copy must never be written into a protected root, which includes the resource's own
  // installation directory. This is the invariant that keeps signed content immutable, and it is
  // enforced here rather than left to the caller.
  for (const auto& root : options.protectedRoots) {
    if (!root.empty() && isInsideRoot(destination, root))
      return core::failure<Output>(core::ErrorCode::Conflict,
                                   "A creator draft must not be written inside a protected "
                                   "installation root",
                                   root.string());
  }
  if (isInsideRoot(destination, candidate.resourceRoot))
    return core::failure<Output>(core::ErrorCode::Conflict,
                                 "A creator draft must not be written inside the installed singer",
                                 candidate.resourceRoot.string());
  std::error_code error;
  const auto recipePath = candidate.resourceRoot / candidate.manifest.recipeEntry;
  if (!isRealRegularFile(recipePath))
    return core::failure<Output>(core::ErrorCode::NotFound,
                                 "The installed singer's recipe is absent", recipePath.string());
  auto bytes = core::readFileBytesLimited(recipePath, 16U * 1024U * 1024U);
  if (!bytes) return core::Result<Output>{bytes.error()};
  // Read the installed recipe as the resource this project selected, so a file replaced since
  // discovery is refused rather than silently copied under the old identity.
  const auto resource = voice_design::loadVoiceRecipeResource(
      recipePath, std::optional<domain::SingerResourceIdentity>{candidate.renderIdentity});
  if (!resource) return core::Result<Output>{resource.error()};
  const std::string text(reinterpret_cast<const char*>(bytes.value().data()),
                         bytes.value().size());
  auto recipe = voice_design::decodeVoiceRecipe(text);
  if (!recipe) return core::Result<Output>{recipe.error()};
  const auto canonical = voice_design::encodeVoiceRecipe(recipe.value());
  if (!canonical) return core::Result<Output>{canonical.error()};
  const auto parent = destination.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error);
    if (error)
      return core::failure<Output>(core::ErrorCode::IoError,
                                   "Unable to create the draft directory", error.message());
  }
  if (std::filesystem::exists(destination, error) && !options.overwriteExisting)
    return core::failure<Output>(core::ErrorCode::Conflict,
                                 "A draft already exists at the destination",
                                 destination.string());
  // Writing the canonical encoding means the draft is identical in identity to the installed
  // recipe it came from, so editing it produces a new identity rather than an ambiguous one.
  const auto saved = core::durableAtomicWriteText(destination, canonical.value());
  if (!saved) return core::Result<Output>{saved.error()};
  return destination;
}

}  // namespace seam::distribution
