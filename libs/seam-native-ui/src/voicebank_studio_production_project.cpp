#include "seam/native_ui/voicebank_studio.hpp"

#include "voicebank_studio_production_support.hpp"

#include "seam/core/sha256.hpp"
#include "seam/authoring/generation_job.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank_production/candidate_markers.hpp"
#include <limits>
#include <chrono>
#include <exception>
#include <cmath>

#include <algorithm>
#include <memory>
#include <utility>

namespace seam::native_ui {
namespace {
std::string productionCommitStatus(std::string successStatus, const voicebank_production::ProductionCommitReceipt& receipt) {
  if (receipt.durabilityConfirmed) return successStatus;
  return "COMMIT DURABILITY UNCONFIRMED / RECOVER / DO NOT REPEAT / GENERATION " +
      std::to_string(receipt.committedGeneration) + " / " + receipt.committedProjectSha256 + " / " + receipt.diagnostic;
}
}

core::Result<void> VoicebankStudioController::toggleCandidatePitchView() {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate work is busy");
  if (!candidatePitch_) return core::failure(core::ErrorCode::InvalidState, "Inspect the gesture pitch before switching its view");
  candidatePitchView_ = !candidatePitchView_;
  return core::success();
}

std::pair<time::SampleFrame, time::SampleFrame> VoicebankStudioController::candidateWaveformView() const noexcept {
  return candidateView_.value_or(std::pair{time::SampleFrame{0}, candidateMarkerPreview_ ? candidateMarkerPreview_->frameCount : time::SampleFrame{0}});
}
const std::vector<std::pair<float, float>>& VoicebankStudioController::candidateWaveformPeaks() const noexcept {
  return candidateView_ ? candidateViewPeaks_ : (candidateWaveform_ ? candidateWaveform_->peaks : candidateViewPeaks_);
}
void VoicebankStudioController::centerCandidateWaveform(time::SampleFrame length, std::optional<time::SampleFrame> centerOverride) {
  const auto total = candidateWaveform_->frameCount;
  if (length >= total) { candidateView_.reset(); candidateViewPeaks_.clear(); return; }
  const auto& marker = candidateMarkerPreview_->markers[selectedCandidateMarker_];
  const auto center = centerOverride.value_or(marker.ownedSpan.start + (marker.ownedSpan.end - marker.ownedSpan.start) / 2);
  const auto start = std::clamp(center - length / 2, time::SampleFrame{0}, total - length);
  std::vector<std::pair<float, float>> peaks;
  const auto count = std::min<std::size_t>(1024U, static_cast<std::size_t>(length));
  peaks.reserve(count);
  const auto& samples = candidateWaveform_->audio->interleaved;
  for (std::size_t index = 0U; index < count; ++index) {
    const auto first = static_cast<std::size_t>(start) + index * static_cast<std::size_t>(length) / count;
    const auto last = static_cast<std::size_t>(start) + (index + 1U) * static_cast<std::size_t>(length) / count;
    const auto bounds = std::minmax_element(samples.begin() + static_cast<std::ptrdiff_t>(first), samples.begin() + static_cast<std::ptrdiff_t>(last));
    peaks.emplace_back(*bounds.first, *bounds.second);
  }
  candidateView_ = std::pair{start, start + length};
  candidateViewPeaks_ = std::move(peaks);
}
core::Result<void> VoicebankStudioController::zoomCandidateWaveform(bool zoomIn) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate work is busy");
  if (!candidateWaveform_ || !candidateWaveform_->audio || !candidateMarkerPreview_) return core::failure(
      core::ErrorCode::InvalidState, "Load the raw waveform before zooming");
  const auto [first, last] = candidateWaveformView();
  const auto total = candidateWaveform_->frameCount;
  const auto minimum = std::min<time::SampleFrame>(32, total);
  centerCandidateWaveform(zoomIn ? std::max(minimum, (last - first) / 2) : std::min(total, (last - first) * 2));
  return core::success();
}
core::Result<void> VoicebankStudioController::panCandidateWaveform(bool right) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate work is busy");
  if (!candidateWaveform_ || !candidateMarkerPreview_) return core::failure(core::ErrorCode::InvalidState, "Load the raw waveform before panning");
  const auto [first, last] = candidateWaveformView();
  const auto length = last - first;
  centerCandidateWaveform(length, first + length / 2 + (right ? length / 2 : -length / 2));
  return core::success();
}

ui::Rect VoicebankStudioController::candidateWaveformBounds(double width, double height) noexcept {
  const auto rowTop = std::min(476.0, height - 64.0);
  return {294.0, 388.0, std::max(0.0, width - 574.0), std::max(0.0, std::min(68.0, rowTop - 408.0))};
}

core::Result<bool> VoicebankStudioController::beginCandidateMarkerDrag(ui::Point point) {
  if (proceduralImportBusy()) return core::failure<bool>(core::ErrorCode::Conflict, "Candidate work is busy");
  const auto bounds = candidateWaveformBounds(logicalWidth_, logicalHeight_);
  if (!std::isfinite(point.x) || !std::isfinite(point.y) || !manifest_.units.empty() ||
      !candidateWaveform_ || !candidateMarkerPreview_ || bounds.width <= 0.0 || bounds.height <= 0.0 ||
      point.y < bounds.y || point.y >= bounds.bottom() || point.x < bounds.x - 6.0 || point.x > bounds.right() + 6.0) return false;
  const auto& marker = candidateMarkerPreview_->markers[selectedCandidateMarker_];
  const auto [viewStart, viewEnd] = candidateWaveformView();
  const auto scale = bounds.width / static_cast<double>(viewEnd - viewStart);
  const auto startDistance = marker.ownedSpan.start < viewStart || marker.ownedSpan.start > viewEnd ? 1e9 :
      std::abs(point.x - bounds.x - static_cast<double>(marker.ownedSpan.start - viewStart) * scale);
  const auto endDistance = marker.ownedSpan.end < viewStart || marker.ownedSpan.end > viewEnd ? 1e9 :
      std::abs(point.x - bounds.x - static_cast<double>(marker.ownedSpan.end - viewStart) * scale);
  if (std::min(startDistance, endDistance) > 6.0) return false;
  candidateDrag_ = CandidateMarkerDrag{selectedCandidateMarker_, marker.ownedSpan.start, marker.ownedSpan.end,
      endDistance <= 6.0 && (startDistance > 6.0 || point.y >= bounds.y + bounds.height / 2.0), status_};
  candidatePitch_.reset(); candidatePitchError_.clear();
  status_ = "DRAG PREVIEW / RELEASE TO SAVE";
  return true;
}

core::Result<void> VoicebankStudioController::updateCandidateMarkerDrag(ui::Point point) {
  if (!candidateDrag_ || !candidateMarkerPreview_) return core::failure(core::ErrorCode::InvalidState, "No candidate drag is active");
  if (!std::isfinite(point.x) || !std::isfinite(point.y)) return core::failure(core::ErrorCode::InvalidArgument, "Candidate drag position is invalid");
  const auto bounds = candidateWaveformBounds(logicalWidth_, logicalHeight_);
  if (bounds.width <= 0.0) return core::failure(core::ErrorCode::InvalidState, "Candidate waveform has no editable width");
  auto& markers = candidateMarkerPreview_->markers;
  auto& marker = markers[candidateDrag_->index];
  const auto [viewStart, viewEnd] = candidateWaveformView();
  const auto frame = viewStart + static_cast<time::SampleFrame>(std::llround(std::clamp((point.x - bounds.x) / bounds.width, 0.0, 1.0) *
      static_cast<double>(viewEnd - viewStart)));
  if (candidateDrag_->endHandle) {
    const auto next = candidateDrag_->index + 1U;
    marker.ownedSpan.end = std::clamp(frame, marker.ownedSpan.start + 1,
        next < markers.size() ? markers[next].ownedSpan.start : candidateMarkerPreview_->frameCount);
  } else {
    marker.ownedSpan.start = std::clamp(frame, candidateDrag_->index == 0U ? time::SampleFrame{0} : markers[candidateDrag_->index - 1U].ownedSpan.end,
        marker.ownedSpan.end - 1);
  }
  return core::success();
}

