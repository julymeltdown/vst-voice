#include "seam/authoring/generation_campaign.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/core/sha256.hpp"
#include <algorithm>
#include <set>

namespace seam::authoring {
core::Result<std::string> planGenerationCampaign(
    const voicebank_production::VoicebankProductionProject& producer,
    std::span<const std::string> plannedTakeIds,
    const synthesis::ProceduralSingerResource& recipe,
    GenerationCampaignLimits limits, std::stop_token stop) {
  const auto fail = [](std::string message) { return core::failure<std::string>(core::ErrorCode::InvalidArgument, std::move(message)); };
  const auto cancelled = [] { return core::failure<std::string>(core::ErrorCode::Conflict, "Campaign planning cancelled"); };
  if (stop.stop_requested()) return cancelled();
  if (limits.maximumJobs == 0U || limits.maximumJobs > 16384U || plannedTakeIds.empty() || plannedTakeIds.size() > limits.maximumJobs ||
      limits.maximumFrames == 0U || limits.maximumFrames > (1ULL << 32U) || limits.maximumEstimatedBytes == 0U ||
      limits.maximumEstimatedBytes > (1ULL << 40U) || limits.batch.maximumJobs == 0U || limits.batch.maximumJobs > 64U ||
      limits.batch.maximumFrames == 0U || limits.batch.maximumFrames > 32ULL * 1024ULL * 1024ULL)
    return fail("Campaign exceeds supported aggregate or per-batch limits");
  const auto valid = voicebank_production::validateProductionProject(producer);
  if (!valid) return core::Result<std::string>{valid.error()};
  const auto decoded = voice_design::decodeVoiceRecipeResource(recipe, stop);
  if (!decoded) return core::Result<std::string>{decoded.error()};
  std::set<std::string> ids;
  for (const auto& id : plannedTakeIds)
    if (id.empty() || id.size() > 128U || !ids.insert(id).second) return fail("Campaign contains an invalid or duplicate take ID");
  const auto producerJson = voicebank_production::encodeProductionProject(producer);
  const auto recipeBytes = recipe.patch->bytes();
  const std::string recipeJson{reinterpret_cast<const char*>(recipeBytes.data()), recipeBytes.size()};
  using formats::JsonValue;
  JsonValue::Array jobs;
  std::uint64_t frames = 0U, bytes = producerJson.size() + recipeJson.size(), batchFrames = 0U;
  std::size_t batchIndex = 0U, batchJobs = 0U;
  if (bytes > limits.maximumEstimatedBytes) return fail("Campaign frozen inputs exceed disk allowance");
  for (const auto& id : ids) {
    if (stop.stop_requested()) return cancelled();
    const auto score = buildInventoryGenerationScore(producer, id);
    if (!score) return fail("Campaign take " + id + ": " + score.error().message);
    const auto& row = *std::find_if(producer.unitAssignments.begin(), producer.unitAssignments.end(),
        [&](const auto& value) { return value.plannedTakeId == id; });
    const auto snapshot = rendering::RenderSnapshotFactory{}.createProcedural(score.value().project, recipe,
        score.value().trackId, score.value().regionId, 0U, rendering::RenderQuality::Final,
        static_cast<std::uint32_t>(score.value().project.settings().sampleRate), row.style);
    if (!snapshot) return fail("Campaign take " + id + ": " + snapshot.error().message);
    const auto notes = snapshot.value().compiledPerformance->notes();
    if (notes.empty() || notes.back().endFrame <= notes.front().startFrame) return fail("Campaign template has no bounded audio span");
    const auto count = static_cast<std::uint64_t>(notes.back().endFrame - notes.front().startFrame);
    const auto scoreJson = formats::ProjectJsonCodec{}.encode(score.value().project);
    if (!scoreJson) return core::Result<std::string>{scoreJson.error()};
    // Conservative planning allowance: dry/master audio, copied recipe/score,
    // metadata and publication staging. Not a measured filesystem quota.
    const std::uint64_t estimate = count * 32U + scoreJson.value().size() * 2U + recipeJson.size() * 2U + 65536U;
    if (count > limits.batch.maximumFrames || count > limits.maximumFrames - frames || estimate > limits.maximumEstimatedBytes - bytes)
      return fail("Campaign take " + id + " exceeds batch, aggregate frame or estimated disk allowance");
    if (batchJobs == limits.batch.maximumJobs || count > limits.batch.maximumFrames - batchFrames) {
      ++batchIndex; batchJobs = 0U; batchFrames = 0U;
    }
    frames += count; bytes += estimate; batchFrames += count; ++batchJobs;
    jobs.emplace_back(JsonValue::Object{{"takeId", id}, {"style", row.style}, {"coverageKey", row.coverageKey},
        {"pitchLayer", static_cast<std::int64_t>(row.pitchLayer)}, {"batchIndex", static_cast<std::int64_t>(batchIndex)},
        {"frameCount", static_cast<std::int64_t>(count)}, {"templateIdentity", score.value().templateIdentity},
        {"scoreJson", scoreJson.value()}, {"state", "UNPREPARED"}});
  }
  if (stop.stop_requested()) return cancelled();
  auto output = formats::stringifyJson(JsonValue::Object{
      {"formatId", "com.project-seam.generation-campaign"}, {"schemaVersion", std::int64_t{1}},
      {"status", "PLANNED_UNPREPARED"}, {"releaseEligible", false}, {"templateRevision", std::int64_t{1}},
      {"initialProducerSha256", core::sha256Hex(producerJson)}, {"initialProducerJson", producerJson},
      {"recipeSha256", recipe.identity.contentHash}, {"recipeJson", recipeJson},
      {"totalFrames", static_cast<std::int64_t>(frames)}, {"estimatedBytes", static_cast<std::int64_t>(bytes)},
      {"batchCount", static_cast<std::int64_t>(batchIndex + 1U)},
      {"maximumJobs", static_cast<std::int64_t>(limits.maximumJobs)},
      {"maximumFrames", static_cast<std::int64_t>(limits.maximumFrames)},
      {"maximumEstimatedBytes", static_cast<std::int64_t>(limits.maximumEstimatedBytes)},
      {"batchMaximumJobs", static_cast<std::int64_t>(limits.batch.maximumJobs)},
      {"batchMaximumFrames", static_cast<std::int64_t>(limits.batch.maximumFrames)}, {"jobs", std::move(jobs)}}, true);
  if (output.size() > 32U * 1024U * 1024U) return fail("Campaign definition exceeds 32 MiB");
  return output;
}

core::Result<void> verifyGenerationCampaign(std::string_view definition, std::string_view expectedSha256, std::stop_token stop) {
  const auto fail = [] { return core::failure<void>(core::ErrorCode::InvalidArgument, "Campaign definition is invalid or differs from canonical frozen inputs"); };
  if (stop.stop_requested()) return core::failure<void>(core::ErrorCode::Conflict, "Campaign verification cancelled");
  if (definition.size() > 32U * 1024U * 1024U || expectedSha256.size() != 64U || core::sha256Hex(definition) != expectedSha256) return fail();
  const auto parsed = formats::parseJson(definition);
  if (!parsed || !parsed.value().isObject()) return fail();
  const auto& root = parsed.value();
  for (const auto* name : {"initialProducerJson", "recipeJson"})
    if (!root.find(name) || !root.find(name)->isString()) return fail();
  for (const auto* name : {"maximumJobs", "maximumFrames", "maximumEstimatedBytes", "batchMaximumJobs", "batchMaximumFrames"})
    if (!root.find(name) || !root.find(name)->isInteger() || root.find(name)->asInt64() <= 0) return fail();
  if (!root.find("jobs") || !root.find("jobs")->isArray() || root.find("jobs")->asArray().size() > 16384U) return fail();
  std::vector<std::string> ids;
  for (const auto& job : root.find("jobs")->asArray()) {
    if (!job.isObject() || !job.find("takeId") || !job.find("takeId")->isString()) return fail();
    ids.push_back(job.find("takeId")->asString());
  }
  const auto producer = voicebank_production::decodeProductionProject(root.find("initialProducerJson")->asString());
  const auto recipe = voice_design::decodeVoiceRecipe(root.find("recipeJson")->asString());
  if (!producer || !recipe) return fail();
  const auto frozen = voice_design::freezeVoiceRecipeResource(recipe.value());
  if (!frozen) return fail();
  const auto get = [&](const char* key) { return static_cast<std::uint64_t>(root.find(key)->asInt64()); };
  const auto rebuilt = planGenerationCampaign(producer.value(), ids, frozen.value(), {
      .maximumJobs = static_cast<std::size_t>(get("maximumJobs")), .maximumFrames = get("maximumFrames"),
      .maximumEstimatedBytes = get("maximumEstimatedBytes"),
      .batch = {.maximumJobs = static_cast<std::size_t>(get("batchMaximumJobs")), .maximumFrames = get("batchMaximumFrames")}}, stop);
  if (!rebuilt) return core::Result<void>{rebuilt.error()};
  if (rebuilt.value() != definition) return fail();
  return core::success();
}
}
