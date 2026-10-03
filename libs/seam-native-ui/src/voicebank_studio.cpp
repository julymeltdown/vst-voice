#include "seam/native_ui/voicebank_studio.hpp"

#include "voicebank_studio_production_view.hpp"

#include "seam/native_ui/voicebank_studio_type_scale.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank_production/take_inspection_receipt.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/text/unicode.hpp"
#include "seam/voicebank/asset_path.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace seam::native_ui {
namespace {

ui::Rect waveformRect(double width, double height) {
  const auto left = 270.0;
  const auto rightPanel = 250.0;
  return ui::Rect{left, 100.0, std::max(160.0, width - left - rightPanel - 18.0),
                  std::max(120.0, (height - 170.0) * 0.42)};
}

ui::Rect spectrogramRect(double width, double height) {
  auto wave = waveformRect(width, height);
  return ui::Rect{wave.x, wave.bottom() + 16.0, wave.width,
                  std::max(120.0, height - wave.bottom() - 76.0)};
}

}  // namespace

std::vector<ui::Rect> voicebankStudioMarkerLabelBounds(
    std::span<const ui::AcousticMarkerVisual> markers,
    ui::Rect waveformBounds) {
  // The labels sit in a band across the top of the waveform rather than on the trace. They were 6
  // point on a 9 point row pitch with a width estimate of 3.8 points per column, all of which were
  // tuned to that size; at 12 point the same numbers would put two labels on one row and the estimate
  // would be short, so a label would be drawn narrower than the text in it. The band is the height the
  // rows need and the estimate is the width of the text at this size, both measured rather than
  // carried over.
  constexpr double kMarkerLabelRow = 15.0;
  constexpr double kMarkerLabelBand = 62.0;
  constexpr double kMarkerLabelColumnWidth = 7.2;
  std::vector<ui::Rect> result;
  result.reserve(markers.size());
  std::vector<double> rowRights;
  for (const auto& marker : markers) {
    const auto displayWidth =
        static_cast<double>(text::utf8DisplayWidth(marker.label));
    const auto estimatedWidth = std::max(
        12.0, displayWidth * kMarkerLabelColumnWidth + 6.0);
    const auto width = std::min(
        estimatedWidth, std::max(1.0, waveformBounds.width - 4.0));
    const auto leftLimit = waveformBounds.x + 2.0;
    const auto rightLimit = waveformBounds.right() - width - 2.0;
    const auto x = std::clamp(marker.x + 3.0, leftLimit, rightLimit);
    std::size_t row = 0U;
    while (row < rowRights.size() && x < rowRights[row] + 2.0) ++row;
    if (row == rowRights.size()) {
      rowRights.push_back(x + width);
    } else {
      rowRights[row] = x + width;
    }
    // A label that would fall below the band is not drawn inside the waveform; the caller decides
    // what to do with a label that has no room, rather than the label being drawn on the trace. The
    // band holds four rows at this pitch, so the second row a crowded waveform pushes a label onto is
    // still inside it rather than being cut in half by the edge of the band.
    const auto top = waveformBounds.y + 2.0 + static_cast<double>(row) * kMarkerLabelRow;
    if (top + kMarkerLabelRow > waveformBounds.y + kMarkerLabelBand) {
      result.push_back(ui::Rect{waveformBounds.x, waveformBounds.y - 1.0, 0.0, 0.0});
      continue;
    }
    result.push_back(ui::Rect{x, top, width, kMarkerLabelRow});
  }
  return result;
}

std::optional<std::size_t> voicebankStudioUnitRailIndexAt(
    double y, std::size_t firstVisibleIndex, std::size_t unitCount,
    double viewportHeight, bool productionLayout) noexcept {
  constexpr auto top = 108.0;
  const auto stride = productionLayout ? 36.0 : 32.0;
  const auto rowHeight = productionLayout ? 32.0 : 28.0;
  if (!std::isfinite(y) || y < top || firstVisibleIndex >= unitCount) {
    return std::nullopt;
  }
  const auto relative = y - top;
  const auto offset = static_cast<std::size_t>(std::floor(relative / stride));
  const auto visibleRows =
      voicebankStudioUnitRailVisibleRows(viewportHeight, productionLayout);
  if (relative - static_cast<double>(offset) * stride >= rowHeight ||
      offset >= visibleRows || offset >= unitCount - firstVisibleIndex) {
    return std::nullopt;
  }
  return firstVisibleIndex + offset;
}

std::size_t voicebankStudioUnitRailVisibleRows(
    double viewportHeight, bool productionLayout) noexcept {
  constexpr auto top = 108.0;
  const auto stride = productionLayout ? 36.0 : 32.0;
  const auto rowHeight = productionLayout ? 32.0 : 28.0;
  const std::size_t maximumRows = productionLayout ? 12U : 18U;
  if (!std::isfinite(viewportHeight) || viewportHeight < top + rowHeight) {
    return 0U;
  }
  const auto fittingRows = static_cast<std::size_t>(
      std::floor((viewportHeight - top - rowHeight) / stride)) + 1U;
  return std::min(maximumRows, fittingRows);
}

