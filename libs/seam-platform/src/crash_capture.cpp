#include "seam/platform/crash_capture.hpp"

#include "crash_capture_internal.hpp"
#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace seam::platform {
namespace {

constexpr std::uint64_t kMaximumJsonBytes{64U * 1024U};

std::filesystem::path primitivePath(const std::filesystem::path& root) {
  return root / "crash-marker.raw";
}

std::filesystem::path contextPath(const std::filesystem::path& root) {
  return root / "crash-context.json";
}

std::filesystem::path markerPath(const std::filesystem::path& root) {
  return root / "crash-marker.json";
}

bool safeTechnicalText(std::string_view value, std::size_t maximumBytes,
                       bool allowEmpty = true) noexcept {
  if ((!allowEmpty && value.empty()) || value.size() > maximumBytes) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return character >= 0x20U && character != 0x7FU;
  });
}

bool safeHash(std::string_view value) noexcept {
  if (value.empty()) return true;
  return value.size() == 64U &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool validContext(const CrashRecoveryContext& context,
                  bool requireIdentity = true) noexcept {
  return safeTechnicalText(context.candidateId, 128U, !requireIdentity) &&
         safeTechnicalText(context.bankId, 128U) &&
         safeTechnicalText(context.bankVersion, 64U) &&
         safeHash(context.bankContentHash) &&
         safeTechnicalText(context.host, 128U, !requireIdentity) &&
         context.audioUnderflowFrames <=
             static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
         context.audioXruns <=
             static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
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

std::string crashCode(CrashCause cause) {
  switch (cause) {
    case CrashCause::Terminate: return "TERMINATE";
    case CrashCause::FatalSignal: return "FATAL_SIGNAL";
    case CrashCause::UnhandledException: return "UNHANDLED_EXCEPTION";
  }
  return "UNKNOWN_CRASH";
}

bool knownCrashCode(std::string_view code) noexcept {
  return code == "TERMINATE" || code == "FATAL_SIGNAL" ||
         code == "UNHANDLED_EXCEPTION";
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
  struct stat rootStatus {};
  if (::lstat(root.c_str(), &rootStatus) != 0 || !S_ISDIR(rootStatus.st_mode) ||
      rootStatus.st_uid != ::geteuid() ||
      (rootStatus.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
    return core::failure(core::ErrorCode::Conflict,
                         "Crash marker root ownership is invalid",
                         root.string());
  }
#endif
  return core::success();
}

core::Result<void> validateRegularMarkerPath(
    const std::filesystem::path& path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error || std::filesystem::is_symlink(status) ||
      !std::filesystem::is_regular_file(status)) {
    return core::failure(core::ErrorCode::Conflict,
                         "Crash marker is not a regular file", path.string());
  }
#ifndef _WIN32
  struct stat fileStatus {};
  if (::lstat(path.c_str(), &fileStatus) != 0 || !S_ISREG(fileStatus.st_mode) ||
      fileStatus.st_uid != ::geteuid() || fileStatus.st_nlink != 1) {
    return core::failure(core::ErrorCode::Conflict,
                         "Crash marker ownership is invalid", path.string());
  }
#endif
  return core::success();
}

formats::JsonValue contextJson(const CrashRecoveryContext& context) {
  formats::JsonValue::Object value;
  value.emplace("candidateId", context.candidateId);
  value.emplace("bankId", context.bankId);
  value.emplace("bankVersion", context.bankVersion);
  value.emplace("bankContentHash", context.bankContentHash);
  value.emplace("host", context.host);
  value.emplace("audioUnderflowFrames",
                static_cast<std::int64_t>(context.audioUnderflowFrames));
  value.emplace("audioXruns", static_cast<std::int64_t>(context.audioXruns));
  return formats::JsonValue{std::move(value)};
}

core::Result<CrashRecoveryContext> parseContext(
    const formats::JsonValue& value, bool requireIdentity = true) {
  if (!value.isObject() || value.asObject().size() != 7U) {
    return core::failure<CrashRecoveryContext>(
        core::ErrorCode::ParseError, "Crash recovery context is invalid");
  }
  const auto* candidateId = value.find("candidateId");
  const auto* bankId = value.find("bankId");
  const auto* bankVersion = value.find("bankVersion");
  const auto* bankContentHash = value.find("bankContentHash");
  const auto* host = value.find("host");
  const auto* underflow = value.find("audioUnderflowFrames");
  const auto* xruns = value.find("audioXruns");
  if (candidateId == nullptr || bankId == nullptr || bankVersion == nullptr ||
      bankContentHash == nullptr || host == nullptr || underflow == nullptr ||
      xruns == nullptr || !candidateId->isString() || !bankId->isString() ||
      !bankVersion->isString() || !bankContentHash->isString() ||
      !host->isString() || !underflow->isInteger() || !xruns->isInteger() ||
      underflow->asInt64() < 0 || xruns->asInt64() < 0) {
    return core::failure<CrashRecoveryContext>(
        core::ErrorCode::ParseError, "Crash recovery context fields are invalid");
  }
  CrashRecoveryContext context{
      .candidateId = candidateId->asString(),
      .bankId = bankId->asString(),
      .bankVersion = bankVersion->asString(),
      .bankContentHash = bankContentHash->asString(),
      .host = host->asString(),
      .audioUnderflowFrames = static_cast<std::uint64_t>(underflow->asInt64()),
      .audioXruns = static_cast<std::uint64_t>(xruns->asInt64()),
  };
  if (!validContext(context, requireIdentity)) {
    return core::failure<CrashRecoveryContext>(
        core::ErrorCode::ParseError, "Crash recovery context exceeds its bounds");
  }
  return context;
}

formats::JsonValue markerJson(const CrashMarker& marker) {
  formats::JsonValue::Object value;
  value.emplace("schemaVersion", marker.schemaVersion);
  value.emplace("purpose", marker.purpose);
  value.emplace("code", marker.code);
  value.emplace("createdAt", marker.createdAt);
  value.emplace("platformCode", static_cast<std::int64_t>(marker.platformCode));
  value.emplace("processId", static_cast<std::int64_t>(marker.processId));
  value.emplace("contextAvailable", marker.contextAvailable);
  value.emplace("context", contextJson(marker.context));
  return formats::JsonValue{std::move(value)};
}

core::Result<CrashMarker> parseMarker(const formats::JsonValue& value) {
  if (!value.isObject() || value.asObject().size() != 8U) {
    return core::failure<CrashMarker>(core::ErrorCode::ParseError,
                                      "Crash marker root is invalid");
  }
  const auto* schema = value.find("schemaVersion");
  const auto* purpose = value.find("purpose");
  const auto* code = value.find("code");
  const auto* createdAt = value.find("createdAt");
  const auto* platformCode = value.find("platformCode");
  const auto* processId = value.find("processId");
  const auto* contextAvailable = value.find("contextAvailable");
  const auto* context = value.find("context");
  if (schema == nullptr || purpose == nullptr || code == nullptr ||
      createdAt == nullptr || platformCode == nullptr || processId == nullptr ||
      contextAvailable == nullptr || context == nullptr || !schema->isInteger() ||
      !purpose->isString() || !code->isString() || !createdAt->isString() ||
      !platformCode->isInteger() || !processId->isInteger() ||
      !contextAvailable->isBool() || schema->asInt64() != 2 ||
      purpose->asString() != "local-crash-recovery" ||
      !knownCrashCode(code->asString()) || createdAt->asString().empty() ||
      createdAt->asString().size() > 64U || platformCode->asInt64() < 0 ||
      processId->asInt64() < 0 ||
      platformCode->asInt64() > std::numeric_limits<std::uint32_t>::max() ||
      processId->asInt64() > std::numeric_limits<std::uint32_t>::max()) {
    return core::failure<CrashMarker>(core::ErrorCode::ParseError,
                                      "Crash marker fields are invalid");
  }
  auto parsedContext = parseContext(*context, contextAvailable->asBool());
  if (!parsedContext) return core::Result<CrashMarker>{parsedContext.error()};
  return CrashMarker{
      .schemaVersion = schema->asInt64(),
      .purpose = purpose->asString(),
      .code = code->asString(),
      .createdAt = createdAt->asString(),
      .platformCode = static_cast<std::uint32_t>(platformCode->asInt64()),
      .processId = static_cast<std::uint32_t>(processId->asInt64()),
      .contextAvailable = contextAvailable->asBool(),
      .context = std::move(parsedContext).value(),
  };
}

core::Result<std::optional<CrashRecoveryContext>> readContext(
    const std::filesystem::path& root) {
  const auto path = contextPath(root);
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    if (error) {
      return core::failure<std::optional<CrashRecoveryContext>>(
          core::ErrorCode::IoError, "Unable to inspect crash recovery context",
          error.message());
    }
    return std::optional<CrashRecoveryContext>{};
  }
  auto validPath = validateRegularMarkerPath(path);
  if (!validPath) {
    return core::Result<std::optional<CrashRecoveryContext>>{validPath.error()};
  }
  auto text = core::readTextFileLimited(path, kMaximumJsonBytes);
  if (!text) {
    return core::Result<std::optional<CrashRecoveryContext>>{text.error()};
  }
  auto parsed = formats::parseJson(text.value(), formats::JsonParseLimits{
      .maximumInputBytes = kMaximumJsonBytes,
      .maximumDepth = 8U,
      .maximumNodes = 32U,
      .maximumStringBytes = 4096U,
      .maximumCollectionEntries = 16U});
  if (!parsed || !parsed.value().isObject() ||
      parsed.value().asObject().size() != 3U) {
    return core::failure<std::optional<CrashRecoveryContext>>(
        core::ErrorCode::ParseError, "Crash recovery context document is invalid");
  }
  const auto* schema = parsed.value().find("schemaVersion");
  const auto* purpose = parsed.value().find("purpose");
  const auto* context = parsed.value().find("context");
  if (schema == nullptr || purpose == nullptr || context == nullptr ||
      !schema->isInteger() || schema->asInt64() != 1 || !purpose->isString() ||
      purpose->asString() != "crash-recovery-context") {
    return core::failure<std::optional<CrashRecoveryContext>>(
        core::ErrorCode::ParseError, "Crash recovery context document fields are invalid");
  }
  auto parsedContext = parseContext(*context);
  if (!parsedContext) {
    return core::Result<std::optional<CrashRecoveryContext>>{
        parsedContext.error()};
  }
  return std::optional<CrashRecoveryContext>{std::move(parsedContext).value()};
}

core::Result<std::optional<CrashMarker>> readMarkerAt(
    const std::filesystem::path& root) {
  const auto path = markerPath(root);
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    if (error) {
      return core::failure<std::optional<CrashMarker>>(
          core::ErrorCode::IoError, "Unable to inspect crash marker",
          error.message());
    }
    return std::optional<CrashMarker>{};
  }
  auto validPath = validateRegularMarkerPath(path);
  if (!validPath) return core::Result<std::optional<CrashMarker>>{validPath.error()};
  auto text = core::readTextFileLimited(path, kMaximumJsonBytes);
  if (!text) return core::Result<std::optional<CrashMarker>>{text.error()};
  auto parsed = formats::parseJson(text.value(), formats::JsonParseLimits{
      .maximumInputBytes = kMaximumJsonBytes,
      .maximumDepth = 8U,
      .maximumNodes = 48U,
      .maximumStringBytes = 4096U,
      .maximumCollectionEntries = 24U});
  if (!parsed) return core::Result<std::optional<CrashMarker>>{parsed.error()};
  auto marker = parseMarker(parsed.value());
  if (!marker) return core::Result<std::optional<CrashMarker>>{marker.error()};
  return std::optional<CrashMarker>{std::move(marker).value()};
}

