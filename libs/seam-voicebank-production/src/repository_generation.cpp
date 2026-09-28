#include "seam/voicebank_production/generation_request.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/repository.hpp"

#include "seam/core/exclusive_file_lock.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <system_error>
#include <tuple>

namespace seam::voicebank_production {
namespace {
using formats::JsonValue;
constexpr const char* kRequestFormat = "com.project-seam.generation-request";
constexpr const char* kTerminalFormat = "com.project-seam.generation-request-terminal";
constexpr std::uint64_t kMaximumRequestBytes = 16U * 1024U * 1024U;
constexpr std::uint64_t kMaximumTerminalBytes = 64U * 1024U;
constexpr std::size_t kMaximumRegistryEntries = 65536U;

template <typename T> core::Result<T> conflict(std::string message) {
  return core::failure<T>(core::ErrorCode::Conflict, std::move(message));
}
template <typename T> core::Result<T> invalid(std::string message) {
  return core::failure<T>(core::ErrorCode::InvalidArgument, std::move(message));
}
bool hex64(std::string_view value) {
  return value.size() == 64U && std::all_of(value.begin(), value.end(),
      [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
// Bounded text without control characters.
bool text(std::string_view value, std::size_t maximum, bool allowEmpty = false) {
  if ((value.empty() && !allowEmpty) || value.size() > maximum) return false;
  return std::none_of(value.begin(), value.end(), [](char c) {
    const auto code = static_cast<unsigned char>(c);
    return code < 0x20U || code == 0x7fU;
  });
}
std::string generationName(std::int64_t generation) {
  std::ostringstream stream;
  stream << std::setw(20) << std::setfill('0') << generation << ".json";
  return stream.str();
}
bool registered(const VoicebankProductionProject& producer, std::string_view operatorId) {
  return std::any_of(producer.operators.begin(), producer.operators.end(),
      [&](const OperatorRecord& row) { return row.operatorId == operatorId; });
}

core::Result<void> validateRequestShape(const GenerationRequest& request) {
  const auto bad = [](std::string message) {
    return core::failure(core::ErrorCode::InvalidArgument, "Generation request " + std::move(message));
  };
  const auto& budget = request.budget;
  if (!hex64(request.requestId) || !text(request.definitionLocator, 4096U, true)) return bad("identity is invalid");
  if (request.expectedGeneration < 1 || !hex64(request.expectedProjectSha256)) return bad("expected producer state is invalid");
  if (!text(request.language, 64U) || !text(request.recipeId, 256U) || !text(request.recipeVersion, 256U) ||
      !hex64(request.recipeHash)) return bad("language or recipe identity is invalid");
  if (budget.maximumJobs < 1 || budget.maximumJobs > 16384 || budget.maximumFrames < 1 ||
      budget.maximumFrames > (std::int64_t{1} << 32) || budget.maximumBytes < 1 || budget.maximumBytes > (std::int64_t{1} << 40) ||
      budget.batchMaximumJobs < 1 || budget.batchMaximumJobs > 64 || budget.batchMaximumFrames < 1 ||
      budget.batchMaximumFrames > 32LL * 1024LL * 1024LL) return bad("budget is outside supported limits");
  if (!text(request.submittedBy, 128U) || !isProductionUtcTimestamp(request.submittedAtUtc)) return bad("submitter or time is invalid");
  if (request.jobs.empty() || static_cast<std::int64_t>(request.jobs.size()) > budget.maximumJobs)
    return bad("job count is outside its budget");
  std::set<std::string_view> jobIds, takeIds;
  std::int64_t total = 0, batch = 0, batchJobs = 0, batchFrames = 0;
  for (const auto& job : request.jobs) {
    if (!text(job.jobId, 128U) || !text(job.takeId, 128U) || !text(job.style, 128U) || !text(job.coverageKey, 256U) ||
        job.pitchLayer < 0 || job.pitchLayer > 127 || job.frameCount < 1 || job.frameCount > budget.batchMaximumFrames)
      return bad("job identity, pitch or duration is invalid");
    if (!jobIds.insert(job.jobId).second || !takeIds.insert(job.takeId).second) return bad("repeats a job or take ID");
    if (batchJobs > 0 && job.batchIndex == batch + 1) { batch = job.batchIndex; batchJobs = 0; batchFrames = 0; }
    if (job.batchIndex != batch) return bad("batches are not contiguous from zero");
    if (++batchJobs > budget.batchMaximumJobs || job.frameCount > budget.batchMaximumFrames - batchFrames ||
        job.frameCount > budget.maximumFrames - total) return bad("exceeds its batch or aggregate frame budget");
    batchFrames += job.frameCount;
    total += job.frameCount;
  }
  return core::success();
}

core::Result<void> validateTerminalShape(const GenerationRequestTerminal& terminal) {
  if (terminal.observedGeneration < 1 || !hex64(terminal.observedProjectSha256) || terminal.completedBatches < 0 ||
      terminal.retainedBytes < 0 || !text(terminal.detail, 1024U, true) || !text(terminal.recordedBy, 128U) ||
      !isProductionUtcTimestamp(terminal.recordedAtUtc))
    return core::failure(core::ErrorCode::InvalidArgument, "Generation request terminal outcome is invalid");
  return core::success();
}

JsonValue requestJson(const GenerationRequest& request) {
  JsonValue::Array jobs;
  for (const auto& job : request.jobs)
    jobs.emplace_back(JsonValue::Object{{"batchIndex", job.batchIndex}, {"coverageKey", job.coverageKey},
        {"frameCount", job.frameCount}, {"jobId", job.jobId}, {"pitchLayer", static_cast<std::int64_t>(job.pitchLayer)},
        {"style", job.style}, {"takeId", job.takeId}});
  const auto& budget = request.budget;
  return JsonValue::Object{{"formatId", kRequestFormat}, {"schemaVersion", std::int64_t{1}},
      {"requestId", request.requestId}, {"definitionLocator", request.definitionLocator},
      {"expectedGeneration", request.expectedGeneration}, {"expectedProjectSha256", request.expectedProjectSha256},
      {"language", request.language},
      {"recipe", JsonValue::Object{{"contentHash", request.recipeHash}, {"id", request.recipeId},
          {"version", request.recipeVersion}}},
      {"budget", JsonValue::Object{{"batchMaximumFrames", budget.batchMaximumFrames},
          {"batchMaximumJobs", budget.batchMaximumJobs}, {"maximumBytes", budget.maximumBytes},
          {"maximumFrames", budget.maximumFrames}, {"maximumJobs", budget.maximumJobs}}},
      {"jobs", std::move(jobs)}, {"submittedBy", request.submittedBy}, {"submittedAtUtc", request.submittedAtUtc},
      {"releaseEligible", false}};
}

// Strict field reader: any missing field or wrong type fails the whole record.
struct Fields final {
  const JsonValue* object;
  bool ok{true};
  const JsonValue* get(std::string_view key) {
    const auto* value = object != nullptr && object->isObject() ? object->find(key) : nullptr;
    if (value == nullptr) ok = false;
    return value;
  }
  std::string string(std::string_view key) {
    const auto* value = get(key);
    if (value == nullptr || !value->isString()) { ok = false; return {}; }
    return value->asString();
  }
  std::int64_t integer(std::string_view key) {
    const auto* value = get(key);
    if (value == nullptr || !value->isInteger()) { ok = false; return 0; }
    return value->asInt64();
  }
  bool falseFlag(std::string_view key) {
    const auto* value = get(key);
    return value != nullptr && value->isBool() && !value->asBool();
  }
};

core::Result<GenerationRequest> decodeRequest(std::string_view bytes) {
  const auto malformed = [] { return invalid<GenerationRequest>("Generation request record is malformed or not canonical"); };
  const auto parsed = formats::parseJson(bytes);
  if (!parsed || !parsed.value().isObject()) return malformed();
  Fields root{&parsed.value()};
  GenerationRequest request;
  const bool header = root.string("formatId") == kRequestFormat && root.integer("schemaVersion") == 1 &&
                      root.falseFlag("releaseEligible");
  request.requestId = root.string("requestId");
  request.definitionLocator = root.string("definitionLocator");
  request.expectedGeneration = root.integer("expectedGeneration");
  request.expectedProjectSha256 = root.string("expectedProjectSha256");
  request.language = root.string("language");
  request.submittedBy = root.string("submittedBy");
  request.submittedAtUtc = root.string("submittedAtUtc");
  Fields recipe{root.get("recipe")};
  request.recipeHash = recipe.string("contentHash");
  request.recipeId = recipe.string("id");
  request.recipeVersion = recipe.string("version");
  Fields budget{root.get("budget")};
  request.budget = {.maximumJobs = budget.integer("maximumJobs"), .maximumFrames = budget.integer("maximumFrames"),
      .maximumBytes = budget.integer("maximumBytes"), .batchMaximumJobs = budget.integer("batchMaximumJobs"),
      .batchMaximumFrames = budget.integer("batchMaximumFrames")};
  const auto* jobs = root.get("jobs");
  if (!header || !root.ok || !recipe.ok || !budget.ok || jobs == nullptr || !jobs->isArray() ||
      jobs->asArray().size() > 16384U) return malformed();
  for (const auto& row : jobs->asArray()) {
    Fields job{&row};
    const auto pitch = job.integer("pitchLayer");
    request.jobs.push_back({.jobId = job.string("jobId"), .takeId = job.string("takeId"), .style = job.string("style"),
        .coverageKey = job.string("coverageKey"), .pitchLayer = static_cast<std::int32_t>(std::clamp<std::int64_t>(pitch, -1, 128)),
        .frameCount = job.integer("frameCount"), .batchIndex = job.integer("batchIndex")});
    if (!job.ok) return malformed();
  }
  const auto encoded = encodeGenerationRequest(request);
  if (!encoded) return core::Result<GenerationRequest>{encoded.error()};
  if (encoded.value() != bytes) return malformed();
  return request;
}

core::Result<GenerationRequestTerminal> decodeTerminal(
    std::string_view bytes, std::string_view requestId, std::string_view requestSha256) {
  const auto malformed = [] { return invalid<GenerationRequestTerminal>("Generation request terminal record is malformed or not canonical"); };
  const auto parsed = formats::parseJson(bytes);
  if (!parsed || !parsed.value().isObject()) return malformed();
  Fields root{&parsed.value()};
  GenerationRequestTerminal terminal;
  const auto outcome = root.string("outcome");
  if (outcome == "COMPLETED") terminal.outcome = GenerationRequestOutcome::Completed;
  else if (outcome == "STALE") terminal.outcome = GenerationRequestOutcome::Stale;
  else if (outcome == "BUDGET_EXHAUSTED") terminal.outcome = GenerationRequestOutcome::BudgetExhausted;
  else return malformed();
  terminal.observedGeneration = root.integer("observedGeneration");
  terminal.observedProjectSha256 = root.string("observedProjectSha256");
  terminal.completedBatches = root.integer("completedBatches");
  terminal.retainedBytes = root.integer("retainedBytes");
  terminal.detail = root.string("detail");
  terminal.recordedBy = root.string("recordedBy");
  terminal.recordedAtUtc = root.string("recordedAtUtc");
  if (!root.ok || root.string("formatId") != kTerminalFormat || root.integer("schemaVersion") != 1 ||
      !root.falseFlag("releaseEligible") || root.string("requestId") != requestId ||
      root.string("requestSha256") != requestSha256 || !root.ok) return malformed();
  const auto encoded = encodeGenerationRequestTerminal(terminal, requestId, requestSha256);
  if (!encoded) return core::Result<GenerationRequestTerminal>{encoded.error()};
  if (encoded.value() != bytes) return malformed();
  return terminal;
}

// Missing is false; anything other than a real directory is refused.
core::Result<bool> directoryState(const std::filesystem::path& path, bool create) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory || (!error && status.type() == std::filesystem::file_type::not_found)) {
    if (!create) return false;
    error.clear();
    if (!std::filesystem::create_directory(path, error) || error)
      return conflict<bool>("Cannot create generation request directory " + path.string());
    return true;
  }
  if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status))
    return conflict<bool>("Generation request directory is unsafe: " + path.string());
  return true;
}
core::Result<bool> regularFile(const std::filesystem::path& path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory || (!error && status.type() == std::filesystem::file_type::not_found)) return false;
  if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
    return conflict<bool>("Generation request record is unsafe: " + path.string());
  return true;
}

