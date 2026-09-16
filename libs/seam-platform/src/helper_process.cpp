#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "seam/platform/helper_process.hpp"
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif
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
  void release() noexcept { pid = -1; }
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

std::uint64_t timevalNanoseconds(const timeval& value) {
  if (value.tv_sec < 0 || value.tv_usec < 0) return 0U;
  const auto seconds = static_cast<std::uint64_t>(value.tv_sec);
  const auto microseconds = static_cast<std::uint64_t>(value.tv_usec);
  if (seconds > (std::numeric_limits<std::uint64_t>::max() - microseconds * 1000U) /
          1000000000U)
    return std::numeric_limits<std::uint64_t>::max();
  return seconds * 1000000000U + microseconds * 1000U;
}

ChildUsage completedChildUsage(const rusage& usage) {
  const auto user = timevalNanoseconds(usage.ru_utime);
  const auto system = timevalNanoseconds(usage.ru_stime);
  const auto cpu = user > std::numeric_limits<std::uint64_t>::max() - system
      ? std::numeric_limits<std::uint64_t>::max() : user + system;
#ifdef __APPLE__
  const auto resident = usage.ru_maxrss < 0 ? 0U : static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  const auto kilobytes = usage.ru_maxrss < 0 ? 0U : static_cast<std::uint64_t>(usage.ru_maxrss);
  const auto resident = kilobytes > std::numeric_limits<std::uint64_t>::max() / 1024U
      ? std::numeric_limits<std::uint64_t>::max() : kilobytes * 1024U;
#endif
  return ChildUsage{resident, cpu};
}

std::optional<ChildUsage> childUsage(pid_t pid) {
#ifdef __APPLE__
  rusage_info_v4 usage{};
  if (proc_pid_rusage(pid, RUSAGE_INFO_V4,
          reinterpret_cast<rusage_info_t*>(&usage)) != 0) return std::nullopt;
  return ChildUsage{usage.ri_resident_size, usage.ri_user_time + usage.ri_system_time};
#elif defined(__linux__)
  std::ifstream status{"/proc/" + std::to_string(pid) + "/status"};
  if (!status.is_open()) return std::nullopt;
  std::string line;
  std::uint64_t resident = 0U;
  while (std::getline(status, line)) {
    if (!line.starts_with("VmRSS:")) continue;
    std::size_t value = 6U;
    while (value < line.size() && (line[value] == ' ' || line[value] == '\t')) ++value;
    std::uint64_t kilobytes = 0U;
    while (value < line.size() && line[value] >= '0' && line[value] <= '9') {
      if (kilobytes > (std::numeric_limits<std::uint64_t>::max() - 9U) / 10U) return std::nullopt;
      kilobytes = kilobytes * 10U + static_cast<std::uint64_t>(line[value] - '0'); ++value;
    }
    resident = kilobytes * 1024U;
    break;
  }
  // VmRSS can legitimately be absent or zero during the first scheduling
  // slice and after the process has become a zombie. Treat that as a zero
  // live sample: wait4 supplies the authoritative peak before success.
  if (status.bad()) return std::nullopt;
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
#if defined(_WIN32)
namespace {
struct WindowsHandle final {
  HANDLE value{nullptr};
  WindowsHandle() = default;
  explicit WindowsHandle(HANDLE handle) : value(handle) {}
  WindowsHandle(const WindowsHandle&) = delete;
  WindowsHandle& operator=(const WindowsHandle&) = delete;
  WindowsHandle(WindowsHandle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
  WindowsHandle& operator=(WindowsHandle&& other) noexcept {
    if (this != &other) { close(); value = std::exchange(other.value, nullptr); }
    return *this;
  }
  ~WindowsHandle() { close(); }
  void close() {
    if (value != nullptr && value != INVALID_HANDLE_VALUE) ::CloseHandle(value);
    value = nullptr;
  }
  [[nodiscard]] explicit operator bool() const { return value != nullptr && value != INVALID_HANDLE_VALUE; }
};

bool windowsPipePair(WindowsHandle& read, WindowsHandle& write, bool childReads) {
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE rawRead = nullptr, rawWrite = nullptr;
  if (!::CreatePipe(&rawRead, &rawWrite, &security, 0U)) return false;
  read = WindowsHandle{rawRead}; write = WindowsHandle{rawWrite};
  const HANDLE parentEnd = childReads ? write.value : read.value;
  return ::SetHandleInformation(parentEnd, HANDLE_FLAG_INHERIT, 0U) != FALSE;
}

struct AttributeList final {
  std::vector<std::byte> storage;
  LPPROC_THREAD_ATTRIBUTE_LIST value{nullptr};
  ~AttributeList() { if (value != nullptr) ::DeleteProcThreadAttributeList(value); }
  bool initialize(const std::array<HANDLE, 3U>& handles) {
    SIZE_T bytes = 0U;
    ::InitializeProcThreadAttributeList(nullptr, 1U, 0U, &bytes);
    if (bytes == 0U) return false;
    storage.resize(bytes);
    value = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!::InitializeProcThreadAttributeList(value, 1U, 0U, &bytes)) { value = nullptr; return false; }
    return ::UpdateProcThreadAttribute(value, 0U, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        const_cast<HANDLE*>(handles.data()), handles.size() * sizeof(HANDLE), nullptr, nullptr) != FALSE;
  }
};

std::optional<std::wstring> utf8ToWide(std::string_view source) {
  if (source.empty()) return std::wstring{};
  if (source.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return std::nullopt;
  const auto inputSize = static_cast<int>(source.size());
  const int required = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(), inputSize, nullptr, 0);
  if (required <= 0) return std::nullopt;
  std::wstring result(static_cast<std::size_t>(required), L'\0');
  if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(), inputSize,
          result.data(), required) != required) return std::nullopt;
  return result;
}

