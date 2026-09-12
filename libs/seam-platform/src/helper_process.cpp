#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "seam/platform/helper_process.hpp"
#include <array>
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <unistd.h>
#ifdef __APPLE__
#include <libproc.h>
#endif
#endif

namespace seam::platform {
#if defined(__APPLE__) || defined(__linux__)
namespace {
struct Descriptor {
  int value{-1};
  ~Descriptor() { if (value >= 0) ::close(value); }
  void close() { if (value >= 0) ::close(value); value = -1; }
};
bool pipePair(Descriptor& read, Descriptor& write) {
  int raw[2]; if (::pipe(raw) != 0) return false;
  // Keep spawn action source descriptors away from stdin/stdout/stderr, even
  // when a host has closed one of those standard descriptors.
  read.value = ::fcntl(raw[0], F_DUPFD_CLOEXEC, 3);
  write.value = ::fcntl(raw[1], F_DUPFD_CLOEXEC, 3);
  ::close(raw[0]); ::close(raw[1]);
  if (read.value < 0 || write.value < 0) return false;
  return ::fcntl(read.value, F_SETFL, O_NONBLOCK) == 0;
}
struct Child {
  pid_t pid{-1};
  ~Child() {
    if (pid <= 0) return;
    siginfo_t ownership{};
    if (::waitid(P_PID, static_cast<id_t>(pid), &ownership, WEXITED | WNOHANG | WNOWAIT) != 0 && errno == ECHILD) return;
    // The leader is deliberately not reaped before group cleanup, preventing
    // PID reuse from redirecting this signal to an unrelated process group.
    ::kill(-pid, SIGKILL);
    int status{};
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
  }
};
bool inputPair(Descriptor& read, Descriptor& write) {
  int raw[2]; if (::socketpair(AF_UNIX, SOCK_STREAM, 0, raw) != 0) return false;
  read.value = ::fcntl(raw[0], F_DUPFD_CLOEXEC, 3);
  write.value = ::fcntl(raw[1], F_DUPFD_CLOEXEC, 3);
  ::close(raw[0]); ::close(raw[1]);
  if (read.value < 0 || write.value < 0 || ::fcntl(write.value, F_SETFL, O_NONBLOCK) != 0) return false;
#ifdef __APPLE__
  const int enabled = 1;
  if (::setsockopt(write.value, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) return false;
#endif
  return true;
}
struct SpawnSetup {
  posix_spawn_file_actions_t actions{};
  posix_spawnattr_t attributes{};
  bool hasActions{false}, hasAttributes{false};
  ~SpawnSetup() {
    if (hasActions) posix_spawn_file_actions_destroy(&actions);
    if (hasAttributes) posix_spawnattr_destroy(&attributes);
  }
};

struct ChildUsage final {
  std::uint64_t residentBytes{0U};
  std::uint64_t cpuNanoseconds{0U};
};

std::optional<ChildUsage> childUsage(pid_t pid) {
#ifdef __APPLE__
  rusage_info_v4 usage{};
  if (proc_pid_rusage(pid, RUSAGE_INFO_V4,
          reinterpret_cast<rusage_info_t*>(&usage)) != 0) return std::nullopt;
  return ChildUsage{usage.ri_resident_size, usage.ri_user_time + usage.ri_system_time};
#elif defined(__linux__)
  std::ifstream status{"/proc/" + std::to_string(pid) + "/status"};
  std::string line;
  std::uint64_t resident = 0U;
  while (std::getline(status, line)) {
    if (!line.starts_with("VmRSS:")) continue;
    std::size_t value = 6U;
    while (value < line.size() && line[value] == ' ') ++value;
    std::uint64_t kilobytes = 0U;
    while (value < line.size() && line[value] >= '0' && line[value] <= '9') {
      if (kilobytes > (std::numeric_limits<std::uint64_t>::max() - 9U) / 10U) return std::nullopt;
      kilobytes = kilobytes * 10U + static_cast<std::uint64_t>(line[value] - '0'); ++value;
    }
    resident = kilobytes * 1024U;
    break;
  }
  if (!status || resident == 0U) return std::nullopt;
  std::ifstream stat{"/proc/" + std::to_string(pid) + "/stat"};
  std::string contents{std::istreambuf_iterator<char>{stat}, std::istreambuf_iterator<char>{}};
  const auto close = contents.rfind(')');
  if (!stat || close == std::string::npos || close + 2U >= contents.size()) return std::nullopt;
  std::istringstream fields{contents.substr(close + 2U)};
  std::string field;
  // After the executable name, fields start at process state (field 3).
  for (int index = 3; index <= 14; ++index) if (!(fields >> field)) return std::nullopt;
  std::uint64_t userTicks = 0U, systemTicks = 0U;
  try {
    userTicks = std::stoull(field);
    if (!(fields >> field)) return std::nullopt;
    systemTicks = std::stoull(field);
  } catch (...) { return std::nullopt; }
  const auto ticks = ::sysconf(_SC_CLK_TCK);
  if (ticks <= 0) return std::nullopt;
  const auto totalTicks = userTicks > std::numeric_limits<std::uint64_t>::max() - systemTicks
      ? std::numeric_limits<std::uint64_t>::max() : userTicks + systemTicks;
  return ChildUsage{resident, (totalTicks / static_cast<std::uint64_t>(ticks)) * 1000000000ULL +
      (totalTicks % static_cast<std::uint64_t>(ticks)) * 1000000000ULL / static_cast<std::uint64_t>(ticks)};
#else
  (void)pid;
  return std::nullopt;
#endif
}
}
#endif
core::Result<HelperProcessOutput> runBoundedHelperProcess(const HelperProcessRequest& request, std::stop_token stop) {
  using Output = HelperProcessOutput;
  const auto fail = [](core::ErrorCode code, const char* message) { return core::failure<Output>(code, message); };
  if (stop.stop_requested()) return fail(core::ErrorCode::Conflict, "Helper cancelled before launch");
  const auto executable = request.executable.string();
  if (!request.executable.is_absolute() || executable.empty() || executable.size() > 4096U || executable.find('\0') != std::string::npos ||
      request.arguments.size() > 32U || request.timeout.count() < 1 || request.timeout.count() > 60000 ||
      request.maximumStdoutBytes == 0U || request.maximumStdoutBytes > 64ULL * 1024ULL * 1024ULL || request.maximumStderrBytes == 0U ||
      request.maximumStderrBytes > 1ULL * 1024ULL * 1024ULL || request.maximumStdinBytes == 0U ||
      request.maximumStdinBytes > 64ULL * 1024ULL * 1024ULL || request.standardInput.size() > request.maximumStdinBytes ||
      request.maximumResidentBytes > 4ULL * 1024ULL * 1024ULL * 1024ULL ||
      request.maximumCpuTime.count() < 0 || request.maximumCpuTime.count() > 60000)
    return fail(core::ErrorCode::InvalidArgument, "Helper request exceeds admission limits");
  std::size_t remaining = 65536U;
  for (const auto& argument : request.arguments) {
    if (argument.size() > remaining || argument.find('\0') != std::string::npos)
      return fail(core::ErrorCode::InvalidArgument, "Helper argument exceeds admission limits");
    remaining -= argument.size();
  }
#if defined(__APPLE__) || defined(__linux__)
  Descriptor outRead, outWrite, errRead, errWrite, inRead, inWrite;
  if (!pipePair(outRead, outWrite) || !pipePair(errRead, errWrite) || !inputPair(inRead, inWrite))
    return fail(core::ErrorCode::Internal, "Cannot create helper channels");
  SpawnSetup setup;
  setup.hasActions = posix_spawn_file_actions_init(&setup.actions) == 0;
  setup.hasAttributes = posix_spawnattr_init(&setup.attributes) == 0;
  if (!setup.hasActions || !setup.hasAttributes) return fail(core::ErrorCode::Internal, "Cannot initialize helper launch");
  sigset_t mask; sigemptyset(&mask); sigset_t defaults; sigemptyset(&defaults); sigaddset(&defaults, SIGPIPE);
  short flags = POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
#ifdef __APPLE__
  flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
  if (posix_spawnattr_setflags(&setup.attributes, flags) != 0 ||
      posix_spawnattr_setpgroup(&setup.attributes, 0) != 0 || posix_spawnattr_setsigmask(&setup.attributes, &mask) != 0 ||
      posix_spawnattr_setsigdefault(&setup.attributes, &defaults) != 0 ||
      posix_spawn_file_actions_adddup2(&setup.actions, inRead.value, STDIN_FILENO) != 0 ||
      posix_spawn_file_actions_adddup2(&setup.actions, outWrite.value, STDOUT_FILENO) != 0 ||
      posix_spawn_file_actions_adddup2(&setup.actions, errWrite.value, STDERR_FILENO) != 0)
    return fail(core::ErrorCode::Internal, "Cannot configure helper launch");
  for (int fd : {outRead.value, outWrite.value, errRead.value, errWrite.value, inRead.value, inWrite.value})
    if (posix_spawn_file_actions_addclose(&setup.actions, fd) != 0) return fail(core::ErrorCode::Internal, "Cannot configure helper pipe closure");
#ifdef __linux__
#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 34)
  if (posix_spawn_file_actions_addclosefrom_np(&setup.actions, 3) != 0)
    return fail(core::ErrorCode::Internal, "Cannot isolate inherited helper descriptors");
#else
  return fail(core::ErrorCode::Unsupported, "Helper descriptor isolation requires glibc 2.34 or newer");
#endif
#else
  return fail(core::ErrorCode::Unsupported, "Helper descriptor isolation is unavailable on this libc");
#endif
#endif
  std::vector<std::string> arguments; arguments.reserve(request.arguments.size() + 1U);
  arguments.push_back(executable); arguments.insert(arguments.end(), request.arguments.begin(), request.arguments.end());
  std::vector<char*> argv; for (auto& argument : arguments) argv.push_back(argument.data()); argv.push_back(nullptr);
  char* environment[] = {nullptr}; // No inherited DYLD/LD/MECAB or shell configuration environment.
  Child child;
  const auto deadline = std::chrono::steady_clock::now() + request.timeout;
  if (posix_spawn(&child.pid, executable.c_str(), &setup.actions, &setup.attributes, argv.data(), environment) != 0) {
    child.pid = -1; return fail(core::ErrorCode::Internal, "Cannot launch selected helper");
  }
  outWrite.close(); errWrite.close(); inRead.close();
  std::size_t inputOffset = 0U;
  if (request.standardInput.empty()) inWrite.close();
  Output output;
  for (;;) {
    if (stop.stop_requested()) return fail(core::ErrorCode::Conflict, "Helper cancelled");
    if (std::chrono::steady_clock::now() >= deadline) return fail(core::ErrorCode::Conflict, "Helper deadline exceeded");
    if (request.maximumResidentBytes != 0U || request.maximumCpuTime.count() != 0) {
      const auto usage = childUsage(child.pid);
      if (!usage) return fail(core::ErrorCode::Unsupported, "Child resource usage is unavailable on this platform");
      if (request.maximumResidentBytes != 0U && usage->residentBytes > request.maximumResidentBytes)
        return fail(core::ErrorCode::Conflict, "Helper resident-memory limit exceeded");
      if (request.maximumCpuTime.count() != 0 && usage->cpuNanoseconds >
          static_cast<std::uint64_t>(request.maximumCpuTime.count()) * 1000000ULL)
        return fail(core::ErrorCode::Conflict, "Helper CPU-time limit exceeded");
    }
    if (inWrite.value >= 0) {
#ifdef __APPLE__
      constexpr int sendFlags = 0; // SO_NOSIGPIPE is set on this socket only.
#else
      constexpr int sendFlags = MSG_NOSIGNAL;
#endif
      const auto sent = ::send(inWrite.value, request.standardInput.data() + inputOffset,
          request.standardInput.size() - inputOffset, sendFlags);
      if (sent > 0) {
        inputOffset += static_cast<std::size_t>(sent);
        if (inputOffset == request.standardInput.size()) inWrite.close(); // Explicit EOF after exact bytes.
      } else if (sent == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
        return fail(core::ErrorCode::Internal, "Cannot deliver helper input");
    }
    for (int stream = 0; stream < 2; ++stream) {
      auto& fd = stream == 0 ? outRead : errRead;
      auto& text = stream == 0 ? output.standardOutput : output.standardError;
      const auto limit = stream == 0 ? request.maximumStdoutBytes : request.maximumStderrBytes;
      if (fd.value < 0) continue;
      std::array<char, 4096U> buffer{};
      // One bounded read per stream per iteration prevents a busy writer from
      // starving deadline/cancellation checks or the other pipe.
      const auto count = ::read(fd.value, buffer.data(), buffer.size());
      if (count == 0) fd.close();
      else if (count > 0) {
        const auto size = static_cast<std::size_t>(count);
        if (size > limit - text.size()) return fail(core::ErrorCode::InvalidArgument, "Helper output exceeded byte limit");
        text.append(buffer.data(), size);
      } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        return fail(core::ErrorCode::Internal, "Cannot read helper output");
    }
    siginfo_t info{};
    if (::waitid(P_PID, static_cast<id_t>(child.pid), &info, WEXITED | WNOHANG | WNOWAIT) != 0) {
      if (errno == EINTR) continue;
      return fail(core::ErrorCode::Internal, "Cannot observe helper exit");
    }
    if (info.si_pid == child.pid && outRead.value < 0 && errRead.value < 0) {
      if (inputOffset != request.standardInput.size()) return fail(core::ErrorCode::Internal, "Helper exited before input delivery");
      if (info.si_code != CLD_EXITED || info.si_status != 0) return fail(core::ErrorCode::Internal, "Helper exited unsuccessfully");
      return output;
    }
    std::array<pollfd, 3U> pending{{{outRead.value, POLLIN, 0}, {errRead.value, POLLIN, 0}, {inWrite.value, POLLOUT, 0}}};
    if (::poll(pending.data(), pending.size(), 10) < 0 && errno != EINTR)
      return fail(core::ErrorCode::Internal, "Cannot poll helper output");
  }
#else
  return fail(core::ErrorCode::Unsupported, "Bounded helper execution is not implemented on this platform");
#endif
}
}
