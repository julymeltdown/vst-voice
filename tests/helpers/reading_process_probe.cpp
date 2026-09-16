#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>
#if defined(_WIN32)
#include <windows.h>
#endif
#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif
namespace {
bool hasSecretEnvironment() {
#if defined(_WIN32)
  char* value = nullptr;
  std::size_t length = 0U;
  const int result = ::_dupenv_s(&value, &length, "SEAM_HELPER_SECRET");
  const bool present = result == 0 && value != nullptr;
  std::free(value);
  return present;
#else
  return std::getenv("SEAM_HELPER_SECRET") != nullptr;
#endif
}
}
int main(int argc, char** argv) {
  if (argc < 2) return 2;
  const std::string_view mode{argv[1]};
  if (mode == "argument") {
    if (argc != 3) return 2;
    std::fwrite(argv[2], 1, std::string_view{argv[2]}.size(), stdout);
    return 0;
  }
#if defined(_WIN32)
  if (mode == "handle") {
    if (argc != 3) return 2;
    char* end = nullptr;
    const auto raw = std::strtoull(argv[2], &end, 10);
    if (end == argv[2] || *end != '\0') return 2;
    DWORD flags = 0U;
    return ::GetHandleInformation(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(raw)), &flags) ? 9 : 0;
  }
#endif
  if (argc != 2) return 2;
  if (mode == "echo") { std::fputs("ok\n", stdout); std::fputs("diagnostic\n", stderr); return hasSecretEnvironment() || std::getchar() != EOF ? 3 : 0; }
  if (mode == "fail") { std::fputs("partial", stdout); return 7; }
  if (mode == "input") {
    std::array<char, 128> buffer{};
    for (;;) {
      const auto count = std::fread(buffer.data(), 1, buffer.size(), stdin);
      if (count == 0) return std::ferror(stdin) ? 4 : 0;
      std::fwrite(buffer.data(), 1, count, stdout); std::fflush(stdout);
      std::fputs("read\n", stderr); std::fflush(stderr);
    }
  }
  if (mode == "sleep") { std::this_thread::sleep_for(std::chrono::seconds{5}); return 0; }
  if (mode == "flood" || mode == "stderr") {
    std::array<char, 4096> buffer{}; buffer.fill('a');
    for (int i = 0; i < 512; ++i) std::fwrite(buffer.data(), 1, buffer.size(), mode == "flood" ? stdout : stderr);
    return 0;
  }
#if defined(__APPLE__) || defined(__linux__)
  if (mode == "descriptors") {
    for (int fd = 3; fd < 256; ++fd) if (::fcntl(fd, F_GETFD) >= 0) return 9;
    return 0;
  }
  if (mode == "descendant") {
    const auto child = ::fork(); if (child < 0) return 3;
    if (child == 0) { ::sleep(5); _exit(0); }
    return 0; // Descendant retains pipes; runner must time out and clean the group.
  }
#endif
#if defined(_WIN32)
  if (mode == "descendant") {
    std::array<wchar_t, 32768U> executable{};
    const DWORD length = ::GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0U || static_cast<std::size_t>(length) >= executable.size()) return 3;
    std::wstring command = L"\"" + std::wstring{executable.data(), length} + L"\" sleep";
    STARTUPINFOW startup{}; startup.cb = static_cast<DWORD>(sizeof(startup));
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(executable.data(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &process)) return 3;
    ::CloseHandle(process.hThread); ::CloseHandle(process.hProcess);
    return 0; // Descendant retains inherited pipes; the runner job must terminate it.
  }
#endif
  return 2;
}