void VoicebankStudioController::cancelCandidateMarkerDrag() noexcept {
  if (!candidateDrag_) return;
  if (candidateMarkerPreview_ && candidateDrag_->index < candidateMarkerPreview_->markers.size())
    candidateMarkerPreview_->markers[candidateDrag_->index].ownedSpan = {candidateDrag_->start, candidateDrag_->end};
  status_ = std::move(candidateDrag_->previousStatus);
  candidateDrag_.reset();
}

core::Result<void> VoicebankStudioController::finishCandidateMarkerDrag() {
  if (!candidateDrag_ || !candidateMarkerPreview_) return core::failure(core::ErrorCode::InvalidState, "No candidate drag is active");
  const auto span = candidateMarkerPreview_->markers[candidateDrag_->index].ownedSpan;
  cancelCandidateMarkerDrag();
  return editSelectedCandidateMarker(span.start, span.end);
}

core::Result<void> VoicebankStudioController::selectCandidateMarker(std::size_t index) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate work is busy");
  if (!candidateMarkerPreview_ || index >= candidateMarkerPreview_->markers.size()) return core::failure(
      core::ErrorCode::InvalidArgument, "Candidate gesture index is outside the take");
  selectedCandidateMarker_ = index;
  candidatePitch_.reset(); candidatePitchError_.clear();
  if (candidateView_ && candidateWaveform_) centerCandidateWaveform(candidateView_->second - candidateView_->first);
  return core::success();
}

core::Result<void> VoicebankStudioController::moveCandidateMarker(std::int32_t delta) {
  if (!candidateMarkerPreview_ || candidateMarkerPreview_->markers.empty()) return core::failure(
      core::ErrorCode::InvalidState, "No candidate gestures are available");
  const auto next = std::clamp(static_cast<std::int64_t>(selectedCandidateMarker_) + delta, std::int64_t{0},
      static_cast<std::int64_t>(candidateMarkerPreview_->markers.size() - 1U));
  return selectCandidateMarker(static_cast<std::size_t>(next));
}

std::pair<std::size_t, std::size_t> VoicebankStudioController::candidateMarkerWindow(std::size_t visibleRows) const noexcept {
  if (!candidateMarkerPreview_ || visibleRows == 0U) return {0U, 0U};
  const auto count = std::min(visibleRows, candidateMarkerPreview_->markers.size());
  const auto first = selectedCandidateMarker_ >= count ? selectedCandidateMarker_ - count + 1U : 0U;
  return {first, count};
}

void VoicebankStudioController::refreshCandidateMarkerPreview(bool preserveHistory) {
  invalidateSampleReview();
  candidatePitch_.reset(); candidatePitchError_.clear();
  cancelCandidateMarkerDrag();
  if (!preserveHistory) { candidateUndo_.clear(); candidateRedo_.clear(); candidateView_.reset(); candidateViewPeaks_.clear(); }
  selectedCandidateMarker_ = 0U;
  candidateWaveform_.reset();
  candidateWaveformError_.clear();
  candidateMarkerPreview_.reset();
  candidateMarkerError_.clear();
  candidateMarkerRevisionId_.clear();
  candidateMarkersEdited_ = false;
  const auto* assignment = selectedProductionAssignment();
  if (!productionProject_ || !assignment || assignment->takeId.empty()) return;
  const auto lineage = std::find_if(productionProject_->metadataRevisions.begin(), productionProject_->metadataRevisions.end(),
      [&](const auto& revision) { return revision.takeId == assignment->takeId && revision.kind == "procedural-lineage"; });
  if (lineage == productionProject_->metadataRevisions.end()) return;
  auto resolved = voicebank_production::resolveCandidateMarkers(*productionProject_, assignment->takeId);
  if (!resolved) { candidateMarkerError_ = resolved.error().message; return; }
  candidateMarkerRevisionId_ = std::move(resolved.value().revisionId);
  candidateMarkersEdited_ = candidateMarkerRevisionId_ != lineage->revisionId;
  candidateMarkerPreview_ = std::move(resolved.value().candidate);
}

core::Result<void> VoicebankStudioController::editSelectedCandidateMarker(
    time::SampleFrame start, time::SampleFrame end, std::string occurredAtUtc) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate work is busy");
  if (!candidateMarkerPreview_ || selectedCandidateMarker_ >= candidateMarkerPreview_->markers.size())
    return core::failure(core::ErrorCode::InvalidState, "Select a candidate gesture before editing");
  const auto before = candidateMarkerPreview_->markers[selectedCandidateMarker_].ownedSpan;
  if (before.start == start && before.end == end) return core::success();
  auto history = candidateUndo_;
  if (history.size() == 128U) history.erase(history.begin());
  history.push_back({selectedCandidateMarker_, before.start, before.end, start, end});
  auto saved = commitCandidateMarkerBounds(start, end, std::move(occurredAtUtc));
  if (!saved) return saved;
  candidateUndo_ = std::move(history);
  candidateRedo_.clear();
  return core::success();
}

core::Result<void> VoicebankStudioController::undoCandidateMarkerEdit(std::string occurredAtUtc) {
  return replayCandidateMarkerEdit(false, std::move(occurredAtUtc));
}
core::Result<void> VoicebankStudioController::redoCandidateMarkerEdit(std::string occurredAtUtc) {
  return replayCandidateMarkerEdit(true, std::move(occurredAtUtc));
}
core::Result<void> VoicebankStudioController::replayCandidateMarkerEdit(bool redo, std::string occurredAtUtc) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate work is busy");
  auto& source = redo ? candidateRedo_ : candidateUndo_;
  auto& destination = redo ? candidateUndo_ : candidateRedo_;
  if (source.empty() || !candidateMarkerPreview_) return core::failure(core::ErrorCode::InvalidState, "No candidate boundary history is available");
  const auto entry = source.back();
  if (entry.index >= candidateMarkerPreview_->markers.size()) return core::failure(core::ErrorCode::Conflict, "Candidate boundary history is stale");
  const auto current = candidateMarkerPreview_->markers[entry.index].ownedSpan;
  if (current.start != (redo ? entry.beforeStart : entry.afterStart) || current.end != (redo ? entry.beforeEnd : entry.afterEnd))
    return core::failure(core::ErrorCode::Conflict, "Candidate boundary history differs from the current gesture");
  auto nextDestination = destination;
  nextDestination.push_back(entry);
  const auto previousSelection = selectedCandidateMarker_;
  selectedCandidateMarker_ = entry.index;
  const auto saved = commitCandidateMarkerBounds(redo ? entry.afterStart : entry.beforeStart,
      redo ? entry.afterEnd : entry.beforeEnd, std::move(occurredAtUtc));
  if (!saved) { selectedCandidateMarker_ = previousSelection; return saved; }
  source.pop_back();
  destination = std::move(nextDestination);
  if (!status_.starts_with("COMMIT DURABILITY UNCONFIRMED"))
    status_ = redo ? "REDO BOUNDS / MARKER REVIEW" : "UNDO BOUNDS / MARKER REVIEW";
  return core::success();
}

core::Result<void> VoicebankStudioController::commitCandidateMarkerBounds(
    time::SampleFrame start, time::SampleFrame end, std::string occurredAtUtc) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate work is busy");
  const auto* assignment = selectedProductionAssignment();
  if (!productionRepository_ || !productionProject_ || !candidateMarkerPreview_ || !assignment ||
      selectedCandidateMarker_ >= candidateMarkerPreview_->markers.size()) return core::failure(
          core::ErrorCode::InvalidState, "Select a candidate gesture before editing");
  const auto& marker = candidateMarkerPreview_->markers[selectedCandidateMarker_];
  if (marker.ownedSpan.start == start && marker.ownedSpan.end == end) return core::success();
  if (occurredAtUtc.empty()) occurredAtUtc = voicebank_studio_internal::currentUtcTimestamp();
  const auto revisionId = "candidate-marker-" + std::to_string(productionProject_->lastDurableGeneration + 1U);
  auto saved = productionRepository_->recordMetadataRevision(*productionProject_,
      {.revisionId = revisionId, .takeId = assignment->takeId, .rawAssetSha256 = candidateMarkerPreview_->audioSha256,
       .kind = "candidate-marker-edit", .values = {{"previousRevisionId", candidateMarkerRevisionId_},
           {"key", marker.key.toString()}, {"startFrame", std::to_string(start)}, {"endFrame", std::to_string(end)}},
       .operatorId = productionOperatorId_, .performedAtUtc = occurredAtUtc},
      {.action = "marker", .subjectId = revisionId, .operatorId = productionOperatorId_, .occurredAtUtc = occurredAtUtc});
  if (!saved) return core::Result<void>{saved.error()};
  const auto index = selectedCandidateMarker_;
  auto waveform = std::move(candidateWaveform_);
  refreshCandidateMarkerPreview(true);
  candidateWaveform_ = std::move(waveform);
  selectedCandidateMarker_ = index;
  takeInspection_.reset();
  status_ = productionCommitStatus("MANUAL BOUNDS / MARKER REVIEW", saved.value());
  return core::success();
}