// Every job names exactly one assignment with the same identity and no take exists for it yet.
core::Result<void> verifyJobsAgainst(const VoicebankProductionProject& producer, const GenerationRequest& request) {
  if (producer.language != request.language) return conflict<void>("Generation request language differs from the producer");
  if (!registered(producer, request.submittedBy)) return conflict<void>("Generation request submitter is not a registered operator");
  for (const auto& job : request.jobs) {
    const auto matches = [&](const UnitAssignment& row) { return row.plannedTakeId == job.takeId; };
    if (std::count_if(producer.unitAssignments.begin(), producer.unitAssignments.end(), matches) != 1)
      return conflict<void>("Generation request take " + job.takeId + " does not identify exactly one assignment");
    const auto& row = *std::find_if(producer.unitAssignments.begin(), producer.unitAssignments.end(), matches);
    if (row.style != job.style || row.coverageKey != job.coverageKey || row.pitchLayer != job.pitchLayer)
      return conflict<void>("Generation request take " + job.takeId + " differs from its assignment identity");
    if (std::any_of(producer.takes.begin(), producer.takes.end(), [&](const TakeRecord& take) { return take.takeId == job.takeId; }))
      return conflict<void>("Generation request take " + job.takeId + " already exists; a request cannot duplicate or replace it");
  }
  return core::success();
}

