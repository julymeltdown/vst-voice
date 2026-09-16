#pragma once

#include "seam/core/result.hpp"
#include "seam/authoring/generation_job.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/pixel_surface.hpp"
#include "seam/ui/sample_microscope_model.hpp"
#include "seam/voicebank/manifest_json.hpp"
#include "seam/voicebank/validator.hpp"
#include "seam/voicebank/voicebank.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank/pitch.hpp"
#include "seam/native_ui/pitch_contour.hpp"
#include "seam/voicebank_production/repository.hpp"
#include "seam/voicebank_production/candidate_publication.hpp"
#include "seam/voicebank_production/manifest_draft.hpp"
#include "seam/voice_design/procedural_candidate.hpp"

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <future>
#include <optional>
#include <memory>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace seam::platform { class IFileDialog; }
namespace seam::native_ui {

struct VoicebankStudioTheme final {
  Color background{15, 14, 18, 255};
  Color panel{24, 22, 28, 255};
  Color panelAlternate{20, 20, 24, 255};
  Color grid{58, 52, 64, 255};
  Color primaryText{239, 233, 241, 255};
  Color secondaryText{166, 154, 170, 255};
  Color accent{169, 79, 119, 255};
  Color pitch{101, 187, 184, 255};
  Color waveform{184, 136, 161, 255};
  Color selected{72, 52, 76, 255};
};

[[nodiscard]] std::vector<ui::Rect> voicebankStudioMarkerLabelBounds(
    std::span<const ui::AcousticMarkerVisual> markers,
    ui::Rect waveformBounds);

[[nodiscard]] std::optional<std::size_t> voicebankStudioUnitRailIndexAt(
    double y, std::size_t firstVisibleIndex, std::size_t unitCount,
    double viewportHeight, bool productionLayout) noexcept;
[[nodiscard]] std::size_t voicebankStudioUnitRailVisibleRows(
    double viewportHeight, bool productionLayout) noexcept;

[[nodiscard]] core::Result<std::filesystem::path> nextVoicebankRecordingPath(
    const std::filesystem::path& directory, std::string_view unitId);

class VoicebankStudioController final {
public:
  // A modal/worker capability binds the entire edited manifest, not just its
  // dirty flag. A selection round trip also invalidates it.
  struct SampleReviewContext final {
    SampleReviewContext()
        : epoch(0U), generation(0U), selectionRevision(0U), selectedIndex(0U) {}
    SampleReviewContext(std::uint64_t epochValue, std::uint64_t generationValue,
        std::uint64_t selectionRevisionValue, std::size_t selectedIndexValue,
        std::string projectSha256Value, std::string manifestSha256Value,
        std::string unitIdValue)
        : epoch(epochValue), generation(generationValue),
          selectionRevision(selectionRevisionValue), selectedIndex(selectedIndexValue),
          projectSha256(std::move(projectSha256Value)),
          manifestSha256(std::move(manifestSha256Value)),
          unitId(std::move(unitIdValue)) {}

