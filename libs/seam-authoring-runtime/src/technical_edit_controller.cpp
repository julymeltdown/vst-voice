#include "seam/authoring/technical_edit_controller.hpp"

#include "seam/application/command.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"

#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>

namespace seam::authoring {

TechnicalEditController::TechnicalEditController(
    ProjectDocument& document, domain::RegionId regionId,
    RenderViewProvider renderViewProvider,
    EditCommittedCallback editCommitted)
    : document_(&document),
      regionId_(regionId),
      renderViewProvider_(std::move(renderViewProvider)),
      editCommitted_(std::move(editCommitted)) {}

const domain::VocalRegion* TechnicalEditController::region() const noexcept {
  return document_->session().project().findRegion(regionId_);
}

domain::VocalRegion* TechnicalEditController::region() noexcept {
  return document_->session().project().findRegion(regionId_);
}

void TechnicalEditController::notifyEdit() const {
  if (editCommitted_) editCommitted_();
}

core::Result<void> TechnicalEditController::commit(
    std::unique_ptr<application::ICommand> command) {
  const auto result = document_->execute(std::move(command));
  if (result) notifyEdit();
  return result;
}

phonemizer::Result TechnicalEditController::phonemes() const {
  const auto* current = region();
  if (current == nullptr) return {};
  phonemizer::JapaneseKanaPhonemizer engine;
  return engine.phonemize(*current);
}

std::optional<TechnicalUnitView> TechnicalEditController::unitView(
    domain::PhonemeKey key) const {
  if (!renderViewProvider_) return std::nullopt;
  const auto tokens = phonemes();
  const auto view = renderViewProvider_();
  for (const auto& unit : view.units) {
    if (unit.entry.tokenStart >= tokens.tokens.size()) continue;
    if (tokens.tokens[unit.entry.tokenStart].key == key) return unit;
  }
  return std::nullopt;
}

std::optional<TechnicalUnitView> TechnicalEditController::unitDiagnostic(
    domain::PhonemeKey key) const {
  return unitView(key);
}

core::Result<void> TechnicalEditController::movePhonemeBoundary(
    domain::PhonemeKey key, bool startBoundary,
    time::Microseconds offset) {
  auto* current = region();
  if (current == nullptr || current->findNote(key.noteId) == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Phoneme boundary target is missing");
  }
  const auto generated = phonemes();
  const auto token = std::find_if(
      generated.tokens.begin(), generated.tokens.end(),
      [key](const auto& value) { return value.key == key; });
  if (token == generated.tokens.end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Phoneme boundary key is unavailable");
  }

  domain::PhonemeOverride value{};
  value.key = key;
  value.locked = true;
  if (const auto* existing = current->findPhonemeOverride(key)) value = *existing;
  if (startBoundary) value.timing.startOffset = offset;
  else value.timing.endOffset = offset;
  if (value.timing.startOffset.has_value() &&
      value.timing.endOffset.has_value() &&
      *value.timing.startOffset >= *value.timing.endOffset) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Phoneme start boundary must precede its end boundary");
  }
  return commit(std::make_unique<application::UpsertPhonemeOverrideCommand>(
      regionId_, std::move(value)));
}

core::Result<void> TechnicalEditController::selectUnitVariant(
    domain::PhonemeKey key, std::string unitId,
    domain::UnitRendererKind rendererKind) {
  if (unitId.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Selected Unit ID cannot be empty");
  }
  auto selectedView = unitView(key);
  if (!selectedView.has_value()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Unit plan entry is unavailable for this phoneme");
  }
  std::vector<std::string> allowed{selectedView->entry.unitId};
  allowed.insert(allowed.end(), selectedView->entry.alternatives.begin(),
                 selectedView->entry.alternatives.end());
  std::sort(allowed.begin(), allowed.end());
  allowed.erase(std::unique(allowed.begin(), allowed.end()), allowed.end());
  if (!std::binary_search(allowed.begin(), allowed.end(), unitId)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Selected Unit is not an available variant", unitId);
  }
  const auto tokens = phonemes();
  if (selectedView->entry.tokenStart >= tokens.tokens.size() ||
      tokens.tokens[selectedView->entry.tokenStart].key != key) {
    return core::failure(core::ErrorCode::NotFound,
                         "Selected Unit start phoneme is unavailable");
  }
  const auto tokenCount = static_cast<std::uint16_t>(
      std::clamp<std::size_t>(selectedView->entry.tokenCount, 1U, 65535U));
  domain::UnitSelectionOverride value{
      .startKey = key,
      .tokenCount = tokenCount,
      .unitId = std::move(unitId),
      .renderer = rendererKind,
      .locked = true,
  };
  return commit(
      std::make_unique<application::UpsertUnitSelectionOverrideCommand>(
          regionId_, std::move(value)));
}

