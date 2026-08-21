#include "seam/authoring/autosave_service.hpp"

#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <charconv>
#include <system_error>
#include <utility>

namespace seam::authoring {
namespace {

constexpr std::string_view kFormat = "com.project-seam.autosave";
constexpr std::int64_t kSchemaVersion = 1;
constexpr std::uint64_t kMaximumMetadataBytes = 256U * 1024U;

std::string pathToUtf8(const std::filesystem::path& path) {
  const auto bytes = path.generic_u8string();
  return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::filesystem::path pathFromUtf8(std::string_view text) {
  std::u8string bytes;
  bytes.resize(text.size());
  std::transform(text.begin(), text.end(), bytes.begin(),
                 [](char value) { return static_cast<char8_t>(value); });
  return std::filesystem::path{bytes};
}

std::int64_t unixMilliseconds(std::chrono::system_clock::time_point value) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             value.time_since_epoch())
      .count();
}

std::string stableAutosaveId(const domain::Project& project,
                             const std::optional<std::filesystem::path>& path) {
  std::string identity = project.id().toString();
  identity.push_back('|');
  identity += path.has_value() ? pathToUtf8(path->lexically_normal())
                               : std::string{"<unsaved>"};
  return core::sha256Hex(identity).substr(0U, 32U);
}

std::string generationStem(const AutosaveService::Snapshot& snapshot) {
  return "revision-" + std::to_string(snapshot.revision) + "-" +
         std::to_string(snapshot.createdAtUnixMs) + "-" +
         std::to_string(snapshot.sequence);
}

formats::JsonValue metadataValue(const AutosaveService::Snapshot& snapshot,
                                 std::string projectFileName) {
  return formats::JsonValue{formats::JsonValue::Object{
      {"format", formats::JsonValue{std::string{kFormat}}},
      {"schemaVersion", formats::JsonValue{kSchemaVersion}},
      {"projectId", formats::JsonValue{snapshot.project.id().toString()}},
      {"baseProjectHash", formats::JsonValue{snapshot.baseProjectHash}},
      {"revision", formats::JsonValue{static_cast<std::int64_t>(snapshot.revision)}},
      {"createdAtUnixMs", formats::JsonValue{snapshot.createdAtUnixMs}},
      {"projectFile", formats::JsonValue{std::move(projectFileName)}},
      {"originalProjectPath",
       formats::JsonValue{snapshot.explicitProjectPath.has_value()
                              ? pathToUtf8(*snapshot.explicitProjectPath)
                              : std::string{}}},
  }};
}

core::Result<RecoveryCandidate> parseMetadata(
    const std::filesystem::path& metadataPath) {
  auto text = core::readTextFileLimited(metadataPath, kMaximumMetadataBytes);
  if (!text) return core::Result<RecoveryCandidate>{text.error()};
  auto parsed = formats::parseJson(
      text.value(), formats::JsonParseLimits{
                        .maximumInputBytes = kMaximumMetadataBytes,
                        .maximumDepth = 8U,
                        .maximumNodes = 64U,
                        .maximumStringBytes = 64U * 1024U,
                        .maximumCollectionEntries = 32U,
                    });
  if (!parsed) return core::Result<RecoveryCandidate>{parsed.error()};
  if (!parsed.value().isObject()) {
    return core::failure<RecoveryCandidate>(core::ErrorCode::ParseError,
                                             "Autosave metadata must be an object");
  }
  const auto* format = parsed.value().find("format");
  const auto* schema = parsed.value().find("schemaVersion");
  const auto* projectId = parsed.value().find("projectId");
  const auto* baseProjectHash = parsed.value().find("baseProjectHash");
  const auto* revision = parsed.value().find("revision");
  const auto* created = parsed.value().find("createdAtUnixMs");
  const auto* projectFile = parsed.value().find("projectFile");
  const auto* original = parsed.value().find("originalProjectPath");
  if (format == nullptr || schema == nullptr || projectId == nullptr ||
      revision == nullptr || created == nullptr || projectFile == nullptr ||
      original == nullptr || !format->isString() || !schema->isNumber() ||
      !projectId->isString() || !revision->isNumber() || !created->isNumber() ||
      !projectFile->isString() || !original->isString() ||
      format->asString() != kFormat || schema->asInt64() != kSchemaVersion ||
      revision->asInt64() < 0 || projectFile->asString().empty()) {
    return core::failure<RecoveryCandidate>(core::ErrorCode::ParseError,
                                             "Autosave metadata is invalid");
  }
  const auto relative = pathFromUtf8(projectFile->asString());
  if (relative.is_absolute() || relative.has_parent_path() ||
      relative.filename() != relative) {
    return core::failure<RecoveryCandidate>(core::ErrorCode::ParseError,
                                             "Autosave project file is unsafe");
  }
  RecoveryCandidate result{
      .autosavePath = metadataPath.parent_path() / relative,
      .metadataPath = metadataPath,
      .originalProjectPath = std::nullopt,
      .projectId = projectId->asString(),
      .baseProjectHash = baseProjectHash != nullptr && baseProjectHash->isString()
                             ? baseProjectHash->asString()
                             : std::string{},
      .revision = static_cast<std::uint64_t>(revision->asInt64()),
      .createdAtUnixMs = created->asInt64(),
      .recoverable = false,
      .diagnostic = {},
  };
  if (!original->asString().empty()) {
    result.originalProjectPath = pathFromUtf8(original->asString());
  }
  return result;
}

bool metadataFile(const std::filesystem::path& path) {
  const auto name = path.filename().string();
  return name.size() > 10U &&
         name.ends_with(".meta.json");
}

}  // namespace

