// SEAM production neural worker: admitted acoustic -> vocoder inference.
//
// Launch contract, selected by the application and never by a bank:
//   seam-neural-worker --seam-neural-worker-v2 BUNDLE_DIR MODEL_ID MODEL_VERSION \
//       BUNDLE_CONTENT_HASH MAXIMUM_BUNDLE_BYTES
// One SNW1 request frame on stdin, exactly one SNW1 response frame on stdout.
//
// Structural admission of the exact bundle bytes the child loads precedes every
// inference session. This executable never serves the v1 transport fixture
// contract, so a probe cannot satisfy the production launch by accident.
#include "seam/core/sha256.hpp"
#include "seam/core/standard_stream_mode.hpp"
#include "seam/neural_synthesis/bundle_metadata.hpp"
#include "seam/neural_synthesis/diffsinger_inputs.hpp"
#include "seam/neural_synthesis/worker_protocol.hpp"

#include <onnxruntime_cxx_api.h>
#if defined(SEAM_NATIVE_GRAPH_INSPECTION)
#include "inspection.hpp"
#include "pair_contract.hpp"
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using seam::core::ErrorCode;
using seam::neural_synthesis::NeuralBundleMetadata;
using seam::neural_synthesis::NeuralRequest;
using seam::synthesis::FrozenNeuralBundle;
using seam::synthesis::FrozenSingerData;
using seam::synthesis::NeuralAssetRole;

constexpr std::size_t kMaximumFrameBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumBundleBytes = 512U * 1024U * 1024U;
constexpr std::size_t kMaximumDiagnosticBytes = 4096U;
// Diffusion steps are a model-configuration property. The admitted configuration
// schema does not yet carry them, so this pinned diagnostic value is explicit and
// reported through the backend identity until a configuration revision owns it.
constexpr std::int64_t kInferenceSteps = 10;
constexpr const char* kBackendId = "seam-neural-worker-acoustic-vocoder-1";

int fail(int code,std::string_view message) {
  std::cerr.write(message.data(),static_cast<std::streamsize>(std::min(message.size(),kMaximumDiagnosticBytes)));
  std::cerr.put('\n');
  return code;
}

seam::core::Result<std::string> readRequestFrame() {
  std::string frame;
  std::array<char,65536> block{};
  while (std::cin) {
    std::cin.read(block.data(),static_cast<std::streamsize>(block.size()));
    const auto count=static_cast<std::size_t>(std::cin.gcount());
    if (count>kMaximumFrameBytes-frame.size())
      return seam::core::failure<std::string>(ErrorCode::InvalidArgument,"Neural request frame exceeds the byte limit");
    frame.append(block.data(),count);
  }
  if (!std::cin.eof())
    return seam::core::failure<std::string>(ErrorCode::IoError,"Neural request frame read failed");
  return seam::core::success(std::move(frame));
}

struct TensorInterface final {
  const char* name;
  ONNXTensorElementDataType element;
  std::int64_t rank;
};

// Cross-checks the loaded session against the admitted declaration. Geometry is
// owned by native admission; this rejects a session whose interface disagrees
// with the metadata that authorized it.
bool sessionMatches(const Ort::Session& session,std::span<const TensorInterface> inputs,
    std::span<const TensorInterface> outputs) {
  if (session.GetInputCount()!=inputs.size() || session.GetOutputCount()!=outputs.size()) return false;
  Ort::AllocatorWithDefaultOptions allocator;
  for (std::size_t index=0;index<inputs.size();++index) {
    const auto name=session.GetInputNameAllocated(index,allocator);
    if (!name || std::string_view{name.get()}!=inputs[index].name) return false;
    const auto type=session.GetInputTypeInfo(index);
    if (type.GetONNXType()!=ONNX_TYPE_TENSOR) return false;
    const auto tensor=type.GetTensorTypeAndShapeInfo();
    const auto shape=tensor.GetShape();
    if (tensor.GetElementType()!=inputs[index].element ||
        static_cast<std::int64_t>(shape.size())!=inputs[index].rank) return false;
    if (!shape.empty() && shape.front()!=1) return false;
  }
  for (std::size_t index=0;index<outputs.size();++index) {
    const auto name=session.GetOutputNameAllocated(index,allocator);
    if (!name || std::string_view{name.get()}!=outputs[index].name) return false;
    const auto type=session.GetOutputTypeInfo(index);
    if (type.GetONNXType()!=ONNX_TYPE_TENSOR) return false;
    const auto tensor=type.GetTensorTypeAndShapeInfo();
    const auto shape=tensor.GetShape();
    if (tensor.GetElementType()!=outputs[index].element ||
        static_cast<std::int64_t>(shape.size())!=outputs[index].rank) return false;
    if (!shape.empty() && shape.front()!=1) return false;
  }
  return true;
}

