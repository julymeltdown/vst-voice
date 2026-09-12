#include <onnxruntime_cxx_api.h>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <cstdint>
#include <vector>

// Real-runtime integration probe only. These tiny arithmetic graphs are not
// learned acoustic/vocoder models and this is not the admitted worker protocol.
int main(int argc,char** argv) {
  if (argc!=3) {std::cerr<<"Usage: seam_onnx_runtime_probe ACOUSTIC_FIXTURE VOCODER_FIXTURE\n"; return 2;}
  try {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING,"seam-inference-probe"};
    Ort::SessionOptions options;
    options.SetIntraOpNumThreads(1); options.SetInterOpNumThreads(1);
    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    const std::filesystem::path acousticPath{argv[1]},vocoderPath{argv[2]};
    Ort::Session acoustic{environment,acousticPath.c_str(),options};
    Ort::Session vocoder{environment,vocoderPath.c_str(),options};
    if (acoustic.GetInputCount()!=1U || acoustic.GetOutputCount()!=1U ||
        vocoder.GetInputCount()!=1U || vocoder.GetOutputCount()!=1U) return 3;
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
}