VoicebankStudioController::~VoicebankStudioController() {
  cancelProceduralCandidateImport();
  if (workspaceOpen_.valid()) workspaceOpen_.wait();
  if (proceduralImport_.valid()) proceduralImport_.wait();
  if (waveformLoad_.valid()) waveformLoad_.wait();
  if (pitchLoad_.valid()) pitchLoad_.wait();
  if (sampleReviewWork_.valid()) sampleReviewWork_.wait();
  if (editableUnitLoad_.valid()) editableUnitLoad_.wait();
}

void VoicebankStudioController::cancelProceduralCandidateImport() noexcept {
  cancelCandidateMarkerDrag();
  proceduralImportStop_.request_stop();
}

core::Result<void> VoicebankStudioController::finishProceduralCandidateImport() {
  cancelProceduralCandidateImport();
  if (workspaceOpen_.valid()) workspaceOpen_.wait();
  if (proceduralImport_.valid()) proceduralImport_.wait();
  if (waveformLoad_.valid()) waveformLoad_.wait();
  if (pitchLoad_.valid()) pitchLoad_.wait();
  if (sampleReviewWork_.valid()) sampleReviewWork_.wait();
  if (editableUnitLoad_.valid()) editableUnitLoad_.wait();
  return pollProceduralCandidateImport();
}

core::Result<void> VoicebankStudioController::beginProceduralCandidateImport(
    std::filesystem::path metadataPath, std::filesystem::path audioPath,
    std::filesystem::path recipePath, std::string occurredAtUtc) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate import is busy");
  const auto* assignment = selectedProductionAssignment();
  if (!productionProject_ || !productionRepository_ || !assignment) return core::failure(
      core::ErrorCode::InvalidState, "Candidate import requires a selected production inventory row");
  auto worker = std::make_unique<VoicebankStudioController>();
  worker->productionProject_ = *productionProject_;
  worker->productionWorkspaceRoot_ = productionWorkspaceRoot_;
  worker->productionRepository_ = std::make_unique<voicebank_production::ProductionProjectRepository>(productionWorkspaceRoot_);
  worker->productionOperatorId_ = productionOperatorId_;
  worker->selectedIndex_ = static_cast<std::size_t>(assignment - productionProject_->unitAssignments.data());
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token();
  statusBeforeImport_ = status_;
  try {
    proceduralImport_ = std::async(std::launch::async,
        [worker = std::move(worker), metadataPath = std::move(metadataPath), audioPath = std::move(audioPath),
         recipePath = std::move(recipePath), occurredAtUtc = std::move(occurredAtUtc), stop]() mutable
            -> core::Result<ProductionImportResult> {
      const auto imported = worker->importSelectedProceduralCandidate(metadataPath, audioPath, recipePath,
          std::move(occurredAtUtc), stop);
      if (!imported) return core::Result<ProductionImportResult>{imported.error()};
      return ProductionImportResult{std::move(*worker->productionProject_), std::move(worker->status_)};
    });
  } catch (const std::exception& error) {
    return core::failure(core::ErrorCode::Internal, "Unable to start candidate import", error.what());
  }
  status_ = "IMPORTING CANDIDATE / ESC CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::beginGenerationScoreInspection(std::filesystem::path scorePath,
    std::optional<authoring::GenerationRecipeSelection> selectedRecipe) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Production worker is busy");
  if (!productionProject_ || !selectedProductionAssignment())
    return core::failure(core::ErrorCode::InvalidState, "Select a planned producer assignment first");
  auto captured = *productionProject_;
  const auto epoch = productionSessionEpoch_;
  const auto selected = selectedIndex_;
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token();
  statusBeforeImport_ = status_;
  generationScoreSelection_.reset();
  try {
    proceduralImport_ = std::async(std::launch::async,
        [scorePath = std::move(scorePath), captured = std::move(captured), epoch, selected, stop, selectedRecipe = std::move(selectedRecipe)]() mutable
        -> core::Result<ProductionImportResult> {
      std::error_code error;
      const auto path = std::filesystem::absolute(scorePath, error);
      if (error) return core::failure<ProductionImportResult>(core::ErrorCode::InvalidArgument, "Cannot resolve score path");
      if (stop.stop_requested()) return core::failure<ProductionImportResult>(core::ErrorCode::Conflict, "Score inspection cancelled");
      const auto bytes = core::readTextFileLimited(path, 16U * 1024U * 1024U);
      if (!bytes) return core::Result<ProductionImportResult>{bytes.error()};
      const auto score = formats::ProjectJsonCodec{}.decode(bytes.value());
      if (!score) return core::Result<ProductionImportResult>{score.error()};
      GenerationScoreSelection selection{path, core::sha256Hex(bytes.value()), {}, epoch, captured.lastDurableGeneration, selected};
      selection.selectedRecipe = std::move(selectedRecipe);
      if (selection.selectedRecipe) {
        const auto valid = voice_design::decodeVoiceRecipeResource(selection.selectedRecipe->resource, stop);
        if (!valid) return core::Result<ProductionImportResult>{valid.error()};
        if (std::none_of(valid.value().poses.begin(), valid.value().poses.end(), [&](const auto& pose) { return pose.style == selection.selectedRecipe->style; }))
          return core::failure<ProductionImportResult>(core::ErrorCode::InvalidArgument, "Selected draft style is absent from its recipe");
      }
      const auto shortLabel = [](std::string_view text) {
        auto length = std::min<std::size_t>(80U, text.size());
        while (length > 0U && length < text.size() && (static_cast<unsigned char>(text[length]) & 0xc0U) == 0x80U) --length;
        std::string label{text.substr(0U, length)};
        for (auto& character : label) if (static_cast<unsigned char>(character) < 32U || character == 127) character = ' ';
        if (length < text.size()) label += "...";
        return label;
      };
      for (const auto& track : score.value().vocalTracks()) {
        if (!selection.selectedRecipe && !track.proceduralRecipe) continue;
        for (const auto& region : track.regions) {
          if (region.notes.empty()) continue;
          if (selection.regions.size() == 64U)
            return core::failure<ProductionImportResult>(core::ErrorCode::InvalidArgument, "Preparation score exceeds 64 selectable regions");
          selection.regions.push_back({track.id, region.id, shortLabel(track.name) + " / " + shortLabel(region.name) + " [" + region.id.toString() + "]"});
        }
      }
      if (stop.stop_requested()) return core::failure<ProductionImportResult>(core::ErrorCode::Conflict, "Score inspection cancelled");
      if (selection.regions.empty()) return core::failure<ProductionImportResult>(core::ErrorCode::InvalidArgument, "Score has no nonempty procedural regions");
      const auto status = selection.selectedRecipe ? "DESIGNER SNAPSHOT READY / SHIFT-P" : "SCORE READY / SHIFT-P PREPARE";
      return ProductionImportResult{std::move(captured), status, false, std::move(selection)};
    });
  } catch (const std::exception& error) {
    return core::failure(core::ErrorCode::Internal, "Unable to start score inspection", error.what());
  }
  status_ = "READING SCORE / ESC CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::beginGenerationPreparation(
    std::filesystem::path scorePath, domain::TrackId trackId, domain::RegionId regionId,
    std::filesystem::path directory, std::string jobId, std::string expectedScoreSha256,
    std::optional<authoring::GenerationRecipeSelection> selectedRecipe) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Production worker is busy");
  const auto* assignment = selectedProductionAssignment();
  if (!productionProject_ || !productionRepository_ || !assignment || assignment->plannedTakeId.empty())
    return core::failure(core::ErrorCode::InvalidState, "Preparation requires a selected planned producer assignment");
  auto captured = *productionProject_;
  const auto plannedTakeId = assignment->plannedTakeId;
  const auto root = productionWorkspaceRoot_;
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token();
  // Score inspection already retained the underlying producer status.
  // Do not replace it with a transient snapshot-ready prompt.
  if (!generationScoreSelection_) statusBeforeImport_ = status_;
  try {
    proceduralImport_ = std::async(std::launch::async,
        [scorePath = std::move(scorePath), trackId, regionId, directory = std::move(directory), jobId = std::move(jobId),
         captured = std::move(captured), plannedTakeId, root, stop, expectedScoreSha256 = std::move(expectedScoreSha256),
         selectedRecipe = std::move(selectedRecipe)]() mutable -> core::Result<ProductionImportResult> {
      if (stop.stop_requested()) return core::failure<ProductionImportResult>(core::ErrorCode::Conflict, "Generation preparation cancelled");
      voicebank_production::ProductionProjectRepository repository{root};
      const auto durable = repository.recover();
      if (!durable) return core::Result<ProductionImportResult>{durable.error()};
      if (core::sha256Hex(voicebank_production::encodeProductionProject(captured)) !=
          core::sha256Hex(voicebank_production::encodeProductionProject(durable.value())))
        return core::failure<ProductionImportResult>(core::ErrorCode::Conflict, "Producer changed since the Studio workspace was loaded");
      const auto prepared = authoring::prepareGenerationJobFromScore(directory, std::move(jobId), scorePath,
          trackId, regionId, captured, plannedTakeId, stop, expectedScoreSha256, std::move(selectedRecipe));
      if (!prepared) return core::Result<ProductionImportResult>{prepared.error()};
      // Package publication is not interrupted halfway; a late stop must not hide
      // its successfully written reference. Later generation enforces the frozen state.
      return ProductionImportResult{std::move(captured), "JOB PREPARED / NOT GENERATED", false};
    });
  } catch (const std::exception& error) {
    return core::failure(core::ErrorCode::Internal, "Unable to start preparation worker", error.what());
  }
  // The dialog's snapshot remains available until dispatch succeeds. Avoid
  // restoring a now-consumed "snapshot ready" prompt if the worker cancels.
  if (generationScoreSelection_) {
    generationScoreSelection_.reset();
  }
  status_ = "PREPARING JOB / ESC CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::beginPreparedGenerationJob(
    std::filesystem::path directory, std::string manifestSha256, std::string occurredAtUtc) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Production worker is busy");
  const auto* assignment = selectedProductionAssignment();
  if (!productionProject_ || !productionRepository_ || !assignment) return core::failure(
      core::ErrorCode::InvalidState, "Generation requires a selected production assignment");
  if (occurredAtUtc.empty()) occurredAtUtc = voicebank_studio_internal::currentUtcTimestamp();
  if (!voicebank_production::isProductionUtcTimestamp(occurredAtUtc)) return core::failure(core::ErrorCode::InvalidArgument, "Generation collection timestamp is invalid");
  const auto selected = *assignment;
  auto captured = *productionProject_;
  const auto root = productionWorkspaceRoot_;
  const auto operatorId = productionOperatorId_;
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token();
  statusBeforeImport_ = status_;
  try {
    proceduralImport_ = std::async(std::launch::async,
        [directory = std::move(directory), manifestSha256 = std::move(manifestSha256), occurredAtUtc = std::move(occurredAtUtc),
         selected, captured = std::move(captured), root, operatorId, stop]() mutable -> core::Result<ProductionImportResult> {
      const auto cancelled = [] { return core::failure<ProductionImportResult>(core::ErrorCode::Conflict, "Generation cancelled"); };
      if (stop.stop_requested()) return cancelled();
      const auto job = authoring::loadGenerationJob(directory, manifestSha256);
      if (!job) return core::Result<ProductionImportResult>{job.error()};
      const auto& request = job.value().expectation;
      if (request.coverageKey != selected.coverageKey || request.pitchLayer != selected.pitchLayer || request.promptId != selected.promptId)
        return core::failure<ProductionImportResult>(core::ErrorCode::Conflict, "Generation job belongs to another selected assignment");
      voicebank_production::ProductionProjectRepository repository{root};
      const auto durable = repository.recover();
      if (!durable) return core::Result<ProductionImportResult>{durable.error()};
      const auto capturedHash = core::sha256Hex(voicebank_production::encodeProductionProject(captured));
      if (capturedHash != core::sha256Hex(voicebank_production::encodeProductionProject(durable.value())))
        return core::failure<ProductionImportResult>(core::ErrorCode::Conflict, "Producer changed since the Studio workspace was loaded");
      const auto collected = repository.findCollectedGeneration(request);
      if (!collected) return core::Result<ProductionImportResult>{collected.error()};
      if (stop.stop_requested()) return cancelled();
      if (collected.value()) {
        if (collected.value()->generation != captured.lastDurableGeneration)
          return core::failure<ProductionImportResult>(core::ErrorCode::Conflict, "Producer changed during generation recognition");
        return ProductionImportResult{std::move(captured), "GENERATION ALREADY COLLECTED", false};
      }
      if (request.projectStateSha256 != capturedHash || request.takeId != selected.plannedTakeId || request.supersedesTakeId != selected.takeId)
        return core::failure<ProductionImportResult>(core::ErrorCode::Conflict, "Generation request is stale or differs from the planned take");
      const auto output = authoring::runGenerationJob(directory, manifestSha256, stop);
      if (!output) return core::Result<ProductionImportResult>{output.error()};
      const auto& recipe = std::get<synthesis::ProceduralSingerResource>(job.value().snapshot.resource);
      const auto imported = repository.importProceduralCandidate(captured, output.value().metadataPath, output.value().audioPath, recipe,
          {.takeId = request.takeId, .promptId = request.promptId, .coverageKey = request.coverageKey,
           .pitchLayer = request.pitchLayer, .supersedesTakeId = request.supersedesTakeId},
          {.action = request.supersedesTakeId.empty() ? "import-procedural" : "retake", .subjectId = request.takeId,
           .operatorId = operatorId, .occurredAtUtc = occurredAtUtc}, stop, &request);
      if (!imported) return core::Result<ProductionImportResult>{imported.error()};
      return ProductionImportResult{std::move(captured), productionCommitStatus("GENERATED CANDIDATE: MARKER REVIEW", imported.value())};
    });
  } catch (const std::exception& error) {
    return core::failure(core::ErrorCode::Internal, "Unable to start generation worker", error.what());
  }
  status_ = "GENERATING CANDIDATE / ESC CANCEL";
  return core::success();
}