AutosaveService::AutosaveService(AutosaveConfig config)
    : config_(std::move(config)),
      worker_([this](std::stop_token token) { workerLoop(token); }) {
  if (!config_.wallClock) {
    config_.wallClock = [] { return std::chrono::system_clock::now(); };
  }
}

AutosaveService::~AutosaveService() { shutdown(); }

core::Result<void> AutosaveService::validateConfig() const {
  if (config_.root.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Autosave root cannot be empty");
  }
  if (config_.interval <= std::chrono::seconds{0} ||
      config_.minimumCommandDelay < std::chrono::seconds{0} ||
      config_.commandThreshold == 0U || config_.maximumGenerations == 0U ||
      config_.maximumGenerations > 100U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Autosave policy is invalid");
  }
  return core::success();
}

AutosaveService::Snapshot AutosaveService::snapshot(
    const ProjectDocument& document) {
  const auto explicitPath = document.identity().projectPath;
  return Snapshot{
      .project = document.session().project(),
      .explicitProjectPath = explicitPath,
      .revision = document.session().revision(),
      .stableId = stableAutosaveId(document.session().project(), explicitPath),
      .baseProjectHash = document.identity().baseProjectHash,
      .createdAtUnixMs = unixMilliseconds(config_.wallClock()),
      .sequence = ++sequence_,
  };
}

core::Result<void> AutosaveService::request(const ProjectDocument& document) {
  const auto configValidation = validateConfig();
  if (!configValidation) return configValidation;
  if (!document.dirty()) return core::success();
  auto value = snapshot(document);
  {
    std::lock_guard lock(mutex_);
    if (stopped_) {
      return core::failure(core::ErrorCode::InvalidState,
                           "Autosave service is stopped");
    }
    pending_ = std::move(value);
    lastError_.reset();
  }
  condition_.notify_all();
  return core::success();
}

core::Result<void> AutosaveService::onSuccessfulCommand(
    const ProjectDocument& document,
    std::chrono::steady_clock::time_point now) {
  if (!document.dirty()) {
    successfulCommands_ = 0U;
    return core::success();
  }
  ++successfulCommands_;
  if (successfulCommands_ < config_.commandThreshold ||
      now - lastRequestedAt_ < config_.minimumCommandDelay) {
    return core::success();
  }
  successfulCommands_ = 0U;
  lastRequestedAt_ = now;
  return request(document);
}

