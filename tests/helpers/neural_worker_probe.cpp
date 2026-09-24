#include "seam/neural_synthesis/worker_protocol.hpp"
#include "seam/core/sha256.hpp"
#include "seam/core/standard_stream_mode.hpp"
#include "seam/neural_synthesis/neural_phrase_backend.hpp"
#include "seam/neural_synthesis/deployment_descriptor.hpp"
#include "seam/formats/json_value.hpp"

#include <array>
#include <cstdio>
#include <iostream>
#include <vector>
#include <thread>

int main(int argc, char** argv) {
  if (!seam::core::useBinaryStandardStreams()) return 2;
  if (argc==6 && std::string_view{argv[1]}=="--seam-neural-deployment-load-probe") {
    // Generated ephemeral key is test-only, never a release credential.
    const std::string_view version{argv[5]};
    if (version!="1" && version!="2" && version!="3") return 2;
    std::string json; char byte{};
    while (std::cin.get(byte)) {
      if (json.size()>=16U*1024U) return 6;
      json.push_back(byte);
    }
    if (!std::cin.eof()) return 7;
    const auto key=seam::distribution::generateSigningKeyPair(); if (!key) return 10;
    const auto signature=seam::distribution::signEd25519(
        std::as_bytes(std::span{json.data(),json.size()}),key.value().privateKey);
    if (!signature) return 11;
    const auto verified=seam::neural_synthesis::VerifiedNeuralDeployment::verify(json,
        signature.value(),key.value().publicKey,{argv[2],argv[3],argv[4],
            static_cast<std::uint32_t>(version.front()-'0')});
    if (!verified) return 12;
    static const char anchor=0;
    const auto loaded=verified.value().load(&anchor);
    if (!loaded) return 13;
    return loaded.value().protocolVersion==static_cast<std::uint32_t>(version.front()-'0')?0:14;
  }
  if (argc==2 && std::string_view{argv[1]}=="--seam-convert-vocabulary-probe") {
    std::string json;
    char byte{};
    while (std::cin.get(byte)) {
      if (json.size()>=4U*1024U*1024U) return 6;
      json.push_back(byte);
    }
    if (!std::cin.eof()) return 7;
    const auto converted=seam::neural_synthesis::convertDiffSingerVocabulary(json);
    if (!converted) {std::cerr<<converted.error().message; return 8;}
    std::cout<<converted.value();
    return std::cout?0:5;
  }
  if (argc==6 && std::string_view{argv[1]}=="--seam-neural-package-load-probe") {
    static const char anchor=0;
    // Ephemeral signing is fixture setup only. Production must receive its
    // signed descriptor and pinned release key from the deployment trust owner.
    const auto key=seam::distribution::generateSigningKeyPair(); if (!key) return 10;
    const auto descriptor=seam::formats::stringifyJson(seam::formats::JsonValue::Object{
        {"formatId","com.project-seam.neural-deployment"},{"schemaVersion",std::int64_t{1}},
        {"buildId",argv[5]},{"platform","macos-arm64"},{"surface","standalone"},
        {"modulePath",argv[2]},{"manifestPath",argv[3]},{"manifestSha256",argv[4]}});
    const auto signature=seam::distribution::signEd25519(
        std::as_bytes(std::span{descriptor.data(),descriptor.size()}),key.value().privateKey);
    if (!signature) return 11;
    const auto verified=seam::neural_synthesis::VerifiedNeuralDeployment::verify(descriptor,
        signature.value(),key.value().publicKey,{argv[5],"macos-arm64","standalone"});
    if (!verified) return 12;
    const auto loaded=verified.value().load(&anchor);
    if (!loaded) {std::cerr<<loaded.error().message; return 9;}
    return 0;
  }
  if (argc==3 && std::string_view{argv[1]}=="--seam-neural-package-probe") {
    std::string manifest;
    char byte{};
    while (std::cin.get(byte)) {
      if (manifest.size()>=256U*1024U) return 6;
      manifest.push_back(byte);
    }
    if (!std::cin.eof()) return 7;
    return seam::neural_synthesis::NeuralHelperPackage::decode(manifest,argv[2])?0:8;
  }
  if (argc != 2 || std::string_view{argv[1]} != "--seam-neural-worker-v1") return 2;
  const auto phase = [](const char* value) {
    std::fputs(value, stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
  };
  phase("worker-phase:start");
  std::string input;
  std::array<char, 65536U> block{};
  const auto maximum = seam::neural_synthesis::WorkerProtocolLimits{}.maximumFrameBytes;
  for (;;) {
    const auto count = std::fread(block.data(), 1U, block.size(), stdin);
    if (count > maximum - input.size()) return 6;
    input.append(block.data(), count);
    if (count != block.size()) {
      if (std::ferror(stdin)) return 7;
      if (std::feof(stdin)) break;
    }
  }
  phase("worker-phase:input-eof");
  const auto request = seam::neural_synthesis::decodeRequest(
      std::span<const std::byte>{reinterpret_cast<const std::byte*>(input.data()), input.size()});
  if (!request) return 3;
  phase("worker-phase:request-decoded");
  // Remain observable long enough for the parent's memory-limit regression.
  if (request.value().requestId==46U) std::this_thread::sleep_for(std::chrono::milliseconds{250});
  if (request.value().requestId==47U) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{1};
    while (std::chrono::steady_clock::now()<deadline) {}
  }
  seam::neural_synthesis::NeuralResponse response{
      .requestId = request.value().requestId,
      .backendId = "seam.test.neural.worker",
      .modelContentHash = request.value().modelContentHash,
      .sampleRate = request.value().sampleRate,
      .channels = request.value().channels,
      .frameCount = request.value().frameCount,
      .pcm = std::vector<float>(static_cast<std::size_t>(request.value().frameCount) * request.value().channels, 0.0F),
  };
  if (request.value().conditioning) {
    response.requestContentHash=seam::core::sha256Hex(input);
    // Deliberately faulty responses for runner rejection tests only.
    if (request.value().requestId==44U) response.requestContentHash=std::string(64U,'d');
    if (request.value().requestId==45U) response.requestContentHash.clear();
  }
  const auto encoded = seam::neural_synthesis::encodeResponse(response);
  if (!encoded) return 4;
  phase("worker-phase:response-encoded");
  const auto written = std::fwrite(encoded.value().data(), 1U, encoded.value().size(), stdout);
  const auto flushed = std::fflush(stdout);
  phase("worker-phase:output-flushed");
  return written == encoded.value().size() && flushed == 0 ? 0 : 5;
}