std::optional<VoicebankStudioController::GenerationBatchProgress>
VoicebankStudioController::generationBatchProgress() const noexcept {
  if (!generationBatchProgress_) return std::nullopt;
  const auto value = generationBatchProgress_->load(std::memory_order_relaxed);
  return GenerationBatchProgress{static_cast<GenerationBatchProgress::Phase>(value >> 16U),
      value & 255U, (value >> 8U) & 255U};
}

core::Result<void> VoicebankStudioController::beginGenerationBatchAssembly(
    std::vector<std::filesystem::path> references, std::filesystem::path destination) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Production worker is busy");
  if (!productionProject_ || !productionRepository_)
    return core::failure(core::ErrorCode::InvalidState, "Batch assembly requires a producer workspace");
  if (references.empty() || references.size() > 64U)
    return core::failure(core::ErrorCode::InvalidArgument, "Select 1 to 64 prepared job references");
  auto captured = *productionProject_;
  const auto root = productionWorkspaceRoot_;
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token();
  statusBeforeImport_ = status_;
  try {
    proceduralImport_ = std::async(std::launch::async,
        [references = std::move(references), destination = std::move(destination), captured = std::move(captured), root, stop]() mutable
        -> core::Result<ProductionImportResult> {
      std::vector<authoring::GenerationJobReference> jobs;
      for (const auto& path : references) {
        if (stop.stop_requested()) return core::failure<ProductionImportResult>(core::ErrorCode::Conflict, "Batch assembly cancelled");
        const auto reference = authoring::loadGenerationJobReference(path);
        if (!reference) return core::Result<ProductionImportResult>{reference.error()};
        jobs.push_back(reference.value());
      }
      voicebank_production::ProductionProjectRepository repository{root};
      const auto durable = repository.recover();
      if (!durable) return core::Result<ProductionImportResult>{durable.error()};
      const auto capturedHash = core::sha256Hex(voicebank_production::encodeProductionProject(captured));
      if (capturedHash != core::sha256Hex(voicebank_production::encodeProductionProject(durable.value())))
        return core::failure<ProductionImportResult>(core::ErrorCode::Conflict, "Producer changed since the Studio workspace was loaded");
      const auto saved = authoring::saveGenerationBatch(destination, jobs, {}, stop, capturedHash);
      if (!saved) return core::Result<ProductionImportResult>{saved.error()};
      return ProductionImportResult{std::move(captured), "BATCH PREPARED / NOT GENERATED", false};
    });
  } catch (const std::exception& error) {
    return core::failure(core::ErrorCode::Internal, "Unable to start batch assembly", error.what());
  }
  status_ = "ASSEMBLING BATCH / ESC CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::beginPreparedGenerationBatch(
    std::filesystem::path manifestPath, std::string manifestSha256,
    std::uint64_t maximumFrames, std::string occurredAtUtc) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Production worker is busy");
  if (!productionProject_ || !productionRepository_) return core::failure(
      core::ErrorCode::InvalidState, "Batch generation requires a production workspace");
  if (maximumFrames == 0U || maximumFrames > 64ULL * 32ULL * 1024ULL * 1024ULL)
    return core::failure(core::ErrorCode::InvalidArgument, "Generation batch frame budget is invalid");
  if (occurredAtUtc.empty()) occurredAtUtc = voicebank_studio_internal::currentUtcTimestamp();
  if (!voicebank_production::isProductionUtcTimestamp(occurredAtUtc))
    return core::failure(core::ErrorCode::InvalidArgument, "Generation collection timestamp is invalid");
  auto captured = *productionProject_;
  const auto root = productionWorkspaceRoot_;
  const auto operatorId = productionOperatorId_;
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token();
  statusBeforeImport_ = status_;
  try {
    auto progress = std::make_shared<std::atomic<std::uint32_t>>(0U);
    proceduralImport_ = std::async(std::launch::async,
        [manifestPath = std::move(manifestPath), manifestSha256 = std::move(manifestSha256), maximumFrames,
         occurredAtUtc = std::move(occurredAtUtc), captured = std::move(captured), root, operatorId, stop, progress]() mutable
        -> core::Result<ProductionImportResult> {
      const auto cancelled = [] { return core::failure<ProductionImportResult>(core::ErrorCode::Conflict, "Batch generation cancelled"); };
      if (stop.stop_requested()) return cancelled();
      const auto jobs = authoring::loadGenerationBatch(manifestPath, manifestSha256);
      if (!jobs) return core::Result<ProductionImportResult>{jobs.error()};
      const authoring::GenerationBatchLimits limits{.maximumJobs = 64U, .maximumFrames = maximumFrames};
      auto inputs = authoring::inspectGenerationBatch(jobs.value(), limits, stop);
      if (!inputs) return core::Result<ProductionImportResult>{inputs.error()};
      voicebank_production::ProductionProjectRepository repository{root};
      const auto durable = repository.recover();
      if (!durable) return core::Result<ProductionImportResult>{durable.error()};
      const auto capturedHash = core::sha256Hex(voicebank_production::encodeProductionProject(captured));
      if (capturedHash != core::sha256Hex(voicebank_production::encodeProductionProject(durable.value())))
        return core::failure<ProductionImportResult>(core::ErrorCode::Conflict, "Producer changed since the Studio workspace was loaded");
      std::size_t collectedCount = 0U;
      for (const auto& input : inputs.value()) {
        if (stop.stop_requested()) return cancelled();
        const auto collected = repository.findCollectedGeneration(input.expectation);
        if (!collected) return core::Result<ProductionImportResult>{collected.error()};
        if (collected.value()) {
          if (collected.value()->generation != captured.lastDurableGeneration)
            return core::failure<ProductionImportResult>(core::ErrorCode::Conflict, "Producer changed during batch recognition");
          ++collectedCount;
        }
      }
      if (collectedCount == inputs.value().size())
        return ProductionImportResult{std::move(captured), "GENERATION BATCH ALREADY COLLECTED", false};
      if (collectedCount != 0U)
        return core::failure<ProductionImportResult>(core::ErrorCode::Conflict, "Generation batch is partially collected; explicit reconciliation is required");
      if (inputs.value().front().expectation.projectStateSha256 != capturedHash)
        return core::failure<ProductionImportResult>(core::ErrorCode::Conflict, "Generation batch request is stale");
      const auto total = static_cast<std::uint32_t>(jobs.value().size());
      progress->store((1U << 16U) | (total << 8U), std::memory_order_relaxed);
      const auto outputs = authoring::runGenerationBatch(jobs.value(), limits, stop,
          [progress](std::size_t completed, std::size_t count) {
            progress->store((1U << 16U) | (static_cast<std::uint32_t>(count) << 8U) |
                static_cast<std::uint32_t>(completed), std::memory_order_relaxed);
          });
      if (!outputs) return core::Result<ProductionImportResult>{outputs.error()};
      progress->store((2U << 16U) | (total << 8U) | total, std::memory_order_relaxed);
      const auto imported = repository.importGeneratedBatch(captured, inputs.value(),
          {.action = "import-generated-batch", .subjectId = manifestSha256, .operatorId = operatorId,
           .occurredAtUtc = occurredAtUtc}, maximumFrames, stop);
      if (!imported) return core::Result<ProductionImportResult>{imported.error()};
      return ProductionImportResult{std::move(captured), productionCommitStatus("GENERATED BATCH: MARKER REVIEW", imported.value())};
    });
    generationBatchProgress_ = std::move(progress);
  } catch (const std::exception& error) {
    return core::failure(core::ErrorCode::Internal, "Unable to start batch generation worker", error.what());
  }
  status_ = "BATCH PREFLIGHT / ESC CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::pollProceduralCandidateImport() {
  if (workspaceOpen_.valid()) {
    if (workspaceOpen_.wait_for(std::chrono::seconds{0})!=std::future_status::ready) return core::success();
    try {
      auto recovered=workspaceOpen_.get();
      if (proceduralImportStop_.stop_requested()) { status_="WORKSPACE OPEN CANCELLED"; return core::success(); }
      if (!recovered) { status_=statusBeforeImport_; return core::Result<void>{recovered.error()}; }
      if (productionSessionEpoch_!=workspaceOpenEpoch_ || productionProject_ || !manifest_.units.empty()) {
        status_=statusBeforeImport_;
        return core::failure(core::ErrorCode::Conflict,"Workspace opening context changed before adoption");
      }
      auto& worker=*recovered.value();
      productionProject_=std::move(worker.productionProject_);
      productionRepository_=std::move(worker.productionRepository_);
      productionWorkspaceRoot_=std::move(worker.productionWorkspaceRoot_);
      productionOperatorId_=std::move(worker.productionOperatorId_);
      stagedRecoveryCandidateCount_=worker.stagedRecoveryCandidateCount_;
      ++productionSessionEpoch_; generationScoreSelection_.reset(); selectedIndex_=0U;
      // Marker lineage was already parsed by the recovery worker. Adopt it
      // without repeating potentially large metadata work on the UI thread.
      invalidateSampleReview();
      candidateMarkerPreview_=std::move(worker.candidateMarkerPreview_);
      candidateMarkerError_=std::move(worker.candidateMarkerError_);
      candidateMarkerRevisionId_=std::move(worker.candidateMarkerRevisionId_);
      candidateMarkersEdited_=worker.candidateMarkersEdited_; selectedCandidateMarker_=0U;
      candidateUndo_.clear(); candidateRedo_.clear(); candidateView_.reset(); candidateViewPeaks_.clear();
      candidateWaveform_.reset(); candidateWaveformError_.clear(); candidatePitch_.reset(); candidatePitchError_.clear();
      status_=stagedRecoveryCandidateCount_==0U?"PRODUCTION RECOVERED":"RECOVERY CANDIDATES";
      return core::success();
    } catch (const std::exception& error) {
      status_=statusBeforeImport_;
      return core::failure(core::ErrorCode::Internal,"Workspace recovery worker failed",error.what());
    }
  }
  if (editableUnitLoad_.valid()) return pollEditableUnitLoad();
  if (sampleReviewWork_.valid()) return pollSampleReviewWork();
  if (const auto progress = generationBatchProgress()) {
    if (progress->phase == GenerationBatchProgress::Phase::Rendering)
      status_ = "BATCH OUTPUTS " + std::to_string(progress->completedOutputs) + "/" +
          std::to_string(progress->totalOutputs) + " / ESC CANCEL";
    else if (progress->phase == GenerationBatchProgress::Phase::Collecting)
      status_ = "BATCH COLLECTING / NOT APPROVED";
  }
  if (pitchLoad_.valid()) {
    if (pitchLoad_.wait_for(std::chrono::seconds{0}) != std::future_status::ready) return core::success();
    try {
      auto result = pitchLoad_.get();
      if (!result) {
        status_ = statusBeforeImport_;
        candidatePitchError_ = result.error().message;
        return core::Result<void>{result.error()};
      }
      candidatePitch_ = std::move(result.value());
      candidatePitchView_ = true;
      candidatePitchError_.clear();
      status_ = "PITCH ESTIMATE / NOT REVIEWED";
      return core::success();
    } catch (const std::exception& error) {
      status_ = statusBeforeImport_;
      candidatePitchError_ = "Pitch inspection worker failed";
      return core::failure(core::ErrorCode::Internal, candidatePitchError_, error.what());
    }
  }
  if (waveformLoad_.valid()) {
    if (waveformLoad_.wait_for(std::chrono::seconds{0}) != std::future_status::ready) return core::success();
    try {
      auto result = waveformLoad_.get();
      if (!result) {
        status_ = statusBeforeImport_;
        candidateWaveformError_ = result.error().message;
        return core::Result<void>{result.error()};
      }
      candidateWaveform_ = std::move(result.value());
      candidateWaveformError_.clear();
      status_ = "RAW WAVEFORM / NOT REVIEWED";
      return core::success();
    } catch (const std::exception& error) {
      status_ = statusBeforeImport_;
      candidateWaveformError_ = "Waveform worker failed";
      return core::failure(core::ErrorCode::Internal, candidateWaveformError_, error.what());
    }
  }
  if (!proceduralImport_.valid() || proceduralImport_.wait_for(std::chrono::seconds{0}) != std::future_status::ready)
    return core::success();
  generationBatchProgress_.reset();
  try {
    auto result = proceduralImport_.get();
    if (!result) {
      status_ = statusBeforeImport_;
      return core::Result<void>{result.error()};
    }
    if (!result.value().changed) {
      // Read-only inspection has no durable publication to report. Cancellation
      // remains authoritative until its snapshot is adopted, even if the worker
      // finished before the UI processed the stop request.
      if (result.value().scoreSelection && proceduralImportStop_.stop_requested()) {
        generationScoreSelection_.reset();
        status_ = statusBeforeImport_;
        return core::failure(core::ErrorCode::Conflict,"Score inspection cancelled before adoption");
      }
      if (result.value().scoreSelection) generationScoreSelection_ = std::move(result.value().scoreSelection);
      status_ = std::move(result.value().status);
      return core::success();
    }
    productionProject_ = std::move(result.value().project);
    refreshCandidateMarkerPreview();
    takeInspection_.reset();
    status_ = std::move(result.value().status);
    return core::success();
  } catch (const std::exception& error) {
    status_ = statusBeforeImport_;
    return core::failure(core::ErrorCode::Internal, "Candidate import worker failed", error.what());
  }
}