core::Result<void> removeRegularMarker(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    return error ? core::failure(core::ErrorCode::IoError,
                                 "Unable to inspect crash marker", error.message())
                 : core::success();
  }
  auto validPath = validateRegularMarkerPath(path);
  if (!validPath) return validPath;
  if (!std::filesystem::remove(path, error) || error) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to clear crash marker", error.message());
  }
  return core::success();
}

}

CrashCapture::CrashCapture(
    std::filesystem::path root,
    std::unique_ptr<detail::CrashCaptureBackend> backend)
    : root_(std::move(root)), backend_(std::move(backend)) {}

CrashCapture::~CrashCapture() = default;

core::Result<std::unique_ptr<CrashCapture>> CrashCapture::install(
    CrashCaptureConfig config) {
  auto root = ensurePrivateRoot(config.root);
  if (!root) return core::Result<std::unique_ptr<CrashCapture>>{root.error()};
  auto pending = readPending(config);
  if (!pending) {
    return core::Result<std::unique_ptr<CrashCapture>>{pending.error()};
  }
  if (pending.value().has_value()) {
    return core::failure<std::unique_ptr<CrashCapture>>(
        core::ErrorCode::Conflict,
        "Pending crash marker must be recovered before capture is armed");
  }
  auto backend = detail::installCrashCaptureBackend(primitivePath(config.root));
  if (!backend) {
    return core::Result<std::unique_ptr<CrashCapture>>{backend.error()};
  }
  return std::unique_ptr<CrashCapture>{
      new CrashCapture(std::move(config.root), std::move(backend).value())};
}