core::Result<VoicebankProductionProject> journaledGeneration(const ProductionProjectRepository& repository,
    const std::filesystem::path& root, std::int64_t generation, std::string& action) {
  const auto missing = [] { return conflict<VoicebankProductionProject>("Generation request history is not in the producer journal"); };
  const auto bytes = core::readTextFileLimited(root / "journal" / generationName(generation), 1024U * 1024U);
  if (!bytes) return missing();
  const auto parsed = formats::parseJson(bytes.value());
  if (!parsed || !parsed.value().isObject()) return missing();
  const auto* recorded = parsed.value().find("generation");
  const auto* digest = parsed.value().find("projectSha256");
  const auto* name = parsed.value().find("action");
  if (recorded == nullptr || !recorded->isInteger() || recorded->asInt64() != generation || digest == nullptr ||
      !digest->isString() || name == nullptr || !name->isString()) return missing();
  action = name->asString();
  const auto project = repository.recoverGeneration(static_cast<std::uint64_t>(generation), digest->asString());
  if (!project) return missing();
  return project;
}

// The first count generations after the expected one must each be one generated batch collection
// that introduced exactly that batch's takes, with their request identity and no review status.
core::Result<void> verifyCollectedBatches(const ProductionProjectRepository& repository, const std::filesystem::path& root,
    const GenerationRequest& request, const VoicebankProductionProject& expected, std::int64_t count) {
  auto previous = expected;
  for (std::int64_t batch = 0; batch < count; ++batch) {
    std::string action;
    auto next = journaledGeneration(repository, root, request.expectedGeneration + batch + 1, action);
    if (!next) return core::Result<void>{next.error()};
    if (action != "import-generated-batch")
      return conflict<void>("Generation request batch " + std::to_string(batch) + " is not a generated batch collection");
    std::set<std::string> introduced, requested;
    for (const auto& take : next.value().takes)
      if (std::none_of(previous.takes.begin(), previous.takes.end(), [&](const TakeRecord& old) { return old.takeId == take.takeId; }))
        introduced.insert(take.takeId);
    for (const auto& job : request.jobs) {
      if (job.batchIndex != batch) continue;
      requested.insert(job.takeId);
      const auto take = std::find_if(next.value().takes.begin(), next.value().takes.end(),
          [&](const TakeRecord& row) { return row.takeId == job.takeId; });
      if (take == next.value().takes.end() || take->style != job.style || take->coverageKey != job.coverageKey ||
          take->pitchLayer != job.pitchLayer ||
          (take->state != UnitQueueState::MarkerReview && take->state != UnitQueueState::Rejected))
        return conflict<void>("Generation request take " + job.takeId + " was not collected as unreviewed material of its request");
    }
    if (introduced != requested)
      return conflict<void>("Generation request batch " + std::to_string(batch) + " does not introduce exactly its requested takes");
    previous = std::move(next.value());
  }
  return core::success();
}

