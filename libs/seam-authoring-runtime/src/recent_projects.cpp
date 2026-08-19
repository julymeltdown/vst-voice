#include "seam/authoring/recent_projects.hpp"

#include "seam/core/file_io.hpp"
#include "seam/domain/note.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

namespace seam::authoring {
namespace {

constexpr std::string_view kFormat = "com.project-seam.recent-projects";
constexpr std::int64_t kSchemaVersion = 1;
constexpr std::uint64_t kMaximumRecentBytes = 1024U * 1024U;

std::string pathToUtf8(const std::filesystem::path& path) {
  const auto bytes = path.generic_u8string();
  return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::filesystem::path pathFromUtf8(std::string_view text) {
  std::u8string bytes(text.size(), u8'\0');
  std::transform(text.begin(), text.end(), bytes.begin(),
                 [](char value) { return static_cast<char8_t>(value); });
  return std::filesystem::path{bytes};
}

std::int64_t unixMilliseconds(std::chrono::system_clock::time_point value) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             value.time_since_epoch())
      .count();
}

}  // namespace

core::Result<std::filesystem::path> RecentProjectsStore::canonicalPath(
    const std::filesystem::path& path) {
  if (path.empty()) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::InvalidArgument,
        "Recent project path cannot be empty");
  }
  std::error_code error;
  auto absolute = std::filesystem::absolute(path, error);
  if (error) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::IoError,
        "Unable to resolve recent project path", error.message());
  }
  auto canonical = std::filesystem::weakly_canonical(absolute, error);
  return (error ? absolute : canonical).lexically_normal();
}

core::Result<void> RecentProjectsStore::load() {
  if (statePath_.empty() || maximumEntries_ == 0U || maximumEntries_ > 100U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Recent-project store configuration is invalid");
  }
  std::error_code error;
  if (!std::filesystem::exists(statePath_, error)) {
    if (error) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to inspect recent-project state",
                           error.message());
    }
    entries_.clear();
    return core::success();
  }
  auto content = core::readTextFileLimited(statePath_, kMaximumRecentBytes);
  if (!content) return core::Result<void>{content.error()};
  auto parsed = formats::parseJson(
      content.value(), formats::JsonParseLimits{
                           .maximumInputBytes = kMaximumRecentBytes,
                           .maximumDepth = 8U,
                           .maximumNodes = 256U,
                           .maximumStringBytes = 256U * 1024U,
                           .maximumCollectionEntries = 128U,
                       });
  if (!parsed) return core::Result<void>{parsed.error()};
  const auto* format = parsed.value().find("format");
  const auto* schema = parsed.value().find("schemaVersion");
  const auto* entries = parsed.value().find("entries");
  if (!parsed.value().isObject() || format == nullptr || schema == nullptr ||
      entries == nullptr || !format->isString() || !schema->isNumber() ||
      !entries->isArray() || format->asString() != kFormat ||
      schema->asInt64() != kSchemaVersion) {
    return core::failure(core::ErrorCode::ParseError,
                         "Recent-project state header is invalid");
  }

  std::vector<RecentProjectEntry> loaded;
  loaded.reserve(std::min(entries->asArray().size(), maximumEntries_));
  for (const auto& value : entries->asArray()) {
    if (!value.isObject()) {
      return core::failure(core::ErrorCode::ParseError,
                           "Recent-project entry must be an object");
    }
    const auto* path = value.find("path");
    const auto* name = value.find("displayName");
    const auto* opened = value.find("lastOpenedUnixMs");
    if (path == nullptr || name == nullptr || opened == nullptr ||
        !path->isString() || !name->isString() || !opened->isNumber() ||
        path->asString().empty() || name->asString().empty()) {
      return core::failure(core::ErrorCode::ParseError,
                           "Recent-project entry is invalid");
    }
    if (!domain::fromUtf8(name->asString())) {
      return core::failure(core::ErrorCode::ParseError,
                           "Recent-project name is invalid UTF-8");
    }
    auto canonical = canonicalPath(pathFromUtf8(path->asString()));
    if (!canonical) return core::Result<void>{canonical.error()};
    std::error_code existsError;
    const bool missing = !std::filesystem::exists(canonical.value(), existsError);
    loaded.push_back(RecentProjectEntry{
        .path = std::move(canonical).value(),
        .displayName = name->asString(),
        .lastOpenedUnixMs = opened->asInt64(),
        .missing = existsError ? true : missing,
    });
  }
  entries_ = std::move(loaded);
  sortAndBound();
  return core::success();
}

