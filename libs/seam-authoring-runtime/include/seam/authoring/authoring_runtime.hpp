#pragma once

#include "seam/authoring/project_document.hpp"
#include "seam/authoring/diagnostic.hpp"
#include "seam/authoring/render_coordinator.hpp"
#include "seam/authoring/technical_edit_controller.hpp"
#include "seam/authoring/transport_controller.hpp"
#include "seam/authoring/voicebank_session.hpp"
#include "seam/core/result.hpp"

#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace seam::authoring {

struct AuthoringRuntimeConfig final {
  std::filesystem::path cacheRoot;
  std::vector<voicebank::VoicebankSearchRoot> voicebankRoots;
  std::uint32_t previewSampleRate{48000U};
  std::uint8_t outputChannels{2U};
  bool allowDevelopmentVoicebanks{false};
  bool enableTransport{true};
  // Observation points of the preview coordinator, for tests that must hold a render in
  // flight. Empty in production.
  RenderCoordinatorHooks renderHooks{};
  // Test-only barriers around the seam preview's publication, for tests that must hold a
  // completion while something else happens. Empty in production. A test that holds one must
  // release it before the runtime is destroyed.
  //  - beforeSeamPreviewPublication runs where the preview's completion is delivered (the render
  //    thread, or the thread that cancelled the render), before the runtime decides anything and
  //    outside every runtime lock.
  //  - duringSeamPreviewPublication runs with the runtime's audition lock held, once the
  //    completion has found its request current and before it hands the audio to the transport.
  std::function<void()> beforeSeamPreviewPublication{};
  std::function<void()> duringSeamPreviewPublication{};
  // How many times the runtime, on its own, asks the transport again for a change it refused (see
  // diagnostics(): the "playback.clear-pending" and "playback.update-pending" entries). The first
  // fifty tries are 2 ms apart and the rest 50 ms apart, so the default keeps trying for about five
  // seconds. Zero leaves the asking to the next request: an edit, or the creator's Retry.
  unsigned transportRetryAttempts{150U};
};

class AuthoringRuntime final {
public:
  struct AudiblePublication final {
    std::shared_ptr<const PublishedProjectAudio> audio;
    bool performanceAudition{false};
    bool stale{false};
  };
  AuthoringRuntime(std::unique_ptr<ProjectDocument> document,
                   AuthoringRuntimeConfig config);
  ~AuthoringRuntime();

  AuthoringRuntime(const AuthoringRuntime&) = delete;
  AuthoringRuntime& operator=(const AuthoringRuntime&) = delete;

  [[nodiscard]] core::Result<void> initialize();
  void shutdown() noexcept;

  [[nodiscard]] ProjectDocument& document() noexcept { return *document_; }
  [[nodiscard]] const ProjectDocument& document() const noexcept {
    return *document_;
  }
  [[nodiscard]] VoicebankSession& voicebanks() noexcept { return voicebanks_; }
  [[nodiscard]] const VoicebankSession& voicebanks() const noexcept {
    return voicebanks_;
  }
  [[nodiscard]] AuthoringRenderCoordinator& renderer() noexcept {
    return renderer_;
  }
  [[nodiscard]] const AuthoringRenderCoordinator& renderer() const noexcept {
    return renderer_;
  }
  [[nodiscard]] TransportController& transport() noexcept { return transport_; }
  [[nodiscard]] const TransportController& transport() const noexcept {
    return transport_;
  }
  [[nodiscard]] TechnicalEditController& technicalEdits() noexcept {
    return technicalEdits_;
  }
  [[nodiscard]] const TechnicalEditController& technicalEdits() const noexcept {
    return technicalEdits_;
  }
  // What the creator should be told now. Two kinds of render diagnostic are told apart by where
  // they come from. A standing condition of the project (no vocal track has a bank it can be
  // rendered with) is reported for as long as it holds, whatever a render is doing, and is
  // dropped only when the bank resolves. The outcome of a render attempt is left out while a newer
  // attempt is queued or running, because it describes an attempt that one has replaced, and is
  // cleared for good by a render that succeeds.
  // The standing condition is an assessment made when a render request was last composed, which
  // happens after every document change and every bank selection, relink or refresh the runtime
  // performs. It is not a live look at the resources: a bank bound directly through
  // VoicebankSession::bindTrack is reported as resolved only once the runtime has composed again,
  // which handleDocumentChanged does.
  // A third kind is the transport being behind the score: the feeder refused a clear or a
  // publication (its control queue was full), so playback still holds audio that no longer matches.
  // Like a missing bank it is a standing condition, not a record: it is reported for as long as the
  // runtime still owes the transport the change, whatever the render is doing, and it goes when the
  // change has been made. The runtime asks again on its own for a bounded time, and again at every
  // request that decides what the transport should hold.
  [[nodiscard]] std::vector<Diagnostic> diagnostics() const;
  // Forgets what has been recorded. A condition that still holds is not a record and is reported
  // again, so dismissing it does not make a missing bank go away.
  void clearDiagnostics() noexcept {
    std::lock_guard lock(diagnosticsMutex_);
    diagnostics_.clear();
  }

