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
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::ProjectAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
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
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::ProjectAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  std::optional<domain::VocalTrack> removed_;
  std::size_t originalIndex_{0U};
};

class DuplicateVocalTrackCommand final : public ICommand {
public:
  explicit DuplicateVocalTrackCommand(domain::TrackId sourceTrackId)
      : sourceTrackId_(sourceTrackId) {}
  explicit DuplicateVocalTrackCommand(domain::VocalTrack sourceTrack)
      : sourceTrackId_(sourceTrack.id), sourceSnapshot_(std::move(sourceTrack)) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Duplicate vocal track";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::ProjectAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
  [[nodiscard]] domain::TrackId duplicatedTrackId() const noexcept {
    return duplicated_.has_value() ? duplicated_->id : domain::TrackId{};
  }

private:
  domain::TrackId sourceTrackId_;
  std::optional<domain::VocalTrack> sourceSnapshot_;
  std::optional<domain::VocalTrack> duplicated_;
  std::size_t originalIndex_{0U};
};

class MoveVocalTrackCommand final : public ICommand {
public:
  MoveVocalTrackCommand(domain::TrackId trackId, std::size_t destinationIndex)
      : trackId_(trackId), destinationIndex_(destinationIndex) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Reorder vocal track";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::MetadataOnly;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  std::size_t destinationIndex_{0U};
  std::size_t previousIndex_{0U};
  bool captured_{false};
};

class RenameVocalTrackCommand final : public ICommand {
public:
  RenameVocalTrackCommand(domain::TrackId trackId, std::string name)
      : trackId_(trackId), name_(std::move(name)) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Rename vocal track";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::MetadataOnly;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  std::string name_;
  std::optional<std::string> previous_;
};

class AddAudioTrackCommand final : public ICommand {
public:
  explicit AddAudioTrackCommand(domain::AudioTrack track)
      : track_(std::move(track)) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Add audio track";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::ProjectAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::AudioTrack track_;
};

class RemoveAudioTrackCommand final : public ICommand {
public:
  explicit RemoveAudioTrackCommand(domain::TrackId trackId)
      : trackId_(trackId) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Remove audio track";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::ProjectAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  std::optional<domain::AudioTrack> removed_;
  std::size_t originalIndex_{0U};
};

class RenameAudioTrackCommand final : public ICommand {
public:
  RenameAudioTrackCommand(domain::TrackId trackId, std::string name)
      : trackId_(trackId), name_(std::move(name)) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Rename audio track";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::MetadataOnly;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  std::string name_;
  std::optional<std::string> previous_;
};

class MoveAudioTrackCommand final : public ICommand {
public:
  MoveAudioTrackCommand(domain::TrackId trackId, std::size_t destinationIndex)
      : trackId_(trackId), destinationIndex_(destinationIndex) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Reorder audio track";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::MetadataOnly;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  std::size_t destinationIndex_{0U};
  std::size_t previousIndex_{0U};
  bool captured_{false};
};

class ReplaceAudioTrackCommand final : public ICommand {
public:
  ReplaceAudioTrackCommand(domain::TrackId trackId, domain::AudioTrack replacement)
      : trackId_(trackId), replacement_(std::move(replacement)) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Replace audio track";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::ProjectAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  domain::AudioTrack replacement_;
  std::optional<domain::AudioTrack> previous_;
};

class AddVocalRegionCommand final : public ICommand {
public:
  AddVocalRegionCommand(domain::TrackId trackId, domain::VocalRegion region)
      : trackId_(trackId), region_(std::move(region)) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Add vocal region";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::ProjectAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
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
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::ProjectAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  domain::RegionId regionId_;
  std::optional<domain::VocalRegion> removed_;
  std::size_t originalIndex_{0U};
};

class DuplicateVocalRegionCommand final : public ICommand {
public:
  DuplicateVocalRegionCommand(domain::TrackId trackId,
                              domain::RegionId sourceRegionId)
      : trackId_(trackId), sourceTrackId_(trackId),
        sourceRegionId_(sourceRegionId) {}
  DuplicateVocalRegionCommand(domain::TrackId sourceTrackId,
                              domain::TrackId targetTrackId,
                              domain::RegionId sourceRegionId)
      : trackId_(targetTrackId), sourceTrackId_(sourceTrackId),
        sourceRegionId_(sourceRegionId) {}
  DuplicateVocalRegionCommand(domain::TrackId trackId,
                              domain::VocalRegion region)
      : trackId_(trackId), sourceTrackId_(trackId), sourceRegionId_(region.id),
        region_(std::move(region)) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Duplicate vocal region";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::ProjectAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
  [[nodiscard]] domain::RegionId duplicatedRegionId() const noexcept {
    return region_.has_value() ? region_->id : domain::RegionId{};
  }

private:
  domain::TrackId trackId_;
  domain::TrackId sourceTrackId_;
  domain::RegionId sourceRegionId_;
  std::optional<domain::VocalRegion> region_;
};

class MoveVocalRegionCommand final : public ICommand {
public:
  MoveVocalRegionCommand(domain::TrackId trackId, domain::RegionId regionId,
                         time::Tick newStart)
      : trackId_(trackId), regionId_(regionId), newStart_(newStart) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Move vocal region";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::ProjectAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  domain::RegionId regionId_;
  time::Tick newStart_;
  std::optional<time::Tick> previousStart_;
};

class ResizeVocalRegionCommand final : public ICommand {
public:
  ResizeVocalRegionCommand(domain::TrackId trackId, domain::RegionId regionId,
                           time::Tick newDuration)
      : trackId_(trackId), regionId_(regionId), newDuration_(newDuration) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Resize vocal region";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::ProjectAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  domain::RegionId regionId_;
  time::Tick newDuration_;
  std::optional<time::Tick> previousDuration_;
};

class RenameVocalRegionCommand final : public ICommand {
public:
  RenameVocalRegionCommand(domain::TrackId trackId, domain::RegionId regionId,
                           std::string name)
      : trackId_(trackId), regionId_(regionId), name_(std::move(name)) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Rename vocal region";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::MetadataOnly;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  domain::RegionId regionId_;
  std::string name_;
  std::optional<std::string> previous_;
};

class SplitVocalRegionCommand final : public ICommand {
public:
  SplitVocalRegionCommand(domain::TrackId trackId, domain::RegionId regionId,
                          time::Tick splitTick)
      : trackId_(trackId), regionId_(regionId), splitTick_(splitTick) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Split vocal region";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::ProjectAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
  [[nodiscard]] domain::RegionId splitRegionId() const noexcept {
    return right_.has_value() ? right_->id : domain::RegionId{};
  }

private:
  domain::TrackId trackId_;
  domain::RegionId regionId_;
  time::Tick splitTick_;
  std::optional<domain::VocalRegion> original_;
  std::optional<domain::VocalRegion> left_;
  std::optional<domain::VocalRegion> right_;
};

}