void appendWindowsArgument(std::wstring& command, std::wstring_view argument) {
  if (!command.empty()) command.push_back(L' ');
  command.push_back(L'"');
  std::size_t slashes = 0U;
  for (const wchar_t character : argument) {
    if (character == L'\\') { ++slashes; continue; }
    if (character == L'"') {
      command.append(slashes * 2U + 1U, L'\\');
      command.push_back(L'"');
    } else {
      command.append(slashes, L'\\');
      command.push_back(character);
    }
    slashes = 0U;
  }
  command.append(slashes * 2U, L'\\');
  command.push_back(L'"');
}

struct WindowsChildUsage final {
  std::uint64_t residentBytes{0U};
  std::uint64_t peakResidentBytes{0U};
  std::uint64_t cpuNanoseconds{0U};
};

std::uint64_t fileTimeTicks(FILETIME value) {
  ULARGE_INTEGER ticks{}; ticks.LowPart = value.dwLowDateTime; ticks.HighPart = value.dwHighDateTime;
  return ticks.QuadPart;
}

std::optional<WindowsChildUsage> windowsChildUsage(HANDLE process) {
  PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = static_cast<DWORD>(sizeof(memory));
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!::GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
          static_cast<DWORD>(sizeof(memory))) ||
      !::GetProcessTimes(process, &created, &exited, &kernel, &user)) return std::nullopt;
  const std::uint64_t ticks = fileTimeTicks(kernel) + fileTimeTicks(user);
  const std::uint64_t nanoseconds = ticks > std::numeric_limits<std::uint64_t>::max() / 100U
      ? std::numeric_limits<std::uint64_t>::max() : ticks * 100U;
  return WindowsChildUsage{static_cast<std::uint64_t>(memory.WorkingSetSize),
      static_cast<std::uint64_t>(memory.PeakWorkingSetSize), nanoseconds};
}

struct PipeTransferState final {
  std::atomic<bool> done{false};
  std::atomic<bool> failed{false};
  std::atomic<bool> overflow{false};
};

