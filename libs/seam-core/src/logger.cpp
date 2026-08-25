#include "seam/core/logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>

namespace seam::core {

std::string_view toString(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
  }
  return "UNKNOWN";
}

void StreamLogger::write(LogLevel level, std::string_view category, std::string_view message) {
  recordRealtimeLogger();
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &time);
#else
  localtime_r(&time, &local);
#endif

  recordRealtimeLockAttempt();
  const std::lock_guard lock(mutex_);
  stream_ << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << " [" << toString(level)
          << "] [" << category << "] " << message << '\n';
}

void StreamLogger::writeEvent(const LogEvent& event) {
  write(event.level, event.category, exportSafeProjection(event));
}

}  // namespace seam::core
