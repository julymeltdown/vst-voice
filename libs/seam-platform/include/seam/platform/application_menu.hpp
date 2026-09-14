#pragma once

#include "seam/core/result.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seam::platform {

enum class ApplicationCommand {
  NewProject,
  OpenProject,
  OpenExternalProject,
  RecoverLatestAutosave,
  SaveProject,
  SaveProjectAs,
  ImportAudio,
  InstallVoicebank,
  RelinkVoicebank,
  RelinkBackingAudio,
  OpenAudioSettings,
  ExportSet,
  ExportAudio,
  ExportScore,
  Quit,
  Undo,
  Redo,
  TogglePlayback,
  StopPlayback,
  ToggleLoop,
  SelectProceduralRecipe,
  RelinkProceduralRecipe,
  BakeProceduralCandidates,
  ProposeAutomaticPerformance,
  ProposeAutomaticPerformanceOverSelectedNotes,
  EditPronunciationHint,
  FindReplaceLyrics,
  ClearSelectedVibrato,
  RemoveSelectedOverlaps,
  CloseSelectedGaps,
  AutoLegatoSelectedNotes,
  ClearRegionDynamicsCurve,
  FindNotes,
  FindActiveDiagnostics,
  FindNextNote,
  FindPreviousNote,
  EditSelectedVibrato,
  EditRegionDynamics,
  NudgeFormantUp,
  NudgeFormantDown,
  ResetRegionFormantCurve,
  NudgeBreathinessUp,
  NudgeBreathinessDown,
  ResetRegionBreathinessCurve,
  NudgeTensionUp,
  NudgeTensionDown,
  ResetRegionTensionCurve,
  NudgeAirinessUp,
  NudgeAirinessDown,
  ResetRegionAirinessCurve,
  NudgeGenderUp,
  NudgeGenderDown,
  ResetRegionGenderCurve,
  NudgeGrowlUp,
  NudgeGrowlDown,
  ResetRegionGrowlCurve,
  // One drawn lane for the timbral channels that share a region curve.
  OpenExpressionLane,
  NextExpressionChannel,
  PreviousExpressionChannel,
  NudgeExpressionChannelUp,
  NudgeExpressionChannelDown,
  CloseExpressionLane,
  EditTrackStyle,
  EditJapaneseReading,
};

struct RecentProjectMenuItem final {
  std::filesystem::path path;
  std::string displayName;
  bool missing{false};
};

struct RecoveryMenuItem final {
  std::filesystem::path metadataPath;
  std::string displayName;
};

struct VoicebankMenuItem final {
  std::string id;
  std::string version;
  std::string contentHash;
  std::string displayName;
  std::string trustLabel;
  bool selectable{false};
  bool selected{false};
};

// One installed neural singer a surface can select. The list comes from the
// surface's verified installed-resource index, so an item is always a bundle the
// selected helper can actually admit; the identity a project saves is the same
// three fields the renderer compares against.
struct NeuralResourceMenuItem final {
  std::string id;
  std::string version;
  std::string contentHash;
  std::string displayName;
  bool selected{false};
};

struct DocumentationMenuItem final {
  std::string id;
  std::string displayName;
  std::filesystem::path path;
};

// One performance proposal the surface can still decide. The label is built by
// the surface from the take's own recorded identity -- generator, seed, span and
// channels -- so a menu never has to invent a name for material it did not make.
// Which span a performance action applies to. Whole means the take's own captured
// span when deciding, and the selected region when proposing. SelectedNotes means the
// span the creator currently has selected, which must lie inside the material an
// action can honestly cover.
enum class PerformanceEditScope { Whole, SelectedNotes };

struct PerformanceTakeMenuItem final {
  std::string id;
  std::string label;
  // True when an accepted selection already uses this take, which the surface
  // shows as the current choice rather than offering it as a new decision.
  bool accepted{false};
  // Channel ids the take actually carries, in lane order. A surface offers a decision
  // only over these, so it can never ask to accept a channel the backend did not
  // generate for this take.
  std::vector<std::string> channels;
};

// The alternate-take comparison a surface is holding, if any. The label names the
// candidate take, and the flag reports which of the two states the region carries
// right now, so a surface can mark the side that is currently sounding.
struct PerformanceComparisonMenuItem final {
  std::string takeId;
  std::string label;
  bool candidateApplied{false};
};

