#include "seam/voicebank_production/repository.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include <algorithm>

namespace seam::voicebank_production {
namespace {
bool hashValid(std::string_view value) {
  return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
core::Result<void> validate(const GenerationImportExpectation& value) {
  if (!value.language.empty() && value.language != "ja" && value.language != "en" && value.language != "ko")
    return core::failure(core::ErrorCode::InvalidArgument, "Generation expectation language is unsupported");
  for (const auto* text : {&value.takeId, &value.promptId, &value.coverageKey, &value.recipeId, &value.recipeVersion, &value.style})
    if (text->empty() || text->size() > 256U || std::any_of(text->begin(), text->end(), [](unsigned char c) { return c < 32U || c == 127U; }))
      return core::failure(core::ErrorCode::InvalidArgument, "Generation expectation text exceeds bounds");
  if (value.supersedesTakeId.size() > 256U || std::any_of(value.supersedesTakeId.begin(), value.supersedesTakeId.end(), [](unsigned char c) { return c < 32U || c == 127U; }) ||
      !hashValid(value.projectStateSha256) || !hashValid(value.recipeHash) || !hashValid(value.renderContentHash) ||
      value.pitchLayer < 0 || value.pitchLayer > 127 || value.sampleRate < 8000U || value.sampleRate > 384000U ||
      value.frameCount <= 0 || value.frameCount > 32LL * 1024LL * 1024LL)
    return core::failure(core::ErrorCode::InvalidArgument, "Generation expectation identity or dimensions are invalid");
  return core::success();
}
}

core::Result<std::string> encodeGenerationImportExpectation(const GenerationImportExpectation& expectation) {
  const auto valid = validate(expectation);
  if (!valid) return core::Result<std::string>{valid.error()};
  using formats::JsonValue;
  JsonValue::Object object{
      {"formatId", JsonValue{"com.project-seam.generation-import-expectation"}}, {"schemaVersion", JsonValue{std::int64_t{expectation.language.empty() ? 1 : 2}}},
      {"projectStateSha256", JsonValue{expectation.projectStateSha256}}, {"takeId", JsonValue{expectation.takeId}},
      {"promptId", JsonValue{expectation.promptId}}, {"coverageKey", JsonValue{expectation.coverageKey}},
      {"supersedesTakeId", JsonValue{expectation.supersedesTakeId}}, {"pitchLayer", JsonValue{static_cast<std::int64_t>(expectation.pitchLayer)}},
      {"recipeId", JsonValue{expectation.recipeId}}, {"recipeVersion", JsonValue{expectation.recipeVersion}},
      {"recipeHash", JsonValue{expectation.recipeHash}}, {"style", JsonValue{expectation.style}},
      {"renderContentHash", JsonValue{expectation.renderContentHash}}, {"sampleRate", JsonValue{static_cast<std::int64_t>(expectation.sampleRate)}},
      {"frameCount", JsonValue{expectation.frameCount}}};
  if (!expectation.language.empty()) object.emplace("language", expectation.language);
  return formats::stringifyJson(JsonValue{std::move(object)});
}

core::Result<std::string> saveGenerationImportExpectation(const std::filesystem::path& path, const GenerationImportExpectation& expectation) {
  const auto encoded = encodeGenerationImportExpectation(expectation);
  if (!encoded) return encoded;
  const auto written = core::durableAtomicWriteTextNew(path, encoded.value());
  if (!written) return core::Result<std::string>{written.error()};
  return core::sha256Hex(encoded.value());
}

core::Result<GenerationImportExpectation> loadGenerationImportExpectation(const std::filesystem::path& path, std::string_view expectedSha256) {
  using Output = GenerationImportExpectation;
  const auto fail = [] { return core::failure<Output>(core::ErrorCode::InvalidArgument, "Generation expectation file is malformed or differs from its retained digest"); };
  if (!hashValid(expectedSha256)) return fail();
  const auto bytes = core::readTextFileLimited(path, 16U * 1024U);
  if (!bytes) return core::Result<Output>{bytes.error()};
  if (core::sha256Hex(bytes.value()) != expectedSha256) return fail();
  const auto json = formats::parseJson(bytes.value(), {.maximumInputBytes = 16U * 1024U, .maximumDepth = 2U,
      .maximumNodes = 32U, .maximumStringBytes = 256U, .maximumCollectionEntries = 16U});
  if (!json || !json.value().isObject()) return fail();
  const auto& root = json.value();
  for (const auto* key : {"formatId", "projectStateSha256", "takeId", "promptId", "coverageKey", "supersedesTakeId",
      "recipeId", "recipeVersion", "recipeHash", "style", "renderContentHash"})
    if (!root.find(key) || !root.find(key)->isString()) return fail();
  for (const auto* key : {"schemaVersion", "pitchLayer", "sampleRate", "frameCount"})
    if (!root.find(key) || !root.find(key)->isInteger()) return fail();
  const auto schema = root.find("schemaVersion")->asInt64();
  if (root.find("formatId")->asString() != "com.project-seam.generation-import-expectation" || (schema != 1 && schema != 2) ||
      root.asObject().size() != (schema == 1 ? 15U : 16U)) return fail();
  if (schema == 2 && (!root.find("language") || !root.find("language")->isString() || root.find("language")->asString().empty())) return fail();
  const auto pitch = root.find("pitchLayer")->asInt64(), rate = root.find("sampleRate")->asInt64();
  if (pitch < 0 || pitch > 127 || rate < 8000 || rate > 384000) return fail();
  const auto text = [&](const char* key) { return root.find(key)->asString(); };
  Output result{text("projectStateSha256"), text("takeId"), text("promptId"), text("coverageKey"), text("supersedesTakeId"),
      static_cast<std::int32_t>(pitch), text("recipeId"), text("recipeVersion"), text("recipeHash"), text("style"), text("renderContentHash"),
      static_cast<std::uint32_t>(rate), root.find("frameCount")->asInt64()};
  if (schema == 2) result.language = text("language");
  const auto valid = validate(result);
  if (!valid) return core::Result<Output>{valid.error()};
  return result;
}
}
