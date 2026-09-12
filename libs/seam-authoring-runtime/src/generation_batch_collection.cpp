#include "seam/authoring/generation_batch_collection.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/exclusive_file_lock.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include <limits>

namespace seam::authoring {
core::Result<voicebank_production::VoicebankProductionProject> loadVerifiedGenerationBatchReceipt(
    const voicebank_production::ProductionProjectRepository& repository,
    const voicebank_production::VoicebankProductionProject& originalProducer,
    std::span<const GenerationJobReference> jobs, const std::filesystem::path& receiptPath,
    GenerationBatchLimits limits, std::stop_token stop) {
  using Output = voicebank_production::VoicebankProductionProject;
  const auto fail = [] { return core::failure<Output>(core::ErrorCode::Conflict,
      "Collection receipt does not match verified repository history"); };
  if (originalProducer.lastDurableGeneration == std::numeric_limits<std::uint64_t>::max()) return fail();
  const auto inputs = inspectGenerationBatch(jobs, limits, stop);
  if (!inputs) return core::Result<Output>{inputs.error()};
  const auto beforeHash = core::sha256Hex(voicebank_production::encodeProductionProject(originalProducer));
  if (inputs.value().front().expectation.projectStateSha256 != beforeHash) return fail();
  const auto before = repository.recoverGeneration(originalProducer.lastDurableGeneration, beforeHash);
  if (!before) return core::Result<Output>{before.error()};
  std::error_code error;
  const auto status = std::filesystem::symlink_status(receiptPath, error);
  if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) return fail();
  const auto bytes = core::readTextFileLimited(receiptPath, 1024U * 1024U);
  if (!bytes) return core::Result<Output>{bytes.error()};
  const auto parsed = formats::parseJson(bytes.value());
  if (!parsed || !parsed.value().isObject() || !parsed.value().find("committedProducerSha256") ||
      !parsed.value().find("committedProducerSha256")->isString()) return fail();
  const auto& afterHash = parsed.value().find("committedProducerSha256")->asString();
  const auto generation = originalProducer.lastDurableGeneration + 1U;
  const auto after = repository.recoverGeneration(generation, afterHash);
  if (!after) return core::Result<Output>{after.error()};
  formats::JsonValue::Array takes;
  for (const auto& input : inputs.value()) {
    if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Receipt verification cancelled");
    const auto found = repository.findCollectedGeneration(input.expectation, generation, afterHash);
    if (!found) return core::Result<Output>{found.error()};
    if (!found.value() || !found.value()->active) return fail();
    takes.emplace_back(formats::JsonValue::Object{{"takeId", found.value()->takeId}, {"audioSha256", found.value()->audioSha256},
        {"expectationSha256", core::sha256Hex(voicebank_production::encodeGenerationImportExpectation(input.expectation).value())}});
  }
  const auto canonical = formats::stringifyJson(formats::JsonValue::Object{
      {"formatId", "com.project-seam.generation-batch-collection"}, {"schemaVersion", std::int64_t{1}},
      {"originalProducerSha256", beforeHash}, {"committedProducerSha256", afterHash},
      {"committedGeneration", std::to_string(generation)}, {"takes", std::move(takes)}}, true);
  if (canonical != bytes.value()) return fail();
  return after.value();
}

