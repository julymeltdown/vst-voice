#include "seam/platform/crash_capture.hpp"

#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <exception>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <system_error>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace seam::platform {
namespace {

std::mutex gHandlerMutex;
CrashCapture* gActiveCapture = nullptr;
std::terminate_handler gPreviousHandler = nullptr;

std::filesystem::path markerPath(const std::filesystem::path& root) {
  return root / "crash-marker.json";
}

bool safeCode(std::string_view code) noexcept {
  if (code.empty() || code.size() > 64U ||
      code.front() < 'A' || code.front() > 'Z') {
    return false;
  }
  return std::all_of(code.begin() + 1, code.end(), [](char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '_' ||
           character == '.' || character == '-';
  });
}

std::string timestampNow() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

core::Result<void> ensurePrivateRoot(const std::filesystem::path& root) {
  if (root.empty() || root == root.root_path()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Crash marker root is invalid");
  }
  std::error_code error;
  if (std::filesystem::exists(root, error)) {
    const auto status = std::filesystem::symlink_status(root, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
      return core::failure(core::ErrorCode::Conflict,
                           "Crash marker root must be a real directory",
                           root.string());
    }
  } else {
    std::filesystem::create_directories(root, error);
    if (error) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create crash marker root", error.message());
    }
  }
#ifndef _WIN32
  if (::chmod(root.c_str(), S_IRWXU) != 0) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to restrict crash marker root permissions",
                         root.string());
  }
#endif
  return core::success();
}

void terminateHandler() noexcept {
  CrashCapture* capture = nullptr;
  std::terminate_handler previous = nullptr;
  {
    const std::lock_guard lock(gHandlerMutex);
    capture = gActiveCapture;
    previous = gPreviousHandler;
  }
  if (capture != nullptr) static_cast<void>(capture->writeMarker("TERMINATE"));
  if (previous != nullptr) {
    previous();
  }
  std::abort();
}

core::Result<CrashMarker> parseMarker(const formats::JsonValue& root) {
  if (!root.isObject()) {
    return core::failure<CrashMarker>(core::ErrorCode::ParseError,
                                      "Crash marker root must be an object");
  }
  const auto* schema = root.find("schemaVersion");
  const auto* purpose = root.find("purpose");
  const auto* code = root.find("code");
  const auto* createdAt = root.find("createdAt");
  if (schema == nullptr || purpose == nullptr || code == nullptr ||
      createdAt == nullptr || !schema->isInteger() || !purpose->isString() ||
      !code->isString() || !createdAt->isString() || schema->asInt64() != 1 ||
      purpose->asString() != "local-crash-marker" ||
      !safeCode(code->asString()) || createdAt->asString().empty()) {
    return core::failure<CrashMarker>(core::ErrorCode::ParseError,
                                      "Crash marker fields are invalid");
  }
  return CrashMarker{.schemaVersion = schema->asInt64(),
                     .purpose = purpose->asString(),
                     .code = code->asString(),
                     .createdAt = createdAt->asString()};
}

}

core::Result<std::unique_ptr<CrashCapture>> CrashCapture::install(
    CrashCaptureConfig config) {
  auto root = ensurePrivateRoot(config.root);
  if (!root) return core::Result<std::unique_ptr<CrashCapture>>{root.error()};
  auto capture = std::unique_ptr<CrashCapture>{new CrashCapture(std::move(config.root))};
  {
    const std::lock_guard lock(gHandlerMutex);
    if (gActiveCapture == nullptr) {
      gPreviousHandler = std::set_terminate(terminateHandler);
      gActiveCapture = capture.get();
    }
  }
  return capture;
}

CrashCapture::~CrashCapture() {
  const std::lock_guard lock(gHandlerMutex);
  if (gActiveCapture == this) {
    gActiveCapture = nullptr;
    static_cast<void>(std::set_terminate(gPreviousHandler));
    gPreviousHandler = nullptr;
  }
}

core::Result<CrashMarker> CrashCapture::writeMarker(std::string_view code) const {
  auto root = ensurePrivateRoot(root_);
  if (!root) return core::Result<CrashMarker>{root.error()};
  if (!safeCode(code)) {
    return core::failure<CrashMarker>(core::ErrorCode::InvalidArgument,
                                      "Crash marker code is invalid");
  }
  const CrashMarker marker{.schemaVersion = 1,
                           .purpose = "local-crash-marker",
                           .code = std::string{code},
                           .createdAt = timestampNow()};
  formats::JsonValue::Object value;
  value.emplace("schemaVersion", marker.schemaVersion);
  value.emplace("purpose", marker.purpose);
  value.emplace("code", marker.code);
  value.emplace("createdAt", marker.createdAt);
  auto written = core::durableAtomicWriteText(
      markerPath(root_), formats::stringifyJson(formats::JsonValue{std::move(value)}, true));
  if (!written) return core::Result<CrashMarker>{written.error()};
#ifndef _WIN32
  if (::chmod(markerPath(root_).c_str(), S_IRUSR | S_IWUSR) != 0) {
    return core::failure<CrashMarker>(core::ErrorCode::IoError,
                                      "Unable to restrict crash marker permissions");
  }
#endif
  return marker;
}

core::Result<std::optional<CrashMarker>> CrashCapture::readMarker() const {
  auto root = ensurePrivateRoot(root_);
  if (!root) return core::Result<std::optional<CrashMarker>>{root.error()};
  std::error_code error;
  const auto path = markerPath(root_);
  if (!std::filesystem::exists(path, error)) {
    if (error) {
      return core::failure<std::optional<CrashMarker>>(
          core::ErrorCode::IoError, "Unable to inspect crash marker", error.message());
    }
    return std::optional<CrashMarker>{};
  }
  const auto status = std::filesystem::symlink_status(path, error);
  if (error || std::filesystem::is_symlink(status) ||
      !std::filesystem::is_regular_file(status)) {
    return core::failure<std::optional<CrashMarker>>(
        core::ErrorCode::Conflict, "Crash marker is not a regular file", path.string());
  }
  auto text = core::readTextFileLimited(path, 64U * 1024U);
  if (!text) return core::Result<std::optional<CrashMarker>>{text.error()};
  auto parsed = formats::parseJson(text.value(), formats::JsonParseLimits{
      .maximumInputBytes = 64U * 1024U,
      .maximumDepth = 8U,
      .maximumNodes = 32U,
      .maximumStringBytes = 4096U,
      .maximumCollectionEntries = 16U});
  if (!parsed) return core::Result<std::optional<CrashMarker>>{parsed.error()};
  auto marker = parseMarker(parsed.value());
  if (!marker) return core::Result<std::optional<CrashMarker>>{marker.error()};
  return std::optional<CrashMarker>{marker.value()};
}

core::Result<void> CrashCapture::clearMarker() const {
  auto root = ensurePrivateRoot(root_);
  if (!root) return root;
  const auto path = markerPath(root_);
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    return error ? core::failure(core::ErrorCode::IoError,
                                 "Unable to inspect crash marker", error.message())
                 : core::success();
  }
  const auto status = std::filesystem::symlink_status(path, error);
  if (error || std::filesystem::is_symlink(status) ||
      !std::filesystem::is_regular_file(status)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Crash marker is not a regular file", path.string());
  }
  if (!std::filesystem::remove(path, error) || error) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to clear crash marker", error.message());
  }
  return core::success();
}

}
