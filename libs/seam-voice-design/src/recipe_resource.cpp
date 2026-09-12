#include "seam/voice_design/recipe_resource.hpp"
#include "seam/core/sha256.hpp"

namespace seam::voice_design {
core::Result<synthesis::ProceduralSingerResource> loadVoiceRecipeResource(
    const std::filesystem::path& path, std::optional<domain::SingerResourceIdentity> expected,
    std::stop_token stopToken) {
  using Output = synthesis::ProceduralSingerResource;
  if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Recipe file loading cancelled");
  const auto text = core::readTextFileLimited(path, 512ULL * 1024ULL);
  if (!text) return core::Result<Output>{text.error()};
  const auto recipe = decodeVoiceRecipe(text.value());
  if (!recipe) return core::Result<Output>{recipe.error()};
  auto resource = freezeVoiceRecipeResource(recipe.value(), stopToken);
  if (!resource) return resource;
  if (expected && resource.value().identity != *expected) return core::failure<Output>(core::ErrorCode::Conflict,
      "Recipe file does not match the requested singer resource identity");
  return resource;
}

core::Result<void> saveVoiceRecipeFile(const std::filesystem::path& path, const VoiceRecipe& recipe,
    const core::AtomicWriteOptions& options, std::stop_token stopToken) {
  const auto text = encodeVoiceRecipe(recipe);
  if (!text) return core::Result<void>{text.error()};
  if (stopToken.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Recipe file saving cancelled");
  return core::durableAtomicWriteText(path, text.value(), options);
}

core::Result<synthesis::ProceduralSingerResource> freezeVoiceRecipeResource(
    const VoiceRecipe& recipe, std::stop_token stopToken) {
  using Output = synthesis::ProceduralSingerResource;
  if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Recipe freezing cancelled");
  const auto encoded = encodeVoiceRecipe(recipe);
  if (!encoded) return core::Result<Output>{encoded.error()};
  const auto& json = encoded.value();
  domain::SingerResourceIdentity identity{domain::SingerResourceKind::Procedural,
      recipe.id, std::to_string(voiceRecipeSchemaVersion(recipe)), core::sha256Hex(json)};
  return synthesis::freezeProceduralResource(std::move(identity),
      std::as_bytes(std::span<const char>{json.data(), json.size()}), stopToken);
}

core::Result<VoiceRecipe> decodeVoiceRecipeResource(
    const synthesis::ProceduralSingerResource& resource, std::stop_token stopToken, bool allowVoicedFrication) {
  if (stopToken.stop_requested()) return core::failure<VoiceRecipe>(core::ErrorCode::Conflict, "Recipe loading cancelled");
  const auto valid = resource.validate();
  if (!valid) return core::Result<VoiceRecipe>{valid.error()};
  if (resource.identity.version != "1" && resource.identity.version != "2" && resource.identity.version != "3" && resource.identity.version != "4" &&
      !(allowVoicedFrication && resource.identity.version=="5")) return core::failure<VoiceRecipe>(
      core::ErrorCode::Unsupported, "Procedural recipe resource version is unsupported");
  const auto bytes = resource.patch->bytes();
  auto decoded = decodeVoiceRecipe(std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
  if (!decoded) return decoded;
  if (decoded.value().id != resource.identity.id || resource.identity.version != std::to_string(voiceRecipeSchemaVersion(decoded.value()))) return core::failure<VoiceRecipe>(
      core::ErrorCode::Conflict, "Procedural resource identity differs from its recipe");
  if (stopToken.stop_requested()) return core::failure<VoiceRecipe>(core::ErrorCode::Conflict, "Recipe loading cancelled");
  return decoded;
}
}