    std::uint64_t epoch, generation, selectionRevision;
    std::size_t selectedIndex;
    std::string projectSha256, manifestSha256, unitId;
    friend bool operator==(const SampleReviewContext&, const SampleReviewContext&) = default;
  };
  struct SampleReviewInspection final {
    SampleReviewContext context;
    voicebank_production::SampleCandidateReviewPacket packet;
    std::shared_ptr<const voicebank::AudioBuffer> audio;
    std::vector<std::pair<float, float>> peaks;
    std::vector<std::string> details, reviewers;
  };
  struct SourceQualityInspection final {
    SampleReviewContext context;
    voicebank_production::SourceQualityAssessment assessment;
    std::filesystem::path evidencePath;
    std::vector<std::string> details, reviewers;
  };
  struct SourceRegistrationInspection final {
    SampleReviewContext context;
    std::filesystem::path evidencePath;
    std::string evidenceSha256, producerId;
    friend bool operator==(const SourceRegistrationInspection&, const SourceRegistrationInspection&) = default;
  };
  [[nodiscard]] core::Result<void> beginSourceLicenseCapture(const SampleReviewContext& context, std::filesystem::path path);
  [[nodiscard]] core::Result<void> beginSourceRegistration(const SourceRegistrationInspection& expected,
      voicebank_production::SourceStrategyAssessment source);
  [[nodiscard]] const std::optional<SourceRegistrationInspection>& sourceRegistrationInspection() const noexcept { return sourceRegistrationInspection_; }
  [[nodiscard]] const std::optional<voicebank_production::ProductionCommitReceipt>& sourceRegistrationReceipt() const noexcept { return sourceRegistrationReceipt_; }
  [[nodiscard]] core::Result<void> beginSourceQualityEvidenceCapture(const SampleReviewContext& context,
      std::filesystem::path evidencePath);
  [[nodiscard]] core::Result<void> beginSourceQualityDecision(const SourceQualityInspection& expected, std::string id,
      std::string reviewerId, voicebank_production::Feasibility coverage, voicebank_production::Feasibility listening,
      std::string reviewedAtUtc = {});
  [[nodiscard]] const std::optional<SourceQualityInspection>& sourceQualityInspection() const noexcept { return sourceQualityInspection_; }
  [[nodiscard]] const std::optional<voicebank_production::ProductionCommitReceipt>& sourceQualityReceipt() const noexcept { return sourceQualityReceipt_; }
  [[nodiscard]] core::Result<SampleReviewContext> captureSampleReviewContext() const;
  [[nodiscard]] core::Result<void> validateSampleReviewContext(const SampleReviewContext& context) const;
  [[nodiscard]] core::Result<void> beginSelectedSampleReview();
  [[nodiscard]] core::Result<void> beginSampleManifestOpen(const SampleReviewContext& context, std::filesystem::path path);
  [[nodiscard]] core::Result<void> beginSampleManifestDraftCreation(const SampleReviewContext& context,
      voicebank_production::SampleManifestDraftIdentity identity, std::filesystem::path destination,
      voicebank_production::SampleManifestDraftOptions options = {});
  [[nodiscard]] const std::optional<voicebank_production::CreatedSampleManifestDraft>& createdSampleManifestDraft() const noexcept { return createdSampleManifestDraft_; }
  [[nodiscard]] const std::string& sampleManifestDraftLoadDiagnostic() const noexcept { return sampleManifestDraftLoadDiagnostic_; }
  [[nodiscard]] core::Result<void> beginSampleReviewUnitSelection(std::size_t index);
  [[nodiscard]] core::Result<void> selectSampleReviewer(const SampleReviewContext& context, std::string reviewerId);
  [[nodiscard]] core::Result<void> beginSampleReviewDecision(const SampleReviewContext& context,
      std::string reviewerId, voicebank_production::SampleCandidateReviewDecision decision,
      std::string reviewedAtUtc = {});
  [[nodiscard]] core::Result<void> beginSampleCandidatePublication(const SampleReviewContext& context,
      std::filesystem::path destination);
  [[nodiscard]] const std::optional<SampleReviewInspection>& sampleReviewInspection() const noexcept { return sampleReview_; }
  [[nodiscard]] const std::string& sampleReviewerId() const noexcept { return sampleReviewerId_; }
  [[nodiscard]] const std::optional<voicebank_production::SampleCandidateReviewReceipt>& sampleReviewReceipt() const noexcept { return sampleReviewReceipt_; }
  [[nodiscard]] const std::optional<voicebank_production::PublishedSampleCandidate>& publishedSampleCandidate() const noexcept { return publishedSampleCandidate_; }
  [[nodiscard]] const std::string& sampleReviewStatus() const noexcept { return sampleReviewStatus_; }
  struct CandidatePitchInspection final {
    std::string audioSha256, boundaryRevisionId;
    domain::PhonemeKey key;
    time::SampleFrame start{0}, end{0};
    std::int32_t targetMidi{0};
    voicebank::PitchConfig config;
    std::vector<voicebank::PitchFrame> frames; // Absolute raw-source frame origins.
    std::optional<double> medianHz, centsFromTarget;
    std::size_t voicedFrames{0U};
    std::vector<PitchContourPoint> contour;
    std::string projectId, takeId, inventorySha256, operatorId, recipeHash;
    std::uint64_t sourceGeneration{0U};
    std::uint32_t sampleRate{0U};
  };
  [[nodiscard]] core::Result<void> beginCandidatePitchInspection();
  [[nodiscard]] core::Result<void> exportCandidatePitchInspection(
      const std::filesystem::path& destination, std::string exportedAtUtc = {}) const;
  [[nodiscard]] core::Result<void> toggleCandidatePitchView();
  [[nodiscard]] bool candidatePitchView() const noexcept { return candidatePitchView_ && candidatePitch_.has_value(); }
  [[nodiscard]] const std::optional<CandidatePitchInspection>& candidatePitchInspection() const noexcept { return candidatePitch_; }
  [[nodiscard]] const std::string& candidatePitchError() const noexcept { return candidatePitchError_; }
  [[nodiscard]] static ui::Rect candidateWaveformBounds(double width, double height) noexcept;
  [[nodiscard]] core::Result<void> zoomCandidateWaveform(bool zoomIn);
  [[nodiscard]] core::Result<void> panCandidateWaveform(bool right);
  [[nodiscard]] std::pair<time::SampleFrame, time::SampleFrame> candidateWaveformView() const noexcept;
  [[nodiscard]] const std::vector<std::pair<float, float>>& candidateWaveformPeaks() const noexcept;
  [[nodiscard]] core::Result<bool> beginCandidateMarkerDrag(ui::Point point);
  [[nodiscard]] core::Result<void> updateCandidateMarkerDrag(ui::Point point);
  [[nodiscard]] core::Result<void> finishCandidateMarkerDrag();
  void cancelCandidateMarkerDrag() noexcept;
  [[nodiscard]] bool candidateMarkerDragging() const noexcept { return candidateDrag_.has_value(); }
  struct CandidateWaveform final {
    std::vector<std::pair<float, float>> peaks;
    std::string audioSha256;
    std::uint32_t sampleRate{0U};
    time::SampleFrame frameCount{0};
    std::shared_ptr<const voicebank::AudioBuffer> audio;
    voicebank::AudioStatistics statistics;
  };
  [[nodiscard]] core::Result<void> beginCandidateWaveformPreview();
  [[nodiscard]] const std::optional<CandidateWaveform>& candidateWaveform() const noexcept { return candidateWaveform_; }
  [[nodiscard]] const std::string& candidateWaveformError() const noexcept { return candidateWaveformError_; }
  [[nodiscard]] core::Result<void> selectCandidateMarker(std::size_t index);
  [[nodiscard]] core::Result<void> editSelectedCandidateMarker(
      time::SampleFrame start, time::SampleFrame end, std::string occurredAtUtc = {});
  [[nodiscard]] core::Result<void> undoCandidateMarkerEdit(std::string occurredAtUtc = {});
  [[nodiscard]] core::Result<void> redoCandidateMarkerEdit(std::string occurredAtUtc = {});
  [[nodiscard]] bool canUndoCandidateMarkerEdit() const noexcept { return !proceduralImportBusy() && !candidateUndo_.empty(); }
  [[nodiscard]] bool canRedoCandidateMarkerEdit() const noexcept { return !proceduralImportBusy() && !candidateRedo_.empty(); }
  [[nodiscard]] core::Result<void> moveCandidateMarker(std::int32_t delta);
  [[nodiscard]] std::size_t selectedCandidateMarker() const noexcept { return selectedCandidateMarker_; }
  [[nodiscard]] std::pair<std::size_t, std::size_t> candidateMarkerWindow(std::size_t visibleRows) const noexcept;
  ~VoicebankStudioController();
  // Owner-thread API. Workers own an isolated project/repository, never this controller.
  [[nodiscard]] core::Result<void> beginProceduralCandidateImport(
      std::filesystem::path metadataPath, std::filesystem::path audioPath,
      std::filesystem::path recipePath, std::string occurredAtUtc = {});
  [[nodiscard]] core::Result<void> beginPreparedGenerationJob(
      std::filesystem::path directory, std::string manifestSha256, std::string occurredAtUtc = {});
  [[nodiscard]] core::Result<void> beginGenerationPreparation(
      std::filesystem::path scorePath, domain::TrackId trackId, domain::RegionId regionId,
      std::filesystem::path directory, std::string jobId, std::string expectedScoreSha256 = {},
      std::optional<authoring::GenerationRecipeSelection> selectedRecipe = std::nullopt);
  struct GenerationScoreSelection final {
    struct Region final { domain::TrackId trackId; domain::RegionId regionId; std::string label; };
    std::filesystem::path path;
    std::string sha256;
    std::vector<Region> regions;
    std::uint64_t epoch{0U}, generation{0U};
    std::size_t selectedIndex{0U};
    std::optional<authoring::GenerationRecipeSelection> selectedRecipe{std::nullopt};
  };
  [[nodiscard]] core::Result<void> beginGenerationScoreInspection(std::filesystem::path scorePath,
      std::optional<authoring::GenerationRecipeSelection> selectedRecipe = std::nullopt);
  [[nodiscard]] std::optional<GenerationScoreSelection> takeGenerationScoreSelection() {
    if (generationScoreSelection_) status_ = statusBeforeImport_;
    return std::exchange(generationScoreSelection_, std::nullopt);
  }
  // Dialogs inspect a copy without consuming the frozen Designer snapshot.
  // Successful preparation start consumes it; cancelled dialogs retain it.
  [[nodiscard]] const std::optional<GenerationScoreSelection>& generationScoreSelection() const noexcept {
    return generationScoreSelection_;
  }
  [[nodiscard]] core::Result<void> beginPreparedGenerationBatch(
      std::filesystem::path manifestPath, std::string manifestSha256,
      std::uint64_t maximumFrames = 32ULL * 1024ULL * 1024ULL, std::string occurredAtUtc = {});
  [[nodiscard]] core::Result<void> beginGenerationBatchAssembly(
      std::vector<std::filesystem::path> references, std::filesystem::path destination);
  struct GenerationBatchProgress final {
    enum class Phase : std::uint32_t { Preflight, Rendering, Collecting };
    Phase phase{Phase::Preflight};
    std::size_t completedOutputs{0U}, totalOutputs{0U};
  };
  [[nodiscard]] std::optional<GenerationBatchProgress> generationBatchProgress() const noexcept;
  // Campaign orchestration over the same services the CLI calls. Planning
  // publishes one immutable definition into a new directory; advancement commits
  // at most one bounded batch per service call, so a run loops until the campaign
  // completes or the user cancels. Cancelling retains committed batches and the
  // campaign can be resumed from its own receipts, never from a re-plan.
  struct GenerationCampaignProgress final {
    enum class Phase : std::uint32_t { Idle, Planning, Planned, Advancing, Complete, Cancelled, Failed };
    Phase phase{Phase::Idle};
    std::size_t completedBatches{0U}, totalBatches{0U};
  };
  [[nodiscard]] core::Result<void> beginGenerationCampaignPlan(
      std::filesystem::path recipePath, std::vector<std::string> plannedTakeIds,
      std::filesystem::path destination, std::size_t maximumJobsPerBatch = 0U);
  [[nodiscard]] core::Result<void> beginGenerationCampaignAdvance(
      std::filesystem::path campaignPath, std::string campaignSha256,
      std::string occurredAtUtc = {});
  // Advances the campaign identity this controller published or adopted last.
  [[nodiscard]] core::Result<void> beginGenerationCampaignResume(
      std::string occurredAtUtc = {});
  void cancelGenerationCampaign() noexcept;
  [[nodiscard]] std::optional<GenerationCampaignProgress> generationCampaignProgress() const noexcept;
  [[nodiscard]] bool generationCampaignBusy() const noexcept { return campaignWork_.valid(); }
  [[nodiscard]] const std::filesystem::path& generationCampaignPath() const noexcept {
    return campaignPath_;
  }
  [[nodiscard]] const std::string& generationCampaignSha256() const noexcept {
    return campaignSha256_;
  }
  [[nodiscard]] core::Result<void> pollProceduralCandidateImport();
  // Shutdown only: request cancellation, join, then collect the actual commit result.
  [[nodiscard]] core::Result<void> finishProceduralCandidateImport();
  void cancelProceduralCandidateImport() noexcept;
  // Non-consuming observation of the producer result, before UI adoption.
  [[nodiscard]] bool proceduralImportResultReady() const {
    return proceduralImport_.valid() && proceduralImport_.wait_for(std::chrono::seconds{0})==std::future_status::ready;
  }
  [[nodiscard]] bool proceduralImportBusy() const noexcept { return workspaceOpen_.valid() || proceduralImport_.valid() || campaignWork_.valid() || waveformLoad_.valid() || pitchLoad_.valid() || sampleReviewWork_.valid() || editableUnitLoad_.valid() || candidateDrag_.has_value(); }
  [[nodiscard]] const std::optional<voice_design::ProceduralCandidate>& candidateMarkerPreview() const noexcept {
    return candidateMarkerPreview_;
  }
  [[nodiscard]] const std::string& candidateMarkerError() const noexcept { return candidateMarkerError_; }
  [[nodiscard]] bool candidateMarkersEdited() const noexcept { return candidateMarkersEdited_; }
  [[nodiscard]] core::Result<void> openManifest(
      const std::filesystem::path& manifestPath,
      double logicalWidth = 1440.0, double logicalHeight = 900.0);
  [[nodiscard]] core::Result<void> openProductionProject(
      const std::filesystem::path& workspaceRoot,
      std::string_view expectedInventorySha256,
      std::string operatorId, bool requireRegisteredProducer = false);
  [[nodiscard]] core::Result<void> beginOpenProductionProject(std::filesystem::path workspaceRoot,
      std::string expectedInventorySha256, std::string operatorId);
  [[nodiscard]] bool workspaceOpening() const noexcept { return workspaceOpen_.valid(); }
  [[nodiscard]] core::Result<void> save();
  // Returns authorization for this unchanged sample state only. Does not
  // discard model data; the host closes only after other documents agree.
  [[nodiscard]] core::Result<bool> confirmSampleClose(platform::IFileDialog& dialog);
  [[nodiscard]] core::Result<void> selectUnit(std::size_t index);
  [[nodiscard]] core::Result<void> beginEditableUnitSelection(std::size_t index);
  [[nodiscard]] core::Result<void> moveSelectedMarker(ui::AcousticMarkerKind marker,
                                                       double x);
  [[nodiscard]] core::Result<void> moveSelectedPitchMark(std::size_t index,
                                                          double x);
  [[nodiscard]] core::Result<void> inspectTake(
      const std::filesystem::path& path, std::int32_t expectedRootMidi);
  [[nodiscard]] core::Result<void> inspectSelectedProductionTake(
      const std::filesystem::path& path);
  [[nodiscard]] core::Result<std::filesystem::path> persistTakeInspection(
      const std::filesystem::path& takePath) const;
  [[nodiscard]] core::Result<void> importSelectedTake(
      const std::filesystem::path& takePath,
      std::string occurredAtUtc = {});
  [[nodiscard]] core::Result<void> importSelectedProceduralCandidate(
      const std::filesystem::path& metadataPath, const std::filesystem::path& audioPath,
      const std::filesystem::path& recipePath, std::string occurredAtUtc = {},
      std::stop_token stopToken = {});
  [[nodiscard]] std::uint64_t productionSessionEpoch() const noexcept { return productionSessionEpoch_; }
  // Validates a modal interaction captured on this controller instance.
  [[nodiscard]] core::Result<void> validateProductionImportContext(
      std::uint64_t epoch, std::uint64_t generation, std::size_t selectedIndex) const;
  [[nodiscard]] core::Result<voicebank_production::CommittedDerivedRevision>
  applySelectedProductionOperation(
      const voicebank_production::OperationRequest& request,
      std::string occurredAtUtc = {});
  [[nodiscard]] core::Result<voicebank_production::ExportedU57Inputs>
  exportProductionInputs(const std::filesystem::path& destination,
                         std::string occurredAtUtc = {});
  void resize(double logicalWidth, double logicalHeight);

