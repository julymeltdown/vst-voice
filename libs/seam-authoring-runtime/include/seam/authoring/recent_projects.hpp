#pragma once

#include "seam/authoring/project_document.hpp"
#include "seam/authoring/project_lifecycle.hpp"
#include "seam/core/result.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace seam::authoring {

struct RecentProjectEntry final {
  std::filesystem::path path;
  std::string displayName;
  std::int64_t lastOpenedUnixMs{0};
  bool missing{false};

  friend bool operator==(const RecentProjectEntry&,
                         const RecentProjectEntry&) = default;
};

class RecentProjectsStore final {
public:
  explicit RecentProjectsStore(std::filesystem::path statePath,
                               std::size_t maximumEntries = 10U)
      : statePath_(std::move(statePath)), maximumEntries_(maximumEntries) {}

  [[nodiscard]] core::Result<void> load();
  [[nodiscard]] core::Result<void> save() const;
  [[nodiscard]] core::Result<void> record(
      const std::filesystem::path& path, std::string displayName,
      std::chrono::system_clock::time_point openedAt =
          std::chrono::system_clock::now());
  [[nodiscard]] core::Result<void> refreshMissing();

  [[nodiscard]] const std::vector<RecentProjectEntry>& entries() const noexcept {
    return entries_;
  }

private:
  [[nodiscard]] static core::Result<std::filesystem::path> canonicalPath(
      const std::filesystem::path& path);
  void sortAndBound();

  std::filesystem::path statePath_;
  std::size_t maximumEntries_{10U};
  std::vector<RecentProjectEntry> entries_;
};

enum class CloseChoice { Save, Discard, Cancel };
enum class CloseDisposition { Close, RemainOpen };

[[nodiscard]] core::Result<CloseDisposition> resolveUnsavedClose(
    ProjectDocument& document, CloseChoice choice,
    const ProjectLifecycleService& lifecycle);

}  // namespace seam::authoring
