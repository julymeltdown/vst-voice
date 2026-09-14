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
enum class PerformanceAcceptanceMode {
  // The given list becomes the region's whole selection state.
  Replace,
  // The given list wins only inside the spans it covers: an existing selection
  // whose channel and span meet one of the new selections is replaced by it, and
  // every other existing selection survives. This is what lets a creator accept a
  // take over three notes without discarding what was accepted on the others.
  Merge,
};

class SetAcceptedPerformanceCommand final : public ICommand {
public:
  SetAcceptedPerformanceCommand(domain::RegionId regionId,
      domain::RegionPerformanceState expected,
      std::vector<domain::AcceptedPerformanceSelection> selections,
      PerformanceAcceptanceMode mode = PerformanceAcceptanceMode::Replace);
  [[nodiscard]] std::string_view name() const noexcept override { return "Select performance take"; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override { return CommandAudioImpact::PhraseAudio; }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
private:
  domain::RegionId regionId_;
  domain::RegionPerformanceState before_;
  std::vector<domain::AcceptedPerformanceSelection> selections_;
  PerformanceAcceptanceMode mode_{PerformanceAcceptanceMode::Replace};
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

// The formant channel's own curve. It is a separate edit from the dynamics curve because they are
// separate channels of intent: one changes level, the other moves the vocal tract's resonances.
struct RegionFormantEdit final {
  domain::RegionId regionId;
  domain::FormantAutomation curve;
};

// The breathiness channel is a third channel of intent again: it rebalances the excitation's periodic
// and aperiodic energy, so it is neither a level nor a resonance and cannot be expressed as either.
struct RegionBreathinessEdit final {
  domain::RegionId regionId;
  domain::BreathinessAutomation curve;
};

// And a fourth: tension changes the harmonic source's own spectrum, so it is not a level, not a
// resonance, and not a balance between periodic and aperiodic energy.
struct RegionTensionEdit final {
  domain::RegionId regionId;
  domain::TensionAutomation curve;
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

// Marks one proposed take as rejected. The take is retained for comparison and its
// lanes are never edited; a take that an accepted selection still points at cannot
// be rejected, because that would leave the selection referring to rejected
// material. Rejection changes no revision axis, so proposals captured before the
// rejection stay acceptable on exactly the material they were computed for.
class RejectPerformanceProposalCommand final : public ICommand {
public:
  RejectPerformanceProposalCommand(domain::RegionId regionId,
      domain::RegionPerformanceState expected, std::string takeId);
  [[nodiscard]] std::string_view name() const noexcept override { return "Reject performance proposal"; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override { return CommandAudioImpact::PhraseAudio; }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
private:
  domain::RegionId regionId_;
  domain::RegionPerformanceState before_;
  std::string takeId_;
  std::optional<domain::RegionPerformanceState> after_;
};

class EditPerformanceCommand final : public ICommand {
public:
  EditPerformanceCommand(std::vector<NoteExpressionEdit> notes,
                         std::vector<RegionDynamicsEdit> regions = {},
                         std::vector<TrackStyleEdit> tracks = {},
                         std::vector<RegionOwnershipEdit> ownership = {},
                         std::vector<RegionFormantEdit> formant = {},
                         std::vector<RegionBreathinessEdit> breathiness = {},
                         std::vector<RegionTensionEdit> tension = {});

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
  std::vector<RegionFormantEdit> afterFormant_;
  std::vector<RegionBreathinessEdit> afterBreathiness_;
  std::vector<RegionTensionEdit> afterTension_;
  std::vector<TrackStyleEdit> afterTracks_;
  std::vector<NoteExpressionEdit> beforeNotes_;
  std::vector<RegionDynamicsEdit> beforeRegions_;
  std::vector<RegionFormantEdit> beforeFormant_;
  std::vector<RegionBreathinessEdit> beforeBreathiness_;
  std::vector<RegionTensionEdit> beforeTension_;
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