  [[nodiscard]] const voicebank::Manifest& manifest() const noexcept { return manifest_; }
  [[nodiscard]] voicebank::Manifest& manifest() noexcept { return manifest_; }
  [[nodiscard]] const voicebank::Unit* selectedUnit() const noexcept;
  [[nodiscard]] voicebank::Unit* selectedUnit() noexcept;
  [[nodiscard]] const ui::SampleMicroscopeModel& microscope() const noexcept {
    return microscope_;
  }
  [[nodiscard]] std::size_t selectedIndex() const noexcept { return selectedIndex_; }
  [[nodiscard]] double logicalHeight() const noexcept { return logicalHeight_; }
  [[nodiscard]] double logicalWidth() const noexcept { return logicalWidth_; }
  [[nodiscard]] std::size_t selectableUnitCount() const noexcept;
  [[nodiscard]] bool dirty() const noexcept { return dirty_; }
  [[nodiscard]] const std::filesystem::path& manifestPath() const noexcept {
    return manifestPath_;
  }
  [[nodiscard]] std::filesystem::path recordingDirectory() const;
  [[nodiscard]] const std::string& status() const noexcept { return status_; }
  [[nodiscard]] const std::optional<voicebank::DryTakeInspection>&
  takeInspection() const noexcept {
    return takeInspection_;
  }
  [[nodiscard]] const voicebank_production::VoicebankProductionProject*
  productionProject() const noexcept {
    return productionProject_ ? &*productionProject_ : nullptr;
  }
  [[nodiscard]] const voicebank_production::UnitAssignment*
  selectedProductionAssignment() const noexcept;
  [[nodiscard]] voicebank_production::ProductionQueueSummary
  productionQueues() const noexcept;
  [[nodiscard]] std::size_t stagedRecoveryCandidateCount() const noexcept {
    return stagedRecoveryCandidateCount_;
  }
  [[nodiscard]] std::optional<voicebank_production::UnitQueueState>
  productionStateForUnit(const voicebank::Unit& unit) const noexcept;

private:
  struct SampleAudioBindings final {
    std::map<std::string, std::string, std::less<>> hashesByUnit;
    std::optional<std::string> draftDescriptorSha256;
  };
  struct PreparedUnitVisual final {
    voicebank::AudioBuffer audio;
    ui::SampleMicroscopeModel microscope;
  };
  void invalidateSampleReview() noexcept;
  [[nodiscard]] core::Result<void> pollSampleReviewWork();
  struct SampleReviewWorkResult final {
    struct LoadedUnit final {
      LoadedUnit() : index(0U), dirty(false) {}
      LoadedUnit(voicebank::Manifest manifestValue, voicebank::AudioBuffer audioValue,
          ui::SampleMicroscopeModel microscopeValue,
          std::filesystem::path manifestPathValue, std::filesystem::path rootValue,
          std::size_t indexValue, bool dirtyValue, SampleAudioBindings audioBindingsValue)
          : manifest(std::move(manifestValue)), audio(std::move(audioValue)),
            microscope(std::move(microscopeValue)),
            manifestPath(std::move(manifestPathValue)), root(std::move(rootValue)),
            index(indexValue), dirty(dirtyValue),
            audioBindings(std::move(audioBindingsValue)) {}