core::Result<void> RecentProjectsStore::save() const {
  if (statePath_.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Recent-project state path cannot be empty");
  }
  formats::JsonValue::Array values;
  values.reserve(entries_.size());
  for (const auto& entry : entries_) {
    values.emplace_back(formats::JsonValue::Object{
        {"path", formats::JsonValue{pathToUtf8(entry.path)}},
        {"displayName", formats::JsonValue{entry.displayName}},
        {"lastOpenedUnixMs",
         formats::JsonValue{entry.lastOpenedUnixMs}},
    });
  }
  const auto text = formats::stringifyJson(
      formats::JsonValue{formats::JsonValue::Object{
          {"format", formats::JsonValue{std::string{kFormat}}},
          {"schemaVersion", formats::JsonValue{kSchemaVersion}},
          {"entries", formats::JsonValue{std::move(values)}},
      }},
      true);
  auto backup = statePath_;
  backup += ".bak";
  return core::durableAtomicWriteText(
      statePath_, text, core::AtomicWriteOptions{
                            .backupPath = std::move(backup),
                            .maximumBackupBytes = kMaximumRecentBytes,
                            .faultInjector = {},
                        });
}

core::Result<void> RecentProjectsStore::record(
    const std::filesystem::path& path, std::string displayName,
    std::chrono::system_clock::time_point openedAt) {
  if (maximumEntries_ == 0U || maximumEntries_ > 100U) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Recent-project entry limit is invalid");
  }
  auto canonical = canonicalPath(path);
  if (!canonical) return core::Result<void>{canonical.error()};
  if (displayName.empty() || displayName.size() > 1024U ||
      !domain::fromUtf8(displayName)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Recent-project display name is invalid");
  }
  entries_.erase(
      std::remove_if(entries_.begin(), entries_.end(),
                     [&canonical](const RecentProjectEntry& entry) {
                       return entry.path == canonical.value();
                     }),
      entries_.end());
  std::error_code error;
  entries_.push_back(RecentProjectEntry{
      .path = std::move(canonical).value(),
      .displayName = std::move(displayName),
      .lastOpenedUnixMs = unixMilliseconds(openedAt),
      .missing = !std::filesystem::exists(path, error) || error,
  });
  sortAndBound();
  return core::success();
}

core::Result<void> RecentProjectsStore::refreshMissing() {
  std::error_code error;
  for (auto& entry : entries_) {
    entry.missing = !std::filesystem::exists(entry.path, error) || error;
    error.clear();
  }
  entries_.erase(
      std::remove_if(entries_.begin(), entries_.end(),
                     [](const RecentProjectEntry& entry) {
                       return entry.missing;
                     }),
      entries_.end());
  return core::success();
}

void RecentProjectsStore::sortAndBound() {
  std::stable_sort(entries_.begin(), entries_.end(),
                   [](const RecentProjectEntry& left,
                      const RecentProjectEntry& right) {
                     if (left.lastOpenedUnixMs == right.lastOpenedUnixMs) {
                       return left.path.generic_string() <
                              right.path.generic_string();
                     }
                     return left.lastOpenedUnixMs > right.lastOpenedUnixMs;
                   });
  if (entries_.size() > maximumEntries_) {
    entries_.resize(maximumEntries_);
  }
}

core::Result<CloseDisposition> resolveUnsavedClose(
    ProjectDocument& document, CloseChoice choice,
    const ProjectLifecycleService& lifecycle) {
  if (!document.dirty()) return CloseDisposition::Close;
  switch (choice) {
    case CloseChoice::Cancel:
      return CloseDisposition::RemainOpen;
    case CloseChoice::Discard:
      return CloseDisposition::Close;
    case CloseChoice::Save: {
      auto saved = lifecycle.save(document);
      if (!saved) return core::Result<CloseDisposition>{saved.error()};
      return CloseDisposition::Close;
    }
  }
  return core::failure<CloseDisposition>(core::ErrorCode::Internal,
                                          "Unknown close choice");
}

}  // namespace seam::authoring