class IApplicationCommandDispatcher {
public:
  virtual ~IApplicationCommandDispatcher() = default;
  [[nodiscard]] virtual core::Result<void> dispatch(
      ApplicationCommand command) = 0;
  [[nodiscard]] virtual std::vector<RecentProjectMenuItem> recentProjects()
      const { return {}; }
  [[nodiscard]] virtual core::Result<void> openRecentProject(
      const std::filesystem::path&) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Recent projects are not supported");
  }
  [[nodiscard]] virtual std::vector<RecoveryMenuItem> recoveryItems() const {
    return {};
  }
  [[nodiscard]] virtual core::Result<void> recoverAutosave(
      const std::filesystem::path&) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Autosave recovery is not supported");
  }
  [[nodiscard]] virtual std::vector<VoicebankMenuItem> voicebanks() const {
    return {};
  }
  [[nodiscard]] virtual core::Result<void> selectVoicebank(
      std::string_view, std::string_view, std::string_view) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Voicebank selection is not supported");
  }
  // Installed neural singers this surface can run. A surface that ships no
  // verified neural deployment answers with an empty list, which is what its menu
  // then shows, instead of offering bundles nothing could execute.
  [[nodiscard]] virtual std::vector<NeuralResourceMenuItem> neuralResources()
      const {
    return {};
  }
  [[nodiscard]] virtual core::Result<void> selectNeuralResource(
      std::string_view, std::string_view, std::string_view) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Neural singer selection is not supported");
  }
  [[nodiscard]] virtual core::Result<void> clearNeuralResource() {
    return core::failure(core::ErrorCode::Unsupported,
                         "Neural singer selection is not supported");
  }
  // Performance proposals recorded for the region the surface has selected that
  // still await a decision, in the order the project stores them. A surface that
  // cannot reach the performance commands answers with an empty list instead of
  // offering a decision that nothing would record.
  [[nodiscard]] virtual std::vector<PerformanceTakeMenuItem> performanceTakes()
      const {
    return {};
  }
  // Selects the take over the requested span for every channel it carries,
  // replacing the region's current accepted selections. It never edits the take,
  // never changes its state and never touches manual performance ownership. A
  // surface refuses a span the take did not generate instead of clamping it.
  // The channel list names the channels the decision covers, using the ids reported by
  // performanceTakes(). An empty list means every channel the take carries. Naming a
  // channel the take does not carry is refused rather than quietly widened.
  [[nodiscard]] virtual core::Result<void> acceptPerformanceTake(std::string_view,
      PerformanceEditScope = PerformanceEditScope::Whole,
      std::vector<std::string> = {}) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Performance take decisions are not supported");
  }
  // Records the decision on the take instead of deleting it, so a rejected
  // proposal keeps the identity that would explain why it was refused.
  [[nodiscard]] virtual core::Result<void> rejectPerformanceTake(std::string_view) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Performance take decisions are not supported");
  }
  // Starts an alternate-take comparison: the candidate is applied over the
  // requested span while the previous selection state stays held, so the creator
  // can play the same passage twice from one playhead and swap between the two.
  // Returns the comparison a surface should mark, or nothing when none is active.
  [[nodiscard]] virtual core::Result<void> beginPerformanceComparison(
      std::string_view, PerformanceEditScope = PerformanceEditScope::Whole,
      std::vector<std::string> = {}) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Performance take comparison is not supported");
  }
  // Runs the surface's automatic-performance backend over a scope and channel set. An
  // empty channel list means the backend's full supported set; a channel the backend
  // cannot generate is refused instead of being dropped from the request.
  [[nodiscard]] virtual core::Result<void> proposeAutomaticPerformance(
      PerformanceEditScope, std::vector<std::string> = {}) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Automatic performance proposals are not supported");
  }
  // Applies the other side of the active comparison. Refused when none is active.
  [[nodiscard]] virtual core::Result<void> swapPerformanceComparison() {
    return core::failure(core::ErrorCode::Unsupported,
                         "Performance take comparison is not supported");
  }
  // Keeps whichever side is applied and releases the held state.
  [[nodiscard]] virtual core::Result<void> endPerformanceComparison() {
    return core::failure(core::ErrorCode::Unsupported,
                         "Performance take comparison is not supported");
  }
  [[nodiscard]] virtual std::optional<PerformanceComparisonMenuItem>
  performanceComparison() const {
    return std::nullopt;
  }
  [[nodiscard]] virtual std::vector<DocumentationMenuItem> documentation()
      const {
    return {};
  }
  [[nodiscard]] virtual core::Result<void> openDocumentation(
      std::string_view) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Offline documentation is not supported");
  }
};

class IApplicationMenu {
public:
  virtual ~IApplicationMenu() = default;
  [[nodiscard]] virtual core::Result<void> install(
      IApplicationCommandDispatcher& dispatcher) = 0;
  virtual void refresh() noexcept = 0;
  virtual void uninstall() noexcept = 0;
};

enum class UnsavedDecision { Save, Discard, Cancel };

class IUnsavedChangesPrompt {
public:
  virtual ~IUnsavedChangesPrompt() = default;
  [[nodiscard]] virtual core::Result<UnsavedDecision> choose(
      std::string_view projectName) = 0;
};

[[nodiscard]] std::unique_ptr<IApplicationMenu> createNativeApplicationMenu();
[[nodiscard]] std::unique_ptr<IUnsavedChangesPrompt>
createNativeUnsavedChangesPrompt();
[[nodiscard]] core::Result<void> openDocumentationPath(
    const std::filesystem::path& path);
[[nodiscard]] core::Result<void> openExternalPath(
    const std::filesystem::path& path);
[[nodiscard]] core::Result<void> copyTextToClipboard(std::string_view text);
[[nodiscard]] core::Result<bool> requestEulaAcceptance(
    const std::filesystem::path& path);

}  // namespace seam::platform