      voicebank::Manifest manifest;
      voicebank::AudioBuffer audio;
      ui::SampleMicroscopeModel microscope;
      std::filesystem::path manifestPath, root;
      std::size_t index;
      bool dirty;
      SampleAudioBindings audioBindings;
    };
    SampleReviewContext context;
    std::optional<SampleReviewInspection> inspection;
    std::optional<voicebank_production::VoicebankProductionProject> committedProject;
    std::optional<voicebank_production::SampleCandidateReviewReceipt> receipt;
    std::optional<voicebank_production::PublishedSampleCandidate> published;
    std::optional<LoadedUnit> loadedUnit;
    std::optional<voicebank_production::CreatedSampleManifestDraft> createdDraft;
    std::string draftLoadDiagnostic;
    std::optional<SourceQualityInspection> sourceQualityInspection;
    std::optional<voicebank_production::ProductionCommitReceipt> sourceQualityReceipt;
    std::optional<SourceRegistrationInspection> sourceRegistrationInspection;
    std::optional<voicebank_production::ProductionCommitReceipt> sourceRegistrationReceipt;
  };
  void adoptLoadedSampleUnit(SampleReviewWorkResult::LoadedUnit loaded);
  [[nodiscard]] core::Result<SampleReviewWorkResult::LoadedUnit> prepareSampleManifestLoad(
      const std::filesystem::path& path, double width, double height, std::stop_token stop = {},
      const voicebank_production::CreatedSampleManifestDraft* expectedDraft = nullptr) const;
  [[nodiscard]] core::Result<PreparedUnitVisual> prepareUnitVisual(const voicebank::Manifest& manifest,
      const std::filesystem::path& root, const SampleAudioBindings& bindings, std::size_t index,
      double width, double height, std::stop_token stop = {}) const;
  [[nodiscard]] core::Result<void> relayoutSelected();
  std::future<core::Result<SampleReviewWorkResult>> sampleReviewWork_;
  struct EditableUnitLoad final {
    std::string contextIdentity;
    SampleReviewWorkResult::LoadedUnit loaded;
  };
  [[nodiscard]] core::Result<std::string> editableUnitLoadIdentity() const;
  [[nodiscard]] core::Result<void> pollEditableUnitLoad();
  std::future<core::Result<EditableUnitLoad>> editableUnitLoad_;
  std::optional<SampleReviewInspection> sampleReview_;
  std::optional<SourceQualityInspection> sourceQualityInspection_;
  std::optional<voicebank_production::ProductionCommitReceipt> sourceQualityReceipt_;
  std::optional<SourceRegistrationInspection> sourceRegistrationInspection_;
  std::optional<voicebank_production::ProductionCommitReceipt> sourceRegistrationReceipt_;
  std::optional<voicebank_production::SampleCandidateReviewReceipt> sampleReviewReceipt_;
  std::optional<voicebank_production::PublishedSampleCandidate> publishedSampleCandidate_;
  std::optional<voicebank_production::CreatedSampleManifestDraft> createdSampleManifestDraft_;
  std::string sampleManifestDraftLoadDiagnostic_;
  std::uint64_t sampleReviewSelectionRevision_{0U};
  std::string sampleReviewerId_, sampleReviewStatus_{"NO REVIEW CAPTURED"};
  void refreshCandidateMarkerPreview(bool preserveHistory = false);
  void centerCandidateWaveform(time::SampleFrame length, std::optional<time::SampleFrame> centerOverride = {});
  [[nodiscard]] core::Result<void> commitCandidateMarkerBounds(
      time::SampleFrame start, time::SampleFrame end, std::string occurredAtUtc);
  [[nodiscard]] core::Result<void> replayCandidateMarkerEdit(bool redo, std::string occurredAtUtc);
  [[nodiscard]] core::Result<void> rebuildSelected(std::stop_token stop = {});
  [[nodiscard]] std::filesystem::path selectedAudioPath() const;
  [[nodiscard]] core::Result<voicebank_production::CommittedMetadataRevision> persistProductionMetadata();
  [[nodiscard]] core::Result<void> saveProductionProject();

