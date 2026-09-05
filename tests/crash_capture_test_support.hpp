#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace seam::test::support {

struct CrashProbeResult final {
  bool launched{false};
  bool completed{false};
  bool abnormalExit{false};
  int exitCode{0};
};

inline CrashProbeResult runCrashCaptureProbe(
    const std::filesystem::path& root, std::string_view mode) {
#ifdef _WIN32
  const auto executable = std::filesystem::path{SEAM_CRASH_CAPTURE_PROBE_PATH};
  auto command = L"\"" + executable.native() + L"\" \"" + root.native() +
                 L"\" " + std::wstring{mode.begin(), mode.end()};
  command.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0U,
                       nullptr, nullptr, &startup, &process) == 0) {
    return {};
  }
  const auto wait = ::WaitForSingleObject(process.hProcess, 10'000U);
  if (wait != WAIT_OBJECT_0) {
    ::TerminateProcess(process.hProcess, 124U);
    ::WaitForSingleObject(process.hProcess, 1'000U);
  }
  DWORD code = 0U;
  const auto completed = ::GetExitCodeProcess(process.hProcess, &code) != 0 &&
                         code != STILL_ACTIVE;
  ::CloseHandle(process.hThread);
  ::CloseHandle(process.hProcess);
  return CrashProbeResult{
      .launched = true,
      .completed = completed,
      .abnormalExit = completed && code != 0U,
      .exitCode = static_cast<int>(code),
  };
#else
  const auto child = ::fork();
  if (child < 0) return {};
  if (child == 0) {
    const auto executable = std::filesystem::path{
        SEAM_CRASH_CAPTURE_PROBE_PATH};
    ::execl(executable.c_str(), executable.c_str(), root.c_str(),
            std::string{mode}.c_str(), static_cast<char*>(nullptr));
    ::_exit(127);
  }
  int status = 0;
  if (::waitpid(child, &status, 0) != child) {
    return CrashProbeResult{.launched = true};
  }
  const auto exited = WIFEXITED(status);
  const auto signaled = WIFSIGNALED(status);
  const auto code = exited ? WEXITSTATUS(status)
                           : signaled ? 128 + WTERMSIG(status) : 0;
  return CrashProbeResult{
      .launched = true,
      .completed = exited || signaled,
      .abnormalExit = signaled || (exited && code != 0),
      .exitCode = code,
  };
#endif
}

}
