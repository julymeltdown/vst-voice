#include "seam/neural_synthesis/worker_protocol.hpp"
#include "seam/core/sha256.hpp"
#include "seam/neural_synthesis/neural_phrase_backend.hpp"
#include "seam/neural_synthesis/deployment_descriptor.hpp"
#include "seam/formats/json_value.hpp"

#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
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
  const std::string input{std::istreambuf_iterator<char>{std::cin}, std::istreambuf_iterator<char>{}};
  const auto request = seam::neural_synthesis::decodeRequest(
      std::span<const std::byte>{reinterpret_cast<const std::byte*>(input.data()), input.size()});
  if (!request) return 3;
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
  std::cout.write(reinterpret_cast<const char*>(encoded.value().data()),
                  static_cast<std::streamsize>(encoded.value().size()));
  std::cerr << "neural worker probe\n";
  return std::cout ? 0 : 5;
}