core::Result<void> verifyTerminal(const ProductionProjectRepository& repository, const std::filesystem::path& root,
    const GenerationRequestRecord& record, const VoicebankProductionProject& expected, const GenerationRequestTerminal& terminal) {
  const auto shape = validateTerminalShape(terminal);
  if (!shape) return shape;
  const auto& request = record.request;
  const auto batches = record.batchCount();
  if (terminal.completedBatches > batches || terminal.observedGeneration < request.expectedGeneration + terminal.completedBatches)
    return conflict<void>("Generation request terminal outcome claims more collection than its history holds");
  const auto observed = repository.recoverGeneration(static_cast<std::uint64_t>(terminal.observedGeneration),
                                                     terminal.observedProjectSha256);
  if (!observed) return conflict<void>("Generation request terminal outcome does not name a durable producer generation");
  if (!registered(observed.value(), terminal.recordedBy))
    return conflict<void>("Generation request terminal outcome was not recorded by a registered operator");
  const auto collected = verifyCollectedBatches(repository, root, request, expected, terminal.completedBatches);
  if (!collected) return collected;
  switch (terminal.outcome) {
    case GenerationRequestOutcome::Completed:
      if (terminal.completedBatches != batches || terminal.observedGeneration != request.expectedGeneration + batches ||
          terminal.retainedBytes != 0) return conflict<void>("A completed generation request must end at its final batch collection");
      break;
    case GenerationRequestOutcome::Stale:
      if (terminal.completedBatches >= batches || terminal.retainedBytes != 0 ||
          terminal.observedGeneration <= request.expectedGeneration + terminal.completedBatches)
        return conflict<void>("A stale generation request must observe a producer beyond its own collections");
      break;
    case GenerationRequestOutcome::BudgetExhausted:
      if (terminal.completedBatches >= batches || terminal.retainedBytes <= request.budget.maximumBytes)
        return conflict<void>("An exhausted generation request must retain more than its admitted bytes");
      break;
  }
  return core::success();
}

}  // namespace