core::Result<std::optional<PendingCrash>> CrashCapture::readPending(
    const CrashCaptureConfig& config) {
  auto root = ensurePrivateRoot(config.root);
  if (!root) return core::Result<std::optional<PendingCrash>>{root.error()};
  const auto path = primitivePath(config.root);
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    if (error) {
      return core::failure<std::optional<PendingCrash>>(
          core::ErrorCode::IoError, "Unable to inspect pending crash marker",
          error.message());
    }
    return std::optional<PendingCrash>{};
  }
  auto validPath = validateRegularMarkerPath(path);
  if (!validPath) {
    return core::Result<std::optional<PendingCrash>>{validPath.error()};
  }
  auto bytes = core::readFileBytesLimited(
      path, sizeof(detail::CrashPrimitiveRecord) + 1U);
  if (!bytes) return core::Result<std::optional<PendingCrash>>{bytes.error()};
  if (bytes.value().empty()) return std::optional<PendingCrash>{};
  if (bytes.value().size() != sizeof(detail::CrashPrimitiveRecord)) {
    return core::failure<std::optional<PendingCrash>>(
        core::ErrorCode::ParseError, "Pending crash marker length is invalid");
  }
  detail::CrashPrimitiveRecord record;
  std::memcpy(&record, bytes.value().data(), sizeof(record));
  const auto causeValue = static_cast<CrashCause>(record.cause);
  const auto knownCause = causeValue == CrashCause::Terminate ||
                          causeValue == CrashCause::FatalSignal ||
                          causeValue == CrashCause::UnhandledException;
  const auto cleanPadding =
      record.reserved == 0U &&
      std::all_of(record.padding.begin(), record.padding.end(),
                  [](std::uint8_t byte) { return byte == 0U; });
  if (record.magic != detail::kCrashPrimitiveMagic ||
      record.version != detail::kCrashPrimitiveVersion || !knownCause ||
      !cleanPadding || record.commit != detail::kCrashPrimitiveCommit ||
      record.checksum != detail::crashPrimitiveChecksum(
                             causeValue, record.platformCode, record.processId)) {
    return core::failure<std::optional<PendingCrash>>(
        core::ErrorCode::ParseError, "Pending crash marker integrity is invalid");
  }
  return std::optional<PendingCrash>{PendingCrash{
      .cause = causeValue,
      .platformCode = record.platformCode,
      .processId = record.processId,
  }};
}

