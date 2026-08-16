#pragma once

#include "seam/application/command.hpp"

#include <optional>

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

}  // namespace seam::application