core::Result<void> VoicebankStudioController::beginCandidateWaveformPreview() {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate work is busy");
  if (!candidateMarkerPreview_ || !productionProject_ || !productionRepository_) return core::failure(
      core::ErrorCode::InvalidState, "Select an imported procedural candidate first");
  const auto asset = std::find_if(productionProject_->assets.begin(), productionProject_->assets.end(),
      [&](const auto& value) { return value.sha256 == candidateMarkerPreview_->audioSha256 &&
          value.kind == voicebank_production::AssetKind::Raw; });
  if (asset == productionProject_->assets.end()) return core::failure(core::ErrorCode::NotFound, "Candidate raw asset is missing");
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token();
  statusBeforeImport_ = status_;
  const auto path = productionRepository_->assetPath(*asset);
  try {
    waveformLoad_ = std::async(std::launch::async,
        [metadata = candidateMarkerPreview_->metadataJson, recipe = candidateMarkerPreview_->recipe, path, stop]()
            -> core::Result<CandidateWaveform> {
      auto loaded = voice_design::loadProceduralCandidateFromMetadata(metadata, path, recipe, stop);
      if (!loaded) return core::Result<CandidateWaveform>{loaded.error()};
      const auto& candidate = loaded.value();
      const auto& samples = candidate.audio->interleaved;
      const auto count = std::min<std::size_t>(1024U, samples.size());
      CandidateWaveform result{.peaks = {}, .audioSha256 = candidate.audioSha256,
          .sampleRate = candidate.sampleRate, .frameCount = candidate.frameCount, .audio = candidate.audio,
          .statistics = voicebank::analyzeAudio(samples)};
      result.peaks.reserve(count);
      for (std::size_t index = 0U; index < count; ++index) {
        if (stop.stop_requested()) return core::failure<CandidateWaveform>(core::ErrorCode::Conflict, "Waveform loading cancelled");
        const auto begin = samples.begin() + static_cast<std::ptrdiff_t>(index * samples.size() / count);
        const auto end = samples.begin() + static_cast<std::ptrdiff_t>((index + 1U) * samples.size() / count);
        const auto bounds = std::minmax_element(begin, end);
        result.peaks.emplace_back(*bounds.first, *bounds.second);
      }
      return result;
    });
  } catch (const std::exception& error) {
    return core::failure(core::ErrorCode::Internal, "Unable to start waveform preview", error.what());
  }
  candidateWaveform_.reset();
  candidateWaveformError_.clear();
  status_ = "LOADING RAW AUDIO / ESC CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::beginCandidatePitchInspection() {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate work is busy");
  const auto* assignment = selectedProductionAssignment();
  if (!candidateWaveform_ || !candidateWaveform_->audio || !candidateMarkerPreview_ || !assignment)
    return core::failure(core::ErrorCode::InvalidState, "Load the raw candidate before pitch inspection");
  const auto marker = candidateMarkerPreview_->markers[selectedCandidateMarker_];
  const auto audio = candidateWaveform_->audio;
  voicebank::PitchConfig config;
  config.frameSize = 128U;
  while (config.frameSize < (2U * audio->sampleRate + 59U) / 60U) config.frameSize *= 2U;
  config.hopSize = std::max<std::size_t>(1U, audio->sampleRate / 100U);
  config.correlationMethod = voicebank::PitchCorrelationMethod::Fft;
  if (marker.ownedSpan.end - marker.ownedSpan.start < static_cast<time::SampleFrame>(config.frameSize)) return core::failure(
      core::ErrorCode::InvalidArgument, "Selected gesture is too short for an unpadded pitch window");
  CandidatePitchInspection request{.audioSha256 = candidateWaveform_->audioSha256,
      .boundaryRevisionId = candidateMarkerRevisionId_, .key = marker.key,
      .start = marker.ownedSpan.start, .end = marker.ownedSpan.end, .targetMidi = assignment->pitchLayer, .config = config};
  request.projectId = productionProject_->projectId;
  request.takeId = assignment->takeId;
  request.inventorySha256 = productionProject_->inventorySha256;
  request.operatorId = productionOperatorId_;
  request.recipeHash = candidateMarkerPreview_->recipe.identity.contentHash;
  request.sourceGeneration = productionProject_->lastDurableGeneration;
  request.sampleRate = audio->sampleRate;
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token();
  statusBeforeImport_ = status_;
  try {
    pitchLoad_ = std::async(std::launch::async, [request = std::move(request), audio, stop]() mutable -> core::Result<CandidatePitchInspection> {
      const auto samples = std::span<const float>{audio->interleaved}.subspan(static_cast<std::size_t>(request.start),
          static_cast<std::size_t>(request.end - request.start));
      auto measured = voicebank::analyzePitch(samples, audio->sampleRate, request.config, stop,
          {.maximumFrames = 4096U, .maximumCorrelationTerms = 0U, .maximumTransformButterflies = 512000000U});
      if (!measured) return core::Result<CandidatePitchInspection>{measured.error()};
      const auto median = voicebank::medianVoicedPitch(measured.value());
      if (median > 0.0 && std::isfinite(median)) {
        request.medianHz = median;
        request.centsFromTarget = 1200.0 * std::log2(median / (440.0 * std::pow(2.0, (request.targetMidi - 69.0) / 12.0)));
      }
      request.frames = std::move(measured.value());
      for (auto& frame : request.frames) { frame.sourceFrame += static_cast<std::size_t>(request.start); if (frame.voiced) ++request.voicedFrames; }
      request.contour = buildPitchContour(request.frames, request.config, request.targetMidi);
      if (stop.stop_requested()) return core::failure<CandidatePitchInspection>(core::ErrorCode::Conflict, "Pitch inspection cancelled");
      return request;
    });
  } catch (const std::exception& error) {
    return core::failure(core::ErrorCode::Internal, "Unable to start pitch inspection", error.what());
  }
  candidatePitch_.reset(); candidatePitchError_.clear();
  status_ = "ANALYZING GESTURE / ESC CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::exportCandidatePitchInspection(
    const std::filesystem::path& destination, std::string exportedAtUtc) const {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate work is busy");
  if (!candidatePitch_ || !candidateMarkerPreview_ || selectedCandidateMarker_ >= candidateMarkerPreview_->markers.size())
    return core::failure(core::ErrorCode::InvalidState, "Inspect the selected gesture before exporting pitch evidence");
  const auto& inspection = *candidatePitch_;
  const auto& marker = candidateMarkerPreview_->markers[selectedCandidateMarker_];
  if (inspection.boundaryRevisionId != candidateMarkerRevisionId_ || inspection.audioSha256 != candidateMarkerPreview_->audioSha256 ||
      inspection.key != marker.key || inspection.start != marker.ownedSpan.start || inspection.end != marker.ownedSpan.end)
    return core::failure(core::ErrorCode::Conflict, "Pitch evidence no longer matches the selected gesture");
  if (exportedAtUtc.empty()) exportedAtUtc = voicebank_studio_internal::currentUtcTimestamp();
  if (!voicebank_production::isProductionUtcTimestamp(exportedAtUtc)) return core::failure(core::ErrorCode::InvalidArgument, "Export timestamp is invalid");
  using Json = formats::JsonValue;
  Json::Array frames;
  for (const auto& frame : inspection.frames) frames.emplace_back(Json::Object{
      {"sourceFrame", static_cast<std::int64_t>(frame.sourceFrame)}, {"voiced", frame.voiced},
      {"f0Hz", frame.voiced ? Json{frame.f0Hz} : Json{}}, {"confidence", frame.confidence}});
  const auto text = formats::stringifyJson(Json{Json::Object{
      {"formatId", "com.project-seam.candidate-pitch-inspection"}, {"schemaVersion", std::int64_t{1}},
      {"status", "ESTIMATE_ONLY"}, {"approved", false}, {"exportedAtUtc", exportedAtUtc},
      {"projectId", inspection.projectId}, {"takeId", inspection.takeId}, {"inventorySha256", inspection.inventorySha256},
      {"operatorId", inspection.operatorId}, {"sourceGeneration", std::to_string(inspection.sourceGeneration)},
      {"audioSha256", inspection.audioSha256}, {"recipeHash", inspection.recipeHash}, {"boundaryRevisionId", inspection.boundaryRevisionId},
      {"phonemeKey", inspection.key.toString()}, {"startFrame", inspection.start}, {"endFrame", inspection.end},
      {"sampleRate", static_cast<std::int64_t>(inspection.sampleRate)}, {"targetMidi", static_cast<std::int64_t>(inspection.targetMidi)},
      {"analyzer", "seam.normalized-autocorrelation.v1"},
      {"correlationMethod", inspection.config.correlationMethod == voicebank::PitchCorrelationMethod::Fft ? "fft" : "direct"},
      {"frameSize", static_cast<std::int64_t>(inspection.config.frameSize)}, {"hopSize", static_cast<std::int64_t>(inspection.config.hopSize)},
      {"minimumHz", inspection.config.minimumHz}, {"maximumHz", inspection.config.maximumHz}, {"voicingThreshold", inspection.config.voicingThreshold},
      {"medianHz", inspection.medianHz ? Json{*inspection.medianHz} : Json{}},
      {"centsFromTarget", inspection.centsFromTarget ? Json{*inspection.centsFromTarget} : Json{}},
      {"frames", std::move(frames)}}}, true);
  if (text.size() > 1024U * 1024U) return core::failure(core::ErrorCode::InvalidArgument, "Pitch evidence exceeds its export size bound");
  return core::durableAtomicWriteTextNew(destination, text);
}