void readWindowsPipe(WindowsHandle pipe, std::string& output, std::size_t limit, PipeTransferState& state) {
  std::array<char, 4096U> buffer{};
  for (;;) {
    DWORD read = 0U;
    if (!::ReadFile(pipe.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
      const DWORD error = ::GetLastError();
      if (error != ERROR_BROKEN_PIPE && error != ERROR_HANDLE_EOF) state.failed.store(true);
      break;
    }
    if (read == 0U) break;
    const auto count = static_cast<std::size_t>(read);
    if (output.size() > limit || count > limit - output.size()) { state.overflow.store(true); break; }
    output.append(buffer.data(), count);
  }
  state.done.store(true);
}

void writeWindowsPipe(WindowsHandle pipe, std::string_view input, PipeTransferState& state) {
  std::size_t offset = 0U;
  while (offset < input.size()) {
    const auto remaining = input.size() - offset;
    const DWORD chunk = static_cast<DWORD>(remaining > 4096U ? 4096U : remaining);
    DWORD written = 0U;
    if (!::WriteFile(pipe.value, input.data() + offset, chunk, &written, nullptr) || written == 0U) {
      state.failed.store(true); break;
    }
    offset += static_cast<std::size_t>(written);
  }
  state.done.store(true);
}
}
#endif
core::Result<HelperProcessOutput> runBoundedHelperProcess(const HelperProcessRequest& request, std::stop_token stop) {
  using Output = HelperProcessOutput;
  const auto fail = [](core::ErrorCode code, const char* message) { return core::failure<Output>(code, message); };
  if (stop.stop_requested()) return fail(core::ErrorCode::Conflict, "Helper cancelled before launch");
#if defined(_WIN32)
  const auto& executableNative = request.executable.native();
  const bool invalidExecutable = executableNative.empty() || executableNative.size() > 4096U ||
      executableNative.find(L'\0') != std::wstring::npos;
#else
  const auto executable = request.executable.string();
  const bool invalidExecutable = executable.empty() || executable.size() > 4096U ||
      executable.find('\0') != std::string::npos;
#endif
  if (!request.executable.is_absolute() || invalidExecutable || request.arguments.size() > 32U ||
      request.timeout.count() < 1 || request.timeout.count() > 60000 ||
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
      if (!usage) {
        // A short-lived helper can become a zombie between the live /proc or
        // proc_pid_rusage sample and this check. Its authoritative peak usage
        // is collected with wait4 below; only a still-running child without an
        // observable usage record is an unsupported platform condition.
        siginfo_t exited{};
        if (::waitid(P_PID, static_cast<id_t>(child.pid), &exited,
                WEXITED | WNOHANG | WNOWAIT) != 0 || exited.si_pid != child.pid)
          return fail(core::ErrorCode::Unsupported, "Child resource usage is unavailable on this platform");
      } else {
        if (request.maximumResidentBytes != 0U && usage->residentBytes > request.maximumResidentBytes)
          return fail(core::ErrorCode::Conflict, "Helper resident-memory limit exceeded");
        if (request.maximumCpuTime.count() != 0 && usage->cpuNanoseconds >
            static_cast<std::uint64_t>(request.maximumCpuTime.count()) * 1000000ULL)
          return fail(core::ErrorCode::Conflict, "Helper CPU-time limit exceeded");
      }
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
      int status = 0;
      rusage finalUsage{};
      pid_t reaped = -1;
      do { reaped = ::wait4(child.pid, &status, 0, &finalUsage); } while (reaped < 0 && errno == EINTR);
      if (reaped != child.pid) return fail(core::ErrorCode::Internal, "Cannot collect helper exit status");
      child.release();
      const auto completed = completedChildUsage(finalUsage);
      if (request.maximumResidentBytes != 0U && completed.residentBytes > request.maximumResidentBytes)
        return fail(core::ErrorCode::Conflict, "Helper resident-memory limit exceeded");
      if (request.maximumCpuTime.count() != 0 && completed.cpuNanoseconds >
          static_cast<std::uint64_t>(request.maximumCpuTime.count()) * 1000000ULL)
        return fail(core::ErrorCode::Conflict, "Helper CPU-time limit exceeded");
      if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return fail(core::ErrorCode::Internal, "Helper exited unsuccessfully");
      return output;
    }
    std::array<pollfd, 3U> pending{{{outRead.value, POLLIN, 0}, {errRead.value, POLLIN, 0}, {inWrite.value, POLLOUT, 0}}};
    if (::poll(pending.data(), pending.size(), 10) < 0 && errno != EINTR)
      return fail(core::ErrorCode::Internal, "Cannot poll helper output");
  }
