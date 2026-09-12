#include "seam/authoring/generation_campaign.hpp"
#include "seam/authoring/generation_batch_collection.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/exclusive_file_lock.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include <algorithm>

namespace seam::authoring {
core::Result<CampaignAdvanceResult> advanceGenerationCampaign(
    const voicebank_production::ProductionProjectRepository& repository,
    const std::filesystem::path& campaignPath, std::string_view campaignSha256,
    std::string operatorId, std::string occurredAtUtc, std::stop_token stop,
    std::function<bool()> interruptBeforeReceipt) {
  using Output = CampaignAdvanceResult;
  const auto fail = [](std::string message) { return core::failure<Output>(core::ErrorCode::Conflict, std::move(message)); };
  const auto bytes = core::readTextFileLimited(campaignPath, 32U * 1024U * 1024U);
  if (!bytes) return core::Result<Output>{bytes.error()};
  const auto verified = verifyGenerationCampaign(bytes.value(), campaignSha256, stop);
  if (!verified) return core::Result<Output>{verified.error()};
  const auto parsed = formats::parseJson(bytes.value());
  const auto& plan = parsed.value();
  const auto initial = voicebank_production::decodeProductionProject(plan.find("initialProducerJson")->asString());
  auto before = repository.recoverGeneration(initial.value().lastDurableGeneration, plan.find("initialProducerSha256")->asString());
  if (!before) return core::Result<Output>{before.error()};
  if (!voicebank_production::isProductionUtcTimestamp(occurredAtUtc) || std::none_of(before.value().operators.begin(), before.value().operators.end(),
      [&](const auto& entry) { return entry.operatorId == operatorId; })) return fail("Campaign operator or timestamp is invalid");
  const auto root = std::filesystem::absolute(campaignPath).parent_path();
  core::ExclusiveFileLock lock;
  const auto locked = lock.acquire(root / ".campaign-advance.lock");
  if (!locked) return core::Result<Output>{locked.error()};
  const auto total = static_cast<std::size_t>(plan.find("batchCount")->asInt64());
  const GenerationBatchLimits limits{static_cast<std::size_t>(plan.find("batchMaximumJobs")->asInt64()),
      static_cast<std::uint64_t>(plan.find("batchMaximumFrames")->asInt64())};
  std::optional<voicebank_production::ProductionCommitReceipt> predecessor;
  for (std::size_t index = 0U; index < total; ++index) {
    if (stop.stop_requested()) return fail("Campaign advancement cancelled; retained work may be resumed");
    const auto directory = root / ("batch-" + std::to_string(index));
    const auto receipt = directory / "collection.json";
    std::error_code error;
    const bool hasReceipt = std::filesystem::exists(receipt, error);
    if (error) return fail("Cannot inspect campaign collection receipt");
    const auto beforeHash = core::sha256Hex(voicebank_production::encodeProductionProject(before.value()));
    if (!hasReceipt) {
      const auto current = repository.recover();
      if (!current) return core::Result<Output>{current.error()};
      const auto currentHash = core::sha256Hex(voicebank_production::encodeProductionProject(current.value()));
      const bool retainedInputs = std::filesystem::is_regular_file(directory / "batch-inputs.json", error);
      if (error == std::errc::no_such_file_or_directory) error.clear();
      if (error) return fail("Cannot inspect campaign preparation inputs");
      // One advanced generation can be the batch's commit-before-receipt window.
      // Recognition below must prove it; no other external change is adopted.
      if (currentHash != beforeHash && (!retainedInputs || current.value().lastDurableGeneration <= before.value().lastDurableGeneration ||
          current.value().lastDurableGeneration - before.value().lastDurableGeneration != 1U))
        return fail("Producer changed outside the campaign's expected transition");
    }
    const auto prepared = prepareGenerationCampaignBatch(bytes.value(), campaignSha256, index, before.value(), directory, predecessor, stop);
    if (!prepared) return core::Result<Output>{prepared.error()};
    if (hasReceipt) {
      const auto after = loadVerifiedGenerationBatchReceipt(repository, before.value(), prepared.value().jobs, receipt, limits, stop);
      if (!after) return core::Result<Output>{after.error()};
      before = after;
      predecessor = voicebank_production::ProductionCommitReceipt{after.value().lastDurableGeneration,
          core::sha256Hex(voicebank_production::encodeProductionProject(after.value())), true, {}};
      continue;
    }
    const auto inputs = inspectGenerationBatch(prepared.value().jobs, limits, stop);
    if (!inputs) return core::Result<Output>{inputs.error()};
    std::size_t recognized = 0U;
    for (const auto& input : inputs.value()) {
      const auto found = repository.findCollectedGeneration(input.expectation);
      if (!found) return core::Result<Output>{found.error()};
      if (found.value()) ++recognized;
    }
    if (recognized != 0U && recognized != prepared.value().jobs.size()) return fail("Partially collected campaign batch needs reconciliation");
    if (recognized == 0U) {
      const auto current = repository.recover();
      if (!current) return core::Result<Output>{current.error()};
      if (core::sha256Hex(voicebank_production::encodeProductionProject(current.value())) != beforeHash)
        return fail("Producer changed before campaign rendering");
      const auto rendered = runGenerationBatch(prepared.value().jobs, limits, stop);
      if (!rendered) return core::Result<Output>{rendered.error()};
    }
    const auto collected = collectGenerationBatchWithReceipt(repository, before.value(), prepared.value().jobs, receipt,
        {"import-generated-batch", prepared.value().batchSha256, operatorId, occurredAtUtc}, limits, stop, interruptBeforeReceipt);
    if (!collected) return core::Result<Output>{collected.error()};
    return Output{index + 1U, total, collected.value().committedProjectSha256, index + 1U == total};
  }
  const auto current = repository.recover();
  if (!current) return core::Result<Output>{current.error()};
  const auto expectedHash = core::sha256Hex(voicebank_production::encodeProductionProject(before.value()));
  if (core::sha256Hex(voicebank_production::encodeProductionProject(current.value())) != expectedHash)
    return fail("Producer changed after campaign completion");
  return Output{total, total, expectedHash, true};
}
}
