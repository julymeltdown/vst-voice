#pragma once
#include "seam/core/result.hpp"
#include <chrono>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace seam::authoring {
struct HelperProcessRequest final {
  std::filesystem::path executable;
  std::vector<std::string> arguments;
  std::chrono::milliseconds timeout{10000};
  std::size_t maximumStdoutBytes{1048576U};
  std::size_t maximumStderrBytes{65536U};
  // Bounded opaque bytes; never appended to argv or inherited from host stdin.
  std::string standardInput{};
  // Callers may opt into a larger bounded framed payload (for example the
  // neural worker protocol). The historical 4096-byte default remains the
  // safe limit for dictionary/reading helpers.
  std::size_t maximumStdinBytes{4096U};
  // Optional best-effort child resource ceilings. The POSIX runner samples
  // resident memory and CPU time while draining the channels; exceeding a
  // ceiling kills the owned process group. Zero disables that ceiling.
  std::size_t maximumResidentBytes{0U};
  std::chrono::milliseconds maximumCpuTime{0};
};
struct HelperProcessOutput final { std::string standardOutput, standardError; };
// Blocking worker-thread primitive for an explicitly selected application helper.
// No shell/PATH lookup; not binary/resource verification or a security sandbox.
// POSIX implementation; unsupported platforms fail explicitly.
// Requires exclusive ownership of child reaping (no external SIGCHLD handler
// or waitpid(-1) consumer). Plug-in hosts need a qualified supervisor boundary.
[[nodiscard]] core::Result<HelperProcessOutput> runBoundedHelperProcess(
    const HelperProcessRequest& request, std::stop_token stop = {});
}