core::Result<void> AutosaveService::tick(
    const ProjectDocument& document,
    std::chrono::steady_clock::time_point now) {
  if (!document.dirty() || now - lastRequestedAt_ < config_.interval) {
    return core::success();
  }
  successfulCommands_ = 0U;
  lastRequestedAt_ = now;
  return request(document);
}

core::Result<void> AutosaveService::flush() {
  std::unique_lock lock(mutex_);
  condition_.wait(lock, [this] { return !pending_.has_value() && !writing_; });
  if (lastError_.has_value()) return core::Result<void>{*lastError_};
  return core::success();
}

core::Result<std::vector<RecoveryCandidate>> AutosaveService::discover() const {
  const auto validation = validateConfig();
  if (!validation) {
    return core::Result<std::vector<RecoveryCandidate>>{validation.error()};
  }
  std::vector<RecoveryCandidate> result;
  std::error_code error;
  if (!std::filesystem::exists(config_.root, error)) return result;
  if (error) {
    return core::failure<std::vector<RecoveryCandidate>>(
        core::ErrorCode::IoError, "Unable to inspect autosave root",
        error.message());
  }
  for (std::filesystem::recursive_directory_iterator iterator{
           config_.root,
           std::filesystem::directory_options::skip_permission_denied, error},
       end;
       iterator != end; iterator.increment(error)) {
    if (error) {
      error.clear();
      continue;
    }
    if (!iterator->is_regular_file(error) || error ||
        !metadataFile(iterator->path())) {
      error.clear();
      continue;
    }
    auto metadata = parseMetadata(iterator->path());
    if (!metadata) continue;
    auto candidate = std::move(metadata).value();
    auto loaded = codec_.load(candidate.autosavePath);
    if (!loaded) {
      candidate.recoverable = false;
      candidate.diagnostic = loaded.error().message;
    } else if (loaded.value().id().toString() != candidate.projectId) {
      candidate.recoverable = false;
      candidate.diagnostic = "Autosave project ID does not match metadata";
    } else {
      bool recoverable = true;
      if (candidate.originalProjectPath.has_value() &&
          !candidate.baseProjectHash.empty() &&
          std::filesystem::exists(*candidate.originalProjectPath, error) &&
          !error) {
        auto original = core::readFileBytesLimited(
            *candidate.originalProjectPath, 64ULL * 1024ULL * 1024ULL);
        if (!original) {
          recoverable = false;
          candidate.diagnostic = original.error().message;
        } else {
          const auto currentHash = core::sha256Hex(original.value());
          recoverable = currentHash == candidate.baseProjectHash;
          if (!recoverable) {
            candidate.diagnostic =
                "Saved project changed externally after this autosave lineage";
          }
        }
        error.clear();
      }
      candidate.recoverable = recoverable;
      if (candidate.recoverable && candidate.diagnostic.empty()) {
        candidate.diagnostic.clear();
      }
    }
    result.push_back(std::move(candidate));
  }
  std::sort(result.begin(), result.end(),
            [](const RecoveryCandidate& left, const RecoveryCandidate& right) {
              if (left.createdAtUnixMs == right.createdAtUnixMs) {
                return left.revision < right.revision;
              }
              return left.createdAtUnixMs < right.createdAtUnixMs;
            });
  return result;
}

core::Result<void> AutosaveService::recover(
    ProjectDocument& document, const RecoveryCandidate& candidate) const {
  if (!candidate.recoverable) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Autosave is not recoverable", candidate.diagnostic);
  }
  auto loaded = codec_.load(candidate.autosavePath);
  if (!loaded) return core::Result<void>{loaded.error()};
  if (loaded.value().id().toString() != candidate.projectId) {
    return core::failure(core::ErrorCode::Conflict,
                         "Autosave project identity changed");
  }
  auto replaced = document.replaceProject(std::move(loaded).value());
  if (!replaced) return replaced;
  document.markRecovered(candidate.autosavePath,
                         candidate.originalProjectPath);
  return core::success();
}

void AutosaveService::shutdown() noexcept {
  {
    std::lock_guard lock(mutex_);
    if (stopped_) return;
    stopped_ = true;
    pending_.reset();
  }
  worker_.request_stop();
  condition_.notify_all();
  if (worker_.joinable()) worker_.join();
}