  [[nodiscard]] core::Result<void> selectTrack(domain::TrackId trackId);
  [[nodiscard]] core::Result<void> selectRegion(domain::RegionId regionId);
  // The editor has no vocal track to work on: the last one was removed, or the score was replaced by
  // one without a vocal region. Drops the selection, so that nothing this runtime renders, auditions
  // or edits still names a track or region that is gone. Selection is view state and dirties nothing.
  void clearSelection();
  [[nodiscard]] domain::TrackId selectedTrack() const noexcept {
    return selectedTrack_;
  }
  [[nodiscard]] domain::RegionId selectedRegion() const noexcept {
    return selectedRegion_;
  }

  [[nodiscard]] core::Result<void> execute(
      std::unique_ptr<application::ICommand> command);
  [[nodiscard]] core::Result<void> executePerformanceResult(
      const application::PerformanceJobContext& context,
      std::unique_ptr<application::ICommand> command);
  [[nodiscard]] core::Result<void> undo();
  [[nodiscard]] core::Result<void> redo();
  [[nodiscard]] core::Result<void> previewSeam(domain::PhonemeKey key,
                                               bool alternate);
  // Render one alternate accepted-take selection from a copy of the current project. The document,
  // undo stack, autosave and export input are never changed by auditioning. The transport retains its
  // playhead while switching the published timeline. Owner-thread requests; publication is async.
  [[nodiscard]] core::Result<void> auditionPerformance(
      domain::RegionId regionId,
      std::vector<domain::AcceptedPerformanceSelection> accepted);
  // Apply the explicit take-acceptance command while retaining its already audible
  // render until the canonical render for the new project revision is published.
  [[nodiscard]] core::Result<void> acceptPerformanceAudition(
      std::unique_ptr<application::ICommand> command);
  [[nodiscard]] core::Result<void> stopPerformanceAudition();
  [[nodiscard]] bool performanceAuditionActive() const noexcept {
    return performanceAuditionActive_.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool performanceAuditionReady() const noexcept {
    return performanceAuditionReady_.load(std::memory_order_acquire);
  }
  [[nodiscard]] AudiblePublication audiblePublication() const;
  [[nodiscard]] bool seamPreviewActive() const noexcept {
    return seamPreviewActive_.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool seamPreviewReady() const noexcept {
    return seamPreviewReady_.load(std::memory_order_acquire);
  }

  [[nodiscard]] core::Result<void> setPreviewSampleRate(std::uint32_t sampleRate);
  [[nodiscard]] core::Result<void> reconfigureAudio(
      std::uint32_t sampleRate, std::uint8_t outputChannels,
      std::size_t blockFrames);
  void setRenderQuality(rendering::RenderQuality quality);
  [[nodiscard]] rendering::RenderQuality renderQuality() const noexcept {
    return renderQuality_;
  }
  // Optional tempo map that replaces the document's map for subsequent renders. A host
  // that owns timing (Follow Host) supplies an acquired map here, so the render follows
  // the host's own events instead of one instantaneous BPM. The substituted project is
  // what gets rendered and what the render identity covers, so audio can never be
  // attributed to a map it was not rendered with. Owner-thread only, like every other
  // render setting.
  void setTempoMapOverride(std::optional<time::TempoMap> map);
  void setCompletionCallback(std::function<void()> callback);
  // Owner-thread resource/document invalidation. Retain historical PCM but
  // revoke current measurement authority and every pending debounce request.
  void invalidatePreview();
  void requestPreview(
      bool immediate = false,
      application::CommandImpact impact = application::CommandImpact{
          .scope = application::CommandAudioImpact::ProjectAudio,
          .projectWide = true});
  void handleDocumentChanged();

private:
  [[nodiscard]] core::Result<void> afterCommandExecution(
      core::Result<void> result, application::CommandImpact impact,
      std::shared_ptr<const PublishedProjectAudio> retainedAudition = {});
  void requestPreviewImpl(bool immediate, application::CommandImpact impact,
                          bool retainAcceptedAudition);
  struct PreviewRequest final {
    domain::Project project;
    std::vector<rendering::TrackSingerSource> voicebanks;
    domain::TrackId activeTrack;
    domain::RegionId activeRegion;
    std::uint64_t revision{0U};
    std::uint32_t sampleRate{48000U};
    rendering::RenderQuality quality{rendering::RenderQuality::Preview};
    application::CommandImpact impact{
        .scope = application::CommandAudioImpact::ProjectAudio,
        .projectWide = true};
  };
  // What composing a render request learns about the score, whether or not a request results.
  struct PreviewAssessment final {
    // The score has vocal tracks and none of them has a bank (or a recipe) it can be rendered with.
    bool bankUnavailable{false};
    // Nothing anywhere in the score could sound: no backing audio, and no note on a track that has
    // something to sing it. Only then is there nothing to render; a selected region with no notes
    // does not make the score empty.
    bool nothingAudible{false};
  };

  [[nodiscard]] std::optional<PreviewRequest> makePreviewRequest(
      application::CommandImpact impact, PreviewAssessment* assessment = nullptr) const;
  void noteBankAvailability(bool unavailable);
  // Nothing is left to render: drops the attempt outcomes, the audio the transport still holds and
  // the coordinator's state, so nothing keeps sounding or describing a render of a score that is gone.
  void settleWithNothingToRender();
  void submitPreview(PreviewRequest request, bool immediate);
  void previewWorkerLoop(std::stop_token stopToken);
  [[nodiscard]] TechnicalRenderView currentTechnicalRenderView() const;
  [[nodiscard]] std::pair<domain::TrackId, domain::RegionId>
  firstRenderableSelection(const domain::Project& project,
                           const std::vector<TrackVoicebankState>& states) const;
  void publishCompletedAudio();
  void publishCompletedSeamPreview();
  void publishCompletedPerformanceAudition();
  // Ends the transient seam preview: it stops being wanted, and its render is cancelled. The
  // flags change under the audition lock, the lock a completion holds while it publishes, so a
  // completion is entirely before this (and whatever the caller publishes next replaces it) or
  // entirely after (and finds its request revoked).
  void revokeSeamPreview();
  // A format change has discarded what the transport held, so neither the seam preview nor a
  // performance comparison owns playback any longer, and the audio they were rendered as is for the
  // old format. Both end without a hand-back (the coordinator's canonical audio is for the old
  // format too, and the transport would refuse it); the render that follows is the next audio the
  // transport is given.
  void retireTransientAudio();
  void recordDiagnostic(const core::Error& error);
  void recordRenderFailure(RenderFailureKind failure, std::string message);
  void clearRenderDiagnostics() noexcept;

  // What the feeder refused to do for the score, and the score still needs. The transport holds
  // what it held until the change is made, so the runtime keeps the obligation instead of acting as
  // if the change had been made. Only the newest decision counts: a clear replaces a publication
  // that was owed and the other way round, and either is settled by a change that succeeds.
  enum class TransportDebt : std::uint8_t {
    None,
    // The score has nothing to play and the transport still holds audio.
    Clear,
    // The canonical audio was not handed over and the transport still holds an earlier version.
    Publish,
  };
  // The next four need performanceAuditionMutex_, the lock under which every decision about what the
  // transport holds is made.
  void oweTransport(TransportDebt debt, const core::Error& refusal);
  void settleTransportDebt();
  // Makes the change that is owed. True when the debt is gone (paid, or no longer applicable).
  [[nodiscard]] bool repayTransportDebt();
  // Hands the transport the canonical audio again after a transient one, or pauses it when there is
  // none. A refusal is owed.
  [[nodiscard]] core::Result<void> restoreCanonicalAudio();
  // The body of the helper thread that retries an owed change; it exists only while one is owed.
  void retryTransportDebt(std::stop_token stop);
  void notifyCompletion();

  std::unique_ptr<ProjectDocument> document_;
  AuthoringRuntimeConfig config_;
  VoicebankSession voicebanks_;
  AuthoringRenderCoordinator renderer_;
  AuthoringRenderCoordinator seamPreviewRenderer_;
  AuthoringRenderCoordinator performanceAuditionRenderer_;
  TransportController transport_;
  domain::TrackId selectedTrack_{};
  domain::RegionId selectedRegion_{};
  TechnicalEditController technicalEdits_;
  std::uint32_t previewSampleRate_{48000U};
  rendering::RenderQuality renderQuality_{rendering::RenderQuality::Preview};
  // Absent means the document's own tempo map is authoritative for rendering.
  std::optional<time::TempoMap> tempoMapOverride_;
  mutable std::mutex callbackMutex_;
  std::function<void()> completionCallback_;
  mutable std::mutex technicalViewMutex_;
  std::vector<TechnicalUnitView> lastTechnicalUnits_;
  mutable std::mutex previewMutex_;
  std::condition_variable_any previewCondition_;
  std::optional<PreviewRequest> pendingPreview_;
  std::chrono::steady_clock::time_point previewDeadline_{};
  std::uint64_t previewGeneration_{0U};
  std::jthread previewWorker_;
  std::atomic<bool> seamPreviewActive_{false};
  std::atomic<bool> seamPreviewReady_{false};
  mutable std::mutex performanceAuditionMutex_;
  std::atomic<bool> performanceAuditionActive_{false};
  std::atomic<bool> performanceAuditionReady_{false};
  std::shared_ptr<const PublishedProjectAudio> retainedAcceptedAudition_;
  bool initialized_{false};
  mutable std::mutex diagnosticsMutex_;
  struct RecordedDiagnostic final {
    Diagnostic value;
    // The outcome of a render attempt: superseded by a newer attempt, cleared by a success.
    bool attemptOutcome{false};
  };
  std::vector<RecordedDiagnostic> diagnostics_;
  // Derived from the last render request that was composed; see PreviewAssessment.
  bool bankUnavailable_{false};
  // What diagnostics() says of transportDebt_. Written with it, under performanceAuditionMutex_.
  std::optional<Diagnostic> transportDebtDiagnostic_;
  // Guarded by performanceAuditionMutex_.
  TransportDebt transportDebt_{TransportDebt::None};
  // Guarded by performanceAuditionMutex_. True while the canonical audio the transport should hold
  // is behind a seam preview: the preview's audio is on the transport, or a render that finished
  // while the preview was only asked for was left unpublished for it. The preview's end hands the
  // canonical audio over. A preview that fails has no end of its own, so it hands the audio over
  // then, and only when this is set: a preview that changed nothing on the transport costs it
  // nothing, not a republication and not a refusal that would be reported as playback being behind.
  bool canonicalBehindSeamPreview_{false};
  // Counts the changes to transportDebt_, so that a retry can tell that the debt it was working on
  // has been replaced.
  std::uint64_t transportDebtSerial_{0U};
  bool debtRetryActive_{false};
  // Set by shutdown(): no retry thread is started after it.
  bool debtRetryRetired_{false};
  std::condition_variable_any transportDebtChanged_;
  std::jthread debtRetry_;
};

}  // namespace seam::authoring
