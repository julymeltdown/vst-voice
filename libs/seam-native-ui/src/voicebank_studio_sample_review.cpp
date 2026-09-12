#include "seam/native_ui/voicebank_studio.hpp"
#include "voicebank_studio_production_support.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/platform/file_dialog.hpp"

#include <algorithm>
#include <chrono>
#include <exception>

namespace seam::native_ui {
namespace {
namespace production = voicebank_production;
constexpr std::uint64_t kMaximumPreviewBytes = 256ULL * 1024ULL * 1024ULL;

core::Result<void> cancelled(std::stop_token stop) {
  return stop.stop_requested() ? core::failure(core::ErrorCode::Conflict, "Sample review work cancelled") : core::success();
}

std::vector<std::string> inspectionDetails(const VoicebankStudioController::SampleReviewInspection& inspection,
    const production::VoicebankProductionProject& project) {
  const auto& packet = inspection.packet;
  const auto& binding = packet.units.front();
  const auto& unit = packet.manifest.units.front();
  std::vector<std::string> result{
      "UNIT " + unit.id + " / " + std::string(voicebank::unitKindName(unit.kind)) + " / STYLE " + unit.style,
      "TAKE " + binding.takeId,
      "AUDIO SHA256 " + binding.audioSha256,
      "AUDIO " + std::to_string(binding.sampleRate) + " HZ / " + std::to_string(binding.frameCount) + " FRAMES",
      "PEAK " + std::to_string(binding.peak) + " / RMS " + std::to_string(binding.rms),
      "ORIGIN OPERATOR " + binding.originOperatorId + " / GENERATION " + std::to_string(binding.originGeneration),
      "ORIGIN JOURNAL SHA256 " + binding.originJournalSha256,
      "PROJECT " + packet.projectId + " / CAPTURED GENERATION " + std::to_string(packet.sourceGeneration),
      "REVIEW PACKET SHA256 " + packet.packetSha256,
      "ROOT MIDI " + std::to_string(unit.rootMidi) + " / RENDERER " + std::string(voicebank::rendererHintName(unit.renderer)),
      "GAIN DB " + std::to_string(unit.gainDb) + " / PRIORITY " + std::to_string(unit.priority)};
  std::string phones = "PHONES";
  for (const auto& phone : unit.phones) phones += " " + phone;
  result.push_back(std::move(phones));
  for (const auto& source : project.sourceBindings) if (source.takeId == binding.takeId) {
    result.push_back("SOURCE BINDING " + source.id + " / " + production::toString(source.strategy.kind));
    result.push_back("SOURCE IMPORTER " + source.importerId + " / " + source.importedAtUtc);
    result.push_back("SOURCE STRATEGY " + source.strategy.id + " / " + source.strategy.evidenceState);
    result.push_back("LICENSE " + source.strategy.licenseLocator);
    result.push_back("LICENSE SHA256 " + source.strategy.licenseSha256);
    result.push_back("RETAINED LICENSE " + source.licenseSnapshotPath);
    result.push_back("RIGHTS " + production::toString(source.strategy.rights) + " / COVERAGE " +
        production::toString(source.strategy.coverage) + " / LISTENING " + production::toString(source.strategy.listening));
    const auto& permissions = source.strategy.permissions;
    result.push_back("SOURCE PERMISSIONS: USE " + std::string(permissions.sourceUse ? "YES" : "NO") +
        " / TRANSFORM " + (permissions.transformation ? "YES" : "NO") + " / BANK REDISTRIBUTION " +
        (permissions.singingBankRedistribution ? "YES" : "NO") + " / COMMERCIAL RENDERS " + (permissions.commercialRenders ? "YES" : "NO"));
  }
  const auto marker = [&](const char* name, std::optional<time::SampleFrame> frame) {
    result.push_back(std::string("MARKER ") + name + " = " + (frame ? std::to_string(*frame) : "UNSET"));
  };
  marker("OFFSET", unit.markers.audioOffset); marker("CONSONANT END", unit.markers.consonantEnd);
  marker("VOWEL ONSET", unit.markers.vowelOnset); marker("STABLE START", unit.markers.stableStart);
  marker("LOOP START", unit.markers.loopStart); marker("LOOP END", unit.markers.loopEnd);
  marker("RELEASE START", unit.markers.releaseStart); marker("AUDIO END", unit.markers.audioEnd);
  result.push_back("PITCH MARKS " + std::to_string(unit.pitchMarks.size()) + " (FRAMES / CONFIDENCE / LOCK)");
  for (const auto& pitch : unit.pitchMarks) result.push_back("PITCH " + std::to_string(pitch.frame) + " / " +
      std::to_string(pitch.confidence) + (pitch.locked ? " / LOCKED" : " / UNLOCKED"));
  result.push_back("Acceptance records this exact material only; it is not a signed release or Beta qualification.");
  return result;
}
} // namespace

void VoicebankStudioController::invalidateSampleReview() noexcept {
  ++sampleReviewSelectionRevision_;
  sampleReview_.reset();
  sampleReviewerId_.clear();
  sourceQualityInspection_.reset();
  sourceRegistrationInspection_.reset();
  // Durable receipts and publication identities remain visible after selection
  // changes. They describe completed work, never a reusable approval token.
}

core::Result<VoicebankStudioController::SampleReviewContext>
VoicebankStudioController::captureSampleReviewContext() const {
  if (!productionProject_ || !productionRepository_)
    return core::failure<SampleReviewContext>(core::ErrorCode::InvalidState,
        "Open a matching production project before sample review");
  const auto manifest = manifest_.units.empty() ? core::Result<std::string>{std::string{"seam-studio-no-manifest"}} : codec_.encode(manifest_);
  if (!manifest) return core::Result<SampleReviewContext>{manifest.error()};
  return SampleReviewContext{productionSessionEpoch_, productionProject_->lastDurableGeneration,
      sampleReviewSelectionRevision_, selectedIndex_, core::sha256Hex(production::encodeProductionProject(*productionProject_)),
      core::sha256Hex(manifest.value()), selectedUnit() ? selectedUnit()->id : std::string{}};
}

core::Result<void> VoicebankStudioController::validateSampleReviewContext(const SampleReviewContext& context) const {
  const auto current = captureSampleReviewContext();
  if (!current || current.value() != context) return core::failure(core::ErrorCode::Conflict,
      "Sample review context changed: reopen the selected unit review after project, manifest or selection changes");
  return core::success();
}

core::Result<void> VoicebankStudioController::beginSelectedSampleReview() {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Finish current production work first");
  if (!selectedUnit() || !selectedProductionAssignment()) return core::failure(core::ErrorCode::InvalidState,
      "Open the editable manifest and select a matching production unit before capture");
  const auto context = captureSampleReviewContext();
  if (!context) return core::Result<void>{context.error()};
  auto selectedManifest = manifest_;
  selectedManifest.units = {*selectedUnit()};
  sampleReview_.reset(); sampleReviewerId_.clear();
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token();
  statusBeforeImport_ = status_;
  try {
    sampleReviewWork_ = std::async(std::launch::async,
        [root = productionWorkspaceRoot_, project = *productionProject_, manifest = std::move(selectedManifest),
         context = context.value(), stop]() -> core::Result<SampleReviewWorkResult> {
      auto packet = production::prepareSampleCandidateReview(root, project, manifest, stop);
      if (!packet) return core::Result<SampleReviewWorkResult>{packet.error()};
      if (packet.value().units.size() != 1U || packet.value().manifest.units.size() != 1U)
        return core::failure<SampleReviewWorkResult>(core::ErrorCode::Internal, "Selected review did not resolve exactly one unit");
      SampleReviewInspection inspection{.context = context, .packet = std::move(packet.value())};
      const auto& binding = inspection.packet.units.front();
      const auto asset = std::find_if(project.assets.begin(), project.assets.end(),
          [&](const auto& value) { return value.sha256 == binding.audioSha256; });
      if (asset == project.assets.end()) return core::failure<SampleReviewWorkResult>(core::ErrorCode::NotFound, "Reviewed audio asset is missing");
      // Decode exactly the bytes whose digest was presented; a second pathname
      // read could otherwise audition different material after replacement.
      auto bytes = core::readFileBytesLimited(production::ProductionProjectRepository{root}.assetPath(*asset), kMaximumPreviewBytes);
      if (!bytes) return core::Result<SampleReviewWorkResult>{bytes.error()};
      if (core::sha256Hex(bytes.value()) != binding.audioSha256)
        return core::failure<SampleReviewWorkResult>(core::ErrorCode::Conflict, "Review audio changed before preview decode");
      const auto maximumFrames = std::min<std::uint64_t>(binding.frameCount,32ULL*1024ULL*1024ULL);
      auto audio = voicebank::readWav(bytes.value(), binding.audioSha256,
          {.maximumFrames=maximumFrames,.maximumChannels=1U,.maximumDecodedSamples=maximumFrames},stop);
      if (!audio) return core::Result<SampleReviewWorkResult>{audio.error()};
      if (audio.value().channels != 1U || audio.value().sampleRate != binding.sampleRate || audio.value().frameCount() != binding.frameCount)
        return core::failure<SampleReviewWorkResult>(core::ErrorCode::Conflict, "Review audio shape does not match its packet");
      const auto statistics = voicebank::analyzeAudio(audio.value().interleaved);
      if (static_cast<double>(statistics.peak) != binding.peak || statistics.rms != binding.rms)
        return core::failure<SampleReviewWorkResult>(core::ErrorCode::Conflict, "Review statistics do not describe the captured audio bytes");
      inspection.audio = std::make_shared<const voicebank::AudioBuffer>(std::move(audio.value()));
      const auto& samples = inspection.audio->interleaved;
      const auto count = std::min<std::size_t>(1024U, samples.size());
      for (std::size_t i = 0U; i < count; ++i) {
        if (const auto check = cancelled(stop); !check) return core::Result<SampleReviewWorkResult>{check.error()};
        const auto [low, high] = std::minmax_element(samples.begin() + static_cast<std::ptrdiff_t>(i * samples.size() / count),
            samples.begin() + static_cast<std::ptrdiff_t>((i + 1U) * samples.size() / count));
        inspection.peaks.emplace_back(*low, *high);
      }
      for (const auto& actor : project.operators)
        if (actor.role == "REVIEWER") inspection.reviewers.push_back(actor.operatorId);
      inspection.details = inspectionDetails(inspection, project);
      if (const auto check = cancelled(stop); !check) return core::Result<SampleReviewWorkResult>{check.error()};
      return SampleReviewWorkResult{.context = context, .inspection = std::move(inspection)};
    });
  } catch (const std::exception& error) {
    return core::failure(core::ErrorCode::Internal, "Cannot start sample review capture", error.what());
  }
  sampleReviewStatus_ = status_ = "CAPTURING SELECTED REVIEW / ESC CANCEL / NOT APPROVED";
  return core::success();
}

core::Result<void> VoicebankStudioController::selectSampleReviewer(const SampleReviewContext& context, std::string reviewerId) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Sample review work is busy");
  const auto current = validateSampleReviewContext(context); if (!current) return current;
  if (!sampleReview_ || sampleReview_->context != context ||
      std::find(sampleReview_->reviewers.begin(), sampleReview_->reviewers.end(), reviewerId) == sampleReview_->reviewers.end())
    return core::failure(core::ErrorCode::InvalidArgument, "Explicitly choose a registered reviewer from this captured review");
  sampleReviewerId_ = std::move(reviewerId);
  return core::success();
}

