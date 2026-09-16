#include "seam/neural_synthesis/diffsinger_inputs.hpp"

#include <algorithm>
#include <cmath>
#include "seam/core/sha256.hpp"

namespace seam::neural_synthesis {

core::Result<DiffSingerAcousticInputs> prepareDiffSingerAcousticInputs(
    const NeuralRequest& request,const ModelContract& model,
    const NeuralVocabulary& vocabulary,std::int64_t steps,std::stop_token stop) {
  using Output=DiffSingerAcousticInputs;
  const auto cancelled=[] {return core::failure<Output>(core::ErrorCode::Conflict,
      "DiffSinger input preparation cancelled");};
  if (stop.stop_requested()) return cancelled();
  const auto checked=model.validateRequest(request);
  if (!checked) return core::Result<Output>{checked.error()};
  if (!request.conditioning || vocabulary.contentHash()!=model.vocabularyHash ||
      vocabulary.size()!=request.vocabularySize || steps<1 || steps>1000)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "DiffSinger requires verified phonetic conditioning and 1 to 1000 inference steps");
  const auto hop=static_cast<std::uint64_t>(model.hopSize);
  const auto count=(request.frameCount+hop-1U)/hop;
  Output result{.tokens={},.durations={},.f0Hz={},.steps=steps,
      .outputSampleFrames=request.frameCount,.paddedSampleFrames=count*hop};
  result.tokens.reserve(request.conditioning->spans.size());
  result.durations.reserve(request.conditioning->spans.size());
  // Quantize cumulative boundaries so duration error does not accumulate.
  // The final boundary covers the partial final hop; the vocoder owner must trim.
  std::uint64_t previous=0U;
  for (const auto& span:request.conditioning->spans) {
    if (stop.stop_requested()) return cancelled();
    if (span.tokenId==0U)
      return core::failure<Output>(core::ErrorCode::Unsupported,
          "DiffSinger token 0 is padding; explicit silence and phones require nonzero vocabulary IDs");
    const auto boundary=span.endFrame==request.frameCount?count:(span.endFrame+hop/2U)/hop;
    if (boundary<=previous)
      return core::failure<Output>(core::ErrorCode::Unsupported,
          "DiffSinger hop quantization would erase a phoneme; resolve timing or use another compatible model");
    result.tokens.push_back(static_cast<std::int64_t>(span.tokenId));
    result.durations.push_back(static_cast<std::int64_t>(boundary-previous));
    previous=boundary;
  }
  result.f0Hz.resize(static_cast<std::size_t>(count));
  result.breathiness.assign(static_cast<std::size_t>(count), 0.0F);
  std::uint64_t frame=0U;
  for (std::size_t phone=0U;phone<result.tokens.size();++phone) {
    const auto& span=request.conditioning->spans[phone];
    for (std::int64_t offset=0;offset<result.durations[phone];++offset,++frame) {
      if ((frame&4095U)==0U && stop.stop_requested()) return cancelled();
      // Keep a rounded boundary's feature on its owning phone, including explicit
      // silence/unvoiced intervals. The score compiler already applied vibrato.
      const auto sample=std::clamp(frame*hop+hop/2U,span.startFrame,span.endFrame-1U);
      result.f0Hz[static_cast<std::size_t>(frame)]=request.f0Hz[static_cast<std::size_t>(sample)];
      if (!request.breathiness.empty() && sample < request.breathiness.size()) {
        result.breathiness[static_cast<std::size_t>(frame)]=request.breathiness[static_cast<std::size_t>(sample)];
      }
    }
  }
  if (stop.stop_requested()) return cancelled();
  return result;
}

core::Result<std::vector<float>> finalizeDiffSingerAudio(
    const NeuralRequest& request,const ModelContract& model,
    std::span<const float> paddedAudio,std::stop_token stop) {
  using Output=std::vector<float>;
  const auto cancelled=[] {return core::failure<Output>(core::ErrorCode::Conflict,
      "DiffSinger output finalization cancelled");};
  if (stop.stop_requested()) return cancelled();
  const auto checked=model.validateRequest(request);
  if (!checked) return core::Result<Output>{checked.error()};
  const auto expected=((request.frameCount+model.hopSize-1U)/model.hopSize)*model.hopSize;
  if (request.channels!=1U || paddedAudio.size()!=expected)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,"DiffSinger vocoder output shape differs from the mono hop contract");
  Output output(static_cast<std::size_t>(request.frameCount));
  for (std::size_t index=0;index<paddedAudio.size();++index) {
    if ((index&4095U)==0U && stop.stop_requested()) return cancelled();
    const float sample=paddedAudio[index];
    if (!std::isfinite(sample) || std::abs(sample)>1.0F)
      return core::failure<Output>(core::ErrorCode::InvalidArgument,"DiffSinger vocoder PCM is invalid, including padded tail");
    if (index<output.size()) {
      const auto gained=sample*request.dynamics[index];
      if (!std::isfinite(gained) || std::abs(gained)>1.0F)
        return core::failure<Output>(core::ErrorCode::InvalidArgument,"DiffSinger dynamics exceed normalized PCM range");
      output[index]=gained;
    }
  }
  if (stop.stop_requested()) return cancelled();
  return output;
}

core::Result<NeuralResponse> finalizeDiffSingerResponse(
    const NeuralRequest& request,const ModelContract& model,
    std::span<const float> paddedAudio,std::string backendId,std::stop_token stop) {
  auto audio=finalizeDiffSingerAudio(request,model,paddedAudio,stop);
  if (!audio) return core::Result<NeuralResponse>{audio.error()};
  const auto encoded=encodeRequest(request);
  if (!encoded) return core::Result<NeuralResponse>{encoded.error()};
  NeuralResponse response{.requestId=request.requestId,.backendId=std::move(backendId),
      .modelContentHash=request.modelContentHash,.sampleRate=request.sampleRate,
      .channels=request.channels,.frameCount=request.frameCount,.pcm=std::move(audio.value()),
      .requestContentHash=core::sha256Hex(std::span<const std::byte>{encoded.value()}),
      .bundleContentHash=request.bundleContentHash};
  const auto valid=response.validate();
  if (!valid) return core::Result<NeuralResponse>{valid.error()};
  if (stop.stop_requested()) return core::failure<NeuralResponse>(core::ErrorCode::Conflict,
      "DiffSinger response finalization cancelled");
  return response;
}

}  // namespace seam::neural_synthesis
