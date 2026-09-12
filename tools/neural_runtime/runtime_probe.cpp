#include <onnxruntime_cxx_api.h>
#include "seam/neural_synthesis/diffsinger_inputs.hpp"
#include "seam/neural_synthesis/bundle_metadata.hpp"
#include "seam/core/sha256.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {
// Bound and retain the bytes passed to ORT instead of reopening a pathname
// during session construction. This is not protobuf/operator admission.
std::vector<char> readGraph(const std::filesystem::path& path) {
  constexpr std::size_t maximumBytes=16U*1024U*1024U;
  std::ifstream stream(path,std::ios::binary);
  if (!stream) throw std::runtime_error("Cannot open graph fixture");
  std::vector<char> bytes;
  std::array<char,65536> block{};
  while (stream) {
    stream.read(block.data(),static_cast<std::streamsize>(block.size()));
    const auto count=static_cast<std::size_t>(stream.gcount());
    if (count>maximumBytes-bytes.size()) throw std::runtime_error("Graph fixture exceeds byte limit");
    bytes.insert(bytes.end(),block.data(),block.data()+count);
  }
  if (!stream.eof() || bytes.empty()) throw std::runtime_error("Graph fixture is empty or unreadable");
  return bytes;
}
bool matches(Ort::Session& session,const char* input,const char* output) {
  if (session.GetInputCount()!=1U || session.GetOutputCount()!=1U) return false;
  Ort::AllocatorWithDefaultOptions allocator;
  const auto inputName=session.GetInputNameAllocated(0,allocator);
  const auto outputName=session.GetOutputNameAllocated(0,allocator);
  if (std::string_view{inputName.get()}!=input || std::string_view{outputName.get()}!=output) return false;
  // Keep TypeInfo owners alive while inspecting their borrowed tensor views.
  const auto inputType=session.GetInputTypeInfo(0),outputType=session.GetOutputTypeInfo(0);
  for (const auto* type:{&inputType,&outputType}) {
    if (type->GetONNXType()!=ONNX_TYPE_TENSOR) return false;
    const auto tensor=type->GetTensorTypeAndShapeInfo();
    if (tensor.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
        tensor.GetShape()!=std::vector<std::int64_t>{1,4}) return false;
  }
  return true;
}
bool shapeMatches(const Ort::Value& value,const std::vector<std::int64_t>& shape) {
  if (!value.IsTensor()) return false;
  const auto info=value.GetTensorTypeAndShapeInfo();
  return info.GetElementType()==ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT && info.GetShape()==shape;
}
int pairedProfile(Ort::Session& acoustic,Ort::Session& vocoder,
    const seam::neural_synthesis::NeuralBundleMetadata& metadata) {
  using namespace seam::neural_synthesis;
  if (acoustic.GetInputCount()!=4 || acoustic.GetOutputCount()!=1 ||
      vocoder.GetInputCount()!=2 || vocoder.GetOutputCount()!=1) return 3;
  auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
  const auto& model=metadata.model;
  const auto& vocabulary=metadata.vocabulary;
  for (const std::int64_t frames:{3,5}) {
    NeuralRequest request;
    request.requestId=1; request.modelId=model.modelId; request.modelVersion=model.modelVersion;
    request.modelContentHash=model.modelContentHash; request.pronunciationHash=seam::core::sha256Hex("fixture-phones");
    request.frameCount=static_cast<std::uint64_t>(frames*256-37);
    request.f0Hz.resize(static_cast<std::size_t>(request.frameCount));
    request.dynamics.assign(request.f0Hz.size(),0.5F);
    for (std::size_t index=0;index<request.f0Hz.size();++index)
      request.f0Hz[index]=100.0F+10.0F*static_cast<float>(index/256);
    request.vocabularySize=4;
    request.conditioning=PhoneticConditioning{model.vocabularyHash,{{2,0,256},{3,256,request.frameCount}}};
    const auto encoded=encodeRequest(request);
    if (!encoded) throw std::runtime_error(encoded.error().message);
    const auto decoded=decodeRequest(encoded.value());
    if (!decoded) throw std::runtime_error(decoded.error().message);
    auto prepared=prepareDiffSingerAcousticInputs(decoded.value(),model,vocabulary,10);
    if (!prepared) throw std::runtime_error(prepared.error().message);
    auto& tokens=prepared.value().tokens; auto& durations=prepared.value().durations;
    auto& steps=prepared.value().steps; auto& f0=prepared.value().f0Hz;
    const std::array<std::int64_t,2> phoneShape{1,static_cast<std::int64_t>(tokens.size())},f0Shape{1,static_cast<std::int64_t>(f0.size())};
    std::vector<Ort::Value> inputs;
    inputs.push_back(Ort::Value::CreateTensor<std::int64_t>(memory,tokens.data(),tokens.size(),phoneShape.data(),2));
    inputs.push_back(Ort::Value::CreateTensor<std::int64_t>(memory,durations.data(),durations.size(),phoneShape.data(),2));
    inputs.push_back(Ort::Value::CreateTensor<float>(memory,f0.data(),f0.size(),f0Shape.data(),2));
    inputs.push_back(Ort::Value::CreateTensor<std::int64_t>(memory,&steps,1,nullptr,0));
    const std::array<const char*,4> names{"tokens","durations","f0","steps"};
    const char* melName="mel";
    auto mel=acoustic.Run(Ort::RunOptions{nullptr},names.data(),inputs.data(),inputs.size(),&melName,1);
    if (!shapeMatches(mel.front(),{1,frames,80})) return 5;
    const auto* features=mel.front().GetTensorData<float>();
    for (std::int64_t index=0;index<frames*80;++index) if (!std::isfinite(features[index])) return 6;
    std::vector<Ort::Value> vocoderInputs;
    vocoderInputs.push_back(std::move(mel.front()));
    vocoderInputs.push_back(Ort::Value::CreateTensor<float>(memory,f0.data(),f0.size(),f0Shape.data(),2));
    const std::array<const char*,2> vocoderNames{"mel","f0"};
    const char* audioName="audio";
    auto audio=vocoder.Run(Ort::RunOptions{nullptr},vocoderNames.data(),vocoderInputs.data(),2,&audioName,1);
    if (!shapeMatches(audio.front(),{1,frames*256})) return 5;
    const auto* samples=audio.front().GetTensorData<float>();
    for (std::int64_t index=0;index<frames*256;++index) {
      const float expected=f0[static_cast<std::size_t>(index/256)]*0.0011F+0.05F+
          static_cast<float>(frames)*0.001F+0.001F;
      if (!std::isfinite(samples[index]) || std::abs(samples[index]-expected)>1e-5F) return 6;
    }
    const auto finalized=finalizeDiffSingerResponse(decoded.value(),model,
        {samples,static_cast<std::size_t>(frames*256)},"paired-runtime-fixture");
    if (!finalized) return 5;
    const auto responseFrame=encodeResponse(finalized.value());
    if (!responseFrame) return 5;
    const auto response=decodeResponse(responseFrame.value());
    if (!response || response.value().frameCount!=request.frameCount ||
        response.value().requestContentHash!=seam::core::sha256Hex(std::span<const std::byte>{encoded.value()})) return 5;
    for (std::size_t index=0;index<response.value().pcm.size();++index)
      if (std::abs(response.value().pcm[index]-samples[index]*0.5F)>1e-6F) return 6;
  }
  std::cout<<"{\"status\":\"PAIRED_PROFILE_INFERENCE_ONLY\",\"cases\":2,\"releaseEligible\":false}\n";
  return 0;
}
seam::synthesis::FrozenNeuralBundle freezeFixtureBundle(
    std::span<const char> acoustic,std::span<const char> vocoder) {
  using namespace seam::synthesis;
  const std::string vocabulary=R"({"formatId":"com.project-seam.neural-vocabulary","schemaVersion":1,"tokens":["<PAD>","SP","a","i"]})";
  const std::string features=R"({"sampleRate":48000,"hopSize":256,"bins":80,"layout":"BTF","amplitudeScale":"ln-amplitude","multiplier":1.0,"offset":0.0,"minimumHz":40.0,"maximumHz":16000.0})";
  const std::string configuration=R"({"formatId":"com.project-seam.neural-bundle-configuration","schemaVersion":1,"maximumFrames":48000,"acousticFeatures":)"+
      features+",\"vocoderFeatures\":"+features+"}";
  const auto asset=[](NeuralAssetRole role,const char* name,std::span<const char> bytes) {
    return NeuralBundleAssetInput{role,name,std::as_bytes(bytes),seam::core::sha256Hex(std::as_bytes(bytes))};
  };
  const std::array assets{asset(NeuralAssetRole::Acoustic,"acoustic",acoustic),
      asset(NeuralAssetRole::Vocoder,"vocoder",vocoder),
      asset(NeuralAssetRole::Configuration,"configuration",std::span{configuration.data(),configuration.size()}),
      asset(NeuralAssetRole::Vocabulary,"vocabulary",std::span{vocabulary.data(),vocabulary.size()})};
  constexpr std::size_t limit=33U*1024U*1024U;
  const auto manifest=FrozenNeuralBundle::manifest(assets,limit);
  if (!manifest) throw std::runtime_error(manifest.error().message);
  auto bundle=FrozenNeuralBundle::freeze({seam::domain::SingerResourceKind::Neural,"paired-fixture","1",
      seam::core::sha256Hex(manifest.value())},assets,limit);
  if (!bundle) throw std::runtime_error(bundle.error().message);
  return std::move(bundle.value());
}
}

