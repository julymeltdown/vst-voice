#pragma once

#include "seam/core/log_event.hpp"

#include <mutex>
#include <ostream>
#include <string_view>

namespace seam::core {

class ILogger {
public:
  virtual ~ILogger() = default;
  virtual void write(LogLevel level, std::string_view category, std::string_view message) = 0;
  virtual void writeEvent(const LogEvent& event) {
    write(event.level, event.category, exportSafeProjection(event));
  }
};

class NullLogger final : public ILogger {
public:
  void write(LogLevel, std::string_view, std::string_view) override {}
};

class StreamLogger final : public ILogger {
public:
  explicit StreamLogger(std::ostream& stream) : stream_(stream) {}
  void write(LogLevel level, std::string_view category, std::string_view message) override;
  void writeEvent(const LogEvent& event) override;

private:
  std::ostream& stream_;
  std::mutex mutex_;
};

[[nodiscard]] std::string_view toString(LogLevel level) noexcept;

}
