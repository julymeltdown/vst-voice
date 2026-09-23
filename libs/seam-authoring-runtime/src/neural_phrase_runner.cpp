#include "seam/authoring/neural_phrase_runner.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"

#include <limits>
#include <utility>

namespace seam::authoring {
namespace {
constexpr std::size_t kMaximumManifestBytes=32768U;
constexpr std::uint64_t kMaximumRequestId=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
}

core::Result<void> NeuralPhraseRunnerOptions::validate() const {
  if (bundleDirectory.empty() || !bundleDirectory.is_absolute())
    return core::failure(core::ErrorCode::InvalidArgument,
        "Neural runner requires an absolute canonical bundle directory");
  if (maximumBundleBytes==0U || maximumBundleBytes>512U*1024U*1024U)
    return core::failure(core::ErrorCode::InvalidArgument,
        "Neural runner requires a bounded bundle payload budget");
  // The bundle launch contract is the only admitted production contract.
  if (worker.protocolVersion!=2U)
    return core::failure(core::ErrorCode::Unsupported,
        "Neural runner requires the bundle worker launch contract");
  if (worker.helper.empty() || !worker.helper.is_absolute() || worker.helperContentHash.size()!=64U)
    return core::failure(core::ErrorCode::InvalidArgument,
        "Neural runner requires an absolute helper and its expected digest");
  if (worker.maximumResidentBytes==0U || worker.maximumCpuTime.count()<=0 || worker.timeout.count()<=0)
    return core::failure(core::ErrorCode::InvalidArgument,
        "Neural runner requires measured process budgets");
  if (silencePhone.empty() || silencePhone.size()>64U)
    return core::failure(core::ErrorCode::InvalidArgument,
        "Neural runner requires an explicit bounded silence symbol");
  return core::success();
}

core::Result<AuthoringNeuralPhraseRunner> AuthoringNeuralPhraseRunner::create(
    NeuralPhraseRunnerOptions options,std::stop_token stop) {
  using Output=AuthoringNeuralPhraseRunner;
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,
      "Neural runner creation cancelled");
  const auto valid=options.validate();
  if (!valid) return core::Result<Output>{valid.error()};
  std::error_code error;
  const auto canonical=std::filesystem::canonical(options.bundleDirectory,error);
  if (error || !std::filesystem::is_directory(canonical,error) || error)
    return core::failure<Output>(core::ErrorCode::NotFound,
        "Neural runner bundle directory is unavailable");
  if (canonical!=options.bundleDirectory) return core::failure<Output>(core::ErrorCode::InvalidArgument,
      "Neural runner bundle directory must already be canonical");
  const auto manifest=core::readFileBytesLimited(canonical/"manifest.json",kMaximumManifestBytes);
  if (!manifest) return core::Result<Output>{manifest.error()};
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,
      "Neural runner creation cancelled");
  return core::success(Output{std::move(options),core::sha256Hex(manifest.value())});
}

core::Result<synthesis::PhraseRenderResult> AuthoringNeuralPhraseRunner::render(
    const rendering::RenderSnapshot& snapshot,std::stop_token stopToken) const {
  using Output=synthesis::PhraseRenderResult;
  if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,
      "Neural phrase rendering cancelled");
  if (!snapshot.neuralExecution || !snapshot.neuralExecution->valid() || !snapshot.compiledPerformance ||
      !snapshot.phonemes || !snapshot.pronunciationIdentity || snapshot.compiledPerformance->notes().empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument,"Neural snapshot is incomplete");
  const auto& execution=snapshot.neuralExecution->execution();
  // The child re-admits whatever the directory now holds. Refuse to hand it a
  // bundle that differs from the one this snapshot admitted.
  if (execution.bundleContentHash!=bundleContentHash_)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Neural snapshot bundle differs from the selected runner bundle");
  const auto& metadata=snapshot.neuralExecution->metadata();
  if (!metadata.vocabulary.tokenId(options_.silencePhone)) return core::failure<Output>(
      core::ErrorCode::Unsupported,"Admitted vocabulary has no explicit silence symbol");
  const auto& performance=*snapshot.compiledPerformance;
  const auto context=performance.phoneticContext();
  if (!context) return core::Result<Output>{context.error()};
  const auto full=context.value();
  const auto range=snapshot.ownedFrames.value_or(full);
  const auto outputValid=synthesis::PhraseOutputContract{snapshot.sampleRate,full,range}.validate();
  if (!outputValid) return core::Result<Output>{outputValid.error()};
  const auto requestId=nextRequestId_->fetch_add(1U);
  if (requestId==0U || requestId>kMaximumRequestId) return core::failure<Output>(
      core::ErrorCode::InvalidState,"Neural runner exhausted its request identity range");
  auto prepared=neural_synthesis::prepareNeuralScoreRequest(requestId,metadata.model,
      metadata.vocabulary,performance,snapshot.phonemes->tokens,
      snapshot.pronunciationIdentity->sequenceHash,full.start,full.end,options_.silencePhone,
      options_.worker.limits,stopToken);
  if (!prepared) return core::Result<Output>{prepared.error()};
  // Building the score request is model-agnostic; the bundle launch contract is
  // what binds this request to the admitted bundle identity.
  auto request=std::move(prepared).value();
  request.bundleContentHash=execution.bundleContentHash;
  const auto requestValid=metadata.model.validateRequest(request,options_.worker.limits);
  if (!requestValid) return core::Result<Output>{requestValid.error()};
  auto worker=options_.worker;
  worker.inferenceSteps=execution.inferenceSteps;
  const auto run=neural_synthesis::runNeuralBundleWorker(request,options_.bundleDirectory,
      options_.maximumBundleBytes,std::move(worker),stopToken);
  if (!run) return core::Result<Output>{run.error()};
  const auto& response=run.value().response;
  if (response.frameCount!=request.frameCount || response.channels!=1U ||
      response.sampleRate!=snapshot.sampleRate)
    return core::failure<Output>(core::ErrorCode::Conflict,
        "Neural worker response does not match the requested phrase window");
  if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,
      "Neural phrase rendering cancelled");
  // A model result carries audio only: no sample unit plan and no markers.
  // Ownership is a publication crop, not an acoustic context boundary. Every
  // chunk uses the same full phrase conditioning, including preceding phones.
  const auto begin=static_cast<std::size_t>(range.start-full.start);
  const auto end=static_cast<std::size_t>(range.end-full.start);
  if (response.pcm.size()!=static_cast<std::size_t>(full.end-full.start))
    return core::failure<Output>(core::ErrorCode::Conflict,"Neural PCM length differs from full context");
  return core::success(Output{.audio={range.start,std::vector<float>(
      response.pcm.begin()+static_cast<std::ptrdiff_t>(begin),
      response.pcm.begin()+static_cast<std::ptrdiff_t>(end))},.placements={}});
}

}  // namespace seam::authoring