core::Result<void> VoicebankStudioController::beginOpenProductionProject(std::filesystem::path workspaceRoot,
    std::string expectedInventorySha256,std::string operatorId) {
  if (proceduralImportBusy() || productionProject_ || !manifest_.units.empty() || dirty_)
    return core::failure(core::ErrorCode::Conflict,"Workspace entry requires an idle source-free Studio");
  if (workspaceRoot.empty() || expectedInventorySha256.empty() || operatorId.empty() ||
      productionSessionEpoch_==std::numeric_limits<std::uint64_t>::max())
    return core::failure(core::ErrorCode::InvalidArgument,"Workspace opening identity is invalid");
  proceduralImportStop_=std::stop_source{}; const auto stop=proceduralImportStop_.get_token();
  workspaceOpenEpoch_=productionSessionEpoch_; statusBeforeImport_=status_;
  try {
    workspaceOpen_=std::async(std::launch::async,[root=std::move(workspaceRoot),inventory=std::move(expectedInventorySha256),
        actor=std::move(operatorId),stop]() -> core::Result<std::unique_ptr<VoicebankStudioController>> {
      using Output=std::unique_ptr<VoicebankStudioController>;
      if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,"Workspace opening cancelled");
      auto worker=std::make_unique<VoicebankStudioController>();
      const auto opened=worker->openProductionProject(root,inventory,actor,true);
      if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,"Workspace opening cancelled");
      if (!opened) return core::Result<Output>{opened.error()};
      return worker;
    });
  } catch (const std::exception& error) { return core::failure(core::ErrorCode::Internal,"Cannot start workspace recovery",error.what()); }
  status_="OPENING WORKSPACE / ESC CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::openProductionProject(
    const std::filesystem::path& workspaceRoot,
    std::string_view expectedInventorySha256,
    std::string operatorId, bool requireRegisteredProducer) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate import is busy");
  if (workspaceRoot.empty() || expectedInventorySha256.empty() ||
      operatorId.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Production workspace, inventory hash, and operator are required");
  }
  if (productionSessionEpoch_ == std::numeric_limits<std::uint64_t>::max()) return core::failure(
      core::ErrorCode::Conflict, "Production session epoch is exhausted");
  auto repository = std::make_unique<
      voicebank_production::ProductionProjectRepository>(workspaceRoot);
  auto recovered = repository->recover();
  if (!recovered) return core::Result<void>{recovered.error()};
  if (recovered.value().inventorySha256 != expectedInventorySha256) {
    return core::failure(core::ErrorCode::Conflict,
                         "Production project inventory digest does not match");
  }
  if (requireRegisteredProducer && std::none_of(recovered.value().operators.begin(),recovered.value().operators.end(),
      [&](const auto& actor){return actor.operatorId==operatorId && actor.role=="PRODUCER";}))
    return core::failure(core::ErrorCode::InvalidArgument,"Select an existing registered PRODUCER operator");
  stagedRecoveryCandidateCount_ =
      repository->inspectStaged(recovered.value()).size();
  productionProject_ = std::move(recovered.value());
  productionRepository_ = std::move(repository);
  productionWorkspaceRoot_ =
      std::filesystem::absolute(workspaceRoot).lexically_normal();
  productionOperatorId_ = std::move(operatorId);
  ++productionSessionEpoch_;
  generationScoreSelection_.reset();
  if (manifest_.units.empty()) selectedIndex_ = 0U;
  refreshCandidateMarkerPreview();
  status_ = stagedRecoveryCandidateCount_ == 0U
                ? "PRODUCTION RECOVERED" : "RECOVERY CANDIDATES";
  return core::success();
}