core::Result<std::filesystem::path> nextVoicebankRecordingPath(
    const std::filesystem::path& directory, std::string_view unitId) {
  std::error_code error;
  const auto directoryStatus = std::filesystem::symlink_status(directory, error);
  if (error || std::filesystem::is_symlink(directoryStatus) ||
      !std::filesystem::is_directory(directoryStatus)) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::Conflict,
        "Recording directory must be a real directory", directory.string());
  }
  std::string stem;
  stem.reserve(std::min<std::size_t>(unitId.size(), 80U));
  for (const auto character : unitId) {
    if (stem.size() == 80U) break;
    const auto byte = static_cast<unsigned char>(character);
    stem.push_back((byte < 128U && (std::isalnum(byte) != 0 || character == '-' ||
                                   character == '_'))
                       ? character
                       : '_');
  }
  if (stem.empty()) stem = "take";
  auto upper = stem;
  std::transform(upper.begin(), upper.end(), upper.begin(), [](char value) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
  });
  const auto reserved = upper == "CON" || upper == "PRN" || upper == "AUX" ||
                        upper == "NUL" ||
                        (upper.size() == 4U &&
                         (upper.starts_with("COM") || upper.starts_with("LPT")) &&
                         upper.back() >= '1' && upper.back() <= '9');
  if (reserved) stem.insert(stem.begin(), '_');
  const auto resolvedDirectory = std::filesystem::canonical(directory, error);
  if (error) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::IoError,
        "Unable to resolve recording directory", error.message());
  }
  for (std::size_t index = 1U; index <= 10000U; ++index) {
    std::ostringstream name;
    name << stem << "-take-" << std::setw(4) << std::setfill('0') << index
         << ".wav";
    const auto candidate = directory / name.str();
    if (candidate.parent_path() != directory ||
        std::filesystem::canonical(candidate.parent_path(), error) !=
            resolvedDirectory ||
        error) {
      return core::failure<std::filesystem::path>(
          core::ErrorCode::Conflict,
          "Recording destination is not a direct child", candidate.string());
    }
    const auto status = std::filesystem::symlink_status(candidate, error);
    if (error == std::errc::no_such_file_or_directory ||
        status.type() == std::filesystem::file_type::not_found) {
      return candidate;
    }
    if (error) {
      return core::failure<std::filesystem::path>(
          core::ErrorCode::IoError,
          "Unable to inspect recording destination", error.message());
    }
    if (std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status)) {
      return core::failure<std::filesystem::path>(
          core::ErrorCode::Conflict,
          "Recording destination is not a regular file", candidate.string());
    }
  }
  return core::failure<std::filesystem::path>(
      core::ErrorCode::Conflict,
      "Voicebank Studio has no available take filename");
}

core::Result<void> VoicebankStudioController::openManifest(
    const std::filesystem::path& manifestPath, double logicalWidth,
    double logicalHeight) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate import is busy");
  auto loaded = prepareSampleManifestLoad(manifestPath, logicalWidth, logicalHeight);
  if (!loaded) return core::Result<void>{loaded.error()};
  logicalWidth_ = std::max(720.0, logicalWidth);
  logicalHeight_ = std::max(520.0, logicalHeight);
  adoptLoadedSampleUnit(std::move(loaded.value()));
  status_ = "LOADED";
  return core::success();
}

core::Result<void> VoicebankStudioController::save() {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate import is busy");
  if (manifestPath_.empty()) {
    return saveProductionProject();
  }
  const auto validation = manifest_.validate();
  if (!validation) return validation;
  const auto productionMetadataDirty = dirty_ && productionProject_.has_value();
  auto result = codec_.save(manifest_, manifestPath_);
  if (!result) return result;
  bool productionDurabilityConfirmed = true;
  if (productionMetadataDirty) {
    auto productionSaved = persistProductionMetadata();
    if (!productionSaved) return core::Result<void>{productionSaved.error()};
    productionDurabilityConfirmed = productionSaved.value().durabilityConfirmed;
  }
  dirty_ = false;
  status_ = productionDurabilityConfirmed ? "SAVED" : "METADATA COMMITTED / RECOVER BEFORE FURTHER WORK";
  return result;
}

core::Result<bool> VoicebankStudioController::confirmSampleClose(platform::IFileDialog& dialog) {
  if (proceduralImportBusy()) return core::failure<bool>(core::ErrorCode::Conflict, "Finish or cancel Studio work before closing");
  if (!dirty_) return true;
  const auto manifest = manifest_;
  const auto manifestPath = manifestPath_;
  const auto index = selectedIndex_;
  const auto producer = productionProject_ ? voicebank_production::encodeProductionProject(*productionProject_) : std::string{};
  const auto choice = dialog.confirmUnsavedSampleChanges();
  if (!choice) return core::Result<bool>{choice.error()};
  if (proceduralImportBusy() || !dirty_ || manifest_ != manifest || manifestPath_ != manifestPath || selectedIndex_ != index ||
      (productionProject_ ? voicebank_production::encodeProductionProject(*productionProject_) : std::string{}) != producer)
    return core::failure<bool>(core::ErrorCode::Conflict, "Studio changed during close confirmation; review the current edits before closing");
  if (choice.value() == platform::UnsavedSampleDecision::Cancel) return false;
  if (choice.value() == platform::UnsavedSampleDecision::Discard) return true;
  const auto saved = save();
  if (!saved) return core::Result<bool>{saved.error()};
  return !dirty_;
}

