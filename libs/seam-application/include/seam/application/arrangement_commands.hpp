#pragma once

#include "seam/application/command.hpp"

#include <optional>
#include <string>
#include <utility>

namespace seam::application {

class AddVocalTrackCommand final : public ICommand {
public:
  explicit AddVocalTrackCommand(domain::VocalTrack track)
      : track_(std::move(track)) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Add vocal track";
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::VocalTrack track_;
};

class RemoveVocalTrackCommand final : public ICommand {
public:
  explicit RemoveVocalTrackCommand(domain::TrackId trackId)
      : trackId_(trackId) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Remove vocal track";
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  std::optional<domain::VocalTrack> removed_;
  std::size_t originalIndex_{0U};
};

class RenameVocalTrackCommand final : public ICommand {
public:
  RenameVocalTrackCommand(domain::TrackId trackId, std::string name)
      : trackId_(trackId), name_(std::move(name)) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Rename vocal track";
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  std::string name_;
  std::optional<std::string> previous_;
};

class AddVocalRegionCommand final : public ICommand {
public:
  AddVocalRegionCommand(domain::TrackId trackId, domain::VocalRegion region)
      : trackId_(trackId), region_(std::move(region)) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Add vocal region";
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  domain::VocalRegion region_;
};

class RemoveVocalRegionCommand final : public ICommand {
public:
  RemoveVocalRegionCommand(domain::TrackId trackId, domain::RegionId regionId)
      : trackId_(trackId), regionId_(regionId) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Remove vocal region";
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  domain::RegionId regionId_;
  std::optional<domain::VocalRegion> removed_;
  std::size_t originalIndex_{0U};
};

}
