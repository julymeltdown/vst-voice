#include "test_framework.hpp"
#include "seam/authoring/helper_process.hpp"
#include <thread>
#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

TEST_CASE("bounded helper captures separate streams and rejects failure overflow timeout and cancellation") {
#if defined(__APPLE__) || defined(__linux__)
  using namespace seam::authoring;
  HelperProcessRequest request{SEAM_READING_PROCESS_PROBE, {"echo"}};
  const auto success = runBoundedHelperProcess(request); CHECK(success);
  CHECK(success.value().standardOutput == "ok\n"); CHECK(success.value().standardError == "diagnostic\n");
  const int inherited = ::open("/dev/null", O_RDONLY); CHECK(inherited >= 0);
  request.arguments = {"descriptors"}; const auto isolated = runBoundedHelperProcess(request);
  ::close(inherited); CHECK(isolated);
  for (const auto* mode : {"fail", "flood", "stderr", "sleep", "descendant"}) {
    request.arguments = {mode}; request.timeout = std::chrono::milliseconds{100};
    request.maximumStdoutBytes = 8192U; request.maximumStderrBytes = 8192U;
    const auto start = std::chrono::steady_clock::now(); const auto failed = runBoundedHelperProcess(request); CHECK(!failed);
    if (std::string_view{mode} == "flood" || std::string_view{mode} == "stderr")
      CHECK(failed.error().message.find("byte limit") != std::string::npos);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds{3});
  }
  request.arguments = {"sleep"}; request.timeout = std::chrono::seconds{5};
  std::stop_source stop;
  std::jthread cancel{[&] { std::this_thread::sleep_for(std::chrono::milliseconds{30}); stop.request_stop(); }};
  const auto cancelled = runBoundedHelperProcess(request, stop.get_token()); CHECK(!cancelled);
  CHECK(cancelled.error().message.find("cancelled") != std::string::npos);
  CHECK(!runBoundedHelperProcess(request, stop.get_token()));
  request.executable = "relative-path"; CHECK(!runBoundedHelperProcess(request));
  request.executable = SEAM_READING_PROCESS_PROBE; request.arguments = {std::string("a\0b", 3U)};
  CHECK(!runBoundedHelperProcess(request));
  request.arguments = {}; request.executable = "/nonexistent-seam-helper-fixture"; CHECK(!runBoundedHelperProcess(request));
#endif
}

TEST_CASE("helper stdin delivers exact private binary input with EOF and simultaneous output") {
#if defined(__APPLE__) || defined(__linux__)
  using namespace seam::authoring;
  HelperProcessRequest request{SEAM_READING_PROCESS_PROBE, {"input"}};
  request.standardInput = "私の歌\n"; request.standardInput.push_back('\0'); request.standardInput += "tail";
  auto result = runBoundedHelperProcess(request); CHECK(result); CHECK(result.value().standardOutput == request.standardInput);
  CHECK(!result.value().standardError.empty()); CHECK(request.arguments == std::vector<std::string>{"input"});
  request.standardInput = std::string(4096U, 'a'); result = runBoundedHelperProcess(request);
  CHECK(result); CHECK(result.value().standardOutput == request.standardInput);
  request.standardInput.push_back('a'); CHECK(!runBoundedHelperProcess(request));
  request.maximumStdinBytes = 8192U; result = runBoundedHelperProcess(request);
  CHECK(result); CHECK(result.value().standardOutput == request.standardInput);
  request.standardInput.clear(); result = runBoundedHelperProcess(request); CHECK(result); CHECK(result.value().standardOutput.empty());
  request.standardInput = std::string(4096U, 'a'); request.arguments = {"fail"};
  CHECK(!runBoundedHelperProcess(request)); // Early exit must never SIGPIPE-terminate this process.
  request.arguments = {"sleep"}; request.timeout = std::chrono::milliseconds{50}; CHECK(!runBoundedHelperProcess(request));
#endif
}

TEST_CASE("helper resource ceilings terminate an over-budget child with a bounded diagnostic") {
#if defined(__APPLE__) || defined(__linux__)
  using namespace seam::authoring;
  HelperProcessRequest request{SEAM_READING_PROCESS_PROBE, {"sleep"}};
  request.timeout = std::chrono::milliseconds{500};
  request.maximumResidentBytes = 1U;
  const auto limited = runBoundedHelperProcess(request);
  CHECK(!limited);
  CHECK(limited.error().message.find("memory") != std::string::npos ||
        limited.error().message.find("usage") != std::string::npos);
#endif
}
