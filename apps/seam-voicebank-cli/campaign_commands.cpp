#include "campaign_commands.hpp"
#include "signal_cancellation.hpp"
#include "seam/authoring/generation_campaign.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include <iostream>

namespace seam::voicebank_cli {
void printCampaignUsage() {
  std::cout << "  seam_voicebank_cli draft-generation-campaign WORKSPACE RECIPE_JSON NEW_PLAN_JSON TAKE_ID [TAKE_ID ...]\n"
            << "  seam_voicebank_cli plan-generation-campaign WORKSPACE PLAN_JSON PLAN_SHA256 NEW_OUTPUT_DIRECTORY\n"
            << "  seam_voicebank_cli inspect-generation-campaign CAMPAIGN_JSON CAMPAIGN_SHA256\n";
  std::cout << "  seam_voicebank_cli advance-generation-campaign WORKSPACE CAMPAIGN_JSON CAMPAIGN_SHA256 OPERATOR UTC\n";
}
std::optional<int> runCampaignCommand(int argc, char** argv) {
  const std::string_view command{argv[1]};
  if (command != "draft-generation-campaign" && command != "plan-generation-campaign" && command != "inspect-generation-campaign" &&
      command != "advance-generation-campaign") return std::nullopt;
  const auto error = [](std::string_view message) -> std::optional<int> { std::cerr << "error: " << message << '\n'; return 1; };
  const bool draft = command == "draft-generation-campaign", publish = command == "plan-generation-campaign";
  const bool advance = command == "advance-generation-campaign";
  if ((draft && (argc < 6 || argc > 16389)) || (publish && argc != 6) || (advance && argc != 7) || (!draft && !publish && !advance && argc != 4)) {
    printCampaignUsage(); return 1;
  }
  SignalCancellation cancellation;
  if (!cancellation.install()) return error("cannot install cancellation handlers");
  if (advance) {
    const auto result = authoring::advanceGenerationCampaign(voicebank_production::ProductionProjectRepository{argv[2]},
        argv[3], argv[4], argv[5], argv[6], cancellation.token());
    if (!result) return error(result.error().message);
    std::cout << formats::stringifyJson(formats::JsonValue::Object{
        {"status", result.value().complete ? "COLLECTED_UNREVIEWED" : "BATCH_COLLECTED"}, {"releaseEligible", false},
        {"completedBatches", static_cast<std::int64_t>(result.value().completedBatches)},
        {"totalBatches", static_cast<std::int64_t>(result.value().totalBatches)}, {"producerSha256", result.value().producerSha256}}) << '\n';
    return 0;
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