core::Result<void> VoicebankStudioController::beginSampleReviewDecision(const SampleReviewContext& context,
    std::string reviewerId, production::SampleCandidateReviewDecision decision, std::string reviewedAtUtc) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Sample review work is busy");
  const auto current = validateSampleReviewContext(context); if (!current) return current;
  if (!sampleReview_ || sampleReview_->context != context || reviewerId.empty() || sampleReviewerId_ != reviewerId)
    return core::failure(core::ErrorCode::Conflict, "The explicit reviewer or captured unit changed before confirmation");
  if (reviewedAtUtc.empty()) reviewedAtUtc = voicebank_studio_internal::currentUtcTimestamp();
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token(); statusBeforeImport_ = status_;
  try {
    sampleReviewWork_ = std::async(std::launch::async,
        [root = productionWorkspaceRoot_, project = *productionProject_, packet = sampleReview_->packet,
         context, reviewerId = std::move(reviewerId), reviewedAtUtc = std::move(reviewedAtUtc), decision, stop]() mutable
            -> core::Result<SampleReviewWorkResult> {
      auto receipt = production::commitSampleCandidateReview(root, project, packet, reviewerId, reviewedAtUtc, decision, {}, stop);
      if (!receipt) return core::Result<SampleReviewWorkResult>{receipt.error()};
      // A committed value must be collected even if cancellation arrived late.
      return SampleReviewWorkResult{.context = context, .committedProject = std::move(project), .receipt = std::move(receipt.value())};
    });
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal, "Cannot start sample review decision", error.what()); }
  sampleReviewStatus_ = status_ = "SAVING EXPLICIT REVIEW DECISION / ESC REQUEST CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::beginSampleCandidatePublication(const SampleReviewContext& context,
    std::filesystem::path destination) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Sample review work is busy");
  const auto current = validateSampleReviewContext(context); if (!current) return current;
  if (!selectedUnit()) return core::failure(core::ErrorCode::InvalidState, "Open the complete edited manifest before publication");
  if (destination.empty()) return core::failure(core::ErrorCode::InvalidArgument, "Choose a new engineering candidate directory");
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token(); statusBeforeImport_ = status_;
  try {
    sampleReviewWork_ = std::async(std::launch::async,
        [root = productionWorkspaceRoot_, project = *productionProject_, manifest = manifest_, context,
         destination = std::move(destination), stop]() -> core::Result<SampleReviewWorkResult> {
      const auto request = production::resolveReviewedSampleCandidate(root, project, manifest, stop);
      if (!request) return core::Result<SampleReviewWorkResult>{request.error()};
      auto published = production::publishSampleCandidate(root, project, request.value(), destination, {}, stop);
      if (!published) return core::Result<SampleReviewWorkResult>{published.error()};
      return SampleReviewWorkResult{.context = context, .published = std::move(published.value())};
    });
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal, "Cannot start candidate publication", error.what()); }
  sampleReviewStatus_ = status_ = "PUBLISHING ENGINEERING CANDIDATE / NOT A SIGNED RELEASE";
  return core::success();
}

