#include "seam/core/sha256.hpp"
#include "seam/core/standard_stream_mode.hpp"
#include "seam/neural_synthesis/worker_protocol.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
  if (!seam::core::useBinaryStandardStreams() || argc != 2 ||
      std::string_view{argv[1]} != "--seam-neural-worker-v1")
    return 2;

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

  // Remain observable long enough for the parent's resource-limit regressions.
  if (request.value().requestId == 46U)
    std::this_thread::sleep_for(std::chrono::milliseconds{250});
  if (request.value().requestId == 47U) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
    while (std::chrono::steady_clock::now() < deadline) {}
  }

  seam::neural_synthesis::NeuralResponse response{
      .requestId = request.value().requestId,
      .backendId = "seam.test.neural.worker",
      .modelContentHash = request.value().modelContentHash,
      .sampleRate = request.value().sampleRate,
      .channels = request.value().channels,
      .frameCount = request.value().frameCount,
      .pcm = std::vector<float>(
          static_cast<std::size_t>(request.value().frameCount) * request.value().channels, 0.0F),
  };
  if (request.value().conditioning) {
    response.requestContentHash = seam::core::sha256Hex(input);
    if (request.value().requestId == 44U) response.requestContentHash = std::string(64U, 'd');
    if (request.value().requestId == 45U) response.requestContentHash.clear();
  }

  const auto encoded = seam::neural_synthesis::encodeResponse(response);
  if (!encoded) return 4;
  phase("worker-phase:response-encoded");
  const auto written = std::fwrite(encoded.value().data(), 1U, encoded.value().size(), stdout);
  const auto flushed = std::fflush(stdout);
  phase("worker-phase:output-flushed");
  return written == encoded.value().size() && flushed == 0 ? 0 : 5;
}
