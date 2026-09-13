#pragma once

#include "seam/application/command.hpp"

#include <optional>
#include <vector>
#include <utility>

namespace seam::application {

// Pure project edit: file loading/identity verification belongs to resource
// resolution. The expected selection guards asynchronous picker results.
class SetTrackProceduralRecipeCommand final : public ICommand {
public:
  SetTrackProceduralRecipeCommand(domain::TrackId trackId,
      std::optional<domain::ProceduralRecipeReference> expected,
      std::optional<domain::ProceduralRecipeReference> replacement)
      : trackId_(trackId), before_(std::move(expected)), after_(std::move(replacement)) {}
  [[nodiscard]] std::string_view name() const noexcept override { return "Select procedural recipe"; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override { return CommandAudioImpact::ProjectAudio; }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
private:
  domain::TrackId trackId_;
  std::optional<domain::ProceduralRecipeReference> before_, after_;
};

// Pure project edit: which installed neural singer a track uses. Resolving and
// verifying those bundle bytes belongs to the installed-resource registry and the
// surface's own deployment, so this command records only the identity a surface
// already resolved. The expected selection guards an asynchronous chooser result
// exactly as the procedural recipe command does.
class SetTrackNeuralResourceCommand final : public ICommand {
public:
  SetTrackNeuralResourceCommand(domain::TrackId trackId,
      std::optional<domain::NeuralResourceReference> expected,
      std::optional<domain::NeuralResourceReference> replacement)
      : trackId_(trackId), before_(std::move(expected)), after_(std::move(replacement)) {}
  [[nodiscard]] std::string_view name() const noexcept override { return "Select neural singer"; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override { return CommandAudioImpact::ProjectAudio; }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
private:
  domain::TrackId trackId_;
  std::optional<domain::NeuralResourceReference> before_, after_;
};

class UpsertUnitSelectionOverrideCommand final : public ICommand {
public:
  UpsertUnitSelectionOverrideCommand(
      domain::RegionId regionId,
      domain::UnitSelectionOverride overrideValue);

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Select voice unit";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::RegionId regionId_;
  domain::UnitSelectionOverride after_;
  std::vector<domain::UnitSelectionOverride> beforeOrder_;
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
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::RegionId regionId_;
  domain::PhonemeKey startKey_;
  std::optional<domain::UnitSelectionOverride> removed_;
  std::vector<domain::UnitSelectionOverride> beforeOrder_;
};

class UpsertSeamOverrideCommand final : public ICommand {
public:
  UpsertSeamOverrideCommand(domain::RegionId regionId,
                            domain::SeamOverride overrideValue);

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Edit sample seam";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::RegionId regionId_;
  domain::SeamOverride after_;
  std::vector<domain::SeamOverride> beforeOrder_;
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
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::RegionId regionId_;
  domain::PhonemeKey incomingStartKey_;
  std::optional<domain::SeamOverride> removed_;
  std::vector<domain::SeamOverride> beforeOrder_;
};


class UpsertPitchAutomationPointCommand final : public ICommand {
public:
  UpsertPitchAutomationPointCommand(domain::RegionId regionId,
                                    domain::PitchAutomationPoint point)
      : regionId_(regionId), after_(point) {}

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Edit pitch automation";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
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
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
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
                           domain::VoicebankReference voicebank,
                           std::optional<domain::VoiceStyleSelection> style = std::nullopt)
      : trackId_(trackId), after_(std::move(voicebank)), afterStyle_(std::move(style)) {}

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Set track voicebank";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::ProjectAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  domain::VoicebankReference after_;
  std::optional<domain::VoicebankReference> before_;
  std::optional<domain::VoiceStyleSelection> afterStyle_;
  domain::VoiceStyleSelection beforeStyle_;
  std::optional<domain::ProceduralRecipeReference> beforeRecipe_;
};

class SetTrackOutputRouteCommand final : public ICommand {
public:
  SetTrackOutputRouteCommand(domain::TrackId trackId,
                             domain::TrackOutputRoute route)
      : trackId_(trackId), after_(std::move(route)) {}

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Route track output";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::TrackMix;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::TrackId trackId_;
  domain::TrackOutputRoute after_;
  std::optional<domain::TrackOutputRoute> before_;
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
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::TrackMix;
  }
  [[nodiscard]] CommandImpact impact() const override;
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

class SetAudioTrackMixCommand final : public ICommand {
public:
  SetAudioTrackMixCommand(domain::TrackId trackId, float gainDb, float pan,
                          bool muted, bool solo)
      : trackId_(trackId), afterGainDb_(gainDb), afterPan_(pan),
        afterMuted_(muted), afterSolo_(solo) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Edit audio track mix";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::TrackMix;
  }
  [[nodiscard]] CommandImpact impact() const override;
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
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::TrackMix;
  }
  [[nodiscard]] CommandImpact impact() const override;
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
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::ProjectAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
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
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::TrackMix;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
private:
  std::uint8_t channels_{2U};
  std::optional<domain::ProjectRouting> beforeRouting_;
  std::vector<std::pair<domain::TrackId, domain::TrackOutputRoute>> beforeRoutes_;
};

}  // namespace seam::application