void VoicebankStudioController::adoptLoadedSampleUnit(SampleReviewWorkResult::LoadedUnit loaded) {
  manifest_ = std::move(loaded.manifest); audio_ = std::move(loaded.audio); microscope_ = std::move(loaded.microscope);
  manifestPath_ = std::move(loaded.manifestPath); root_ = std::move(loaded.root);
  if (selectedIndex_ != loaded.index) generationScoreSelection_.reset();
  selectedIndex_ = loaded.index; dirty_ = loaded.dirty;
  sampleAudioBindings_ = std::move(loaded.audioBindings);
  pinnedAudioUnitId_ = selectedUnit() ? selectedUnit()->id : std::string{};
  pinnedAudioPath_ = selectedAudioPath();
  static_cast<void>(relayoutSelected());
  refreshCandidateMarkerPreview(); takeInspection_.reset();
}

core::Result<void> VoicebankStudioController::pollSampleReviewWork() {
  if (!sampleReviewWork_.valid() || sampleReviewWork_.wait_for(std::chrono::seconds{0}) != std::future_status::ready) return core::success();
  try {
    auto result = sampleReviewWork_.get();
    if (!result) { sampleReviewStatus_ = result.error().message; status_ = statusBeforeImport_; return core::Result<void>{result.error()}; }
    auto& value = result.value();
    const auto current = validateSampleReviewContext(value.context);
    if (value.sourceRegistrationReceipt) {
      sourceRegistrationReceipt_ = std::move(value.sourceRegistrationReceipt);
      if (current) { productionProject_ = std::move(value.committedProject); refreshCandidateMarkerPreview(); }
      invalidateSampleReview();
      sampleReviewStatus_ = "SOURCE REGISTERED / PRODUCER DECLARATION / MUSICAL QUALITY NOT ASSESSED";
      if (!current) sampleReviewStatus_ += " / CONTEXT CHANGED: REOPEN PRODUCER";
      if (!sourceRegistrationReceipt_->durabilityConfirmed) sampleReviewStatus_ += " / " + sourceRegistrationReceipt_->diagnostic;
      status_ = sampleReviewStatus_;
      return core::success();
    }
    if (value.sourceQualityReceipt) {
      sourceQualityReceipt_ = std::move(value.sourceQualityReceipt);
      if (current) { productionProject_ = std::move(value.committedProject); refreshCandidateMarkerPreview(); }
      invalidateSampleReview();
      sampleReviewStatus_ = "SOURCE QUALITY RECORDED / RIGHTS UNCHANGED / UNIT REVIEW REQUIRED";
      if (!current) sampleReviewStatus_ += " / CONTEXT CHANGED: REOPEN PRODUCER";
      if (!sourceQualityReceipt_->durabilityConfirmed) sampleReviewStatus_ += " / " + sourceQualityReceipt_->diagnostic;
      status_ = sampleReviewStatus_;
      return core::success();
    }
    if (value.createdDraft) {
      createdSampleManifestDraft_ = std::move(value.createdDraft);
      sampleManifestDraftLoadDiagnostic_ = std::move(value.draftLoadDiagnostic);
      const bool canOpen = current && !dirty_ && !proceduralImportStop_.stop_requested() && value.loadedUnit.has_value();
      if (canOpen) adoptLoadedSampleUnit(std::move(*value.loadedUnit));
      else if (!current || dirty_) sampleManifestDraftLoadDiagnostic_ = "Draft committed, not opened: current Studio context or unsaved edits changed. Existing work was preserved.";
      else if (proceduralImportStop_.stop_requested()) sampleManifestDraftLoadDiagnostic_ = "Draft committed, not opened: cancellation arrived after commit. The receipt and directory are retained.";
      sampleReviewStatus_ = "DRAFT COMMITTED / " + std::string{canOpen ? "OPENED" : "NOT OPENED"} + " / ESTIMATED, NOT APPROVED";
      if (!createdSampleManifestDraft_->durabilityConfirmed) sampleReviewStatus_ += " / DURABILITY UNCONFIRMED: INSPECT BEFORE RETRY";
      if (!createdSampleManifestDraft_->missingAssignments.empty()) sampleReviewStatus_ += " / MISSING " + std::to_string(createdSampleManifestDraft_->missingAssignments.size()) + " ASSIGNMENTS";
      status_ = sampleReviewStatus_;
      return core::success();
    }
    if (value.receipt) {
      sampleReviewReceipt_ = std::move(value.receipt);
      if (current) { productionProject_ = std::move(value.committedProject); refreshCandidateMarkerPreview(); }
      else invalidateSampleReview();
      sampleReviewStatus_ = "REVIEW COMMITTED / GENERATION " + std::to_string(sampleReviewReceipt_->committedGeneration);
      if (!sampleReviewReceipt_->reviews.empty()) sampleReviewStatus_ += " / " + sampleReviewReceipt_->reviews.front().result;
      if (!current) sampleReviewStatus_ += " / LOCAL CONTEXT CHANGED: REOPEN PRODUCER";
      if (!sampleReviewReceipt_->durabilityConfirmed) sampleReviewStatus_ += " / DURABILITY UNCONFIRMED: " + sampleReviewReceipt_->diagnostic;
      status_ = sampleReviewStatus_;
      return core::success();
    }
    if (value.published) {
      publishedSampleCandidate_ = std::move(value.published);
      sampleReviewStatus_ = "ENGINEERING CANDIDATE COMMITTED / NOT SIGNED OR INSTALLED";
      if (!current) sampleReviewStatus_ += " / CAPTURED CONTEXT, NOT CURRENT EDITS";
      if (!publishedSampleCandidate_->durabilityConfirmed) sampleReviewStatus_ += " / DURABILITY UNCONFIRMED: " + publishedSampleCandidate_->diagnostic;
      status_ = sampleReviewStatus_;
      return core::success();
    }
    if (!current || proceduralImportStop_.stop_requested()) {
      invalidateSampleReview(); sampleReviewStatus_ = "REVIEW CAPTURE DISCARDED / CANCELLED OR STALE"; status_ = statusBeforeImport_;
      return current ? core::failure(core::ErrorCode::Conflict, sampleReviewStatus_) : current;
    }
    if (value.loadedUnit) {
      adoptLoadedSampleUnit(std::move(*value.loadedUnit));
      sampleReviewStatus_ = status_ = "EDITABLE UNIT LOADED / CAPTURE REVIEW AFTER EDITS";
      return core::success();
    }
    if (value.sourceRegistrationInspection) {
      sourceRegistrationInspection_ = std::move(value.sourceRegistrationInspection);
      sampleReviewStatus_ = status_ = "LICENSE CAPTURED / REGISTER SOURCE WITH EXPLICIT DECLARATIONS / NO APPROVAL";
      return core::success();
    }
    if (value.sourceQualityInspection) {
      sourceQualityInspection_ = std::move(value.sourceQualityInspection);
      sampleReviewStatus_ = status_ = "SOURCE EVIDENCE CAPTURED / INSPECT BEFORE RECORDING / NO APPROVAL";
      return core::success();
    }
    sampleReview_ = std::move(value.inspection); sampleReviewerId_.clear();
    sampleReviewStatus_ = status_ = "REVIEW CAPTURED / CHOOSE REVIEWER / NO APPROVAL CHANGED";
    return core::success();
  } catch (const std::exception& error) {
    status_ = statusBeforeImport_; sampleReviewStatus_ = "Sample review worker failed";
    return core::failure(core::ErrorCode::Internal, sampleReviewStatus_, error.what());
  }
}