core::Result<void> TechnicalEditController::cycleUnitVariant(
    domain::PhonemeKey key) {
  auto selectedView = unitView(key);
  if (!selectedView.has_value()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Unit plan entry is unavailable for this phoneme");
  }
  std::vector<std::string> choices{selectedView->entry.unitId};
  choices.insert(choices.end(), selectedView->entry.alternatives.begin(),
                 selectedView->entry.alternatives.end());
  std::sort(choices.begin(), choices.end());
  choices.erase(std::unique(choices.begin(), choices.end()), choices.end());
  if (choices.size() < 2U) {
    return core::failure(core::ErrorCode::Conflict,
                         "No alternative Unit is available for this boundary");
  }

  std::string currentId = selectedView->entry.unitId;
  auto rendererKind = selectedView->entry.renderer;
  if (const auto* currentRegion = region(); currentRegion != nullptr) {
    if (const auto* value = currentRegion->findUnitSelectionOverride(key)) {
      currentId = value->unitId;
      rendererKind = value->renderer;
    }
  }
  const auto current = std::find(choices.begin(), choices.end(), currentId);
  const auto next = current == choices.end() || std::next(current) == choices.end()
                        ? choices.begin()
                        : std::next(current);
  return selectUnitVariant(key, *next, rendererKind);
}

core::Result<void> TechnicalEditController::cycleUnitRenderer(
    domain::PhonemeKey key) {
  auto selectedView = unitView(key);
  if (!selectedView.has_value()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Unit plan entry is unavailable for renderer cycling");
  }
  std::string currentId = selectedView->entry.unitId;
  auto currentRenderer = selectedView->entry.renderer;
  if (const auto* currentRegion = region(); currentRegion != nullptr) {
    if (const auto* value = currentRegion->findUnitSelectionOverride(key)) {
      currentId = value->unitId;
      currentRenderer = value->renderer;
    }
  }
  domain::UnitRendererKind next = domain::UnitRendererKind::Raw;
  switch (currentRenderer) {
    case domain::UnitRendererKind::Inherit:
    case domain::UnitRendererKind::Raw:
      next = domain::UnitRendererKind::ClassicPsola;
      break;
    case domain::UnitRendererKind::ClassicPsola:
      next = domain::UnitRendererKind::SpectralClassic;
      break;
    case domain::UnitRendererKind::SpectralClassic:
      next = domain::UnitRendererKind::Stretch;
      break;
    case domain::UnitRendererKind::Stretch:
      next = domain::UnitRendererKind::Raw;
      break;
  }
  return selectUnitVariant(key, std::move(currentId), next);
}

core::Result<void> TechnicalEditController::upsertPitchPoint(
    domain::PitchAutomationPoint point) {
  return commit(
      std::make_unique<application::UpsertPitchAutomationPointCommand>(
          regionId_, point));
}

core::Result<void> TechnicalEditController::movePitchPoint(
    time::Tick from, domain::PitchAutomationPoint point) {
  auto command = std::make_unique<application::CompositeCommand>(
      "Move pitch automation point");
  command->add(
      std::make_unique<application::RemovePitchAutomationPointCommand>(
          regionId_, from));
  command->add(
      std::make_unique<application::UpsertPitchAutomationPointCommand>(
          regionId_, point));
  return commit(std::move(command));
}

core::Result<void> TechnicalEditController::removePitchPoint(time::Tick tick) {
  return commit(
      std::make_unique<application::RemovePitchAutomationPointCommand>(
          regionId_, tick));
}

core::Result<void> TechnicalEditController::cyclePitchInterpolation(
    time::Tick tick) {
  const auto* current = region();
  if (current == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Pitch automation region is missing");
  }
  const auto iterator = std::find_if(
      current->pitchAutomation.points().begin(),
      current->pitchAutomation.points().end(),
      [tick](const auto& point) { return point.tick == tick; });
  if (iterator == current->pitchAutomation.points().end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Pitch automation point is missing");
  }
  auto updated = *iterator;
  switch (updated.interpolation) {
    case domain::CurveInterpolation::Step:
      updated.interpolation = domain::CurveInterpolation::Linear;
      break;
    case domain::CurveInterpolation::Linear:
      updated.interpolation = domain::CurveInterpolation::Smooth;
      break;
    case domain::CurveInterpolation::Smooth:
      updated.interpolation = domain::CurveInterpolation::Step;
      break;
  }
  return upsertPitchPoint(updated);
}

core::Result<void> TechnicalEditController::upsertSeam(
    domain::SeamOverride seam) {
  return commit(std::make_unique<application::UpsertSeamOverrideCommand>(
      regionId_, std::move(seam)));
}

core::Result<void> TechnicalEditController::removeSeam(
    domain::PhonemeKey key) {
  return commit(std::make_unique<application::RemoveSeamOverrideCommand>(
      regionId_, key));
}

core::Result<void> TechnicalEditController::undo() {
  const auto result = document_->undo();
  if (result) notifyEdit();
  return result;
}

core::Result<void> TechnicalEditController::redo() {
  const auto result = document_->redo();
  if (result) notifyEdit();
  return result;
}

}  // namespace seam::authoring