core::Result<void> VoicebankStudioController::selectUnit(std::size_t index) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate import is busy");
  if (index >= selectableUnitCount()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Voicebank unit index is outside the manifest");
  }
  if (!manifest_.units.empty()) {
    auto prepared = prepareUnitVisual(manifest_, root_, sampleAudioBindings_, index, logicalWidth_, logicalHeight_);
    if (!prepared) return core::Result<void>{prepared.error()};
    audio_ = std::move(prepared.value().audio); microscope_ = std::move(prepared.value().microscope);
    pinnedAudioUnitId_ = manifest_.units[index].id; pinnedAudioPath_ = root_ / manifest_.units[index].audioPath;
  }
  if (selectedIndex_ != index) generationScoreSelection_.reset();
  selectedIndex_ = index;
  refreshCandidateMarkerPreview();
  takeInspection_.reset();
  if (!generationScoreSelection_) status_ = "UNIT " + std::to_string(index + 1U);
  return core::success();
}

core::Result<std::string> VoicebankStudioController::editableUnitLoadIdentity() const {
  const auto manifest = codec_.encode(manifest_);
  if (!manifest) return core::Result<std::string>{manifest.error()};
  core::Sha256 digest;
  const auto field = [&](std::string_view value) {
    digest.update(std::to_string(value.size())); digest.update(":"); digest.update(value);
  };
  field(manifest.value()); field(manifestPath_.generic_string()); field(root_.generic_string());
  field(std::to_string(selectedIndex_)); field(dirty_ ? "dirty" : "clean");
  field(std::to_string(productionSessionEpoch_)); field(std::to_string(sampleReviewSelectionRevision_));
  field(productionProject_ ? voicebank_production::encodeProductionProject(*productionProject_) : std::string{});
  return digest.hexDigest();
}

core::Result<void> VoicebankStudioController::beginEditableUnitSelection(std::size_t index) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Finish or cancel the current Studio work before selecting a unit");
  // Inventory-only selection has no audio or FFT work to move off-thread.
  if (manifest_.units.empty()) return selectUnit(index);
  if (index >= manifest_.units.size()) return core::failure(core::ErrorCode::InvalidArgument, "Editable unit index is outside the manifest");
  const auto identity = editableUnitLoadIdentity(); if (!identity) return core::Result<void>{identity.error()};
  auto worker = std::make_unique<VoicebankStudioController>();
  worker->manifest_ = manifest_; worker->manifestPath_ = manifestPath_; worker->root_ = root_;
  worker->sampleAudioBindings_ = sampleAudioBindings_; worker->selectedIndex_ = index;
  worker->logicalWidth_ = logicalWidth_; worker->logicalHeight_ = logicalHeight_;
  proceduralImportStop_ = std::stop_source{};
  const auto stop = proceduralImportStop_.get_token(); statusBeforeImport_ = status_;
  try {
    editableUnitLoad_ = std::async(std::launch::async,
        [worker = std::move(worker), identity = identity.value(), dirty = dirty_, stop]() mutable -> core::Result<EditableUnitLoad> {
      const auto loaded = worker->rebuildSelected(stop);
      if (!loaded) return core::Result<EditableUnitLoad>{loaded.error()};
      return EditableUnitLoad{std::move(identity), {std::move(worker->manifest_), std::move(worker->audio_),
          std::move(worker->microscope_), std::move(worker->manifestPath_), std::move(worker->root_),
          worker->selectedIndex_, dirty, std::move(worker->sampleAudioBindings_)}};
    });
  } catch (const std::exception& error) {
    return core::failure(core::ErrorCode::Internal, "Cannot start editable unit loading", error.what());
  }
  sampleReviewStatus_ = status_ = "LOADING EDITABLE UNIT / ESC CANCEL";
  return core::success();
}

core::Result<void> VoicebankStudioController::pollEditableUnitLoad() {
  if (!editableUnitLoad_.valid() || editableUnitLoad_.wait_for(std::chrono::seconds{0}) != std::future_status::ready)
    return core::success();
  try {
    auto result = editableUnitLoad_.get();
    if (!result) { sampleReviewStatus_ = result.error().message; status_ = statusBeforeImport_; return core::Result<void>{result.error()}; }
    const auto identity = editableUnitLoadIdentity();
    if (proceduralImportStop_.stop_requested() || !identity || identity.value() != result.value().contextIdentity) {
      sampleReviewStatus_ = "EDITABLE UNIT LOAD CANCELLED OR STALE / EXISTING EDITS PRESERVED";
      status_ = statusBeforeImport_;
      return core::failure(core::ErrorCode::Conflict, "Editable unit load cancelled or stale; existing edits were preserved");
    }
    adoptLoadedSampleUnit(std::move(result.value().loaded));
    sampleReviewStatus_ = status_ = "EDITABLE UNIT LOADED / UNSAVED EDITS PRESERVED";
    return core::success();
  } catch (const std::exception& error) {
    sampleReviewStatus_ = "EDITABLE UNIT LOAD FAILED / EXISTING EDITS PRESERVED";
    status_ = statusBeforeImport_;
    return core::failure(core::ErrorCode::Internal, "Editable unit loading failed", error.what());
  }
}

std::size_t VoicebankStudioController::selectableUnitCount() const noexcept {
  if (!manifest_.units.empty()) return manifest_.units.size();
  return productionProject_.has_value()
             ? productionProject_->unitAssignments.size()
             : 0U;
}

std::filesystem::path VoicebankStudioController::recordingDirectory() const {
  if (!manifestPath_.empty()) return manifestPath_.parent_path() / "recordings";
  return productionWorkspaceRoot_ / "recordings";
}

const voicebank::Unit* VoicebankStudioController::selectedUnit() const noexcept {
  return selectedIndex_ < manifest_.units.size() ? &manifest_.units[selectedIndex_]
                                                 : nullptr;
}