core::Result<void> VoicebankStudioController::beginSampleManifestOpen(const SampleReviewContext& context, std::filesystem::path path) {
  if (proceduralImportBusy() || dirty_) return core::failure(core::ErrorCode::Conflict, "Save edited markers and pitch before replacing the manifest");
  const auto current = validateSampleReviewContext(context); if (!current) return current;
  auto loader = std::make_unique<VoicebankStudioController>();
  loader->sampleAudioBindings_ = sampleAudioBindings_; loader->manifestPath_ = manifestPath_;
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token(); statusBeforeImport_ = status_;
  try {
    sampleReviewWork_ = std::async(std::launch::async,
        [loader = std::move(loader), context, path = std::move(path), width = logicalWidth_, height = logicalHeight_, stop]() -> core::Result<SampleReviewWorkResult> {
      if (const auto check = cancelled(stop); !check) return core::Result<SampleReviewWorkResult>{check.error()};
      auto loaded = loader->prepareSampleManifestLoad(path,width,height,stop);
      if (!loaded) return core::Result<SampleReviewWorkResult>{loaded.error()};
      return SampleReviewWorkResult{.context = context, .loadedUnit = std::move(loaded.value())};
    });
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal, "Cannot start editable manifest loading", error.what()); }
  sampleReviewStatus_ = status_ = "LOADING EDITABLE MANIFEST / ESC CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::beginSampleReviewUnitSelection(std::size_t index) {
  const auto context = captureSampleReviewContext(); if (!context) return core::Result<void>{context.error()};
  if (manifest_.units.empty()) return core::failure(core::ErrorCode::InvalidArgument, "Select a unit in the editable manifest");
  return beginEditableUnitSelection(index);
}

