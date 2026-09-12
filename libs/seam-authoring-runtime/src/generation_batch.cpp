#include "seam/authoring/generation_job.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include <algorithm>
#include <set>

namespace seam::authoring {
core::Result<std::string> saveGenerationBatch(const std::filesystem::path& path,
    std::span<const GenerationJobReference> jobs, GenerationBatchLimits limits, std::stop_token stopToken,
    std::string_view expectedProducerSha256) {
  const auto valid = inspectGenerationBatch(jobs, limits, stopToken);
  if (!valid) return core::Result<std::string>{valid.error()};
  if (!expectedProducerSha256.empty() && valid.value().front().expectation.projectStateSha256 != expectedProducerSha256)
    return core::failure<std::string>(core::ErrorCode::Conflict, "Batch jobs differ from the selected producer state");
  std::error_code error;
  const auto destination = std::filesystem::absolute(path, error).lexically_normal();
  if (error) return core::failure<std::string>(core::ErrorCode::InvalidArgument, "Cannot resolve batch destination");
  formats::JsonValue::Array entries;
  for (const auto& job : jobs) {
    const auto absolute = std::filesystem::absolute(job.directory, error).lexically_normal();
    if (error) return core::failure<std::string>(core::ErrorCode::InvalidArgument, "Cannot resolve batch job directory");
    auto relative = absolute.lexically_relative(destination.parent_path());
    if (relative.empty()) relative = absolute;
    const auto directory = relative.generic_string();
    if (directory.empty() || directory.size() > 4096U ||
        std::any_of(directory.begin(), directory.end(), [](unsigned char c) { return c < 32U || c == 127U; }))
      return core::failure<std::string>(core::ErrorCode::InvalidArgument, "Batch job path exceeds manifest bounds");
    entries.emplace_back(formats::JsonValue::Object{{"directory", directory}, {"manifestSha256", job.manifestSha256}});
  }
  const auto bytes = formats::stringifyJson(formats::JsonValue{formats::JsonValue::Object{
      {"formatId", "com.project-seam.generation-batch"}, {"schemaVersion", std::int64_t{1}}, {"jobs", std::move(entries)}}});
  if (bytes.size() > 64U * 1024U)
    return core::failure<std::string>(core::ErrorCode::InvalidArgument, "Batch manifest exceeds 64 KiB");
  if (stopToken.stop_requested()) return core::failure<std::string>(core::ErrorCode::Conflict, "Batch preparation cancelled");
  const auto written = core::durableAtomicWriteTextNew(destination, bytes);
  if (!written) return core::Result<std::string>{written.error()};
  return core::sha256Hex(bytes);
}

core::Result<std::vector<GenerationJobReference>> loadGenerationBatch(const std::filesystem::path& path, std::string_view expectedSha256) {
  using Output = std::vector<GenerationJobReference>;
  const auto fail = [] { return core::failure<Output>(core::ErrorCode::InvalidArgument, "Generation batch manifest is invalid"); };
  if (expectedSha256.size() != 64U) return fail();
  std::error_code error;
  const auto absolute = std::filesystem::absolute(path, error);
  if (error) return fail();
  const auto bytes = core::readTextFileLimited(absolute, 64U * 1024U);
  if (!bytes) return core::Result<Output>{bytes.error()};
  if (core::sha256Hex(bytes.value()) != expectedSha256) return fail();
  const auto parsed = formats::parseJson(bytes.value(), {.maximumInputBytes = 64U * 1024U, .maximumDepth = 4U,
      .maximumNodes = 1024U, .maximumStringBytes = 4096U, .maximumCollectionEntries = 256U});
  if (!parsed || !parsed.value().isObject() || parsed.value().asObject().size() != 3U) return fail();
  const auto& root = parsed.value();
  if (!root.find("formatId") || !root.find("formatId")->isString() || root.find("formatId")->asString() != "com.project-seam.generation-batch" ||
      !root.find("schemaVersion") || !root.find("schemaVersion")->isInteger() || root.find("schemaVersion")->asInt64() != 1 ||
      !root.find("jobs") || !root.find("jobs")->isArray() || root.find("jobs")->asArray().empty()) return fail();
  Output result;
  for (const auto& entry : root.find("jobs")->asArray()) {
    if (!entry.isObject() || entry.asObject().size() != 2U || !entry.find("directory") || !entry.find("directory")->isString() ||
        !entry.find("manifestSha256") || !entry.find("manifestSha256")->isString()) return fail();
    const auto& directory = entry.find("directory")->asString();
    const auto& hash = entry.find("manifestSha256")->asString();
    if (directory.empty() || std::any_of(directory.begin(), directory.end(), [](unsigned char c) { return c < 32U || c == 127U; }) ||
        hash.size() != 64U || !std::all_of(hash.begin(), hash.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) return fail();
    result.push_back({(absolute.parent_path() / directory).lexically_normal(), hash});
  }
  return result;
}

core::Result<std::vector<voicebank_production::GeneratedCandidateInput>> inspectGenerationBatch(std::span<const GenerationJobReference> jobs,
    GenerationBatchLimits limits, std::stop_token stopToken) {
  using Output = std::vector<voicebank_production::GeneratedCandidateInput>;
  const auto cancelled = [] { return core::failure<Output>(core::ErrorCode::Conflict, "Generation batch cancelled; completed job output is retained"); };
  if (stopToken.stop_requested()) return cancelled();
  if (limits.maximumJobs == 0U || limits.maximumJobs > 256U || limits.maximumFrames == 0U ||
      limits.maximumFrames > 256ULL * 32ULL * 1024ULL * 1024ULL || jobs.empty() || jobs.size() > limits.maximumJobs)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Generation batch exceeds job or frame limits");
  for (const auto& reference : jobs) if (reference.directory.native().size() > 4096U || reference.manifestSha256.size() != 64U)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Generation batch reference exceeds bounds");
  const std::vector<GenerationJobReference> inputs(jobs.begin(), jobs.end());
  std::set<std::string> ids, takes;
  // Version-2 expectations own language/style. Legacy expectations have no
  // language and must retain their original style-free assignment identity.
  std::set<voicebank_production::ProductionUnitIdentity> assignments;
  std::string producerState;
  std::uint64_t frames = 0U;
  Output prepared;
  for (const auto& reference : inputs) {
    if (stopToken.stop_requested()) return cancelled();
    const auto job = loadGenerationJob(reference.directory, reference.manifestSha256);
    if (!job) return core::Result<Output>{job.error()};
    const auto& expected = job.value().expectation;
    if (producerState.empty()) producerState = expected.projectStateSha256;
    const auto count = static_cast<std::uint64_t>(expected.frameCount);
    if (expected.projectStateSha256 != producerState || !ids.insert(job.value().jobId).second || !takes.insert(expected.takeId).second ||
        !assignments.insert({expected.language, expected.language.empty() ? std::string{} : expected.style,
            expected.coverageKey, expected.pitchLayer}).second || count > limits.maximumFrames - frames)
      return core::failure<Output>(core::ErrorCode::Conflict, "Generation batch has duplicate targets, different producer states or excessive frame work");
    frames += count;
    const auto& snapshot = job.value().snapshot;
    const auto prefix = reference.directory / "output/candidates" / (snapshot.trackId.toString() + "-" + snapshot.segment.regionId.toString());
    prepared.push_back({prefix.string() + ".json", prefix.string() + ".wav",
        std::get<synthesis::ProceduralSingerResource>(snapshot.resource), expected});
  }
  return prepared;
}

core::Result<std::vector<GenerationJobOutput>> runGenerationBatch(std::span<const GenerationJobReference> jobs,
    GenerationBatchLimits limits, std::stop_token stopToken, std::function<void(std::size_t, std::size_t)> progress) {
  using Output = std::vector<GenerationJobOutput>;
  {
    const auto valid = inspectGenerationBatch(jobs, limits, stopToken);
    if (!valid) return core::Result<Output>{valid.error()};
  }
  const std::vector<GenerationJobReference> inputs(jobs.begin(), jobs.end());
  const auto cancelled = [] { return core::failure<Output>(core::ErrorCode::Conflict, "Generation batch cancelled; completed job output is retained"); };
  Output outputs;
  outputs.reserve(inputs.size());
  for (const auto& reference : inputs) {
    if (stopToken.stop_requested()) return cancelled();
    auto output = runGenerationJob(reference.directory, reference.manifestSha256, stopToken);
    if (!output) return core::Result<Output>{output.error()};
    outputs.push_back(std::move(output.value()));
    if (progress) progress(outputs.size(), inputs.size());
  }
  if (stopToken.stop_requested()) return cancelled();
  return outputs;
}
}