std::string toString(GenerationRequestOutcome value) {
  switch (value) {
    case GenerationRequestOutcome::Completed: return "COMPLETED";
    case GenerationRequestOutcome::Stale: return "STALE";
    case GenerationRequestOutcome::BudgetExhausted: return "BUDGET_EXHAUSTED";
  }
  return "STALE";
}

core::Result<std::string> encodeGenerationRequest(const GenerationRequest& request) {
  const auto valid = validateRequestShape(request);
  if (!valid) return core::Result<std::string>{valid.error()};
  return formats::stringifyJson(requestJson(request), true);
}

core::Result<std::string> encodeGenerationRequestTerminal(
    const GenerationRequestTerminal& terminal, std::string_view requestId, std::string_view requestSha256) {
  const auto valid = validateTerminalShape(terminal);
  if (!valid) return core::Result<std::string>{valid.error()};
  if (!hex64(requestId) || !hex64(requestSha256)) return invalid<std::string>("Generation request terminal binding is invalid");
  return formats::stringifyJson(JsonValue::Object{{"formatId", kTerminalFormat}, {"schemaVersion", std::int64_t{1}},
      {"requestId", std::string{requestId}}, {"requestSha256", std::string{requestSha256}},
      {"outcome", toString(terminal.outcome)}, {"observedGeneration", terminal.observedGeneration},
      {"observedProjectSha256", terminal.observedProjectSha256}, {"completedBatches", terminal.completedBatches},
      {"retainedBytes", terminal.retainedBytes}, {"detail", terminal.detail}, {"recordedBy", terminal.recordedBy},
      {"recordedAtUtc", terminal.recordedAtUtc}, {"releaseEligible", false}}, true);
}

GenerationRequestRegistry::GenerationRequestRegistry(std::filesystem::path workspaceRoot)
    : root_(std::move(workspaceRoot)) {}

