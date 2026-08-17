#include "seam/clap/session.hpp"
#include "seam/clap/state_codec.hpp"

#include <chrono>
#include <iostream>

int main() {
  const auto session = seam::clap::makeDiagnosticSession(48000U, 4U, 1.0);
  if (!session) return 1;
  constexpr int warmupIterations = 2;
  constexpr int iterations = 8;
  for (int iteration = 0; iteration < warmupIterations; ++iteration) {
    const auto encoded = seam::clap::encodeState(session.value());
    if (!encoded || !seam::clap::decodeState(encoded.value())) return 1;
  }
  const auto encodeStart = std::chrono::steady_clock::now();
  std::size_t bytes = 0U;
  for (int iteration = 0; iteration < iterations; ++iteration) {
    const auto encoded = seam::clap::encodeState(session.value());
    if (!encoded) return 1;
    bytes = encoded.value().size();
  }
  const auto encodeEnd = std::chrono::steady_clock::now();
  const auto state = seam::clap::encodeState(session.value());
  if (!state) return 1;
  const auto decodeStart = std::chrono::steady_clock::now();
  for (int iteration = 0; iteration < iterations; ++iteration) {
    if (!seam::clap::decodeState(state.value())) return 1;
  }
  const auto decodeEnd = std::chrono::steady_clock::now();
  const auto encodeMs = std::chrono::duration<double, std::milli>(encodeEnd - encodeStart).count();
  const auto decodeMs = std::chrono::duration<double, std::milli>(decodeEnd - decodeStart).count();
  std::cout << "{\n"
            << "  \"warmupIterations\": " << warmupIterations << ",\n"
            << "  \"iterations\": " << iterations << ",\n"
            << "  \"stateBytes\": " << bytes << ",\n"
            << "  \"encodeTotalMs\": " << encodeMs << ",\n"
            << "  \"decodeTotalMs\": " << decodeMs << "\n"
            << "}\n";
  return 0;
}
