#include "crash_capture_internal.hpp"

#include <atomic>
#include <cstdint>
#include <exception>
#include <process.h>
#include <sddl.h>
#include <thread>
#include <windows.h>

namespace seam::platform {
namespace {

detail::CrashWriterSlot<std::uintptr_t, 0U> gCrashHandle;
std::atomic<bool> gCrashInstalled{false};

static_assert(std::atomic<bool>::is_always_lock_free);

void writeCrashPrimitive(CrashCause cause,
                         std::uint32_t platformCode) noexcept {
  const auto value = gCrashHandle.acquire();
  if (value != 0U) {
    const auto handle = reinterpret_cast<HANDLE>(value);
    const auto record = detail::makeCrashPrimitive(
        cause, platformCode, static_cast<std::uint32_t>(::GetCurrentProcessId()));
    LARGE_INTEGER start{};
    DWORD written = 0U;
    if (::SetFilePointerEx(handle, start, nullptr, FILE_BEGIN) != 0) {
      ::WriteFile(handle, &record, static_cast<DWORD>(sizeof(record)), &written,
                  nullptr);
    }
  }
  gCrashHandle.release();
}

LONG WINAPI unhandledExceptionHandler(EXCEPTION_POINTERS* exception) noexcept {
  const auto code = exception != nullptr && exception->ExceptionRecord != nullptr
                        ? exception->ExceptionRecord->ExceptionCode
                        : 0U;
  writeCrashPrimitive(CrashCause::UnhandledException, code);
  return EXCEPTION_CONTINUE_SEARCH;
}

[[noreturn]] void terminateHandler() noexcept {
  writeCrashPrimitive(CrashCause::Terminate, 0U);
  ::TerminateProcess(::GetCurrentProcess(), 3U);
  ::_exit(3);
}

class Win32CrashCaptureBackend final : public detail::CrashCaptureBackend {
public:
  Win32CrashCaptureBackend(
      HANDLE handle, LPTOP_LEVEL_EXCEPTION_FILTER previousException,
      std::terminate_handler previousTerminate)
      : handle_(handle),
        previousException_(previousException),
        previousTerminate_(previousTerminate) {}

  ~Win32CrashCaptureBackend() override {
    if (gCrashHandle.load() !=
        reinterpret_cast<std::uintptr_t>(handle_)) {
      return;
    }
    const auto currentException =
        ::SetUnhandledExceptionFilter(previousException_);
    if (currentException != unhandledExceptionHandler) {
      ::SetUnhandledExceptionFilter(currentException);
    }
    const auto currentTerminate = std::set_terminate(previousTerminate_);
    if (currentTerminate != terminateHandler) {
      std::set_terminate(currentTerminate);
    }
    if (!gCrashHandle.retire(reinterpret_cast<std::uintptr_t>(handle_), [] {
          std::this_thread::yield();
        })) {
      return;
    }
    ::CloseHandle(handle_);
    gCrashInstalled.store(false, std::memory_order_release);
  }

private:
  HANDLE handle_{INVALID_HANDLE_VALUE};
  LPTOP_LEVEL_EXCEPTION_FILTER previousException_{nullptr};
  std::terminate_handler previousTerminate_{nullptr};
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
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (::ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"D:P(A;;GA;;;SY)(A;;GA;;;OW)", SDDL_REVISION_1, &descriptor,
          nullptr) == 0) {
    gCrashInstalled.store(false, std::memory_order_release);
    return core::failure<std::unique_ptr<CrashCaptureBackend>>(
        core::ErrorCode::IoError,
        "Unable to create private crash marker security descriptor");
  }
  SECURITY_ATTRIBUTES attributes{
      .nLength = sizeof(SECURITY_ATTRIBUTES),
      .lpSecurityDescriptor = descriptor,
      .bInheritHandle = FALSE,
  };
  const auto handle = ::CreateFileW(
      primitivePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0U, &attributes,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_OPEN_REPARSE_POINT |
          FILE_FLAG_WRITE_THROUGH,
      nullptr);
  ::LocalFree(descriptor);
  if (handle == INVALID_HANDLE_VALUE) {
    gCrashInstalled.store(false, std::memory_order_release);
    return core::failure<std::unique_ptr<CrashCaptureBackend>>(
        core::ErrorCode::IoError, "Unable to open crash-safe marker slot",
        primitivePath.string());
  }
  FILE_ATTRIBUTE_TAG_INFO tag{};
  BY_HANDLE_FILE_INFORMATION info{};
  LARGE_INTEGER start{};
  if (::GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag,
                                     sizeof(tag)) == 0 ||
      (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
      (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U ||
      ::GetFileInformationByHandle(handle, &info) == 0 ||
      info.nNumberOfLinks != 1U ||
      ::SetFilePointerEx(handle, start, nullptr, FILE_BEGIN) == 0 ||
      ::SetEndOfFile(handle) == 0) {
    ::CloseHandle(handle);
    gCrashInstalled.store(false, std::memory_order_release);
    return core::failure<std::unique_ptr<CrashCaptureBackend>>(
        core::ErrorCode::Conflict, "Crash-safe marker slot is not private",
        primitivePath.string());
  }
  gCrashHandle.publish(reinterpret_cast<std::uintptr_t>(handle));
  const auto previousException =
      ::SetUnhandledExceptionFilter(unhandledExceptionHandler);
  const auto previousTerminate = std::set_terminate(terminateHandler);
  return std::unique_ptr<CrashCaptureBackend>{new Win32CrashCaptureBackend(
      handle, previousException, previousTerminate)};
}

}

std::string_view crashCaptureBackendName() noexcept {
  return "windows-exception-safe-v1";
}

}