const FrozenSingerData* asset(const FrozenNeuralBundle& bundle,NeuralAssetRole role) {
  for (const auto& entry:bundle.assets()) if (entry.role==role) return entry.data.get();
  return nullptr;
}

// Returns the padded mono vocoder buffer. Trimming the final partial hop and
// applying sample-domain dynamics happen exactly once, in response finalization.
seam::core::Result<std::vector<float>> executePadded(const FrozenNeuralBundle& bundle,
    const NeuralBundleMetadata& metadata,const NeuralRequest& request) {
  using Output=std::vector<float>;
  const auto rejected=[](ErrorCode code,const char* message) {return seam::core::failure<Output>(code,message);};
  const auto* acoustic=asset(bundle,NeuralAssetRole::Acoustic);
  const auto* vocoder=asset(bundle,NeuralAssetRole::Vocoder);
  if (!acoustic || !vocoder) return rejected(ErrorCode::InvalidArgument,"Neural bundle graphs are missing");
  const auto acousticBytes=acoustic->bytes(),vocoderBytes=vocoder->bytes();
#if defined(SEAM_NATIVE_GRAPH_INSPECTION)
  {
    const auto inspect=[&](std::span<const std::byte> data,onnx::ModelProto& model,google::protobuf::Struct& report) {
      return inspectBytes({reinterpret_cast<const char*>(data.data()),data.size()},model,report);
    };
    onnx::ModelProto acousticModel,vocoderModel;
    google::protobuf::Struct report;
    if (inspect(acousticBytes,acousticModel,report) || inspect(vocoderBytes,vocoderModel,report))
      return rejected(ErrorCode::InvalidArgument,"Native graph admission rejected the bundle bytes");
    if (!pairContract(acousticModel,vocoderModel,metadata.features.bins,metadata.features.layout,
        metadata.stepsLayout,metadata.vocoderOutput,metadata.features.hopSize,
        static_cast<unsigned>(metadata.model.maximumFrames)))
      return rejected(ErrorCode::InvalidArgument,"Admitted acoustic and vocoder graphs disagree on their interface");
  }
#else
  return rejected(ErrorCode::Unsupported,
      "This build has no native graph admission support; it cannot execute an admitted bundle");
#endif
  auto prepared=seam::neural_synthesis::prepareDiffSingerAcousticInputs(
      request,metadata.model,metadata.vocabulary,kInferenceSteps);
  if (!prepared) return seam::core::Result<Output>{prepared.error()};
  auto& inputs=prepared.value();
  if (inputs.tokens.empty() || inputs.durations.size()!=inputs.tokens.size() || inputs.f0Hz.empty())
    return rejected(ErrorCode::InvalidArgument,"Prepared acoustic inputs are empty or inconsistent");
  const auto frames=static_cast<std::int64_t>(inputs.f0Hz.size());
  const auto padded=static_cast<std::size_t>(frames)*metadata.model.hopSize;
  if (padded!=inputs.paddedSampleFrames || padded==0U)
    return rejected(ErrorCode::InvalidArgument,"Prepared acoustic geometry disagrees with the padded hop contract");
  if (padded>kMaximumFrameBytes/static_cast<std::size_t>(sizeof(float)))
    return rejected(ErrorCode::InvalidArgument,"Padded audio exceeds the worker byte budget");
  Ort::Env environment{ORT_LOGGING_LEVEL_WARNING,"seam-neural-worker"};
  environment.DisableTelemetryEvents();
  Ort::SessionOptions options;
  options.SetIntraOpNumThreads(1);
  options.SetInterOpNumThreads(1);
  options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
  const auto open=[&](std::span<const std::byte> bytes) {
    return Ort::Session{environment,bytes.data(),bytes.size(),options};
  };
  Ort::Session acousticSession=open(acousticBytes);
  Ort::Session vocoderSession=open(vocoderBytes);
  const auto vectorSteps=metadata.stepsLayout=="vector1";
  const bool hasBreathinessInput=acousticSession.GetInputCount()==5;
  if (!hasBreathinessInput && !request.breathiness.empty())
    return rejected(ErrorCode::Unsupported,
        "The admitted acoustic graph cannot consume request breathiness conditioning");
  const std::array acousticInputs4{TensorInterface{"tokens",ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,2},
      TensorInterface{"durations",ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,2},
      TensorInterface{"f0",ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,2},
      TensorInterface{"steps",ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,vectorSteps?1:0}};
  const std::array acousticInputs5{TensorInterface{"tokens",ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,2},
      TensorInterface{"durations",ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,2},
      TensorInterface{"f0",ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,2},
      TensorInterface{"steps",ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,vectorSteps?1:0},
      TensorInterface{"breathiness",ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,2}};
  const std::array acousticOutputs{TensorInterface{"mel",ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,3}};
  const std::array vocoderInputs{TensorInterface{"mel",ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,3},
      TensorInterface{"f0",ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,2}};
  const std::array vocoderOutputs{TensorInterface{metadata.vocoderOutput.c_str(),ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,2}};
  if (!sessionMatches(acousticSession,hasBreathinessInput?std::span<const TensorInterface>{acousticInputs5}:std::span<const TensorInterface>{acousticInputs4},acousticOutputs) ||
      !sessionMatches(vocoderSession,vocoderInputs,vocoderOutputs))
    return rejected(ErrorCode::InvalidArgument,"Loaded inference sessions disagree with the admitted graph interface");
  auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
  const std::array<std::int64_t,2> sequenceShape{1,static_cast<std::int64_t>(inputs.tokens.size())};
  const std::array<std::int64_t,2> frameShape{1,frames};
  std::int64_t steps=inputs.steps;
  const std::array<std::int64_t,1> vectorStepShape{1};
  std::vector<Ort::Value> acousticValues;
  acousticValues.push_back(Ort::Value::CreateTensor<std::int64_t>(memory,inputs.tokens.data(),
      inputs.tokens.size(),sequenceShape.data(),sequenceShape.size()));
  acousticValues.push_back(Ort::Value::CreateTensor<std::int64_t>(memory,inputs.durations.data(),
      inputs.durations.size(),sequenceShape.data(),sequenceShape.size()));
  acousticValues.push_back(Ort::Value::CreateTensor<float>(memory,inputs.f0Hz.data(),
      inputs.f0Hz.size(),frameShape.data(),frameShape.size()));
  acousticValues.push_back(Ort::Value::CreateTensor<std::int64_t>(memory,&steps,1,
      vectorSteps?vectorStepShape.data():nullptr,vectorSteps?1U:0U));
  std::vector<const char*> acousticNames{"tokens","durations","f0","steps"};
  if (hasBreathinessInput) {
    if (inputs.breathiness.size()!=static_cast<std::size_t>(frames))
      inputs.breathiness.assign(static_cast<std::size_t>(frames),0.0F);
    acousticValues.push_back(Ort::Value::CreateTensor<float>(memory,inputs.breathiness.data(),
        inputs.breathiness.size(),frameShape.data(),frameShape.size()));
    acousticNames.push_back("breathiness");
  }
  const char* melName="mel";
  auto mel=acousticSession.Run(Ort::RunOptions{nullptr},acousticNames.data(),acousticValues.data(),
      acousticValues.size(),&melName,1);
  if (mel.size()!=1U || !mel.front().IsTensor()) return rejected(ErrorCode::InvalidArgument,"Acoustic inference returned no mel tensor");
  const auto melInfo=mel.front().GetTensorTypeAndShapeInfo();
  const auto melShape=melInfo.GetShape();
  if (melInfo.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || melShape.size()!=3U ||
      melShape[0]!=1 || melShape[1]!=frames || melShape[2]!=static_cast<std::int64_t>(metadata.features.bins))
    return rejected(ErrorCode::InvalidArgument,"Acoustic inference returned an unexpected mel geometry");
  {
    const auto* values=mel.front().GetTensorData<float>();
    const auto count=static_cast<std::size_t>(melShape[1])*static_cast<std::size_t>(melShape[2]);
    for (std::size_t index=0;index<count;++index)
      if (!std::isfinite(values[index])) return rejected(ErrorCode::InvalidArgument,"Acoustic inference returned nonfinite mel values");
  }
  std::vector<Ort::Value> vocoderValues;
  vocoderValues.push_back(std::move(mel.front()));
  vocoderValues.push_back(Ort::Value::CreateTensor<float>(memory,inputs.f0Hz.data(),
      inputs.f0Hz.size(),frameShape.data(),frameShape.size()));
  const std::array<const char*,2> vocoderNames{"mel","f0"};
  const char* audioName=metadata.vocoderOutput.c_str();
  auto audio=vocoderSession.Run(Ort::RunOptions{nullptr},vocoderNames.data(),vocoderValues.data(),
      vocoderValues.size(),&audioName,1);
  if (audio.size()!=1U || !audio.front().IsTensor()) return rejected(ErrorCode::InvalidArgument,"Vocoder inference returned no audio tensor");
  const auto audioInfo=audio.front().GetTensorTypeAndShapeInfo();
  const auto audioShape=audioInfo.GetShape();
  if (audioInfo.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || audioShape.size()!=2U ||
      audioShape[0]!=1 || audioShape[1]!=static_cast<std::int64_t>(padded))
    return rejected(ErrorCode::InvalidArgument,"Vocoder inference returned an unexpected audio geometry");
  const auto* samples=audio.front().GetTensorData<float>();
  const auto count=static_cast<std::size_t>(audioShape[1]);
  // Ort::Value owns this buffer; the returned copy keeps the padded tail until
  // finalization validates and trims it against the exact requested range.
  return seam::core::success(Output{samples,samples+count});
}
}