voicebank::Unit* VoicebankStudioController::selectedUnit() noexcept {
  return selectedIndex_ < manifest_.units.size() ? &manifest_.units[selectedIndex_]
                                                 : nullptr;
}

core::Result<void> VoicebankStudioController::inspectTake(
    const std::filesystem::path& path, std::int32_t expectedRootMidi, std::stop_token stopToken) {
  return inspectTake(path, voicebank::TakeInspectionRequest{
      .policy = voicebank::TakeQcPolicy::Voiced, .expectedRootMidi = expectedRootMidi}, stopToken);
}

core::Result<void> VoicebankStudioController::inspectTake(
    const std::filesystem::path& path, const voicebank::TakeInspectionRequest& request,
    std::stop_token stopToken) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate import is busy");
  auto inspected = voicebank::inspectTake(path, request, stopToken);
  if (!inspected) {
    takeInspection_.reset();
    status_ = "TAKE ERROR";
    return core::Result<void>{inspected.error()};
  }
  takeInspection_ = std::move(inspected.value());
  status_ = takeInspection_->accepted()
      ? "SIGNAL CHECKS PASS / HUMAN REVIEW PENDING"
      : "SIGNAL CHECKS NEED REVIEW";
  return core::success();
}

core::Result<std::filesystem::path>
VoicebankStudioController::persistTakeInspection(
    const std::filesystem::path& takePath) const {
  if (proceduralImportBusy()) return core::failure<std::filesystem::path>(core::ErrorCode::Conflict, "Candidate import is busy");
  if (!takeInspection_.has_value()) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::InvalidState,
        "Voicebank Studio has no take inspection to persist");
  }
  const auto digest = core::sha256File(
      takePath, voicebank::kMaximumSupportedWavBytes);
  if (!digest) return core::Result<std::filesystem::path>{digest.error()};
  const auto& inspection = *takeInspection_;
  if (inspection.sourceSha256.empty() ||
      digest.value() != inspection.sourceSha256) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::Conflict,
        "Recorded take changed after inspection");
  }
  auto sidecar = takePath;
  sidecar += ".inspection.json";
  std::error_code error;
  const auto status = std::filesystem::symlink_status(sidecar, error);
  if (error != std::errc::no_such_file_or_directory && error) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::IoError,
        "Unable to inspect take inspection destination", error.message());
  }
  if (!error && status.type() != std::filesystem::file_type::not_found) {
    return core::failure<std::filesystem::path>(
        core::ErrorCode::Conflict,
        "Take inspection destination already exists", sidecar.string());
  }
  // Same inspector, policy, measurements and outcomes a production receipt
  // records, named by file instead of by assignment. Evidence, not approval.
  auto record = voicebank_production::takeInspectionJson(inspection);
  record.asObject().emplace("schemaVersion", formats::JsonValue{std::int64_t{2}});
  record.asObject().emplace("takeFile", formats::JsonValue{takePath.filename().generic_string()});
  const auto written = core::durableAtomicWriteText(
      sidecar, formats::stringifyJson(record, true));
  if (!written) return core::Result<std::filesystem::path>{written.error()};
  return sidecar;
}

std::filesystem::path VoicebankStudioController::selectedAudioPath() const {
  const auto* unit = selectedUnit();
  return unit == nullptr ? std::filesystem::path{} : root_ / unit->audioPath;
}

core::Result<VoicebankStudioController::PreparedUnitVisual> VoicebankStudioController::prepareUnitVisual(
    const voicebank::Manifest& manifest, const std::filesystem::path& root, const SampleAudioBindings& bindings,
    std::size_t index, double width, double height, std::stop_token stop) const {
  using Output = PreparedUnitVisual;
  const auto cancelled = [] { return core::failure<Output>(core::ErrorCode::Conflict, "Studio audio loading cancelled"); };
  if (stop.stop_requested()) return cancelled();
  if (index >= manifest.units.size()) return core::failure<Output>(core::ErrorCode::NotFound, "No editable unit is selected");
  const auto& unit = manifest.units[index];
  const auto binding = bindings.hashesByUnit.find(unit.id);
  if (bindings.draftDescriptorSha256 && (binding == bindings.hashesByUnit.end() ||
      unit.audioPath.generic_string() != "audio/" + binding->second + ".wav"))
    return core::failure<Output>(core::ErrorCode::Conflict, "Draft unit no longer matches its retained audio binding", unit.id);
  const auto path = voicebank::resolveBankAsset(root, unit.audioPath);
  if (!path) return core::Result<Output>{path.error()};
  // Hold exactly one bounded encoded payload. Hash it before any WAV decode,
  // then pass the same bytes to the allocation-bounded, cancellable decoder.
  const auto bytes = core::readFileBytesLimited(path.value(), 256ULL * 1024ULL * 1024ULL);
  if (!bytes) return core::Result<Output>{bytes.error()};
  if (binding != bindings.hashesByUnit.end()) {
    core::Sha256 digest;
    for (std::size_t offset=0U; offset<bytes.value().size(); offset+=65536U) {
      if (stop.stop_requested()) return cancelled();
      digest.update(std::span<const std::byte>{bytes.value()}.subspan(offset, std::min<std::size_t>(65536U, bytes.value().size()-offset)));
    }
    if (digest.hexDigest() != binding->second)
      return core::failure<Output>(core::ErrorCode::Conflict, "Studio audio bytes differ from the retained unit binding", unit.id);
  }
  const bool boundDraft = bindings.draftDescriptorSha256.has_value();
  const voicebank::WavReadLimits limits{.maximumFrames = boundDraft ? 1024ULL*1024ULL : 32ULL*1024ULL*1024ULL,
      .maximumChannels = static_cast<std::uint16_t>(boundDraft ? 1U : 8U),
      .maximumDecodedSamples = boundDraft ? 1024ULL*1024ULL : 32ULL*1024ULL*1024ULL};
  auto audio = voicebank::readWav(bytes.value(), path.value().string(), limits, stop);
  if (!audio) return core::Result<Output>{audio.error()};
  if (boundDraft && audio.value().sampleRate != manifest.expectedSampleRate)
    return core::failure<Output>(core::ErrorCode::Conflict, "Draft audio sample rate differs from its manifest");
  if (stop.stop_requested()) return cancelled();
  ui::SampleMicroscopeModel microscope;
  const auto built = microscope.rebuild(unit, audio.value(), waveformRect(width,height), spectrogramRect(width,height),
      1200U, {.fftSize=1024U,.hopSize=256U});
  if (!built) return core::Result<Output>{built.error()};
  if (stop.stop_requested()) return cancelled();
  return Output{std::move(audio.value()),std::move(microscope)};
}