core::Result<void> openStudioSampleManifest(VoicebankStudioController& controller, platform::IFileDialog& dialog) {
  if (controller.proceduralImportBusy() || controller.dirty())
    return core::failure(core::ErrorCode::Conflict, "Finish work and save edited markers/pitch before opening a manifest");
  const auto context = controller.captureSampleReviewContext(); if (!context) return core::Result<void>{context.error()};
  const auto path = dialog.choose({.purpose = platform::FileDialogPurpose::OpenSampleManifest,
      .title = "Open Editable Sample Manifest for This Producer", .initialDirectory = controller.manifestPath().parent_path(),
      .suggestedName = {}, .extensions = {"json"}});
  if (!path) return core::Result<void>{path.error()};
  if (!path.value()) return core::success();
  return controller.beginSampleManifestOpen(context.value(), *path.value());
}

core::Result<void> chooseStudioSampleReviewer(VoicebankStudioController& controller, platform::IFileDialog& dialog) {
  const auto& inspection = controller.sampleReviewInspection();
  if (controller.proceduralImportBusy() || !inspection)
    return core::failure(core::ErrorCode::Conflict, "Capture the selected unit before choosing its reviewer");
  const auto context = inspection->context;
  const auto current = controller.validateSampleReviewContext(context); if (!current) return current;
  // Copy choices: native modal loops may run callbacks that replace inspection.
  const auto reviewers = inspection->reviewers;
  const auto chosen = dialog.chooseSampleReviewer(reviewers);
  if (!chosen) return core::Result<void>{chosen.error()};
  if (!chosen.value()) return core::success();
  return controller.selectSampleReviewer(context, *chosen.value());
}

