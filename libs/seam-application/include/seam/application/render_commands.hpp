#pragma once

#include "seam/application/command.hpp"

#include <optional>
#include <vector>
#include <utility>

namespace seam::application {

class UpsertUnitSelectionOverrideCommand final : public ICommand {
public:
  UpsertUnitSelectionOverrideCommand(
      domain::RegionId regionId,
      domain::UnitSelectionOverride overrideValue);

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Select voice unit";
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::RegionId regionId_;
  domain::UnitSelectionOverride after_;
  std::optional<domain::UnitSelectionOverride> before_;
  bool captured_{false};
};

class RemoveUnitSelectionOverrideCommand final : public ICommand {
public:
  RemoveUnitSelectionOverrideCommand(domain::RegionId regionId,
                                     domain::PhonemeKey startKey)
      : regionId_(regionId), startKey_(startKey) {}

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Reset voice unit selection";
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::RegionId regionId_;
  domain::PhonemeKey startKey_;
  std::optional<domain::UnitSelectionOverride> removed_;
};

class UpsertSeamOverrideCommand final : public ICommand {
public:
  UpsertSeamOverrideCommand(domain::RegionId regionId,
                            domain::SeamOverride overrideValue);

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Edit sample seam";
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::RegionId regionId_;
  domain::SeamOverride after_;
  std::optional<domain::SeamOverride> before_;
  bool captured_{false};
};

class RemoveSeamOverrideCommand final : public ICommand {
public:
  RemoveSeamOverrideCommand(domain::RegionId regionId,
                            domain::PhonemeKey incomingStartKey)
      : regionId_(regionId), incomingStartKey_(incomingStartKey) {}

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Reset sample seam";
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::RegionId regionId_;
  domain::PhonemeKey incomingStartKey_;
  std::optional<domain::SeamOverride> removed_;
};


class UpsertPitchAutomationPointCommand final : public ICommand {
public:
  UpsertPitchAutomationPointCommand(domain::RegionId regionId,
                                    domain::PitchAutomationPoint point)
      : regionId_(regionId), after_(point) {}

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Edit pitch automation";
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::RegionId regionId_;
  domain::PitchAutomationPoint after_;
  std::optional<domain::PitchAutomationPoint> before_;
  bool captured_{false};
};

class RemovePitchAutomationPointCommand final : public ICommand {
public:
  RemovePitchAutomationPointCommand(domain::RegionId regionId, time::Tick tick)
      : regionId_(regionId), tick_(tick) {}

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Reset pitch automation";
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::RegionId regionId_;
  time::Tick tick_;
  std::optional<domain::PitchAutomationPoint> removed_;
};


class SetTrackVoicebankCommand final : public ICommand {
public:
  SetTrackVoicebankCommand(domain::TrackId trackId,
                           domain::VoicebankReference voicebank)
      : trackId_(trackId), after_(std::move(voicebank)) {}

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Set track voicebank";
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  domain::VoicebankReference after_;
  std::optional<domain::VoicebankReference> before_;
};

}  // namespace seam::application

namespace seam::application {

class SetVocalTrackMixCommand final : public ICommand {
public:
  SetVocalTrackMixCommand(domain::TrackId trackId, float gainDb, float pan,
                          bool muted, bool solo)
      : trackId_(trackId), afterGainDb_(gainDb), afterPan_(pan),
        afterMuted_(muted), afterSolo_(solo) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Edit vocal track mix";
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
private:
  domain::TrackId trackId_;
  float afterGainDb_{0.0F};
  float afterPan_{0.0F};
  bool afterMuted_{false};
  bool afterSolo_{false};
  float beforeGainDb_{0.0F};
  float beforePan_{0.0F};
  bool beforeMuted_{false};
  bool beforeSolo_{false};
  bool captured_{false};
};

class SetProjectRoutingCommand final : public ICommand {
public:
  explicit SetProjectRoutingCommand(domain::ProjectRouting routing)
      : after_(std::move(routing)) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Change project audio routing";
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
private:
  domain::ProjectRouting after_;
  std::optional<domain::ProjectRouting> before_;
};

class SetHostStartOffsetCommand final : public ICommand {
public:
  explicit SetHostStartOffsetCommand(time::Tick tick) : after_(tick) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Set host project start offset";
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
private:
  time::Tick after_;
  time::Tick before_{};
  bool captured_{false};
};

class ConfigureProjectOutputCommand final : public ICommand {
public:
  explicit ConfigureProjectOutputCommand(std::uint8_t channels)
      : channels_(channels) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Configure project output channels";
  }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
private:
  std::uint8_t channels_{2U};
  std::optional<domain::ProjectRouting> beforeRouting_;
  std::vector<std::pair<domain::TrackId, domain::TrackOutputRoute>> beforeRoutes_;
};

}  // namespace seam::application
