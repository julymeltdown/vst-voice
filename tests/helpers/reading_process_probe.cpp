#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>
#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string_view mode{argv[1]};
  if (mode == "echo") { std::fputs("ok\n", stdout); std::fputs("diagnostic\n", stderr); return std::getenv("SEAM_HELPER_SECRET") || std::getchar() != EOF ? 3 : 0; }
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
  return 2;
}