int main(int argc,char** argv) {
  if (!seam::core::useBinaryStandardStreams()) return fail(2,"Cannot configure binary standard streams");
  using namespace seam::neural_synthesis;
  if (argc!=7 || std::string_view{argv[1]}!="--seam-neural-worker-v2")
    return fail(2,"Usage: seam-neural-worker --seam-neural-worker-v2 BUNDLE_DIR MODEL_ID MODEL_VERSION "
                   "BUNDLE_CONTENT_HASH MAXIMUM_BUNDLE_BYTES");
  std::size_t budget{};
  const std::string_view budgetText{argv[6]};
  const auto parsed=std::from_chars(budgetText.data(),budgetText.data()+budgetText.size(),budget);
  if (parsed.ec!=std::errc{} || parsed.ptr!=budgetText.data()+budgetText.size() ||
      budget==0U || budget>kMaximumBundleBytes)
    return fail(3,"Neural worker bundle byte budget is invalid");
  const auto frame=readRequestFrame();
  if (!frame) return fail(6,frame.error().message);
  // The child re-admits the bytes it actually loads. The launch identity above
  // is an expectation, never an authorization of whatever is on disk now.
  const auto bundle=loadNeuralBundleDirectory(argv[2],
      {seam::domain::SingerResourceKind::Neural,argv[3],argv[4],argv[5]},budget);
  if (!bundle) return fail(4,bundle.error().message);
  const auto metadata=inspectNeuralBundleMetadata(bundle.value());
  if (!metadata) return fail(5,metadata.error().message);
  const auto request=decodeRequest(std::as_bytes(std::span{frame.value().data(),frame.value().size()}));
  if (!request) return fail(7,request.error().message);
  const auto& value=request.value();
  const auto& identity=bundle.value().identity();
  if (value.bundleContentHash!=identity.contentHash || value.modelId!=identity.id ||
      value.modelVersion!=identity.version || value.modelContentHash!=identity.contentHash ||
      !value.conditioning || value.vocabularySize!=metadata.value().vocabulary.size() ||
      value.conditioning->vocabularyHash!=metadata.value().model.vocabularyHash ||
      !metadata.value().model.validateRequest(value))
    return fail(7,"Neural worker request does not match the admitted bundle identity");
  const auto audio=executePadded(bundle.value(),metadata.value(),value);
  if (!audio) return fail(8,audio.error().message);
  const auto response=finalizeDiffSingerResponse(value,metadata.value().model,audio.value(),kBackendId);
  if (!response) return fail(8,response.error().message);
  const auto encoded=encodeResponse(response.value());
  if (!encoded) return fail(9,encoded.error().message);
  std::cout.write(reinterpret_cast<const char*>(encoded.value().data()),
      static_cast<std::streamsize>(encoded.value().size()));
  std::cout.flush();
  return std::cout?0:10;
}