core::Result<VoicebankStudioController::SampleReviewWorkResult::LoadedUnit> VoicebankStudioController::prepareSampleManifestLoad(
    const std::filesystem::path& path, double width, double height, std::stop_token stop,
    const voicebank_production::CreatedSampleManifestDraft* expectedDraft) const {
  using Output = SampleReviewWorkResult::LoadedUnit;
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Studio manifest loading cancelled");
  const auto absolute = std::filesystem::absolute(path).lexically_normal();
  const auto root = absolute.parent_path();
  const auto bytes = core::readTextFileLimited(absolute, 32ULL*1024ULL*1024ULL);
  if (!bytes) return core::Result<Output>{bytes.error()};
  if (expectedDraft && core::sha256Hex(bytes.value()) != expectedDraft->manifestSha256)
    return core::failure<Output>(core::ErrorCode::Conflict, "Created draft manifest differs from its commit receipt");
  auto manifest = codec_.decode(bytes.value()); if (!manifest) return core::Result<Output>{manifest.error()};
  if (manifest.value().units.empty()) return core::failure<Output>(core::ErrorCode::NotFound, "Voicebank manifest contains no units");
  SampleAudioBindings bindings;
  std::optional<std::string> requiredDescriptor;
  if (expectedDraft) requiredDescriptor = expectedDraft->draftSha256;
  else if (sampleAudioBindings_.draftDescriptorSha256 && !manifestPath_.empty()) {
    std::error_code leftError, rightError;
    const auto left = std::filesystem::weakly_canonical(absolute,leftError);
    const auto right = std::filesystem::weakly_canonical(manifestPath_,rightError);
    if (!leftError && !rightError && left == right) requiredDescriptor = sampleAudioBindings_.draftDescriptorSha256;
  }
  std::error_code error;
  const auto sidecar = std::filesystem::symlink_status(root / "draft.json",error);
  const bool absent = error == std::errc::no_such_file_or_directory || (!error && sidecar.type() == std::filesystem::file_type::not_found);
  if (requiredDescriptor && absent) return core::failure<Output>(core::ErrorCode::Conflict, "Bound draft metadata is missing; audio cannot become unbound on reopen");
  if (!absent) {
    const auto resolved = voicebank::resolveBankAsset(root, "draft.json"); if (!resolved) return core::Result<Output>{resolved.error()};
    const auto descriptor = core::readTextFileLimited(resolved.value(),32ULL*1024ULL*1024ULL);
    if (!descriptor) return core::Result<Output>{descriptor.error()};
    const auto hash = core::sha256Hex(descriptor.value());
    if (requiredDescriptor && hash != *requiredDescriptor)
      return core::failure<Output>(core::ErrorCode::Conflict, "Bound draft metadata differs from its retained identity");
    const auto parsed = formats::parseJson(descriptor.value(), {.maximumInputBytes=32U*1024U*1024U,.maximumDepth=24U,
        .maximumNodes=131072U,.maximumStringBytes=16384U,.maximumCollectionEntries=8192U});
    if (!parsed) return core::Result<Output>{parsed.error()};
    const auto* format = parsed.value().find("format");
    const bool supported = format && format->isString() && format->asString() == "com.project-seam.editable-sample-draft";
    if (requiredDescriptor && !supported) return core::failure<Output>(core::ErrorCode::Conflict, "Bound draft metadata format changed");
    if (supported) {
      const auto* schema = parsed.value().find("schemaVersion");
      const auto* rows = parsed.value().find("unitBindings");
      if (!schema || !schema->isInteger() || schema->asInt64()!=1 || !rows || !rows->isArray() || rows->asArray().empty() || rows->asArray().size()>4096U)
        return core::failure<Output>(core::ErrorCode::Unsupported, "Draft audio binding metadata has an unsupported shape");
      for (const auto& row : rows->asArray()) {
        const auto* id = row.find("unitId"); const auto* digest = row.find("audioSha256");
        if (!id || !id->isString() || id->asString().empty() || !digest || !digest->isString() || digest->asString().size()!=64U ||
            !std::all_of(digest->asString().begin(),digest->asString().end(),[](char c) { return (c>='0' && c<='9') || (c>='a' && c<='f'); }) ||
            !bindings.hashesByUnit.emplace(id->asString(),digest->asString()).second)
          return core::failure<Output>(core::ErrorCode::Conflict, "Draft unit audio binding is invalid or duplicated");
      }
      for (const auto& unit : manifest.value().units) {
        const auto found = bindings.hashesByUnit.find(unit.id);
        if (found == bindings.hashesByUnit.end() || unit.audioPath.generic_string() != "audio/"+found->second+".wav")
          return core::failure<Output>(core::ErrorCode::Conflict, "Editable draft unit differs from its retained audio binding",unit.id);
      }
      bindings.draftDescriptorSha256 = hash;
    }
  }
  width = std::max(720.0,width); height = std::max(520.0,height);
  auto visual = prepareUnitVisual(manifest.value(),root,bindings,0U,width,height,stop);
  if (!visual) return core::Result<Output>{visual.error()};
  return Output{std::move(manifest.value()),std::move(visual.value().audio),std::move(visual.value().microscope),absolute,root,0U,false,std::move(bindings)};
}