core::Result<void> VoicebankStudioController::saveProductionProject() {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate import is busy");
  if (!productionRepository_ || !productionProject_) {
    return core::failure(core::ErrorCode::Conflict,
                         "Voicebank Studio has no production project");
  }
  const auto occurredAt = voicebank_studio_internal::currentUtcTimestamp();
  auto saved = productionRepository_->save(
      *productionProject_,
      {.action = "save", .subjectId = productionProject_->projectId,
       .operatorId = productionOperatorId_, .occurredAtUtc = occurredAt});
  if (saved) status_ = "PRODUCTION SAVED";
  return saved;
}

const voicebank_production::UnitAssignment*
VoicebankStudioController::selectedProductionAssignment() const noexcept {
  if (!productionProject_) return nullptr;
  if (manifest_.units.empty()) {
    return selectedIndex_ < productionProject_->unitAssignments.size()
               ? &productionProject_->unitAssignments[selectedIndex_]
               : nullptr;
  }
  const auto* unit = selectedUnit();
  if (unit == nullptr) return nullptr;
  const auto coverage = voicebank_studio_internal::coverageKey(*unit);
  const auto assignment = std::find_if(
      productionProject_->unitAssignments.begin(),
      productionProject_->unitAssignments.end(),
      [&coverage, unit](const voicebank_production::UnitAssignment& value) {
        return value.coverageKey == coverage && value.pitchLayer == unit->rootMidi;
      });
  return assignment == productionProject_->unitAssignments.end()
             ? nullptr : &*assignment;
}

