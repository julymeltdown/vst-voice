#include "seam/authoring/inventory_coverage.hpp"
#include "seam/authoring/inventory_generation.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/rendering/render_snapshot.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace seam::authoring {
core::Result<InventoryCoverageReport> inspectInventoryCoverage(
    const voicebank_production::VoicebankProductionProject& producer,
    const synthesis::ProceduralSingerResource& recipe, InventoryCoverageLimits limits,
    std::stop_token stop) {
  using Output = InventoryCoverageReport;
  const auto fail = [](std::string message) {
    return core::failure<Output>(core::ErrorCode::InvalidArgument, std::move(message));
  };
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Coverage inspection cancelled");
  if (limits.maximumAssignments == 0U || limits.maximumAssignments > 16384U)
    return fail("Coverage inspection exceeds its supported assignment bound");
  const auto valid = voicebank_production::validateProductionProject(producer);
  if (!valid) return core::Result<Output>{valid.error()};
  const auto decoded = voice_design::decodeVoiceRecipeResource(recipe, stop);
  if (!decoded) return core::Result<Output>{decoded.error()};
  if (producer.unitAssignments.size() > limits.maximumAssignments)
    return fail("Coverage inspection would exceed its assignment bound");
  const auto producerJson = voicebank_production::encodeProductionProject(producer);
  InventoryCoverageReport report;
  report.assignments = producer.unitAssignments.size();
  // Required items are declared, not inferred from what happens to prepare: a phone
  // listed by any assignment stays required even when every assignment naming it fails.
  std::set<std::string> requiredPhones, requiredKinds, coveredPhones, coveredKinds;
  for (const auto& row : producer.unitAssignments) {
    const auto separator = row.coverageKey.find(':');
    if (separator == std::string::npos || separator == 0U) return fail("Inventory coverage key is not canonical");
    requiredKinds.insert(row.coverageKey.substr(0U, separator));
    auto rest = row.coverageKey.substr(separator + 1U);
    while (!rest.empty()) {
      const auto next = rest.find(':');
      const auto phone = rest.substr(0U, next);
      if (phone.empty()) return fail("Inventory coverage key is not canonical");
      requiredPhones.insert(phone);
      if (next == std::string::npos) break;
      rest = rest.substr(next + 1U);
    }
  }
  report.phones = requiredPhones.size();
  report.kinds = requiredKinds.size();
  for (const auto& row : producer.unitAssignments) {
    if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Coverage inspection cancelled");
    InventoryCoverageEntry entry;
    entry.takeId = row.plannedTakeId;
    entry.coverageKey = row.coverageKey;
    entry.style = row.style;
    entry.pitchLayer = row.pitchLayer;
    std::optional<std::string> refusal;
    const auto score = buildInventoryGenerationScore(producer, row.plannedTakeId);
    if (!score) refusal = score.error().message;
    std::vector<std::string> phones;
    if (!refusal) {
      const auto separator = row.coverageKey.find(':');
      auto rest = row.coverageKey.substr(separator + 1U);
      while (!rest.empty()) {
        const auto next = rest.find(':');
        phones.push_back(rest.substr(0U, next));
        if (next == std::string::npos) break;
        rest = rest.substr(next + 1U);
      }
      const auto snapshot = rendering::RenderSnapshotFactory{}.createProcedural(score.value().project, recipe,
          score.value().trackId, score.value().regionId, 0U, rendering::RenderQuality::Final,
          static_cast<std::uint32_t>(score.value().project.settings().sampleRate), entry.style);
      if (!snapshot) refusal = snapshot.error().message;
      else {
        const auto notes = snapshot.value().compiledPerformance->notes();
        if (notes.empty() || notes.back().endFrame <= notes.front().startFrame) refusal = "Prepared class has no bounded audio span";
        else entry.frameCount = static_cast<std::uint64_t>(notes.back().endFrame - notes.front().startFrame);
      }
    }
    if (refusal) {
      entry.status = "REFUSED";
      entry.detail = *refusal;
      ++report.refused;
      report.refusedClasses.push_back(row.coverageKey);
    } else {
      entry.status = "PREPARED";
      entry.detail = "snapshot compiled";
      ++report.prepared;
      coveredKinds.insert(row.coverageKey.substr(0U, row.coverageKey.find(':')));
      for (const auto& phone : phones) coveredPhones.insert(phone);
    }
    report.entries.push_back(std::move(entry));
  }
  for (const auto& phone : requiredPhones)
    if (coveredPhones.count(phone) == 0U) report.missingPhones.push_back(phone);
  for (const auto& kind : requiredKinds)
    if (coveredKinds.count(kind) == 0U) report.missingKinds.push_back(kind);
  report.complete = report.refused == 0U && report.missingPhones.empty() && report.missingKinds.empty() &&
      !report.entries.empty();
  formats::JsonValue::Array entries;
  entries.reserve(report.entries.size());
  for (const auto& entry : report.entries) {
    entries.emplace_back(formats::JsonValue::Object{
        {"takeId", entry.takeId}, {"coverageKey", entry.coverageKey}, {"style", entry.style},
        {"pitchLayer", entry.pitchLayer}, {"status", entry.status}, {"detail", entry.detail},
        {"frameCount", static_cast<std::int64_t>(entry.frameCount)}});
  }
  const auto strings = [](const std::vector<std::string>& values) {
    formats::JsonValue::Array array;
    for (const auto& value : values) array.emplace_back(value);
    return array;
  };
  report.json = formats::stringifyJson(formats::JsonValue::Object{
      {"formatId", "com.project-seam.inventory-coverage"}, {"schemaVersion", std::int64_t{1}},
      {"status", report.complete ? "COMPLETE" : "INCOMPLETE"}, {"releaseEligible", false},
      {"language", producer.language}, {"producerSha256", core::sha256Hex(producerJson)},
      {"recipeSha256", recipe.identity.contentHash},
      {"assignments", static_cast<std::int64_t>(report.assignments)},
      {"prepared", static_cast<std::int64_t>(report.prepared)},
      {"refused", static_cast<std::int64_t>(report.refused)},
      {"phones", static_cast<std::int64_t>(report.phones)}, {"kinds", static_cast<std::int64_t>(report.kinds)},
      {"missingPhones", strings(report.missingPhones)}, {"missingKinds", strings(report.missingKinds)},
      {"refusedClasses", strings(report.refusedClasses)}, {"entries", std::move(entries)}}, true);
  return core::success(std::move(report));
}
}