// Real-runtime integration probe only. These tiny arithmetic graphs are not
// learned acoustic/vocoder models and this is not the admitted worker protocol.
int main(int argc,char** argv) {
  const bool paired=argc==4 && std::string_view{argv[3]}=="--paired-profile";
  if (argc!=3 && !paired) {std::cerr<<"Usage: seam_onnx_runtime_probe ACOUSTIC_FIXTURE VOCODER_FIXTURE [--paired-profile]\n"; return 2;}
  try {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING,"seam-inference-probe"};
    Ort::SessionOptions options;
    options.SetIntraOpNumThreads(1); options.SetInterOpNumThreads(1);
    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    const std::filesystem::path acousticPath{argv[1]},vocoderPath{argv[2]};
    const auto acousticBytes=readGraph(acousticPath),vocoderBytes=readGraph(vocoderPath);
    if (paired) {
      const auto bundle=freezeFixtureBundle(acousticBytes,vocoderBytes);
      const auto metadata=seam::neural_synthesis::inspectNeuralBundleMetadata(bundle);
      if (!metadata) throw std::runtime_error(metadata.error().message);
      const auto bytes=[&](seam::synthesis::NeuralAssetRole role) {
        for (const auto& asset:bundle.assets()) if (asset.role==role) return asset.data->bytes();
        throw std::runtime_error("Frozen fixture graph is missing");
      };
      const auto frozenAcoustic=bytes(seam::synthesis::NeuralAssetRole::Acoustic);
      const auto frozenVocoder=bytes(seam::synthesis::NeuralAssetRole::Vocoder);
      Ort::Session acoustic{environment,frozenAcoustic.data(),frozenAcoustic.size(),options};
      Ort::Session vocoder{environment,frozenVocoder.data(),frozenVocoder.size(),options};
      return pairedProfile(acoustic,vocoder,metadata.value());
    }
    Ort::Session acoustic{environment,acousticBytes.data(),acousticBytes.size(),options};
    Ort::Session vocoder{environment,vocoderBytes.data(),vocoderBytes.size(),options};
    if (!matches(acoustic,"f0","mel") || !matches(vocoder,"mel","audio")) return 3;
    std::array<float,4> input{0.1F,0.2F,-0.3F,0.4F};
    std::array<std::int64_t,2> shape{1,4};
    auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
    auto tensor=Ort::Value::CreateTensor<float>(memory,input.data(),input.size(),shape.data(),shape.size());
    const char* acousticInput="f0"; const char* acousticOutput="mel";
    auto mel=acoustic.Run(Ort::RunOptions{nullptr},&acousticInput,&tensor,1,&acousticOutput,1);
    const char* vocoderInput="mel"; const char* vocoderOutput="audio";
    auto audio=vocoder.Run(Ort::RunOptions{nullptr},&vocoderInput,mel.data(),1,&vocoderOutput,1);
    if (!audio.front().IsTensor()) return 4;
    const auto info=audio.front().GetTensorTypeAndShapeInfo();
    if (info.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || info.GetShape()!=std::vector<std::int64_t>{1,4}) return 5;
    const auto* samples=audio.front().GetTensorData<float>();
    for (std::size_t index=0;index<input.size();++index)
      if (!std::isfinite(samples[index]) || std::abs(samples[index]-input[index]*0.5F)>1e-6F) return 6;
    std::cout<<"{\"status\":\"ARITHMETIC_INFERENCE_ONLY\",\"samples\":4,\"releaseEligible\":false}\n";
    return 0;
  } catch(const Ort::Exception& error) {std::cerr<<error.what()<<'\n'; return 1;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n'; return 7;}
}