core::Result<void> confirmStudioSampleReview(VoicebankStudioController& controller, platform::IFileDialog& dialog,
    production::SampleCandidateReviewDecision decision) {
  const auto& inspection = controller.sampleReviewInspection();
  if (controller.proceduralImportBusy() || !inspection || controller.sampleReviewerId().empty())
    return core::failure(core::ErrorCode::Conflict, "Capture, inspect and explicitly select the actual reviewer first");
  const auto context = inspection->context;
  const auto current = controller.validateSampleReviewContext(context); if (!current) return current;
  const auto reviewer = controller.sampleReviewerId();
  const auto& unit = inspection->packet.units.front();
  const auto summary = "Reviewer: " + reviewer + "\nUnit: " + unit.unitId + "\nTake: " + unit.takeId +
      "\nAudio SHA256: " + unit.audioSha256 + "\nSource operator: " + unit.originOperatorId +
      "\nCaptured generation: " + std::to_string(inspection->packet.sourceGeneration) +
      "\n\nRecord a genuine review of the displayed audio, markers, pitch and source evidence. This is not Beta qualification or release signing.";
  const auto confirmed = dialog.confirmSampleReview(summary, decision == production::SampleCandidateReviewDecision::Accept);
  if (!confirmed) return core::Result<void>{confirmed.error()};
  if (!confirmed.value()) return core::success();
  return controller.beginSampleReviewDecision(context, reviewer, decision);
}

core::Result<void> publishStudioSampleCandidate(VoicebankStudioController& controller, platform::IFileDialog& dialog) {
  if (controller.proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Finish production work before publication");
  const auto context = controller.captureSampleReviewContext();
  if (!context) return core::Result<void>{context.error()};
  const auto destination = dialog.choose({.purpose = platform::FileDialogPurpose::PublishSampleCandidate,
      .title = "Create New Engineering Candidate Directory (Not a Signed Release)",
      .initialDirectory = controller.manifestPath().parent_path(), .suggestedName = "sample-engineering-candidate", .extensions = {}});
  if (!destination) return core::Result<void>{destination.error()};
  if (!destination.value()) return core::success();
  return controller.beginSampleCandidatePublication(context.value(), *destination.value());
}
} // namespace seam::native_ui