core::Result<void> VoicebankStudioController::rebuildSelected(std::stop_token stop) {
  auto visual = prepareUnitVisual(manifest_,root_,sampleAudioBindings_,selectedIndex_,logicalWidth_,logicalHeight_,stop);
  if (!visual) return core::Result<void>{visual.error()};
  audio_ = std::move(visual.value().audio); microscope_ = std::move(visual.value().microscope);
  pinnedAudioUnitId_ = selectedUnit()->id; pinnedAudioPath_ = selectedAudioPath();
  return core::success();
}

core::Result<void> VoicebankStudioController::relayoutSelected() {
  const auto* unit = selectedUnit();
  if (!unit || unit->id != pinnedAudioUnitId_ || selectedAudioPath() != pinnedAudioPath_)
    return core::failure(core::ErrorCode::Conflict, "Selected unit changed; pinned audio cannot be relabelled by resize");
  return microscope_.relayout(*unit,waveformRect(logicalWidth_,logicalHeight_),spectrogramRect(logicalWidth_,logicalHeight_));
}

core::Result<void> VoicebankStudioController::moveSelectedMarker(
    ui::AcousticMarkerKind marker, double x) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate import is busy");
  auto* unit = selectedUnit();
  if (unit == nullptr) return core::failure(core::ErrorCode::NotFound, "No unit selected");
  auto moved = microscope_.moveMarker(
      *unit, marker, x, static_cast<time::SampleFrame>(audio_.frameCount()));
  if (moved) {
    dirty_ = true;
    status_ = "MARKER EDITED";
  }
  return moved;
}

core::Result<void> VoicebankStudioController::moveSelectedPitchMark(
    std::size_t index, double x) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate import is busy");
  auto* unit = selectedUnit();
  if (unit == nullptr) return core::failure(core::ErrorCode::NotFound, "No unit selected");
  auto moved = microscope_.movePitchMark(*unit, index, x);
  if (moved) {
    dirty_ = true;
    status_ = "PITCH MARK EDITED";
  }
  return moved;
}

void VoicebankStudioController::resize(double logicalWidth, double logicalHeight) {
  cancelCandidateMarkerDrag();
  logicalWidth_ = std::max(720.0, logicalWidth);
  logicalHeight_ = std::max(520.0, logicalHeight);
  if (selectedUnit() != nullptr && audio_.frameCount() != 0U) {
    const auto relayout = relayoutSelected();
    if (!relayout) status_ = relayout.error().message;
  }
}