#elif defined(_WIN32)
  std::wstring command;
  appendWindowsArgument(command, executableNative);
  for (const auto& argument : request.arguments) {
    const auto wide = utf8ToWide(argument);
    if (!wide) return fail(core::ErrorCode::InvalidArgument, "Helper argument is not valid UTF-8");
    appendWindowsArgument(command, *wide);
  }
  // CreateProcessW accepts at most 32767 UTF-16 code units including NUL.
  if (command.size() > 32766U)
    return fail(core::ErrorCode::InvalidArgument, "Helper command line exceeds Windows limit");

  WindowsHandle outRead, outWrite, errRead, errWrite, inRead, inWrite;
  if (!windowsPipePair(outRead, outWrite, false) || !windowsPipePair(errRead, errWrite, false) ||
      !windowsPipePair(inRead, inWrite, true))
    return fail(core::ErrorCode::Internal, "Cannot create helper channels");
  const std::array<HANDLE, 3U> inherited{inRead.value, outWrite.value, errWrite.value};
  AttributeList attributes;
  if (!attributes.initialize(inherited))
    return fail(core::ErrorCode::Internal, "Cannot isolate inherited helper handles");

  WindowsHandle job{::CreateJobObjectW(nullptr, nullptr)};
  if (!job) return fail(core::ErrorCode::Internal, "Cannot create helper job object");
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (request.maximumResidentBytes != 0U) {
    limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit = request.maximumResidentBytes;
  }
  if (!::SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits,
          static_cast<DWORD>(sizeof(limits))))
    return fail(core::ErrorCode::Internal, request.maximumResidentBytes == 0U
        ? "Cannot configure helper job object" : "Cannot apply helper resident-memory limit");

  STARTUPINFOEXW startup{}; startup.StartupInfo.cb = static_cast<DWORD>(sizeof(startup));
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = inRead.value;
  startup.StartupInfo.hStdOutput = outWrite.value;
  startup.StartupInfo.hStdError = errWrite.value;
  startup.lpAttributeList = attributes.value;
  PROCESS_INFORMATION rawProcess{};
  wchar_t emptyEnvironment[2]{L'\0', L'\0'};
  const DWORD creationFlags = CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
      EXTENDED_STARTUPINFO_PRESENT;
  if (!::CreateProcessW(executableNative.c_str(), command.data(), nullptr, nullptr, TRUE, creationFlags,
          emptyEnvironment, nullptr, &startup.StartupInfo, &rawProcess))
    return fail(core::ErrorCode::Internal, "Cannot launch selected helper");
  WindowsHandle process{rawProcess.hProcess}, primaryThread{rawProcess.hThread};
  if (!::AssignProcessToJobObject(job.value, process.value)) {
    ::TerminateProcess(process.value, 1U);
    return fail(core::ErrorCode::Internal, request.maximumResidentBytes == 0U
        ? "Cannot assign helper process to job object" : "Cannot apply helper resident-memory limit");
  }
  inRead.close(); outWrite.close(); errWrite.close();
  if (::ResumeThread(primaryThread.value) == static_cast<DWORD>(-1)) {
    ::TerminateJobObject(job.value, 1U);
    return fail(core::ErrorCode::Internal, "Cannot resume selected helper");
  }
  primaryThread.close();

  Output output;
  PipeTransferState stdoutState, stderrState, stdinState;
  std::jthread stdoutReader{[pipe = std::move(outRead), &output, &stdoutState, limit = request.maximumStdoutBytes]() mutable {
    readWindowsPipe(std::move(pipe), output.standardOutput, limit, stdoutState);
  }};
  std::jthread stderrReader{[pipe = std::move(errRead), &output, &stderrState, limit = request.maximumStderrBytes]() mutable {
    readWindowsPipe(std::move(pipe), output.standardError, limit, stderrState);
  }};
  std::jthread stdinWriter{[pipe = std::move(inWrite), input = std::string_view{request.standardInput}, &stdinState]() mutable {
    writeWindowsPipe(std::move(pipe), input, stdinState);
  }};

  const auto deadline = std::chrono::steady_clock::now() + request.timeout;
  core::ErrorCode failureCode = core::ErrorCode::Internal;
  const char* failureMessage = nullptr;
  bool exited = false;
  DWORD exitCode = STILL_ACTIVE;
  for (;;) {
    if (stdoutState.overflow.load() || stderrState.overflow.load()) {
      failureCode = core::ErrorCode::InvalidArgument; failureMessage = "Helper output exceeded byte limit"; break;
    }
    if (stdoutState.failed.load() || stderrState.failed.load()) {
      failureMessage = "Cannot read helper output"; break;
    }
    if (stdinState.failed.load()) { failureMessage = "Cannot deliver helper input"; break; }
    if (stop.stop_requested()) { failureCode = core::ErrorCode::Conflict; failureMessage = "Helper cancelled"; break; }
    if (std::chrono::steady_clock::now() >= deadline) {
      failureCode = core::ErrorCode::Conflict;
      if (!stdinState.done.load()) failureMessage = "Helper deadline exceeded while delivering input";
      else if (!exited) failureMessage = "Helper deadline exceeded while waiting for process exit";
      else failureMessage = "Helper deadline exceeded while draining output";
      break;
    }
    if (!exited && (request.maximumResidentBytes != 0U || request.maximumCpuTime.count() != 0)) {
      const auto usage = windowsChildUsage(process.value);
      if (!usage) { failureCode = core::ErrorCode::Unsupported; failureMessage = "Child resource usage is unavailable on this platform"; break; }
      if (request.maximumResidentBytes != 0U && usage->residentBytes > request.maximumResidentBytes) {
        failureCode = core::ErrorCode::Conflict; failureMessage = "Helper resident-memory limit exceeded"; break;
      }
      if (request.maximumCpuTime.count() != 0 && usage->cpuNanoseconds >
          static_cast<std::uint64_t>(request.maximumCpuTime.count()) * 1000000ULL) {
        failureCode = core::ErrorCode::Conflict; failureMessage = "Helper CPU-time limit exceeded"; break;
      }
    }
    if (!exited) {
      const DWORD observed = ::WaitForSingleObject(process.value, 10U);
      if (observed == WAIT_FAILED) { failureMessage = "Cannot observe helper exit"; break; }
      if (observed == WAIT_OBJECT_0) {
        exited = true;
        if (!::GetExitCodeProcess(process.value, &exitCode)) { failureMessage = "Cannot observe helper exit"; break; }
      }
    } else {
      ::Sleep(1U);
    }
    if (exited && stdoutState.done.load() && stderrState.done.load() && stdinState.done.load()) break;
  }

  if (failureMessage == nullptr && exitCode != 0U) {
    const auto usage = windowsChildUsage(process.value);
    if (request.maximumResidentBytes != 0U && usage && usage->peakResidentBytes > request.maximumResidentBytes) {
      failureCode = core::ErrorCode::Conflict; failureMessage = "Helper resident-memory limit exceeded";
    } else if (request.maximumCpuTime.count() != 0 && usage && usage->cpuNanoseconds >
        static_cast<std::uint64_t>(request.maximumCpuTime.count()) * 1000000ULL) {
      failureCode = core::ErrorCode::Conflict; failureMessage = "Helper CPU-time limit exceeded";
    } else {
      failureMessage = "Helper exited unsuccessfully";
    }
  }
  // The job owns the complete process tree. Terminating it after the direct
  // child exits also closes descendant-held pipe handles before joining I/O.
  ::TerminateJobObject(job.value, failureMessage == nullptr ? 0U : 1U);
  ::WaitForSingleObject(process.value, 1000U);
  stdinWriter.join(); stdoutReader.join(); stderrReader.join();
  // A transfer can publish done between the loop's initial failure check and
  // its terminal all-done check. Re-read stable states after joining so that
  // this narrow race cannot turn truncated input/output into success.
  if (failureMessage == nullptr && (stdoutState.overflow.load() || stderrState.overflow.load())) {
    failureCode = core::ErrorCode::InvalidArgument; failureMessage = "Helper output exceeded byte limit";
  } else if (failureMessage == nullptr && (stdoutState.failed.load() || stderrState.failed.load())) {
    failureMessage = "Cannot read helper output";
  } else if (failureMessage == nullptr && stdinState.failed.load()) {
    failureMessage = "Cannot deliver helper input";
  }
  if (failureMessage != nullptr)
    return core::failure<Output>(failureCode, failureMessage, std::move(output.standardError));
  return output;
#else
  return fail(core::ErrorCode::Unsupported, "Bounded helper execution is not implemented on this platform");
#endif
}
}
