#pragma once

#include "seam/authoring/project_document.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/result.hpp"
#include "seam/formats/project_json.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace seam::authoring {

struct AutosaveConfig final {
  std::filesystem::path root;
  std::chrono::seconds interval{60};
  std::size_t commandThreshold{25U};
  std::chrono::seconds minimumCommandDelay{15};
  std::size_t maximumGenerations{5U};
  core::AtomicWriteFaultInjector faultInjector;
  std::function<std::chrono::system_clock::time_point()> wallClock;
};

struct RecoveryCandidate final {
  std::filesystem::path autosavePath;
  std::filesystem::path metadataPath;
  std::optional<std::filesystem::path> originalProjectPath;
  std::string projectId;
  std::string baseProjectHash;
  std::uint64_t revision{0U};
  std::int64_t createdAtUnixMs{0};
  bool recoverable{false};
  std::string diagnostic;
};

class AutosaveService final {
public:
  explicit AutosaveService(AutosaveConfig config);
  ~AutosaveService();

  AutosaveService(const AutosaveService&) = delete;
  AutosaveService& operator=(const AutosaveService&) = delete;

  [[nodiscard]] core::Result<void> request(const ProjectDocument& document);
  [[nodiscard]] core::Result<void> onSuccessfulCommand(
      const ProjectDocument& document,
      std::chrono::steady_clock::time_point now =
          std::chrono::steady_clock::now());
  [[nodiscard]] core::Result<void> tick(
      const ProjectDocument& document,
      std::chrono::steady_clock::time_point now =
          std::chrono::steady_clock::now());
  [[nodiscard]] core::Result<void> flush();
  [[nodiscard]] core::Result<std::vector<RecoveryCandidate>> discover() const;
  [[nodiscard]] core::Result<void> recover(
      ProjectDocument& document, const RecoveryCandidate& candidate) const;
  void shutdown() noexcept;

public:
  // Exposed only to keep deterministic metadata helpers testable; callers do not construct it.
  struct Snapshot final {
    domain::Project project;
    std::optional<std::filesystem::path> explicitProjectPath;
    std::uint64_t revision{0U};
    std::string stableId;
    std::string baseProjectHash;
    std::int64_t createdAtUnixMs{0};
    std::uint64_t sequence{0U};
  };

private:
  [[nodiscard]] core::Result<void> validateConfig() const;
  [[nodiscard]] Snapshot snapshot(const ProjectDocument& document);
  [[nodiscard]] core::Result<void> writeSnapshot(const Snapshot& snapshot) const;
  [[nodiscard]] core::Result<void> prune(const std::filesystem::path& directory) const;
  void workerLoop(std::stop_token token);

  AutosaveConfig config_;
  formats::ProjectJsonCodec codec_;
  mutable std::mutex mutex_;
  std::condition_variable_any condition_;
  std::optional<Snapshot> pending_;
  std::optional<core::Error> lastError_;
  std::size_t successfulCommands_{0U};
  std::chrono::steady_clock::time_point lastRequestedAt_{};
  std::uint64_t sequence_{0U};
  bool writing_{false};
  bool stopped_{false};
  // Starts last and therefore stops first. The worker may inspect every field
  // above as soon as std::jthread launches its callable.
  std::jthread worker_;
};

}  // namespace seam::authoring
