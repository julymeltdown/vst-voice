#pragma once

#include "seam/application/command.hpp"
#include "seam/application/note_commands.hpp"

#include <optional>
#include <string>
#include <vector>

namespace seam::application {

// Async producers must deliver this through executePerformanceResult with the
// captured job context. Storing an unaccepted proposal is metadata-only.
class AddPerformanceProposalCommand final : public ICommand {
public:
  AddPerformanceProposalCommand(domain::RegionId regionId,
      domain::RegionPerformanceState expected, domain::PerformanceTake proposal);
  [[nodiscard]] std::string_view name() const noexcept override { return "Add performance proposal"; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override { return CommandAudioImpact::MetadataOnly; }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
private:
  domain::RegionId regionId_;
  domain::RegionPerformanceState before_;
  domain::PerformanceTake proposal_;
  std::optional<domain::RegionPerformanceState> after_;
};

// Changes selection of existing proposals only; never deletes takes or manual
// ownership. New selections must still match their captured musical context.
class SetAcceptedPerformanceCommand final : public ICommand {
public:
  SetAcceptedPerformanceCommand(domain::RegionId regionId,
      domain::RegionPerformanceState expected,
      std::vector<domain::AcceptedPerformanceSelection> selections);
  [[nodiscard]] std::string_view name() const noexcept override { return "Select performance take"; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override { return CommandAudioImpact::PhraseAudio; }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
private:
  domain::RegionId regionId_;
  domain::RegionPerformanceState before_;
  std::vector<domain::AcceptedPerformanceSelection> selections_;
  std::optional<domain::RegionPerformanceState> after_;
};

struct NoteExpressionEdit final {
  domain::NoteId noteId;
  domain::NoteVibrato vibrato;
  std::optional<std::string> phoneticHint;
};

struct RegionDynamicsEdit final {
  domain::RegionId regionId;
  domain::DynamicsAutomation curve;
};

struct TrackStyleEdit final {
  domain::TrackId trackId;
  domain::VoiceStyleSelection selection;
};

// Explicit ownership intent: a hint-only edit must not implicitly claim pitch.
// The expected state prevents overwriting ownership edited since preparation.
struct RegionOwnershipEdit final {
  domain::RegionId regionId;
  domain::PerformanceRevision expectedRevision;
  std::vector<domain::ManualPerformanceOwnership> expectedOwnership;
  std::vector<domain::ManualPerformanceOwnership> ownership;
};

// Run after adding the duplicate notes, in the same composite transaction.
// Region-time scopes remain fixed; only explicit note scopes are copied.
class CopyNotePerformanceCommand final : public ICommand {
public:
  CopyNotePerformanceCommand(domain::RegionId regionId,
                             std::vector<domain::PerformanceNoteRemap> mapping);
  [[nodiscard]] std::string_view name() const noexcept override { return "Copy note performance"; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
private:
  domain::RegionId regionId_;
  std::vector<domain::PerformanceNoteRemap> mapping_;
  std::optional<domain::RegionPerformanceState> before_;
  std::optional<domain::RegionPerformanceState> after_;
  std::vector<domain::PhonemeOverride> beforePhonemes_;
  std::vector<domain::UnitSelectionOverride> beforeUnits_, afterUnits_;
  std::vector<domain::SeamOverride> beforeSeams_, afterSeams_;
  std::vector<domain::PhonemeOverride> afterPhonemes_;
};

class EditPerformanceCommand final : public ICommand {
public:
  EditPerformanceCommand(std::vector<NoteExpressionEdit> notes,
                         std::vector<RegionDynamicsEdit> regions = {},
                         std::vector<TrackStyleEdit> tracks = {},
                         std::vector<RegionOwnershipEdit> ownership = {});

  [[nodiscard]] std::string_view name() const noexcept override;
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override;
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  [[nodiscard]] core::Result<void> set(domain::Project& project, bool after);
  [[nodiscard]] core::Result<void> setExpressions(domain::Project& project, bool after);
  std::optional<SetNoteHintsCommand> hints_;

  std::vector<NoteExpressionEdit> afterNotes_;
  std::vector<RegionDynamicsEdit> afterRegions_;
  std::vector<TrackStyleEdit> afterTracks_;
  std::vector<NoteExpressionEdit> beforeNotes_;
  std::vector<RegionDynamicsEdit> beforeRegions_;
  std::vector<TrackStyleEdit> beforeTracks_;
  std::vector<RegionOwnershipEdit> ownershipEdits_;
  struct OwnershipState final {
    domain::RegionId regionId;
    domain::PerformanceRevision revision;
    std::vector<domain::ManualPerformanceOwnership> ownership;
  };
  std::vector<OwnershipState> beforeOwnership_;
  std::vector<OwnershipState> afterOwnership_;
  bool captured_{false};
};

}