core::Result<void> AutosaveService::writeSnapshot(
    const Snapshot& value) const {
  const auto directory = config_.root / value.stableId;
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to create autosave directory", error.message());
  }
  const auto stem = generationStem(value);
  const auto projectPath = directory / (stem + ".seam.autosave");
  const auto metadataPath = directory / (stem + ".meta.json");

  auto encoded = codec_.encode(value.project);
  if (!encoded) return core::Result<void>{encoded.error()};
  const auto projectWrite = core::durableAtomicWriteText(
      projectPath, encoded.value(), core::AtomicWriteOptions{
          .backupPath = {},
          .maximumBackupBytes = 64ULL * 1024ULL * 1024ULL,
          .faultInjector = config_.faultInjector,
      });
  if (!projectWrite) return projectWrite;

  const auto metadataText = formats::stringifyJson(
      metadataValue(value, projectPath.filename().string()), true);
  const auto metadataWrite = core::durableAtomicWriteText(
      metadataPath, metadataText, core::AtomicWriteOptions{
          .backupPath = {},
          .maximumBackupBytes = kMaximumMetadataBytes,
          .faultInjector = config_.faultInjector,
      });
  if (!metadataWrite) {
    std::filesystem::remove(projectPath, error);
    return metadataWrite;
  }
  return prune(directory);
}

core::Result<void> AutosaveService::prune(
    const std::filesystem::path& directory) const {
  std::vector<std::filesystem::path> metadataFiles;
  std::error_code error;
  for (std::filesystem::directory_iterator iterator{directory, error}, end;
       iterator != end; iterator.increment(error)) {
    if (error) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to enumerate autosave generations",
                           error.message());
    }
    if (iterator->is_regular_file(error) && !error &&
        metadataFile(iterator->path())) {
      metadataFiles.push_back(iterator->path());
    }
    error.clear();
  }
  struct Generation final {
    std::filesystem::path metadataPath;
    std::filesystem::path projectPath;
    std::int64_t createdAtUnixMs{0};
    std::uint64_t revision{0U};
  };
  std::vector<Generation> generations;
  generations.reserve(metadataFiles.size());
  for (const auto& metadataPath : metadataFiles) {
    auto metadata = parseMetadata(metadataPath);
    if (!metadata) continue;
    generations.push_back(Generation{
        .metadataPath = metadataPath,
        .projectPath = metadata.value().autosavePath,
        .createdAtUnixMs = metadata.value().createdAtUnixMs,
        .revision = metadata.value().revision,
    });
  }
  std::sort(generations.begin(), generations.end(),
            [](const Generation& left, const Generation& right) {
              if (left.createdAtUnixMs != right.createdAtUnixMs) {
                return left.createdAtUnixMs < right.createdAtUnixMs;
              }
              if (left.revision != right.revision) {
                return left.revision < right.revision;
              }
              return left.metadataPath.filename().string() <
                     right.metadataPath.filename().string();
            });
  while (generations.size() > config_.maximumGenerations) {
    std::filesystem::remove(generations.front().projectPath, error);
    error.clear();
    std::filesystem::remove(generations.front().metadataPath, error);
    if (error) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to prune old autosave generation",
                           error.message());
    }
    generations.erase(generations.begin());
  }
  return core::success();
}

void AutosaveService::workerLoop(std::stop_token token) {
  while (!token.stop_requested()) {
    std::optional<Snapshot> value;
    {
      std::unique_lock lock(mutex_);
      condition_.wait(lock, token, [this] {
        return pending_.has_value() || stopped_;
      });
      if (token.stop_requested() || stopped_) break;
      value = std::move(pending_);
      pending_.reset();
      writing_ = true;
    }
    const auto result = writeSnapshot(*value);
    {
      std::lock_guard lock(mutex_);
      writing_ = false;
      if (!result) lastError_ = result.error();
    }
    condition_.notify_all();
  }
  {
    std::lock_guard lock(mutex_);
    writing_ = false;
    pending_.reset();
  }
  condition_.notify_all();
}

}  // namespace seam::authoring
