#include "campaign_commands.hpp"
#include "signal_cancellation.hpp"
#include "seam/authoring/generation_campaign.hpp"
#include "seam/authoring/inventory_preflight.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/generation_request.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include <iostream>

namespace seam::voicebank_cli {
namespace {
namespace production = voicebank_production;
// One registry record, verified against producer history; the Python mirror prints the same shape.
formats::JsonValue requestRecordJson(const production::GenerationRequestRecord& record) {
  using formats::JsonValue;
  JsonValue terminal = nullptr;
  if (record.terminal) {
    const auto& value = *record.terminal;
    terminal = JsonValue::Object{{"outcome", production::toString(value.outcome)},
        {"observedGeneration", value.observedGeneration}, {"observedProjectSha256", value.observedProjectSha256},
        {"completedBatches", value.completedBatches}, {"retainedBytes", value.retainedBytes},
        {"recordedBy", value.recordedBy}, {"recordedAtUtc", value.recordedAtUtc}, {"detail", value.detail}};
  }
  return JsonValue::Object{{"requestId", record.request.requestId}, {"requestSha256", record.requestSha256},
      {"state", record.terminal ? production::toString(record.terminal->outcome) : std::string{"SUBMITTED"}},
      {"expectedGeneration", record.request.expectedGeneration},
      {"expectedProjectSha256", record.request.expectedProjectSha256},
      {"jobs", static_cast<std::int64_t>(record.request.jobs.size())}, {"batchCount", record.batchCount()},
      {"submittedBy", record.request.submittedBy}, {"submittedAtUtc", record.request.submittedAtUtc},
      {"definitionLocator", record.request.definitionLocator}, {"terminal", std::move(terminal)}};
}
std::optional<int> runRequestCommand(std::string_view command, int argc, char** argv) {
  const auto error = [](std::string_view message) -> std::optional<int> { std::cerr << "error: " << message << '\n'; return 1; };
  const production::GenerationRequestRegistry registry{argv[2]};
  if (command == "list-generation-requests") {
    if (argc != 3) { printCampaignUsage(); return 1; }
    const auto records = registry.list();
    if (!records) return error(records.error().message);
    formats::JsonValue::Array rows;
    for (const auto& record : records.value()) rows.push_back(requestRecordJson(record));
    std::cout << formats::stringifyJson(formats::JsonValue::Object{{"releaseEligible", false},
        {"requests", std::move(rows)}}) << '\n';
    return 0;
  }
  if (argc != 4) { printCampaignUsage(); return 1; }
  const auto found = registry.find(argv[3]);
  if (!found) return error(found.error().message);
  if (!found.value()) return error("generation request is not submitted to this workspace");
  auto output = requestRecordJson(*found.value());
  output.asObject().emplace("releaseEligible", false);
  std::cout << formats::stringifyJson(output) << '\n';
  return 0;
}
}  // namespace

void printCampaignUsage() {
  std::cout << "  seam_voicebank_cli draft-generation-campaign WORKSPACE RECIPE_JSON NEW_PLAN_JSON TAKE_ID [TAKE_ID ...]\n"
            << "  seam_voicebank_cli plan-generation-campaign WORKSPACE PLAN_JSON PLAN_SHA256 NEW_OUTPUT_DIRECTORY\n"
            << "  seam_voicebank_cli inspect-generation-campaign CAMPAIGN_JSON CAMPAIGN_SHA256\n";
  std::cout << "  seam_voicebank_cli preflight-generation-campaign CAMPAIGN_JSON CAMPAIGN_SHA256\n";
  std::cout << "  seam_voicebank_cli submit-generation-campaign WORKSPACE CAMPAIGN_JSON CAMPAIGN_SHA256 OPERATOR UTC\n";
  std::cout << "  seam_voicebank_cli advance-generation-campaign WORKSPACE CAMPAIGN_JSON CAMPAIGN_SHA256 OPERATOR UTC\n";
  std::cout << "    exit 0 batch collected or complete; 3 stale; 4 output budget exhausted; 128+N on signal N\n";
  std::cout << "  seam_voicebank_cli list-generation-requests WORKSPACE\n";
  std::cout << "  seam_voicebank_cli inspect-generation-request WORKSPACE REQUEST_ID\n";
}
std::optional<int> runCampaignCommand(int argc, char** argv) {
  const std::string_view command{argv[1]};
  if ((command == "list-generation-requests" || command == "inspect-generation-request") && argc >= 3)
    return runRequestCommand(command, argc, argv);
  if (command == "list-generation-requests" || command == "inspect-generation-request") { printCampaignUsage(); return 1; }
  const bool submit = command == "submit-generation-campaign";
  if (command != "draft-generation-campaign" && command != "plan-generation-campaign" && command != "inspect-generation-campaign" &&
      command != "preflight-generation-campaign" && command != "advance-generation-campaign" && !submit) return std::nullopt;
  const auto error = [](std::string_view message) -> std::optional<int> { std::cerr << "error: " << message << '\n'; return 1; };
  const bool draft = command == "draft-generation-campaign", publish = command == "plan-generation-campaign";
  const bool advance = command == "advance-generation-campaign";
  const bool preflight = command == "preflight-generation-campaign";
  if ((draft && (argc < 6 || argc > 16389)) || (publish && argc != 6) || ((advance || submit) && argc != 7) ||
      (preflight && argc != 4) || (!draft && !publish && !advance && !preflight && !submit && argc != 4)) {
    printCampaignUsage(); return 1;
  }
  SignalCancellation cancellation;
  if (!cancellation.install()) return error("cannot install cancellation handlers");
  if (submit) {
    const auto record = authoring::submitGenerationCampaign(argv[2], argv[3], argv[4], argv[5], argv[6], cancellation.token());
    if (!record) return error(record.error().message);
    auto output = requestRecordJson(record.value());
    output.asObject().emplace("releaseEligible", false);
    std::cout << formats::stringifyJson(output) << '\n';
    return 0;
  }
  if (preflight) {
    // The held-out phrase set renders into the campaign's own directory, because a
    // campaign cannot be advanced until a passing report is retained beside it.
    const auto loaded = core::readTextFileLimited(argv[2], 32U * 1024U * 1024U);
    if (!loaded) return error(loaded.error().message);
    const auto campaignRoot = std::filesystem::absolute(argv[2]).parent_path() / "preflight";
    const auto report = authoring::runInventoryPreflight(loaded.value(), argv[3], campaignRoot, {}, cancellation.token());
    if (!report) return error(report.error().message);
    formats::JsonValue::Array defective;
    for (const auto& entry : report.value().defectiveClasses) defective.emplace_back(entry);
    std::cout << formats::stringifyJson(formats::JsonValue::Object{
        {"status", report.value().passed ? "PASS" : "FAIL"}, {"releaseEligible", false},
        {"campaignSha256", std::string{argv[3]}},
        {"phraseCount", static_cast<std::int64_t>(report.value().phrases.size())},
        {"produced", static_cast<std::int64_t>(report.value().produced)},
        {"defective", static_cast<std::int64_t>(report.value().defective)},
        {"defectiveClasses", formats::JsonValue{std::move(defective)}},
        {"reportPath", (campaignRoot / "report.json").string()}}) << '\n';
    if (!report.value().passed) {
      for (const auto& phrase : report.value().phrases)
        if (phrase.verdict != "PRODUCED") std::cerr << "error: " << phrase.coverageKey << ": " << phrase.detail << '\n';
      return 1;
    }
    return 0;
  }
  if (advance) {
    // The same producer operation Studio runs: a fresh campaign is submitted to the workspace, and
    // every stopping point is reported as its actual outcome with the producer state it left.
    const auto result = authoring::advanceGenerationRequest(argv[2], argv[3], argv[4], argv[5], argv[6], cancellation.token());
    if (!result) return error(result.error().message);
    using Outcome = authoring::CampaignAdvanceOutcome;
    const auto& report = result.value();
    const auto status = report.outcome == Outcome::Completed ? std::string{"COLLECTED_UNREVIEWED"} : authoring::toString(report.outcome);
    std::cout << formats::stringifyJson(formats::JsonValue::Object{
        {"status", status}, {"outcome", authoring::toString(report.outcome)}, {"releaseEligible", false},
        {"completedBatches", static_cast<std::int64_t>(report.completedBatches)},
        {"totalBatches", static_cast<std::int64_t>(report.totalBatches)},
        {"producerGeneration", static_cast<std::int64_t>(report.producerGeneration)},
        {"producerSha256", report.producerSha256}, {"requestId", report.requestId},
        {"registered", report.registered}, {"terminalRecorded", report.terminalRecorded},
        {"retainedBytes", static_cast<std::int64_t>(report.retainedBytes)}, {"detail", report.detail}}) << '\n';
    switch (report.outcome) {
      case Outcome::BatchCollected:
      case Outcome::Completed: return 0;
      case Outcome::Stale: std::cerr << "error: campaign is stale: " << report.detail << '\n'; return 3;
      case Outcome::BudgetExhausted: std::cerr << "error: campaign output budget exhausted: " << report.detail << '\n'; return 4;
      case Outcome::Cancelled: break;
    }
    std::cerr << "error: " << report.detail << '\n';
    return cancellation.signal() != 0 ? 128 + cancellation.signal() : 1;
  }
  std::string definition;
  if (draft) {
    const auto producer = voicebank_production::ProductionProjectRepository{argv[2]}.recover();
    if (!producer) return error(producer.error().message);
    const auto recipe = voice_design::loadVoiceRecipeResource(argv[3], {}, cancellation.token());
    if (!recipe) return error(recipe.error().message);
    std::vector<std::string> takes;
    for (int index = 5; index < argc; ++index) takes.emplace_back(argv[index]);
    const auto planned = authoring::planGenerationCampaign(producer.value(), takes, recipe.value(), {}, cancellation.token());
    if (!planned) return error(planned.error().message);
    definition = planned.value();
    if (cancellation.token().stop_requested()) return error("Campaign planning cancelled before publication");
    const auto saved = core::durableAtomicWriteTextNew(argv[4], definition);
    if (!saved) return error(saved.error().message);
  } else {
    const auto loaded = core::readTextFileLimited(argv[publish ? 3 : 2], 32U * 1024U * 1024U);
    if (!loaded) return error(loaded.error().message);
    definition = loaded.value();
    const auto verified = authoring::verifyGenerationCampaign(definition, argv[publish ? 4 : 3], cancellation.token());
    if (!verified) return error(verified.error().message);
    if (publish) {
      const auto producer = voicebank_production::ProductionProjectRepository{argv[2]}.recover();
      if (!producer) return error(producer.error().message);
      const auto parsed = formats::parseJson(definition);
      if (core::sha256Hex(voicebank_production::encodeProductionProject(producer.value())) != parsed.value().find("initialProducerSha256")->asString())
        return error("Campaign initial producer state is stale");
      if (cancellation.token().stop_requested()) return error("Campaign planning cancelled before publication");
      std::error_code ec;
      if (!std::filesystem::create_directory(argv[5], ec) || ec) return error("Campaign directory must be new with an existing parent");
      // The immutable definition is the publication boundary. Partial directories
      // remain evidence, never implicitly resumed or overwritten by this command.
      const auto saved = core::durableAtomicWriteTextNew(std::filesystem::path{argv[5]} / "campaign.json", definition);
      if (!saved) return error(saved.error().message);
    }
  }
  const auto parsed = formats::parseJson(definition);
  std::cout << formats::stringifyJson(formats::JsonValue::Object{
      {"status", "PLANNED_UNPREPARED"}, {"releaseEligible", false},
      {"campaignSha256", core::sha256Hex(definition)}, {"jobs", static_cast<std::int64_t>(parsed.value().find("jobs")->asArray().size())},
      {"batchCount", *parsed.value().find("batchCount")}, {"totalFrames", *parsed.value().find("totalFrames")}}) << '\n';
  return 0;
}
}
