#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace seam::core {

enum class LogLevel { Trace, Debug, Info, Warning, Error };

[[nodiscard]] std::string_view toString(LogLevel level) noexcept;

enum class LogPrivacyClass {
  PublicTechnical,
  ExportSafe,
  LocalPrivate,
  Forbidden,
};

struct LogField final {
  std::string key;
  std::string value;
  LogPrivacyClass privacy{LogPrivacyClass::LocalPrivate};
};

struct LogEvent final {
  std::string code;
  LogLevel level{LogLevel::Info};
  std::string category;
  std::string message;
  std::vector<LogField> fields;
  std::size_t occurrenceCount{1U};
};

[[nodiscard]] std::string_view toString(LogPrivacyClass privacy) noexcept;

[[nodiscard]] std::string exportSafeProjection(const LogEvent& event,
                                               std::size_t maxBytes = 4096U);

[[nodiscard]] bool isValidLogEvent(const LogEvent& event) noexcept;

}