core::Result<std::optional<GenerationRequestRecord>> GenerationRequestRegistry::find(
    std::string_view requestId, std::stop_token stop) const {
  using Output = std::optional<GenerationRequestRecord>;
  if (!hex64(requestId)) return invalid<Output>("Generation request ID is invalid");
  if (stop.stop_requested()) return conflict<Output>("Generation request inspection cancelled");
  const auto registry = directoryState(root_ / "generation-requests", false);
  if (!registry) return core::Result<Output>{registry.error()};
  if (!registry.value()) return Output{};
  const auto directory = root_ / "generation-requests" / std::string{requestId};
  const auto present = directoryState(directory, false);
  if (!present) return core::Result<Output>{present.error()};
  const auto file = present.value() ? regularFile(directory / "request.json") : core::Result<bool>{false};
  if (!file) return core::Result<Output>{file.error()};
  // A directory without its request is an interrupted submission, not a request.
  if (!file.value()) return Output{};
  const auto bytes = core::readTextFileLimited(directory / "request.json", kMaximumRequestBytes);
  if (!bytes) return core::Result<Output>{bytes.error()};
  const auto request = decodeRequest(bytes.value());
  if (!request) return core::Result<Output>{request.error()};
  if (request.value().requestId != requestId) return conflict<Output>("Generation request is filed under another ID");
  const ProductionProjectRepository repository{root_};
  const auto expected = repository.recoverGeneration(static_cast<std::uint64_t>(request.value().expectedGeneration),
                                                     request.value().expectedProjectSha256);
  if (!expected) return conflict<Output>("Generation request does not bind a durable producer generation");
  const auto jobs = verifyJobsAgainst(expected.value(), request.value());
  if (!jobs) return core::Result<Output>{jobs.error()};
  GenerationRequestRecord record{request.value(), core::sha256Hex(bytes.value()), std::nullopt};
  const auto terminalFile = regularFile(directory / "terminal.json");
  if (!terminalFile) return core::Result<Output>{terminalFile.error()};
  if (terminalFile.value()) {
    if (stop.stop_requested()) return conflict<Output>("Generation request inspection cancelled");
    const auto terminalBytes = core::readTextFileLimited(directory / "terminal.json", kMaximumTerminalBytes);
    if (!terminalBytes) return core::Result<Output>{terminalBytes.error()};
    const auto terminal = decodeTerminal(terminalBytes.value(), requestId, record.requestSha256);
    if (!terminal) return core::Result<Output>{terminal.error()};
    const auto verified = verifyTerminal(repository, root_, record, expected.value(), terminal.value());
    if (!verified) return core::Result<Output>{verified.error()};
    record.terminal = terminal.value();
  }
  return Output{std::move(record)};
}

core::Result<GenerationRequestRecord> GenerationRequestRegistry::submit(
    const GenerationRequest& request, std::stop_token stop) const {
  using Output = GenerationRequestRecord;
  const auto encoded = encodeGenerationRequest(request);
  if (!encoded) return core::Result<Output>{encoded.error()};
  if (stop.stop_requested()) return conflict<Output>("Generation request submission cancelled");
  const auto registry = directoryState(root_ / "generation-requests", true);
  if (!registry) return core::Result<Output>{registry.error()};
  core::ExclusiveFileLock lock;
  const auto locked = lock.acquire(root_ / "generation-requests" / ".registry.lock");
  if (!locked) return core::Result<Output>{locked.error()};
  const auto directory = root_ / "generation-requests" / request.requestId;
  const auto present = directoryState(directory, false);
  if (!present) return core::Result<Output>{present.error()};
  const auto file = present.value() ? regularFile(directory / "request.json") : core::Result<bool>{false};
  if (!file) return core::Result<Output>{file.error()};
  if (file.value()) {
    const auto retained = core::readTextFileLimited(directory / "request.json", kMaximumRequestBytes);
    if (!retained) return core::Result<Output>{retained.error()};
    if (retained.value() != encoded.value()) return conflict<Output>("A different generation request is already filed under this ID");
    const auto found = find(request.requestId);
    if (!found) return core::Result<Output>{found.error()};
    if (!found.value()) return conflict<Output>("Retained generation request cannot be verified");
    return *found.value();
  }
  const ProductionProjectRepository repository{root_};
  const auto current = repository.recover();
  if (!current) return core::Result<Output>{current.error()};
  if (static_cast<std::int64_t>(current.value().lastDurableGeneration) != request.expectedGeneration ||
      core::sha256Hex(encodeProductionProject(current.value())) != request.expectedProjectSha256)
    return conflict<Output>("Generation request is stale: the producer is no longer at its expected generation");
  const auto jobs = verifyJobsAgainst(current.value(), request);
  if (!jobs) return core::Result<Output>{jobs.error()};
  if (stop.stop_requested()) return conflict<Output>("Generation request submission cancelled");
  const auto created = directoryState(directory, true);
  if (!created) return core::Result<Output>{created.error()};
  const auto written = core::durableAtomicWriteTextNew(directory / "request.json", encoded.value());
  if (!written) return core::Result<Output>{written.error()};
  return Output{request, core::sha256Hex(encoded.value()), std::nullopt};
}

