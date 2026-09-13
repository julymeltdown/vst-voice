// Dedicated transport fixture. No graph execution or production admission.
#include "seam/neural_synthesis/bundle_metadata.hpp"
#include "seam/core/sha256.hpp"
#include "seam/core/file_io.hpp"
#include <charconv>
#include <iostream>
#include <thread>
#if !defined(_WIN32)
#include <unistd.h>
#endif

int main(int argc,char** argv) {
  using namespace seam::neural_synthesis;
  if (argc!=7 || std::string_view{argv[1]}!="--seam-neural-worker-v2") return 2;
  std::size_t budget{};
  const std::string_view text{argv[6]};
  const auto parsed=std::from_chars(text.data(),text.data()+text.size(),budget);
  if (parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size()) return 3;
  const auto bundle=loadNeuralBundleDirectory(argv[2],
      {seam::domain::SingerResourceKind::Neural,argv[3],argv[4],argv[5]},budget);
  if (!bundle) return 4;
  const auto metadata=inspectNeuralBundleMetadata(bundle.value());
  if (!metadata) return 5;
  std::string frame; char byte{};
  while (std::cin.get(byte)) {
    if (frame.size()>=64U*1024U*1024U) return 6;
    frame.push_back(byte);
  }
  if (!std::cin.eof()) return 6;
  const auto request=decodeRequest(std::as_bytes(std::span{frame.data(),frame.size()}));
  if (!request || request.value().bundleContentHash!=bundle.value().identity().contentHash ||
      !metadata.value().model.validateRequest(request.value()) ||
      request.value().vocabularySize!=metadata.value().vocabulary.size()) return 7;
#if !defined(_WIN32)
  if (request.value().requestId==93U || request.value().requestId==94U) {
    // Test-only readiness handshake, after actual child-side bundle/request load.
    const auto marker=std::filesystem::path{argv[2]}/("child-"+std::to_string(request.value().requestId));
    if (!seam::core::durableAtomicWriteTextNew(marker,std::to_string(getpid()))) return 10;
    std::this_thread::sleep_for(std::chrono::seconds{10});
  }
#endif
  NeuralResponse response{.requestId=request.value().requestId,.backendId="transport-fixture-only",
      .modelContentHash=request.value().modelContentHash,.sampleRate=request.value().sampleRate,
      .channels=request.value().channels,.frameCount=request.value().frameCount,
      .pcm=std::vector<float>(request.value().frameCount,0.0F),
      .requestContentHash=seam::core::sha256Hex(frame),.bundleContentHash=request.value().bundleContentHash};
  if (request.value().requestId==92U) response.bundleContentHash.clear();
  const auto encoded=encodeResponse(response);
  if (!encoded) return 8;
  std::cout.write(reinterpret_cast<const char*>(encoded.value().data()),static_cast<std::streamsize>(encoded.value().size()));
  return std::cout?0:9;
}
