#include "seam/native_ui/voicebank_studio.hpp"

#include "voicebank_studio_production_view.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/platform/file_dialog.hpp"
#include "seam/voicebank_production/project_codec.hpp"
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
  std::vector<ui::Rect> result;
  result.reserve(markers.size());
  std::vector<double> rowRights;
  for (const auto& marker : markers) {
    const auto displayWidth =
        static_cast<double>(text::utf8DisplayWidth(marker.label));
    const auto estimatedWidth = std::max(
        12.0, displayWidth * 3.8 + 4.0);
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
    result.push_back(ui::Rect{x, waveformBounds.y + 2.0 +
                                      static_cast<double>(row) * 9.0,
                              width, 7.0});
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
    const std::filesystem::path& path, std::int32_t expectedRootMidi) {
  if (proceduralImportBusy()) return core::failure(core::ErrorCode::Conflict, "Candidate import is busy");
  auto inspected = voicebank::inspectDryTake(path, expectedRootMidi);
  if (!inspected) {
    takeInspection_.reset();
    status_ = "TAKE ERROR";
    return core::Result<void>{inspected.error()};
  }
  takeInspection_ = std::move(inspected.value());
  status_ = takeInspection_->accepted() ? "TAKE ACCEPTED" : "TAKE REVIEW";
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
  formats::JsonValue::Object quality{
      {"formatValid", formats::JsonValue{inspection.formatValid}},
      {"finite", formats::JsonValue{inspection.finite}},
      {"clippingFree", formats::JsonValue{inspection.clippingFree}},
      {"silenceFree", formats::JsonValue{inspection.silenceFree}},
      {"dcOffsetFree", formats::JsonValue{inspection.dcOffsetFree}},
      {"rootPitchValid", formats::JsonValue{inspection.rootPitchValid}},
      {"peak", formats::JsonValue{static_cast<double>(inspection.peak)}},
      {"rms", formats::JsonValue{inspection.rms}},
      {"dcOffset", formats::JsonValue{inspection.dcOffset}},
  };
  if (inspection.analyzedRootMidi.has_value()) {
    quality.emplace("analyzedRootMidi", formats::JsonValue{
        static_cast<std::int64_t>(*inspection.analyzedRootMidi)});
  }
  formats::JsonValue::Object record{
      {"schemaVersion", formats::JsonValue{std::int64_t{1}}},
      {"takeFile", formats::JsonValue{takePath.filename().generic_string()}},
      {"takeSha256", formats::JsonValue{inspection.sourceSha256}},
      {"status", formats::JsonValue{
          inspection.accepted() ? "ACCEPTED" : "REVIEW"}},
      {"sampleRate", formats::JsonValue{
          static_cast<std::int64_t>(inspection.sampleRate)}},
      {"channels", formats::JsonValue{
          static_cast<std::int64_t>(inspection.channels)}},
      {"bitsPerSample", formats::JsonValue{
          static_cast<std::int64_t>(inspection.bitsPerSample)}},
      {"expectedRootMidi", formats::JsonValue{
          static_cast<std::int64_t>(inspection.expectedRootMidi)}},
      {"quality", formats::JsonValue{std::move(quality)}},
  };
  const auto written = core::durableAtomicWriteText(
      sidecar, formats::stringifyJson(
                   formats::JsonValue{std::move(record)}, true));
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
                  theme_.primaryText, 14.0);
  const auto* productionProject = controller.productionProject();
  const auto displayName = !controller.manifest().displayName.empty()
                               ? controller.manifest().displayName
                               : productionProject != nullptr
                                     ? productionProject->projectId
                                     : std::string{"NO PROJECT"};
  canvas.drawText(ui::Rect{18.0, 34.0, std::max(0.0, width - 396.0), 16.0},
                  displayName, theme_.secondaryText, 8.0);
  if (!controller.manifest().characterId.empty()) {
    canvas.drawText(ui::Point{18.0, 57.0},
                    "CHARACTER " + controller.manifest().characterId + " @ " +
                        controller.manifest().characterVersion,
                    theme_.secondaryText, 6.0);
  }
  canvas.drawText(ui::Point{width - 360.0, 18.0},
                  recording ? "RECORDING" : controller.status(),
                  recording ? theme_.accent : theme_.secondaryText, 8.0);
  canvas.drawText(ui::Point{width - 360.0, 40.0},
                  "MIC " + std::string{recordingBackend}, theme_.secondaryText, 7.0);

  canvas.fillRect(ui::Rect{0.0, 72.0, 252.0, height - 72.0}, theme_.panelAlternate);
  canvas.drawText(ui::Point{12.0, 86.0}, "UNITS", theme_.secondaryText, 8.0);
  const auto& units = controller.manifest().units;
  if (units.empty()) {
    paintProductionAssignmentRail(canvas, controller, theme_);
  } else {
    const auto first = controller.selectedIndex() > 8U ? controller.selectedIndex() - 8U : 0U;
    const auto last = std::min(
        units.size(), first + voicebankStudioUnitRailVisibleRows(height, false));
    for (std::size_t index = first; index < last; ++index) {
      const auto y = 108.0 + static_cast<double>(index - first) * 32.0;
      const ui::Rect row{8.0, y, 236.0, 28.0};
      canvas.fillRect(row, index == controller.selectedIndex() ? theme_.selected
                                                               : theme_.panelAlternate);
      if (index == controller.selectedIndex()) canvas.strokeRect(row, theme_.accent, 1.0);
      auto label = units[index].alias.empty() ? units[index].id : units[index].alias;
      if (label.size() > 24U) label.resize(24U);
      canvas.drawText(ui::Point{16.0, y + 7.0}, label, theme_.primaryText, 7.0);
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
  for (std::size_t index = 0U; index < microscope.markers().size(); ++index) {
    const auto& marker = microscope.markers()[index];
    canvas.line(ui::Point{marker.x, wave.y}, ui::Point{marker.x, spec.bottom()},
                marker.kind == ui::AcousticMarkerKind::VowelOnset ? theme_.accent
                                                                  : theme_.grid, 1.0);
    canvas.drawText(ui::Point{labelBounds[index].x, labelBounds[index].y + 5.0},
                    marker.label,
                    theme_.secondaryText, 6.0);
  }
  for (const auto& mark : microscope.pitchMarks()) {
    canvas.line(ui::Point{mark.x, wave.y}, ui::Point{mark.x, wave.y + 18.0},
                mark.locked ? theme_.accent : theme_.pitch, 1.0);
  }

  const auto inspectorX = width - 238.0;
  canvas.fillRect(ui::Rect{inspectorX, 72.0, 238.0, height - 72.0}, theme_.panel);
  canvas.drawText(ui::Point{inspectorX + 12.0, 88.0}, "UNIT INSPECTOR",
                  theme_.secondaryText, 8.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, 114.0}, unit->id,
                  theme_.primaryText, 7.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, 138.0},
                  "ROOT MIDI " + std::to_string(unit->rootMidi), theme_.secondaryText, 7.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, 157.0},
                  "RENDER " + std::string{voicebank::rendererHintName(unit->renderer)},
                  theme_.secondaryText, 7.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, 182.0}, "UP/DOWN UNIT",
                  theme_.secondaryText, 7.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, 199.0}, "DRAG MARKERS",
                  theme_.secondaryText, 7.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, 216.0}, "CTRL+S SAVE",
                  theme_.secondaryText, 7.0);
  canvas.drawText(ui::Point{inspectorX + 12.0, 233.0}, "R RECORD TAKE",
                  theme_.secondaryText, 7.0);
  paintProductionInspector(canvas, controller, theme_, unit);
}

}  // namespace seam::native_ui
