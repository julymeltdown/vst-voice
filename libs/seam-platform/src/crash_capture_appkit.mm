#include "crash_capture_internal.hpp"

#include <array>
#include <atomic>
#include <csignal>
#include <exception>
#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace seam::platform {
namespace {

detail::CrashWriterSlot<int, -1> gCrashDescriptor;
std::atomic<bool> gCrashInstalled{false};

static_assert(std::atomic<bool>::is_always_lock_free);

void writeCrashPrimitive(CrashCause cause,
                         std::uint32_t platformCode) noexcept {
  const auto descriptor = gCrashDescriptor.acquire();
  if (descriptor >= 0) {
    const auto record = detail::makeCrashPrimitive(
        cause, platformCode, static_cast<std::uint32_t>(::getpid()));
    static_cast<void>(::write(descriptor, &record, sizeof(record)));
  }
  gCrashDescriptor.release();
}

[[noreturn]] void terminateHandler() noexcept {
  writeCrashPrimitive(CrashCause::Terminate, 0U);
  ::_exit(134);
}

void fatalSignalHandler(int signalNumber, siginfo_t*, void*) noexcept {
  writeCrashPrimitive(CrashCause::FatalSignal,
                      static_cast<std::uint32_t>(signalNumber));
  sigset_t unblocked;
  sigemptyset(&unblocked);
  sigaddset(&unblocked, signalNumber);
  ::sigprocmask(SIG_UNBLOCK, &unblocked, nullptr);
  ::kill(::getpid(), signalNumber);
  ::_exit(128 + signalNumber);
}

struct SavedSignalAction final {
  int signalNumber{0};
  struct sigaction action {};
};

class AppKitCrashCaptureBackend final : public detail::CrashCaptureBackend {
public:
  AppKitCrashCaptureBackend(
      int descriptor, std::terminate_handler previousTerminate,
      std::array<SavedSignalAction, 5U> previousSignals)
      : descriptor_(descriptor),
        previousTerminate_(previousTerminate),
        previousSignals_(previousSignals) {}

  ~AppKitCrashCaptureBackend() override {
    if (gCrashDescriptor.load() != descriptor_) {
      return;
    }
    for (const auto& saved : previousSignals_) {
      struct sigaction current {};
      if (::sigaction(saved.signalNumber, nullptr, &current) == 0 &&
          current.sa_sigaction == fatalSignalHandler) {
        ::sigaction(saved.signalNumber, &saved.action, nullptr);
      }
    }
    const auto currentTerminate = std::set_terminate(previousTerminate_);
    if (currentTerminate != terminateHandler) {
      std::set_terminate(currentTerminate);
    }
    if (!gCrashDescriptor.retire(descriptor_, [] {
          std::this_thread::yield();
        })) {
      return;
    }
    ::close(descriptor_);
    gCrashInstalled.store(false, std::memory_order_release);
  }

private:
  int descriptor_{-1};
  std::terminate_handler previousTerminate_{nullptr};
  std::array<SavedSignalAction, 5U> previousSignals_{};
};

}

namespace detail {

core::Result<std::unique_ptr<CrashCaptureBackend>>
installCrashCaptureBackend(const std::filesystem::path& primitivePath) {
  auto expectedInstallation = false;
  if (!gCrashInstalled.compare_exchange_strong(
          expectedInstallation, true, std::memory_order_acq_rel)) {
    return core::failure<std::unique_ptr<CrashCaptureBackend>>(
        core::ErrorCode::Conflict, "Crash capture is already installed");
  }
  const auto descriptor = ::open(
      primitivePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
      S_IRUSR | S_IWUSR);
  if (descriptor < 0) {
    gCrashInstalled.store(false, std::memory_order_release);
    return core::failure<std::unique_ptr<CrashCaptureBackend>>(
        core::ErrorCode::IoError, "Unable to open crash-safe marker slot",
        primitivePath.string());
  }
  struct stat status {};
  if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
      status.st_uid != ::geteuid() || status.st_nlink != 1 ||
      ::fchmod(descriptor, S_IRUSR | S_IWUSR) != 0) {
    ::close(descriptor);
    gCrashInstalled.store(false, std::memory_order_release);
    return core::failure<std::unique_ptr<CrashCaptureBackend>>(
        core::ErrorCode::Conflict, "Crash-safe marker slot is not private",
        primitivePath.string());
  }
  gCrashDescriptor.publish(descriptor);

  const auto previousTerminate = std::set_terminate(terminateHandler);
  constexpr std::array fatalSignals{SIGABRT, SIGSEGV, SIGBUS, SIGILL, SIGFPE};
  std::array<SavedSignalAction, fatalSignals.size()> previousSignals{};
  std::size_t installed = 0U;
  for (const auto signalNumber : fatalSignals) {
    struct sigaction action {};
    action.sa_sigaction = fatalSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    struct sigaction previous {};
    if (::sigaction(signalNumber, &action, &previous) != 0) {
      for (std::size_t index = 0U; index < installed; ++index) {
        ::sigaction(previousSignals[index].signalNumber,
                    &previousSignals[index].action, nullptr);
      }
      std::set_terminate(previousTerminate);
      if (gCrashDescriptor.retire(descriptor, [] {
            std::this_thread::yield();
          })) {
        ::close(descriptor);
      }
      gCrashInstalled.store(false, std::memory_order_release);
      return core::failure<std::unique_ptr<CrashCaptureBackend>>(
          core::ErrorCode::IoError, "Unable to install fatal signal capture");
    }
    previousSignals[installed++] = SavedSignalAction{
        .signalNumber = signalNumber,
        .action = previous,
    };
  }
  return std::unique_ptr<CrashCaptureBackend>{new AppKitCrashCaptureBackend(
      descriptor, previousTerminate, previousSignals)};
}

}

std::string_view crashCaptureBackendName() noexcept {
  return "macos-signal-safe-v1";
}

}
