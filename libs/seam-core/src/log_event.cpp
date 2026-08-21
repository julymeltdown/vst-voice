#include "seam/core/log_event.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>

namespace seam::core {
namespace {

constexpr std::size_t kMaximumCodeBytes = 96U;
constexpr std::size_t kMaximumCategoryBytes = 96U;
constexpr std::size_t kMaximumMessageBytes = 512U;
constexpr std::size_t kMaximumFieldCount = 32U;
constexpr std::size_t kMaximumFieldKeyBytes = 64U;
constexpr std::size_t kMaximumFieldValueBytes = 512U;

bool boundedUtf8(std::string_view value, std::size_t maximum) noexcept {
  if (value.empty() || value.size() > maximum) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char value) {
    return value >= 0x20U && value != 0x7FU;
  });
}

}

std::string_view toString(LogPrivacyClass privacy) noexcept {
  switch (privacy) {
    case LogPrivacyClass::PublicTechnical: return "PUBLIC_TECHNICAL";
    case LogPrivacyClass::ExportSafe: return "EXPORT_SAFE";
    case LogPrivacyClass::LocalPrivate: return "LOCAL_PRIVATE";
    case LogPrivacyClass::Forbidden: return "FORBIDDEN";
  }
  return "UNKNOWN";
}

bool isValidLogEvent(const LogEvent& event) noexcept {
  if (!boundedUtf8(event.code, kMaximumCodeBytes) ||
      !boundedUtf8(event.category, kMaximumCategoryBytes) ||
      !boundedUtf8(event.message, kMaximumMessageBytes) ||
      event.occurrenceCount == 0U || event.fields.size() > kMaximumFieldCount) {
    return false;
  }
  for (const auto& field : event.fields) {
    if (!boundedUtf8(field.key, kMaximumFieldKeyBytes) ||
        field.value.size() > kMaximumFieldValueBytes) {
      return false;
    }
  }
  return true;
}

std::string exportSafeProjection(const LogEvent& event, std::size_t maxBytes) {
  if (maxBytes == 0U) return {};
  std::ostringstream stream;
  stream << "code=" << event.code << ";level=" << toString(event.level)
         << ";category=" << event.category << ";occurrences="
         << event.occurrenceCount;
  if (!event.message.empty()) stream << ";message=" << event.message;
  for (const auto& field : event.fields) {
    if (field.privacy != LogPrivacyClass::PublicTechnical &&
        field.privacy != LogPrivacyClass::ExportSafe) {
      continue;
    }
    stream << ";" << field.key << "=" << field.value;
    if (stream.tellp() >= static_cast<std::streamoff>(maxBytes)) break;
  }
  auto result = stream.str();
  if (result.size() > maxBytes) result.resize(maxBytes);
  return result;
}

}