core::Result<void> VoicebankStudioController::inspectSelectedProductionTake(
    const std::filesystem::path& path) {
  const auto* assignment = selectedProductionAssignment();
  if (assignment == nullptr) {
    return core::failure(core::ErrorCode::InvalidState,
                         "No production inventory unit is selected");
  }
  return inspectTake(path, assignment->pitchLayer);
}

core::Result<void> VoicebankStudioController::validateProductionImportContext(
    std::uint64_t epoch, std::uint64_t generation, std::size_t selectedIndex) const {
  if (!productionProject_ || !productionRepository_ || !selectedProductionAssignment() ||
      productionSessionEpoch_ != epoch || productionProject_->lastDurableGeneration != generation || selectedIndex_ != selectedIndex) {
    return core::failure(core::ErrorCode::Conflict, "Production selection changed while the import dialogs were open");
  }
  return core::success();
}

core::Result<void> VoicebankStudioController::importSelectedProceduralCandidate(
    const std::filesystem::path& metadataPath, const std::filesystem::path& audioPath,
    const std::filesystem::path& recipePath, std::string occurredAtUtc,
    std::stop_token stopToken) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate import is busy");
  if (stopToken.stop_requested()) return core::failure(core::ErrorCode::Conflict,
      "Candidate import cancelled before commit");
  const auto* selected = selectedProductionAssignment();
  if (!productionRepository_ || !productionProject_ || !selected) return core::failure(core::ErrorCode::InvalidState,
      "Candidate import requires a selected production inventory row");
  const auto recipe = voice_design::loadVoiceRecipeResource(recipePath, {}, stopToken);
  if (!recipe) return core::Result<void>{recipe.error()};
  const auto assignment = *selected;
  const bool retake = !assignment.takeId.empty();
  const auto takeId = retake ? assignment.plannedTakeId + "-retake-" + std::to_string(productionProject_->takes.size() + 1U) : assignment.plannedTakeId;
  if (occurredAtUtc.empty()) occurredAtUtc = voicebank_studio_internal::currentUtcTimestamp();
  const auto imported = productionRepository_->importProceduralCandidate(*productionProject_, metadataPath, audioPath, recipe.value(),
      {.takeId = takeId, .promptId = assignment.promptId, .coverageKey = assignment.coverageKey, .pitchLayer = assignment.pitchLayer,
       .supersedesTakeId = assignment.takeId, .initialState = voicebank_production::UnitQueueState::MarkerReview, .review = std::nullopt},
      {.action = retake ? "retake" : "import-procedural", .subjectId = takeId, .operatorId = productionOperatorId_,
       .occurredAtUtc = std::move(occurredAtUtc)}, stopToken);
  if (!imported) return core::Result<void>{imported.error()};
  takeInspection_.reset();
  stagedRecoveryCandidateCount_ = productionRepository_->inspectStaged(*productionProject_).size();
  status_ = productionCommitStatus("CANDIDATE: MARKER REVIEW", imported.value());
  refreshCandidateMarkerPreview();
  return core::success();
}

core::Result<void> VoicebankStudioController::importSelectedTake(
    const std::filesystem::path& takePath, std::string occurredAtUtc) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate import is busy");
  if (!productionRepository_ || !productionProject_) {
    return core::failure(core::ErrorCode::InvalidState,
                         "Voicebank Studio has no production project");
  }
  const auto* selectedAssignment = selectedProductionAssignment();
  if (selectedAssignment == nullptr || !takeInspection_.has_value()) {
    return core::failure(core::ErrorCode::InvalidState,
                         "A selected production unit and inspected take are required");
  }
  if (occurredAtUtc.empty()) {
    occurredAtUtc = voicebank_studio_internal::currentUtcTimestamp();
  }
  const auto assignment = *selectedAssignment;
  const auto retake = !assignment.takeId.empty();
  const auto takeId = retake
      ? assignment.plannedTakeId + "-retake-" +
            std::to_string(productionProject_->takes.size() + 1U)
      : assignment.plannedTakeId;
  auto imported = productionRepository_->importRaw(
      *productionProject_, takePath,
      {.takeId = takeId,
       .promptId = assignment.promptId,
       .coverageKey = assignment.coverageKey,
       .pitchLayer = assignment.pitchLayer,
       .supersedesTakeId = assignment.takeId,
       .initialState = takeInspection_->accepted()
                           ? voicebank_production::UnitQueueState::MarkerReview
                           : voicebank_production::UnitQueueState::Rejected,
       .review = voicebank_production::ReviewRecord{
           .reviewId = "dry-take-" +
                       core::sha256Hex(takeId + takeInspection_->sourceSha256)
                           .substr(0U, 24U),
           .takeId = takeId,
           .reviewerId = productionOperatorId_,
           .result = takeInspection_->accepted() ? "PASS" : "REJECTED",
           .reviewedAtUtc = occurredAtUtc}},
      {.action = retake ? "retake" : "import",
       .subjectId = takeId,
       .operatorId = productionOperatorId_,
       .occurredAtUtc = std::move(occurredAtUtc)});
  if (!imported) return core::Result<void>{imported.error()};
  stagedRecoveryCandidateCount_ =
      productionRepository_->inspectStaged(*productionProject_).size();
  status_ = productionCommitStatus(takeInspection_->accepted() ? "TAKE IMPORTED" : "TAKE REJECTED", imported.value());
  refreshCandidateMarkerPreview();
  return core::success();
}

core::Result<voicebank_production::ExportedU57Inputs>
VoicebankStudioController::exportProductionInputs(
    const std::filesystem::path& destination, std::string occurredAtUtc) {
  if (proceduralImportBusy()) return core::failure<voicebank_production::ExportedU57Inputs>(
      core::ErrorCode::Conflict, "Candidate import is busy");
  if (!productionRepository_ || !productionProject_) {
    return core::failure<voicebank_production::ExportedU57Inputs>(
        core::ErrorCode::InvalidState,
        "Voicebank Studio has no production project");
  }
  if (occurredAtUtc.empty()) {
    occurredAtUtc = voicebank_studio_internal::currentUtcTimestamp();
  }
  auto exported = productionRepository_->exportU57Inputs(
      *productionProject_, destination,
      {.action = "candidate-export", .subjectId = productionProject_->projectId,
       .operatorId = productionOperatorId_,
       .occurredAtUtc = std::move(occurredAtUtc)});
  if (exported) status_ = "U57 INPUTS EXPORTED";
  return exported;
}

voicebank_production::ProductionQueueSummary
VoicebankStudioController::productionQueues() const noexcept {
  return productionProject_.has_value()
             ? voicebank_production::summarizeQueues(*productionProject_)
             : voicebank_production::ProductionQueueSummary{};
}

std::optional<voicebank_production::UnitQueueState>
VoicebankStudioController::productionStateForUnit(
    const voicebank::Unit& unit) const noexcept {
  if (!productionProject_) return std::nullopt;
  const auto coverage = voicebank_studio_internal::coverageKey(unit);
  const auto assignment = std::find_if(
      productionProject_->unitAssignments.begin(),
      productionProject_->unitAssignments.end(),
      [&coverage, &unit](const voicebank_production::UnitAssignment& value) {
        return value.coverageKey == coverage && value.pitchLayer == unit.rootMidi;
      });
  return assignment == productionProject_->unitAssignments.end()
             ? std::nullopt : std::optional{assignment->state};
}

}