void VoicebankStudioScenePainter::paint(
    RasterCanvas& canvas, const VoicebankStudioController& controller,
    bool recording, std::string_view recordingBackend) const noexcept {
  const auto width = canvas.logicalWidth();
  const auto height = canvas.logicalHeight();
  canvas.clear(theme_.background);
  canvas.fillRect(ui::Rect{0.0, 0.0, width, 72.0}, theme_.panel);
  canvas.drawText(ui::Point{18.0, 16.0}, "SEAM VOICEBANK STUDIO",
                  theme_.primaryText, voicebankStudioTypeScale().heading);
  const auto* productionProject = controller.productionProject();
  const auto displayName = !controller.manifest().displayName.empty()
                               ? controller.manifest().displayName
                               : productionProject != nullptr
                                     ? productionProject->projectId
                                     : std::string{"NO PROJECT"};
  canvas.drawText(ui::Rect{18.0, 34.0, std::max(0.0, width - 396.0), 16.0},
                  // The project name and the character line below it are read, not decoration, and
                  // are at the same 8 point as the status they sit beside rather than 8 and 6. The
                  // panel has room: the left column is title 16, project 34 and character 57, which
                  // ends at 67 of the 72 point panel.
                  displayName, theme_.secondaryText, voicebankStudioTypeScale().label);
  if (!controller.manifest().characterId.empty()) {
    canvas.drawText(ui::Point{18.0, 57.0},
                    "CHARACTER " + controller.manifest().characterId + " @ " +
                        controller.manifest().characterVersion,
                    theme_.secondaryText, voicebankStudioTypeScale().label);
  }
  // The top right is one status area rather than labels that have to share it. The status is a
  // sentence, so it wraps over the two lines the panel has for it instead of being clipped to one:
  // clipped, it showed the creator the start of a message and hid what it said. The microphone line
  // has its own row under it, and both stop before the panel ends, so neither can grow into the row
  // below or off the window.
  constexpr double kHeaderStatusWidth = 344.0;
  // The header panel is 72 points tall and the right column of it has 62 of them (8 to 70). The
  // status takes two lines of 12 point and the microphone line one, which is 50 points between them,
  // so both are drawn at a size a person can read at a glance rather than the 8 and 7 point they
  // were. The left column of the panel (title, project, character) is unchanged and ends at 67.
  constexpr double kHeaderStatusTop = 8.0;
  constexpr double kHeaderStatusHeight = 33.0;
  constexpr double kHeaderMicTop = 42.0;
  constexpr double kHeaderMicHeight = 17.0;
  const auto headerStatusLeft = width - kHeaderStatusWidth - 16.0;
  canvas.drawTextWrapped(
      ui::Rect{headerStatusLeft, kHeaderStatusTop, kHeaderStatusWidth, kHeaderStatusHeight},
      recording ? "RECORDING" : controller.status(),
      recording ? theme_.accent : theme_.secondaryText, 12.0, 16.0);
  constexpr std::size_t recordingLabelColumns = 44U;
  const bool recordingLabelTruncated =
      text::utf8DisplayWidth(recordingBackend) > recordingLabelColumns;
  auto recordingLabel = text::truncateUtf8ToDisplayWidth(
      recordingBackend,
      recordingLabelTruncated ? recordingLabelColumns - 1U
                              : recordingLabelColumns);
  if (recordingLabelTruncated) recordingLabel += "…";
  canvas.drawText(ui::Rect{headerStatusLeft, kHeaderMicTop, kHeaderStatusWidth, kHeaderMicHeight},
                  // The microphone line names the device the creator is recording from, so it is
                  // read rather than decoration, and is drawn at the same 12 point as the status
                  // above it rather than at the 7 point it was.
                  "MIC " + recordingLabel, theme_.secondaryText, 12.0);

  canvas.fillRect(ui::Rect{0.0, 72.0, 252.0, height - 72.0}, theme_.panelAlternate);
  canvas.drawText(ui::Point{12.0, 86.0}, "UNITS", theme_.secondaryText,
                  voicebankStudioTypeScale().secondary);
  const auto& units = controller.manifest().units;
  if (units.empty()) {
    paintProductionAssignmentRail(canvas, controller, theme_);
  } else {
    const auto first = controller.selectedIndex() > 8U ? controller.selectedIndex() - 8U : 0U;
    const auto last = std::min(
        units.size(), first + voicebankStudioUnitRailVisibleRows(height, false));
    // A row is 28 points tall and holds one label. The label is drawn at the same 12 point as the
    // rest of the window, so it is 20 points tall and the row has room for it. The rows used to be on
    // a 32 point pitch with a 7 point label, and the label was cut to 24 bytes: a UTF-8 character is
    // more than one byte, so a name written in anything but ASCII lost its last character to a cut
    // through the middle of a character.
    constexpr double kRailText = 12.0;
    constexpr std::size_t kRailLabelColumns = 24U;
    for (std::size_t index = first; index < last; ++index) {
      const auto y = 108.0 + static_cast<double>(index - first) * 32.0;
      const ui::Rect row{8.0, y, 236.0, 28.0};
      canvas.fillRect(row, index == controller.selectedIndex() ? theme_.selected
                                                               : theme_.panelAlternate);
      if (index == controller.selectedIndex()) canvas.strokeRect(row, theme_.accent, 1.0);
      const auto& label = units[index].alias.empty() ? units[index].id : units[index].alias;
      // The label is cut to a number of display columns rather than bytes, and an ellipsis says that
      // it was cut, so the creator is not left with a name that looks complete and is not.
      const auto truncated = text::utf8DisplayWidth(label) > kRailLabelColumns;
      const auto painted = text::truncateUtf8ToDisplayWidth(
          label, truncated ? kRailLabelColumns - 1U : kRailLabelColumns);
      canvas.drawTextWrapped(ui::Rect{16.0, y + 4.0, 220.0, 20.0},
                             truncated ? std::string{painted} + "…" : std::string{painted},
                             theme_.primaryText, kRailText, 20.0);
    }
  }

  const auto* unit = controller.selectedUnit();
  if (unit == nullptr) {
    paintProductionEmptyCanvas(canvas, controller, theme_);
    paintProductionInspector(canvas, controller, theme_, nullptr);
    return;
  }
  const auto& microscope = controller.microscope();
  const auto wave = microscope.waveformBounds();
  const auto spec = microscope.spectrogramBounds();
  canvas.fillRect(wave, Color{17, 16, 20, 255});
  canvas.fillRect(spec, Color{12, 12, 15, 255});
  canvas.strokeRect(wave, theme_.grid, 1.0);
  canvas.strokeRect(spec, theme_.grid, 1.0);
  const auto center = wave.y + wave.height * 0.5;
  canvas.line(ui::Point{wave.x, center}, ui::Point{wave.right(), center}, theme_.grid, 0.5);
  for (const auto& column : microscope.waveform()) {
    const auto y1 = center - static_cast<double>(column.maximum) * wave.height * 0.46;
    const auto y2 = center - static_cast<double>(column.minimum) * wave.height * 0.46;
    canvas.line(ui::Point{column.x, y1}, ui::Point{column.x, y2}, theme_.waveform, 1.0);
  }

  const auto& spectrogram = microscope.spectrogram();
  if (spectrogram.columns > 0U && spectrogram.bins > 0U &&
      !spectrogram.decibels.empty()) {
    const auto maxFrames = std::min<std::size_t>(spectrogram.columns, 420U);
    const auto maxBins = std::min<std::size_t>(spectrogram.bins, 96U);
    for (std::size_t frame = 0U; frame < maxFrames; ++frame) {
      const auto x = spec.x + static_cast<double>(frame) /
          static_cast<double>(std::max<std::size_t>(1U, maxFrames - 1U)) * spec.width;
      for (std::size_t bin = 0U; bin < maxBins; ++bin) {
        const auto sourceFrame = frame * spectrogram.columns / maxFrames;
        const auto sourceBin = bin * spectrogram.bins / maxBins;
        const auto value = spectrogram.decibels[
            std::min(sourceFrame, spectrogram.columns - 1U) * spectrogram.bins +
            std::min(sourceBin, spectrogram.bins - 1U)];
        const auto normalized = std::clamp((static_cast<double>(value) + 90.0) / 90.0,
                                           0.0, 1.0);
        if (normalized < 0.08) continue;
        const auto y = spec.bottom() - static_cast<double>(bin + 1U) /
            static_cast<double>(maxBins) * spec.height;
        canvas.fillRect(ui::Rect{x, y, std::max(1.0, spec.width / static_cast<double>(maxFrames) + 0.4),
                                 std::max(1.0, spec.height / static_cast<double>(maxBins) + 0.4)},
                        Color{static_cast<std::uint8_t>(70 + normalized * 120.0),
                              static_cast<std::uint8_t>(50 + normalized * 75.0),
                              static_cast<std::uint8_t>(80 + normalized * 100.0), 255});
      }
    }
  }

  const auto labelBounds = voicebankStudioMarkerLabelBounds(
      microscope.markers(), wave);
  // Every marker line is drawn before any label. A label names a marker and sits over the line of
  // one, so drawing them in one pass let the next marker line run through the label just drawn; the
  // lines are one colour and the same length, so the two passes are indistinguishable where they do
  // not meet a label.
  for (std::size_t index = 0U; index < microscope.markers().size(); ++index) {
    const auto& marker = microscope.markers()[index];
    canvas.line(ui::Point{marker.x, wave.y}, ui::Point{marker.x, spec.bottom()},
                marker.kind == ui::AcousticMarkerKind::VowelOnset ? theme_.accent
                                                                  : theme_.grid, 1.0);
  }
  for (std::size_t index = 0U; index < microscope.markers().size(); ++index) {
    const auto& marker = microscope.markers()[index];
    // A marker whose label had no room in the band is given an empty rect and is not drawn: its line
    // is above, so the creator still sees where the marker is.
    if (labelBounds[index].width <= 0.0) continue;
    // The label is drawn on its own backing so that no marker line crosses the word.
    canvas.fillRect(labelBounds[index], Color{17, 16, 20, 255});
    canvas.drawTextWrapped(
        ui::Rect{labelBounds[index].x, labelBounds[index].y,
                 std::max(0.0, labelBounds[index].width - 4.0),
                 labelBounds[index].height},
        marker.label, theme_.secondaryText, 12.0, labelBounds[index].height);
  }
  for (const auto& mark : microscope.pitchMarks()) {
    canvas.line(ui::Point{mark.x, wave.y}, ui::Point{mark.x, wave.y + 18.0},
                mark.locked ? theme_.accent : theme_.pitch, 1.0);
  }

  const auto inspectorX = width - 238.0;
  canvas.fillRect(ui::Rect{inspectorX, 72.0, 238.0, height - 72.0}, theme_.panel);
  // These rows are read, not decoration, so they are at the same 12 point as the rest of the window
  // rather than 7 and 8, and each one advances the block by the height it needs. The offsets they
  // sat on (114, 138, 157, 182, 199, 216, 233) assumed one line each, so a unit id longer than the
  // column was cut and a taller row would have landed on the one below it.
  constexpr double kUnitInspectorText = 12.0;
  constexpr double kUnitInspectorLine = 16.0;
  constexpr double kUnitInspectorGap = 6.0;
  auto inspectorTop = 86.0;
  const auto unitRow = [&](const std::string& text, const Color& color) {
    // The longest id is 242 points at this size in a 214 point column, so it takes two lines. The row
    // is given both, and the rows below move down rather than being drawn over it.
    const auto lines = static_cast<double>(
        std::max<std::size_t>(1U, studioWrapWords(canvas, text, 214.0, kUnitInspectorText).size()));
    canvas.drawTextWrapped(
        ui::Rect{inspectorX + 12.0, inspectorTop, 214.0, lines * kUnitInspectorLine}, text, color,
        kUnitInspectorText, kUnitInspectorLine);
    inspectorTop += lines * kUnitInspectorLine + kUnitInspectorGap;
  };
  unitRow("UNIT INSPECTOR", theme_.secondaryText);
  unitRow(unit->id, theme_.primaryText);
  unitRow("ROOT MIDI " + std::to_string(unit->rootMidi), theme_.secondaryText);
  unitRow("RENDER " + std::string{voicebank::rendererHintName(unit->renderer)},
          theme_.secondaryText);
  unitRow("UP/DOWN UNIT", theme_.secondaryText);
  unitRow("DRAG MARKERS", theme_.secondaryText);
  unitRow("CTRL+S SAVE", theme_.secondaryText);
  unitRow("R RECORD TAKE", theme_.secondaryText);
  paintProductionInspector(canvas, controller, theme_, unit);
}

}  // namespace seam::native_ui