core::Result<GenerationRequestRecord> GenerationRequestRegistry::recordTerminal(
    std::string_view requestId, const GenerationRequestTerminal& terminal, std::stop_token stop) const {
  using Output = GenerationRequestRecord;
  const auto shape = validateTerminalShape(terminal);
  if (!shape) return core::Result<Output>{shape.error()};
  const auto registry = directoryState(root_ / "generation-requests", false);
  if (!registry) return core::Result<Output>{registry.error()};
  if (!registry.value()) return core::failure<Output>(core::ErrorCode::NotFound, "Generation request is not submitted");
  core::ExclusiveFileLock lock;
  const auto locked = lock.acquire(root_ / "generation-requests" / ".registry.lock");
  if (!locked) return core::Result<Output>{locked.error()};
  const auto found = find(requestId, stop);
  if (!found) return core::Result<Output>{found.error()};
  if (!found.value()) return core::failure<Output>(core::ErrorCode::NotFound, "Generation request is not submitted");
  auto record = *found.value();
  if (record.terminal) {
    if (*record.terminal == terminal) return record;
    return conflict<Output>("Generation request already has a different terminal outcome");
  }
  const ProductionProjectRepository repository{root_};
  const auto expected = repository.recoverGeneration(static_cast<std::uint64_t>(record.request.expectedGeneration),
                                                     record.request.expectedProjectSha256);
  if (!expected) return conflict<Output>("Generation request does not bind a durable producer generation");
  const auto verified = verifyTerminal(repository, root_, record, expected.value(), terminal);
  if (!verified) return core::Result<Output>{verified.error()};
  const auto encoded = encodeGenerationRequestTerminal(terminal, requestId, record.requestSha256);
  if (!encoded) return core::Result<Output>{encoded.error()};
  const auto written = core::durableAtomicWriteTextNew(root_ / "generation-requests" / std::string{requestId} / "terminal.json",
                                                       encoded.value());
  if (!written) return core::Result<Output>{written.error()};
  record.terminal = terminal;
  return record;
}

core::Result<std::vector<GenerationRequestRecord>> GenerationRequestRegistry::list(std::stop_token stop) const {
  using Output = std::vector<GenerationRequestRecord>;
  const auto registry = directoryState(root_ / "generation-requests", false);
  if (!registry) return core::Result<Output>{registry.error()};
  Output records;
  if (!registry.value()) return records;
  std::vector<std::string> names;
  std::error_code error;
  for (std::filesystem::directory_iterator iterator{root_ / "generation-requests", error}, end; !error && iterator != end;
       iterator.increment(error)) {
    if (names.size() == kMaximumRegistryEntries) return conflict<Output>("Generation request registry has too many entries");
    const auto name = iterator->path().filename().string();
    if (name == ".registry.lock") continue;
    if (!hex64(name)) return conflict<Output>("Generation request registry holds an unrecognized entry: " + name);
    names.push_back(name);
  }
  if (error) return conflict<Output>("Cannot enumerate the generation request registry");
  for (const auto& name : names) {
    if (stop.stop_requested()) return conflict<Output>("Generation request listing cancelled");
    const auto found = find(name, stop);
    if (!found) return core::Result<Output>{found.error()};
    if (found.value()) records.push_back(*found.value());
  }
  std::sort(records.begin(), records.end(), [](const GenerationRequestRecord& left, const GenerationRequestRecord& right) {
    return std::tie(left.request.submittedAtUtc, left.request.requestId) < std::tie(right.request.submittedAtUtc, right.request.requestId);
  });
  return records;
}

}  // namespace seam::voicebank_production
