#include "coverage_commands.hpp"
#include "signal_cancellation.hpp"
#include "seam/authoring/inventory_coverage.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"
#include <iostream>
#include <vector>

namespace seam::voicebank_cli {
void printCoverageUsage() {
  std::cout << "  seam_voicebank_cli inspect-generation-coverage WORKSPACE RECIPE_JSON NEW_REPORT_JSON\n";
}
std::optional<int> runCoverageCommand(int argc, char** argv) {
  if (std::string_view{argv[1]} != "inspect-generation-coverage") return std::nullopt;
  const auto error = [](std::string_view message) -> std::optional<int> { std::cerr << "error: " << message << '\n'; return 1; };
  if (argc != 5) { printCoverageUsage(); return 1; }
  SignalCancellation cancellation;
  if (!cancellation.install()) return error("cannot install cancellation handlers");
  const auto producer = voicebank_production::ProductionProjectRepository{argv[2]}.recover();
  if (!producer) return error(producer.error().message);
  const auto recipe = voice_design::loadVoiceRecipeResource(argv[3], {}, cancellation.token());
  if (!recipe) return error(recipe.error().message);
  const auto report = authoring::inspectInventoryCoverage(producer.value(), recipe.value(), {}, cancellation.token());
  if (!report) return error(report.error().message);
  // The report is the deliverable, so an incomplete inventory is a result and not a
  // command failure: the caller reads status, missingPhones and refusedClasses.
  const auto saved = core::durableAtomicWriteTextNew(argv[4], report.value().json);
  if (!saved) return error(saved.error().message);
  formats::JsonValue::Array missing, refused;
  for (const auto& phone : report.value().missingPhones) missing.emplace_back(phone);
  for (const auto& value : report.value().refusedClasses) refused.emplace_back(value);
  std::cout << formats::stringifyJson(formats::JsonValue::Object{
      {"status", report.value().complete ? "COMPLETE" : "INCOMPLETE"}, {"releaseEligible", false},
      {"assignments", static_cast<std::int64_t>(report.value().assignments)},
      {"prepared", static_cast<std::int64_t>(report.value().prepared)},
      {"refused", static_cast<std::int64_t>(report.value().refused)},
      {"phones", static_cast<std::int64_t>(report.value().phones)},
      {"missingPhones", formats::JsonValue{std::move(missing)}},
      {"refusedClasses", formats::JsonValue{std::move(refused)}},
      {"reportPath", std::string{argv[4]}}}) << '\n';
  return 0;
}
}