core::Result<voicebank_production::ProductionCommitReceipt> collectGenerationBatchWithReceipt(
    const voicebank_production::ProductionProjectRepository& repository,
    const voicebank_production::VoicebankProductionProject& originalProducer,
    std::span<const GenerationJobReference> jobs, const std::filesystem::path& receiptPath,
    const voicebank_production::ProductionJournalEvent& event, GenerationBatchLimits limits,
    std::stop_token stop, std::function<bool()> interruptBeforeReceipt) {
  using Output = voicebank_production::ProductionCommitReceipt;
  const auto conflict = [](std::string message) { return core::failure<Output>(core::ErrorCode::Conflict, std::move(message)); };
  if (stop.stop_requested()) return conflict("Batch collection cancelled before recovery");
  if (originalProducer.lastDurableGeneration == std::numeric_limits<std::uint64_t>::max()) return conflict("Producer generation is exhausted");
  const auto originalHash = core::sha256Hex(voicebank_production::encodeProductionProject(originalProducer));
  const auto inputs = inspectGenerationBatch(jobs, limits, stop);
  if (!inputs) return core::Result<Output>{inputs.error()};
  if (inputs.value().front().expectation.projectStateSha256 != originalHash) return conflict("Batch does not bind the retained original producer");
  core::ExclusiveFileLock lock;
  const auto acquired = lock.acquire(receiptPath.string() + ".lock");
  if (!acquired) return core::Result<Output>{acquired.error()};
  auto current = repository.recover();
  if (!current) return core::Result<Output>{current.error()};
  std::size_t collected = 0U;
  bool pointerDurabilityConfirmed = false;
  for (const auto& input : inputs.value()) {
    const auto found = repository.findCollectedGeneration(input.expectation);
    if (!found) return core::Result<Output>{found.error()};
    if (found.value()) ++collected;
  }
  if (collected != 0U && collected != jobs.size()) return conflict("Partial batch collection requires reconciliation");
  if (collected == 0U) {
    if (core::sha256Hex(voicebank_production::encodeProductionProject(current.value())) != originalHash)
      return conflict("Producer changed before batch collection");
    std::error_code error;
    if (std::filesystem::exists(receiptPath, error) || error) return conflict("Uncollected batch has an existing or unreadable receipt");
    // Repository import performs its own output verification and atomic commit.
    auto writer = repository;
    const auto imported = writer.importGeneratedBatch(current.value(), inputs.value(), event, limits.maximumFrames, stop);
    if (!imported) return core::Result<Output>{imported.error()};
    if (!imported.value().durabilityConfirmed) return conflict("Batch committed with uncertain durability; recover before advancing");
    pointerDurabilityConfirmed = true;
  }
  current = repository.recover();
  if (!current) return core::Result<Output>{current.error()};
  if (current.value().lastDurableGeneration != originalProducer.lastDurableGeneration + 1U)
    return conflict("Producer advanced beyond this batch; do not adopt external changes into a campaign receipt");
  formats::JsonValue::Array takes;
  for (const auto& input : inputs.value()) {
    const auto found = repository.findCollectedGeneration(input.expectation);
    if (!found) return core::Result<Output>{found.error()};
    if (!found.value() || !found.value()->active) return conflict("Committed batch identity cannot be recovered");
    takes.emplace_back(formats::JsonValue::Object{{"takeId", found.value()->takeId}, {"audioSha256", found.value()->audioSha256},
        {"expectationSha256", core::sha256Hex(voicebank_production::encodeGenerationImportExpectation(input.expectation).value())}});
  }
  const auto afterHash = core::sha256Hex(voicebank_production::encodeProductionProject(current.value()));
  if (!pointerDurabilityConfirmed) {
    const auto reconciled = repository.reconcileCurrentPointer(current.value().lastDurableGeneration, afterHash, stop);
    if (!reconciled) return core::Result<Output>{reconciled.error()};
    pointerDurabilityConfirmed = true;
  }
  const auto text = formats::stringifyJson(formats::JsonValue::Object{
      {"formatId", "com.project-seam.generation-batch-collection"}, {"schemaVersion", std::int64_t{1}},
      {"originalProducerSha256", originalHash}, {"committedProducerSha256", afterHash},
      {"committedGeneration", std::to_string(current.value().lastDurableGeneration)}, {"takes", std::move(takes)}}, true);
  if (interruptBeforeReceipt && interruptBeforeReceipt()) return conflict("Injected interruption after commit and before receipt publication");
  std::error_code error;
  if (std::filesystem::exists(receiptPath, error)) {
    const auto retained = core::readTextFileLimited(receiptPath, 1024U * 1024U);
    if (!retained || retained.value() != text) return conflict("Retained collection receipt differs from durable producer evidence");
  } else {
    if (error) return conflict("Cannot inspect collection receipt");
    const auto saved = core::durableAtomicWriteTextNew(receiptPath, text);
    if (!saved) return core::Result<Output>{saved.error()};
  }
  return Output{current.value().lastDurableGeneration, afterHash, pointerDurabilityConfirmed, {}};
}
}