  voicebank::ManifestJsonCodec codec_;
  std::filesystem::path manifestPath_;
  std::filesystem::path root_;
  std::filesystem::path productionWorkspaceRoot_;
  voicebank::Manifest manifest_;
  voicebank::AudioBuffer audio_;
  SampleAudioBindings sampleAudioBindings_;
  std::string pinnedAudioUnitId_;
  std::filesystem::path pinnedAudioPath_;
  ui::SampleMicroscopeModel microscope_;
  std::size_t selectedIndex_{0U};
  bool dirty_{false};
  std::string status_{"NO BANK"};
  std::optional<voicebank::DryTakeInspection> takeInspection_;
  std::uint64_t productionSessionEpoch_{0U};
  std::future<core::Result<std::unique_ptr<VoicebankStudioController>>> workspaceOpen_;
  std::uint64_t workspaceOpenEpoch_{0U};
  std::unique_ptr<voicebank_production::ProductionProjectRepository>
      productionRepository_;
  std::optional<voicebank_production::VoicebankProductionProject>
      productionProject_;
  // Campaign work shares the controller's single stop source with every other
  // production worker: proceduralImportBusy() admits one worker at a time, so a
  // stop request always belongs to the running job.
  struct GenerationCampaignOutcome final {
    std::filesystem::path campaignPath;
    std::string campaignSha256;
    std::size_t completedBatches{0U}, totalBatches{0U};
    voicebank_production::VoicebankProductionProject producer;
    bool adoptedProducer{false};
    std::string status;
  };
  std::future<core::Result<GenerationCampaignOutcome>> campaignWork_;
  std::shared_ptr<std::atomic<std::uint64_t>> generationCampaignProgress_;
  std::filesystem::path campaignPath_;
  std::string campaignSha256_;
  std::string productionOperatorId_;
  std::size_t stagedRecoveryCandidateCount_{0U};
  double logicalWidth_{1440.0};
  double logicalHeight_{900.0};
  std::string statusBeforeImport_;
  std::optional<voice_design::ProceduralCandidate> candidateMarkerPreview_;
  std::string candidateMarkerError_;
  std::size_t selectedCandidateMarker_{0U};
  std::string candidateMarkerRevisionId_;
  bool candidateMarkersEdited_{false};
  struct CandidateMarkerUndo final {
    std::size_t index;
    time::SampleFrame beforeStart, beforeEnd, afterStart, afterEnd;
  };
  std::vector<CandidateMarkerUndo> candidateUndo_, candidateRedo_;
  struct CandidateMarkerDrag final {
    std::size_t index;
    time::SampleFrame start, end;
    bool endHandle;
    std::string previousStatus;
  };
  std::optional<CandidateMarkerDrag> candidateDrag_;
  std::optional<std::pair<time::SampleFrame, time::SampleFrame>> candidateView_;
  std::vector<std::pair<float, float>> candidateViewPeaks_;
  std::stop_source proceduralImportStop_;
  struct ProductionImportResult final {
    voicebank_production::VoicebankProductionProject project;
    std::string status;
    bool changed{true};
    std::optional<GenerationScoreSelection> scoreSelection{std::nullopt};
  };
  std::future<core::Result<ProductionImportResult>> proceduralImport_;
  std::optional<GenerationScoreSelection> generationScoreSelection_;
  // One packed atomic gives the owner thread a coherent phase/total/completed snapshot.
  // The worker owns shared state, never a pointer to this controller.
  std::shared_ptr<std::atomic<std::uint32_t>> generationBatchProgress_;
  std::optional<CandidateWaveform> candidateWaveform_;
  std::string candidateWaveformError_;
  std::future<core::Result<CandidateWaveform>> waveformLoad_;
  std::optional<CandidatePitchInspection> candidatePitch_;
  std::string candidatePitchError_;
  std::future<core::Result<CandidatePitchInspection>> pitchLoad_;
  bool candidatePitchView_{false};
};

class VoicebankStudioScenePainter final {
public:
  explicit VoicebankStudioScenePainter(VoicebankStudioTheme theme = {}) noexcept
      : theme_(theme) {}
  void paint(RasterCanvas& canvas,
             const VoicebankStudioController& controller,
             bool recording = false,
             std::string_view recordingBackend = "OFF") const noexcept;

private:
  VoicebankStudioTheme theme_;
};

struct StudioSampleReviewControl final {
  std::string id, label;
  ui::Rect bounds;
  bool enabled{false};
};
[[nodiscard]] std::vector<StudioSampleReviewControl> studioSampleReviewControls(
    const VoicebankStudioController& controller, double width);
[[nodiscard]] std::vector<StudioSampleReviewControl> studioGenerationControls(
    const VoicebankStudioController& controller, double width, bool recordingActive);
[[nodiscard]] std::vector<std::string> studioSampleReviewDetailLines(
    const VoicebankStudioController& controller, double width);
[[nodiscard]] std::size_t studioSampleReviewVisibleLines(double height) noexcept;
void paintStudioSampleReview(RasterCanvas& canvas, const VoicebankStudioController& controller,
    std::size_t firstDetailLine, std::string_view interactionStatus = {});
// Shared native modal workflows are testable with an injected dialog. They
// never imply approval from capture, reviewer selection or a cancelled modal.
[[nodiscard]] core::Result<void> chooseStudioSampleReviewer(VoicebankStudioController& controller, platform::IFileDialog& dialog);
[[nodiscard]] core::Result<void> confirmStudioSampleReview(VoicebankStudioController& controller, platform::IFileDialog& dialog,
    voicebank_production::SampleCandidateReviewDecision decision);
[[nodiscard]] core::Result<void> publishStudioSampleCandidate(VoicebankStudioController& controller, platform::IFileDialog& dialog);
[[nodiscard]] core::Result<void> openStudioSampleManifest(VoicebankStudioController& controller, platform::IFileDialog& dialog);
[[nodiscard]] core::Result<void> createStudioSampleManifestDraft(VoicebankStudioController& controller, platform::IFileDialog& dialog);
[[nodiscard]] core::Result<void> captureStudioSourceQuality(VoicebankStudioController& controller, platform::IFileDialog& dialog);
[[nodiscard]] core::Result<void> confirmStudioSourceQuality(VoicebankStudioController& controller, platform::IFileDialog& dialog);
[[nodiscard]] core::Result<void> captureStudioSourceLicense(VoicebankStudioController& controller, platform::IFileDialog& dialog);
[[nodiscard]] core::Result<void> registerStudioSource(VoicebankStudioController& controller, platform::IFileDialog& dialog);

}  // namespace seam::native_ui
