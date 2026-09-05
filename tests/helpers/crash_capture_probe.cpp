#include "seam/platform/crash_capture.hpp"

#include <csignal>
#include <exception>
#include <filesystem>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  const seam::platform::CrashCaptureConfig config{
      .root = std::filesystem::path{argv[1]},
  };
  auto recovered = seam::platform::CrashCapture::recoverPending(config);
  if (!recovered) return 3;
  auto capture = seam::platform::CrashCapture::install(config);
  if (!capture) return 4;
  const std::string_view mode{argv[2]};
  if (mode != "terminate-no-context") {
    auto context = capture.value()->updateContext(
        seam::platform::CrashRecoveryContext{
            .candidateId = "candidate-crash-probe",
            .bankId = "beta-bank",
            .bankVersion = "1.0.0",
            .bankContentHash = std::string(64U, 'a'),
            .host = std::string{seam::platform::crashCaptureBackendName()},
            .audioUnderflowFrames = 17U,
            .audioXruns = 3U,
        });
    if (!context) return 5;
  }

  if (mode == "terminate" || mode == "terminate-no-context") {
    std::terminate();
  }
#ifdef _WIN32
  if (mode == "native") {
    ::RaiseException(0xE000534DU, EXCEPTION_NONCONTINUABLE, 0U, nullptr);
  }
#else
  if (mode == "native") std::raise(SIGABRT);
#endif
  return 6;
}
