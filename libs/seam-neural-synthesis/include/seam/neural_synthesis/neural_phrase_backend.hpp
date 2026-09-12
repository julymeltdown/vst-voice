#pragma once

#include "seam/core/result.hpp"
#include "seam/neural_synthesis/model_contract.hpp"
#include "seam/synthesis/performance_compiler.hpp"

#include <filesystem>
#include <chrono>
#include <stop_token>
#include <string>
#include <vector>

namespace seam::neural_synthesis {

struct NeuralPackageFile final {
  std::filesystem::path relativePath;
  std::string contentHash;
  std::uint64_t maximumBytes{256U * 1024U * 1024U};
};

// Typed deployment entry. The caller must obtain this from its trusted surface
// manifest and pass the loaded SEAM module, not the host application's path.
struct NeuralHelperPackage final {
  std::string buildId;
  std::uint32_t protocolVersion{1U};
  NeuralPackageFile module;
  NeuralPackageFile helper;
  std::vector<NeuralPackageFile> dependencies;

  [[nodiscard]] static core::Result<NeuralHelperPackage> decode(
      std::string_view json,std::string_view expectedContentHash);
};

struct NeuralWorkerRunOptions final {
  std::filesystem::path helper;
  // Expected identity supplied by the trusted deployment owner, never the bank.
  std::string helperContentHash;
  std::uint64_t maximumHelperBytes{256U * 1024U * 1024U};
  std::chrono::milliseconds timeout{10'000};
  WorkerProtocolLimits limits{};
  std::optional<NeuralVocabulary> vocabulary{};
};

struct NeuralWorkerResult final {
  NeuralResponse response;
  std::string diagnostic;
};

[[nodiscard]] core::Result<NeuralWorkerRunOptions> resolveNeuralHelperPackage(
    const std::filesystem::path& packageRoot,const std::filesystem::path& loadedModule,
    const NeuralHelperPackage& package,std::string_view expectedBuildId,
    std::stop_token stop = {});

[[nodiscard]] core::Result<NeuralWorkerRunOptions> resolveNeuralHelperForModule(
    const void* moduleAnchor,const NeuralHelperPackage& package,
    std::string_view expectedBuildId,std::stop_token stop = {});

// Paths/build/digest must come from the trusted surface deployment descriptor.
[[nodiscard]] core::Result<NeuralWorkerRunOptions> loadNeuralHelperForModule(
    const void* moduleAnchor,const std::filesystem::path& moduleRelativePath,
    const std::filesystem::path& manifestRelativePath,std::string_view expectedManifestHash,
    std::string_view expectedBuildId,std::stop_token stop = {});

[[nodiscard]] core::Result<NeuralRequest> prepareNeuralScoreRequest(
    std::uint64_t requestId,const ModelContract& model,const NeuralVocabulary& vocabulary,
    const synthesis::CompiledScorePerformance& performance,std::span<const domain::PhonemeToken> phones,
    std::string pronunciationHash,time::SampleFrame origin,time::SampleFrame end,
    std::string_view silencePhone,const WorkerProtocolLimits& limits = {},std::stop_token stop = {});

// Executes only a caller-selected absolute first-party helper. The helper is
// outside the audio callback; the request, response and model identity are
// checked again before a result is returned to the render owner.
[[nodiscard]] core::Result<NeuralWorkerResult> runNeuralWorker(
    const NeuralRequest& request, const ModelContract& model,
    NeuralWorkerRunOptions options, std::stop_token stop = {});

}  // namespace seam::neural_synthesis