core::Result<std::optional<CrashMarker>> CrashCapture::recoverPending(
    const CrashCaptureConfig& config) {
  auto pending = readPending(config);
  if (!pending) return core::Result<std::optional<CrashMarker>>{pending.error()};
  if (!pending.value().has_value()) return readMarkerAt(config.root);

  CrashRecoveryContext context;
  bool contextAvailable = false;
  auto storedContext = readContext(config.root);
  if (storedContext && storedContext.value().has_value()) {
    context = std::move(*storedContext.value());
    contextAvailable = true;
  }
  const auto& raw = *pending.value();
  CrashMarker marker{
      .schemaVersion = 2,
      .purpose = "local-crash-recovery",
      .code = crashCode(raw.cause),
      .createdAt = timestampNow(),
      .platformCode = raw.platformCode,
      .processId = raw.processId,
      .contextAvailable = contextAvailable,
      .context = std::move(context),
  };
  auto written = core::durableAtomicWriteText(
      markerPath(config.root),
      formats::stringifyJson(markerJson(marker), true));
  if (!written) return core::Result<std::optional<CrashMarker>>{written.error()};
#ifndef _WIN32
  if (::chmod(markerPath(config.root).c_str(), S_IRUSR | S_IWUSR) != 0) {
    return core::failure<std::optional<CrashMarker>>(
        core::ErrorCode::IoError, "Unable to restrict crash marker permissions");
  }
#endif
  auto acknowledged = removeRegularMarker(primitivePath(config.root));
  if (!acknowledged) {
    return core::Result<std::optional<CrashMarker>>{acknowledged.error()};
  }
  return std::optional<CrashMarker>{std::move(marker)};
}

core::Result<void> CrashCapture::updateContext(
    const CrashRecoveryContext& context) const {
  auto root = ensurePrivateRoot(root_);
  if (!root) return root;
  if (!validContext(context)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Crash recovery context is invalid");
  }
  formats::JsonValue::Object document;
  document.emplace("schemaVersion", std::int64_t{1});
  document.emplace("purpose", "crash-recovery-context");
  document.emplace("context", contextJson(context));
  auto written = core::durableAtomicWriteText(
      contextPath(root_),
      formats::stringifyJson(formats::JsonValue{std::move(document)}, true));
  if (!written) return written;
#ifndef _WIN32
  if (::chmod(contextPath(root_).c_str(), S_IRUSR | S_IWUSR) != 0) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to restrict crash recovery context permissions");
  }
#endif
  return core::success();
}

core::Result<std::optional<CrashMarker>> CrashCapture::readMarker() const {
  auto root = ensurePrivateRoot(root_);
  if (!root) return core::Result<std::optional<CrashMarker>>{root.error()};
  return readMarkerAt(root_);
}

core::Result<void> CrashCapture::clearMarker() const {
  auto root = ensurePrivateRoot(root_);
  if (!root) return root;
  return removeRegularMarker(markerPath(root_));
}

}
