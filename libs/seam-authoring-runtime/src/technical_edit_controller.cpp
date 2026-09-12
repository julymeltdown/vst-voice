#include "seam/authoring/technical_edit_controller.hpp"

#include "seam/application/command.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/application/render_commands.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"
#include "seam/synthesis/phoneme_timing_plan.hpp"

#include <algorithm>
#include <unordered_set>
#include <cmath>
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

namespace {

core::Result<void> validateProceduralBoundaryEdit(const domain::Project& project,
    const domain::VocalRegion& region, std::span<const domain::PhonemeToken> tokens, domain::NoteId noteId) {
  constexpr std::uint32_t rate = 48000U;
  const auto timing = synthesis::compilePhonemeTimingPlan(project, region, tokens, rate,
      synthesis::PhonemeTimingPolicy::ProceduralInNote);
  if (!timing) return core::Result<void>{timing.error()};
  const auto* note = region.findNote(noteId);
  if (!note) return core::failure(core::ErrorCode::NotFound, "Procedural timing note is missing");
  const auto origin = project.tempoMap().sampleFrameAt(region.startTick + note->startTick, rate);
  const auto noteEnd = project.tempoMap().sampleFrameAt(region.startTick + note->endTick(), rate);
  std::vector<std::pair<time::SampleFrame, time::SampleFrame>> spans;
  for (std::size_t i = 0U; i < tokens.size(); ++i) if (tokens[i].key.noteId == noteId) {
    const auto& anchor = timing.value()[i];
    const bool onset = tokens[i].role == domain::PhonemeRole::Onset;
    const auto start = onset ? anchor.explicitStartFrame.value_or(anchor.inferredStartFrame.value_or(-1)) : anchor.nucleusFrame;
    const auto end = onset && !anchor.endExplicit ? anchor.nucleusFrame : anchor.endFrame;
    if (start < origin || end > noteEnd || end <= start || (onset && end > anchor.nucleusFrame))
      return core::failure(core::ErrorCode::Conflict, "Procedural boundary crosses its nucleus or score note, or leaves an empty gesture");
    spans.emplace_back(start, end);
  }
  std::sort(spans.begin(), spans.end());
  for (std::size_t i = 1U; i < spans.size(); ++i) if (spans[i].first < spans[i - 1U].second)
    return core::failure(core::ErrorCode::Conflict, "Procedural boundary overlaps another gesture");
  return core::success();
}

struct TechnicalReviewPronunciation final {
  phonemizer::ResolvedPronunciation value;
  bool diagnosticFallback{false};
};

core::Result<TechnicalReviewPronunciation> resolveTechnicalReviewPronunciation(
    const domain::VocalRegion& region) {
  const auto resolved = phonemizer::resolvePronunciation(region);
  if (resolved) return core::success(TechnicalReviewPronunciation{resolved.value(), false});
  if (resolved.error().code != core::ErrorCode::Unsupported ||
      resolved.error().context != phonemizer::kMixedPronunciationLanguagesContext) {
    return core::Result<TechnicalReviewPronunciation>{resolved.error()};
  }
  // A mixed-language region has no single production pronunciation identity,
  // so the generic resolver correctly rejects it. Keep a diagnostic sequence
  // for inspection only; every note remains unavailable for rebinding. No
  // diagnostic identity may authorize a production edit or render.
  const auto diagnostic = phonemizer::resolveJapanesePronunciation(region);
  if (diagnostic) return core::success(TechnicalReviewPronunciation{diagnostic.value(), true});
  return core::Result<TechnicalReviewPronunciation>{resolved.error()};
}

core::Result<domain::PitchAutomationPoint> normalizePitchPoint(
    const domain::VocalRegion& region, domain::PitchAutomationPoint point) {
  if (!std::isfinite(point.cents)) {
    return core::failure<domain::PitchAutomationPoint>(
        core::ErrorCode::InvalidArgument,
        "Pitch automation cents must be finite");
  }
  point.cents = std::clamp(point.cents, -4800.0F, 4800.0F);
  point.tick = std::max(time::Tick{0}, point.tick);
  point.tick = std::min(region.durationTick, point.tick);
  return core::success(point);
}

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
  return phonemizer::inspectPronunciation(*current);
}

std::optional<TechnicalUnitView> TechnicalEditController::unitView(
    domain::PhonemeKey key) const {
  if (!renderViewProvider_) return std::nullopt;
  const auto tokens = phonemes();
  const auto view = renderViewProvider_();
  for (const auto& unit : view.units) {
    if (unit.entry.tokenStart >= tokens.tokens.size()) continue;
    const auto remaining = tokens.tokens.size() - unit.entry.tokenStart;
    const auto count = std::min(unit.entry.tokenCount, remaining);
    for (std::size_t offset = 0U; offset < count; ++offset) {
      if (tokens.tokens[unit.entry.tokenStart + offset].key == key) {
        return unit;
      }
    }
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

  const auto& project = document_->session().project();
  const auto& tracks = project.vocalTracks();
  const auto owner = std::find_if(tracks.begin(), tracks.end(), [&](const auto& track) { return track.findRegion(regionId_) != nullptr; });
  if (owner != tracks.end() && owner->proceduralRecipe) {
    if ((startBoundary ? token->timing.startOffset : token->timing.endOffset) == std::optional{offset}) return core::success();
    constexpr std::uint32_t rate = 48000U;
    const auto timing = synthesis::compilePhonemeTimingPlan(project, *current, generated.tokens, rate,
        synthesis::PhonemeTimingPolicy::ProceduralInNote);
    const bool inferred = timing && std::any_of(timing.value().begin(), timing.value().end(), [&](const auto& anchor) {
      return anchor.key.noteId == key.noteId && anchor.inferredStartFrame.has_value();
    });
    if (inferred) {
      // Freeze the note's dependent automatic boundaries in the same undo step.
      // Otherwise editing one group would move adjacent automatic vowel ends.
      const auto* note = current->findNote(key.noteId);
      const auto origin = project.tempoMap().sampleFrameAt(current->startTick + note->startTick, rate);
      const auto asOffset = [&](time::SampleFrame frame) {
        return static_cast<time::Microseconds>(std::llround(static_cast<double>(frame - origin) * 1000000.0 / rate));
      };
      auto tokens = generated.tokens;
      auto command = std::make_unique<application::CompositeCommand>("Edit inferred procedural timing");
      for (std::size_t i = 0U; i < tokens.size(); ++i) {
        if (tokens[i].key.noteId != key.noteId) continue;
        const auto& anchor = timing.value()[i];
        const auto start = anchor.explicitStartFrame.value_or(anchor.inferredStartFrame.value_or(anchor.nucleusFrame));
        const auto end = tokens[i].role == domain::PhonemeRole::Onset && !anchor.endExplicit ? anchor.nucleusFrame : anchor.endFrame;
        domain::PhonemeOverride edit{.key = tokens[i].key};
        if (const auto* existing = current->findPhonemeOverride(edit.key)) edit = *existing;
        edit.locked = true;
        edit.timing = {.startOffset = asOffset(start), .endOffset = asOffset(end)};
        if (edit.key == key) {
          if (startBoundary) edit.timing.startOffset = offset;
          else edit.timing.endOffset = offset;
        }
        const auto valid = edit.validate();
        if (!valid) return valid;
        tokens[i].timing = edit.timing;
        command->add(std::make_unique<application::UpsertPhonemeOverrideCommand>(regionId_, std::move(edit)));
      }
      const auto checked = validateProceduralBoundaryEdit(project, *current, tokens, key.noteId);
      if (!checked) return checked;
      return commit(std::move(command));
    }
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
  if (owner != tracks.end() && owner->proceduralRecipe) {
    auto tokens = generated.tokens;
    const auto index = static_cast<std::size_t>(token - generated.tokens.begin());
    tokens[index].timing = value.timing;
    const auto checked = validateProceduralBoundaryEdit(project, *current, tokens, key.noteId);
    if (!checked) return checked;
  }
  return commit(std::make_unique<application::UpsertPhonemeOverrideCommand>(
      regionId_, std::move(value)));
}

core::Result<void> TechnicalEditController::setPhonemeLocked(
    domain::PhonemeKey key, bool locked) {
  auto* current = region();
  if (current == nullptr || current->findNote(key.noteId) == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Phoneme lock target is missing");
  }
  const auto generated = phonemes();
  const auto token = std::find_if(
      generated.tokens.begin(), generated.tokens.end(),
      [key](const auto& value) { return value.key == key; });
  if (token == generated.tokens.end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Phoneme lock key is unavailable");
  }
  const auto* existing = current->findPhonemeOverride(key);
  if (existing == nullptr && !locked) return core::success();
  domain::PhonemeOverride value = existing != nullptr
                                      ? *existing
                                      : domain::PhonemeOverride{.key = key};
  value.locked = locked;
  return commit(std::make_unique<application::UpsertPhonemeOverrideCommand>(
      regionId_, std::move(value)));
}

core::Result<PhonemeBindingReview> TechnicalEditController::reviewPhonemeBindings() const {
  const auto* current = region();
  if (!current) return core::failure<PhonemeBindingReview>(core::ErrorCode::NotFound, "Phoneme review region is missing");
  if (current->phonemeOverrides.size() > 4096U) {
    return core::failure<PhonemeBindingReview>(core::ErrorCode::InvalidArgument, "Phoneme review exceeds bounds");
  }
  auto baseRegion = *current;
  baseRegion.phonemeOverrides.clear();
  const auto base = resolveTechnicalReviewPronunciation(baseRegion);
  if (!base) return core::Result<PhonemeBindingReview>{base.error()};
  PhonemeBindingReview review{regionId_, {}, base.value().value.pronunciation.tokens,
                             base.value().value.pronunciation.warnings};
  if (base.value().diagnosticFallback) review.targets.clear();
  std::unordered_set<domain::NoteId> unavailable;
  for (const auto& warning : review.warnings) unavailable.insert(warning.noteId);
  for (const auto& note : current->notes) {
    const auto* lyric = current->findLyric(note.lyricTokenId);
    if (!lyric || !phonemizer::hasPronunciationService(lyric->language) ||
        base.value().diagnosticFallback) {
      if (unavailable.insert(note.id).second) {
        review.warnings.push_back({.code = phonemizer::WarningCode::ResolutionFailure,
            .noteId = note.id, .characterIndex = 0U,
            .message = base.value().diagnosticFallback
                ? "Mixed-language pronunciation is inspection-only; use one language before rebinding"
                : "Rebinding requires a supported pronunciation language for this note"});
      }
    }
  }
  std::erase_if(review.targets, [&](const auto& token) { return unavailable.contains(token.key.noteId); });
  for (const auto& edit : current->phonemeOverrides) {
    if (base.value().diagnosticFallback || edit.unresolved || !edit.sourceContextId ||
        edit.sourceContextId != phonemizer::phonemeEditContextId(base.value().value, edit.key)) {
      review.retainedEdits.push_back(edit);
    }
  }
  return core::success(std::move(review));
}

core::Result<void> TechnicalEditController::rebindPhonemeOverride(
    const domain::PhonemeOverride& reviewed, domain::PhonemeKey target,
    std::string_view expectedTargetContext) {
  const auto* current = region();
  if (!current || expectedTargetContext.size() != 64U) {
    return core::failure(core::ErrorCode::InvalidArgument, "Phoneme rebinding requires a current target context");
  }
  const auto* existing = current->findPhonemeOverride(reviewed.key);
  if (!existing || *existing != reviewed) {
    return core::failure(core::ErrorCode::Conflict, "Retained phoneme edit changed since review");
  }
  if (target != reviewed.key && current->findPhonemeOverride(target)) {
    return core::failure(core::ErrorCode::Conflict, "Phoneme rebinding target already has an edit");
  }
  const auto review = reviewPhonemeBindings();
  if (!review) return core::Result<void>{review.error()};
  const auto chosen = std::find_if(review.value().targets.begin(), review.value().targets.end(),
      [&](const auto& token) { return token.key == target && token.contextId == expectedTargetContext; });
  if (chosen == review.value().targets.end()) {
    return core::failure(core::ErrorCode::Conflict, "Phoneme target context changed since review");
  }
  auto rebound = reviewed;
  rebound.key = target;
  rebound.unresolved = false;
  rebound.sourceContextId = std::string{expectedTargetContext};
  auto command = std::make_unique<application::CompositeCommand>("Rebind retained phoneme edit");
  if (target != reviewed.key) {
    command->add(std::make_unique<application::RemovePhonemeOverrideCommand>(regionId_, reviewed.key));
  }
  command->add(std::make_unique<application::UpsertPhonemeOverrideCommand>(regionId_, std::move(rebound)));
  return commit(std::move(command));
}

core::Result<RetainedRenderEditReview> TechnicalEditController::reviewRetainedRenderEdits() const {
  const auto* current = region();
  if (!current) return core::failure<RetainedRenderEditReview>(core::ErrorCode::NotFound, "Render edit review region is missing");
  if (current->unitSelectionOverrides.size() > 4096U || current->seamOverrides.size() > 4096U) {
    return core::failure<RetainedRenderEditReview>(core::ErrorCode::InvalidArgument, "Render edit review exceeds bounds");
  }
  const auto resolved = resolveTechnicalReviewPronunciation(*current);
  if (!resolved) return core::Result<RetainedRenderEditReview>{resolved.error()};
  RetainedRenderEditReview review{regionId_, resolved.value().value.identity, {}, {},
      resolved.value().value.pronunciation.tokens, resolved.value().value.pronunciation.warnings};
  for (const auto& edit : current->unitSelectionOverrides) if (edit.unresolved) review.units.push_back(edit);
  for (const auto& edit : current->seamOverrides) if (edit.unresolved) review.seams.push_back(edit);
  for (const auto& note : current->notes) {
    const auto* lyric = current->findLyric(note.lyricTokenId);
    if (!lyric || !phonemizer::hasPronunciationService(lyric->language) ||
        resolved.value().diagnosticFallback) {
      review.warnings.push_back({.code = phonemizer::WarningCode::ResolutionFailure,
          .noteId = note.id, .characterIndex = 0U,
          .message = resolved.value().diagnosticFallback
              ? "Mixed-language pronunciation is inspection-only; use one language before rebinding"
              : "Render edit rebinding requires a supported pronunciation language"});
    }
  }
  return core::success(std::move(review));
}

core::Result<void> TechnicalEditController::rebindUnitOverride(
    const RetainedRenderEditReview& review, const domain::UnitSelectionOverride& reviewed,
    domain::PhonemeKey target) {
  const auto fresh = reviewRetainedRenderEdits();
  if (!fresh) return core::Result<void>{fresh.error()};
  if (review.regionId != regionId_ || review.pronunciation != fresh.value().pronunciation ||
      review.tokens != fresh.value().tokens) {
    return core::failure(core::ErrorCode::Conflict, "Unit target pronunciation changed since review");
  }
  if (std::find(review.units.begin(), review.units.end(), reviewed) == review.units.end() ||
      std::find(fresh.value().units.begin(), fresh.value().units.end(), reviewed) == fresh.value().units.end()) {
    return core::failure(core::ErrorCode::Conflict, "Retained unit changed since review");
  }
  const auto& tokens = fresh.value().tokens;
  const auto chosen = std::find_if(tokens.begin(), tokens.end(), [&](const auto& token) { return token.key == target; });
  if (chosen == tokens.end() || reviewed.tokenCount == 0U ||
      reviewed.tokenCount > static_cast<std::size_t>(tokens.end() - chosen)) {
    return core::failure(core::ErrorCode::Conflict, "Unit target span is unavailable");
  }
  const auto end = chosen + reviewed.tokenCount;
  for (const auto& warning : fresh.value().warnings) {
    if (std::any_of(chosen, end, [&](const auto& token) { return token.key.noteId == warning.noteId; })) {
      return core::failure(core::ErrorCode::Conflict, "Unit target span has unresolved pronunciation");
    }
  }
  for (const auto& other : region()->unitSelectionOverrides) {
    if (other.startKey == reviewed.startKey) continue;
    const auto start = std::find_if(tokens.begin(), tokens.end(), [&](const auto& token) { return token.key == other.startKey; });
    if (start == tokens.end()) continue;
    const auto count = std::min<std::size_t>(other.tokenCount, static_cast<std::size_t>(tokens.end() - start));
    if (start < end && chosen < start + static_cast<std::ptrdiff_t>(count)) {
      return core::failure(core::ErrorCode::Conflict, "Unit target overlaps another retained or active edit");
    }
  }
  auto rebound = reviewed;
  rebound.startKey = target;
  rebound.unresolved = false;
  auto command = std::make_unique<application::CompositeCommand>("Rebind retained unit edit");
  if (target != reviewed.startKey) {
    command->add(std::make_unique<application::RemoveUnitSelectionOverrideCommand>(regionId_, reviewed.startKey));
  }
  command->add(std::make_unique<application::UpsertUnitSelectionOverrideCommand>(regionId_, std::move(rebound)));
  return commit(std::move(command));
}

core::Result<void> TechnicalEditController::rebindSeamOverride(
    const RetainedRenderEditReview& review, const domain::SeamOverride& reviewed,
    domain::PhonemeKey target) {
  const auto fresh = reviewRetainedRenderEdits();
  if (!fresh) return core::Result<void>{fresh.error()};
  if (review.regionId != regionId_ || review.pronunciation != fresh.value().pronunciation ||
      review.tokens != fresh.value().tokens) {
    return core::failure(core::ErrorCode::Conflict, "Seam target pronunciation changed since review");
  }
  if (std::find(review.seams.begin(), review.seams.end(), reviewed) == review.seams.end() ||
      std::find(fresh.value().seams.begin(), fresh.value().seams.end(), reviewed) == fresh.value().seams.end()) {
    return core::failure(core::ErrorCode::Conflict, "Retained seam changed since review");
  }
  const auto& tokens = fresh.value().tokens;
  const auto chosen = std::find_if(tokens.begin(), tokens.end(), [&](const auto& token) { return token.key == target; });
  if (chosen == tokens.end()) return core::failure(core::ErrorCode::Conflict, "Seam target is unavailable");
  for (const auto& warning : fresh.value().warnings) {
    if (warning.noteId == chosen->key.noteId ||
        (chosen != tokens.begin() && warning.noteId == std::prev(chosen)->key.noteId)) {
      return core::failure(core::ErrorCode::Conflict, "Seam target or predecessor has unresolved pronunciation");
    }
  }
  if (target != reviewed.incomingStartKey && region()->findSeamOverride(target)) {
    return core::failure(core::ErrorCode::Conflict, "Seam target already has an edit");
  }
  auto rebound = reviewed;
  rebound.incomingStartKey = target;
  rebound.unresolved = false;
  auto command = std::make_unique<application::CompositeCommand>("Rebind retained seam edit");
  if (target != reviewed.incomingStartKey) {
    command->add(std::make_unique<application::RemoveSeamOverrideCommand>(regionId_, reviewed.incomingStartKey));
  }
  command->add(std::make_unique<application::UpsertSeamOverrideCommand>(regionId_, std::move(rebound)));
  return commit(std::move(command));
}

core::Result<void> TechnicalEditController::resetPhonemeOverride(
    domain::PhonemeKey key) {
  auto* current = region();
  if (current == nullptr || current->findNote(key.noteId) == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Phoneme reset target is missing");
  }
  if (current->findPhonemeOverride(key) == nullptr) return core::success();
  return commit(std::make_unique<application::RemovePhonemeOverrideCommand>(
      regionId_, key));
}

core::Result<void> TechnicalEditController::resetPhonemeOverrides(
    const std::vector<domain::PhonemeKey>& keys) {
  auto* current = region();
  if (current == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Phoneme reset region is missing");
  }
  auto command = std::make_unique<application::CompositeCommand>(
      "Reset phoneme overrides");
  std::vector<domain::PhonemeKey> seen;
  for (const auto key : keys) {
    if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
    seen.push_back(key);
    if (current->findPhonemeOverride(key) != nullptr) {
      command->add(std::make_unique<application::RemovePhonemeOverrideCommand>(
          regionId_, key));
    }
  }
  if (seen.empty() || command->impact().noteIds.empty()) return core::success();
  return commit(std::move(command));
}

core::Result<void> TechnicalEditController::resetPhonemeRegion() {
  const auto* current = region();
  if (current == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Phoneme reset region is missing");
  }
  std::vector<domain::PhonemeKey> keys;
  keys.reserve(current->phonemeOverrides.size());
  for (const auto& value : current->phonemeOverrides) keys.push_back(value.key);
  return resetPhonemeOverrides(keys);
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
  const auto tokens = phonemes();
  if (selectedView->entry.tokenStart >= tokens.tokens.size()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Selected Unit start phoneme is unavailable");
  }
  const auto startKey = tokens.tokens[selectedView->entry.tokenStart].key;
  std::vector<std::string> allowed{selectedView->entry.unitId};
  allowed.insert(allowed.end(), selectedView->entry.alternatives.begin(),
                 selectedView->entry.alternatives.end());
  std::sort(allowed.begin(), allowed.end());
  allowed.erase(std::unique(allowed.begin(), allowed.end()), allowed.end());
  if (!std::binary_search(allowed.begin(), allowed.end(), unitId)) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Selected Unit is not an available variant", unitId);
  }
  const auto tokenCount = static_cast<std::uint16_t>(
      std::clamp<std::size_t>(selectedView->entry.tokenCount, 1U, 65535U));
  domain::UnitSelectionOverride value{
      .startKey = startKey,
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
  const auto tokens = phonemes();
  if (selectedView->entry.tokenStart >= tokens.tokens.size()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Selected Unit start phoneme is unavailable");
  }
  const auto startKey = tokens.tokens[selectedView->entry.tokenStart].key;
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
    if (const auto* value = currentRegion->findUnitSelectionOverride(startKey)) {
      currentId = value->unitId;
      rendererKind = value->renderer;
    }
  }
  const auto current = std::find(choices.begin(), choices.end(), currentId);
  const auto next = current == choices.end() || std::next(current) == choices.end()
                        ? choices.begin()
                        : std::next(current);
  return selectUnitVariant(startKey, *next, rendererKind);
}

core::Result<void> TechnicalEditController::cycleUnitRenderer(
    domain::PhonemeKey key) {
  auto selectedView = unitView(key);
  if (!selectedView.has_value()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Unit plan entry is unavailable for renderer cycling");
  }
  const auto tokens = phonemes();
  if (selectedView->entry.tokenStart >= tokens.tokens.size()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Selected Unit start phoneme is unavailable");
  }
  const auto startKey = tokens.tokens[selectedView->entry.tokenStart].key;
  std::string currentId = selectedView->entry.unitId;
  auto currentRenderer = selectedView->entry.renderer;
  if (const auto* currentRegion = region(); currentRegion != nullptr) {
    if (const auto* value = currentRegion->findUnitSelectionOverride(startKey)) {
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
  return selectUnitVariant(startKey, std::move(currentId), next);
}

core::Result<void> TechnicalEditController::resetUnitSelection(
    domain::PhonemeKey key) {
  auto* current = region();
  if (current == nullptr || current->findNote(key.noteId) == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Unit reset target is missing");
  }
  auto selectedView = unitView(key);
  if (selectedView.has_value()) {
    const auto tokens = phonemes();
    if (selectedView->entry.tokenStart < tokens.tokens.size()) {
      key = tokens.tokens[selectedView->entry.tokenStart].key;
    }
  }
  if (current->findUnitSelectionOverride(key) == nullptr) {
    return core::success();
  }
  return commit(std::make_unique<application::RemoveUnitSelectionOverrideCommand>(
      regionId_, key));
}

core::Result<void> TechnicalEditController::resetUnitSelections(
    const std::vector<domain::PhonemeKey>& keys) {
  auto* current = region();
  if (current == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Unit reset region is missing");
  }
  auto command = std::make_unique<application::CompositeCommand>(
      "Reset unit selections");
  std::vector<domain::PhonemeKey> seen;
  for (const auto key : keys) {
    if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
    seen.push_back(key);
    if (current->findUnitSelectionOverride(key) != nullptr) {
      command->add(
          std::make_unique<application::RemoveUnitSelectionOverrideCommand>(
              regionId_, key));
    }
  }
  if (seen.empty() || command->impact().noteIds.empty()) return core::success();
  return commit(std::move(command));
}

core::Result<void> TechnicalEditController::resetUnitRegion() {
  const auto* current = region();
  if (current == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Unit reset region is missing");
  }
  std::vector<domain::PhonemeKey> keys;
  keys.reserve(current->unitSelectionOverrides.size());
  for (const auto& value : current->unitSelectionOverrides) {
    keys.push_back(value.startKey);
  }
  return resetUnitSelections(keys);
}

core::Result<void> TechnicalEditController::upsertPitchPoint(
    domain::PitchAutomationPoint point) {
  const auto* current = region();
  if (current == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Pitch automation region is missing");
  }
  auto normalized = normalizePitchPoint(*current, point);
  if (!normalized) return core::Result<void>{normalized.error()};
  return commit(
      std::make_unique<application::UpsertPitchAutomationPointCommand>(
          regionId_, normalized.value()));
}

core::Result<void> TechnicalEditController::movePitchPoint(
    time::Tick from, domain::PitchAutomationPoint point) {
  const auto* current = region();
  if (current == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Pitch automation region is missing");
  }
  auto normalized = normalizePitchPoint(*current, point);
  if (!normalized) return core::Result<void>{normalized.error()};
  auto command = std::make_unique<application::CompositeCommand>(
      "Move pitch automation point");
  command->add(
      std::make_unique<application::RemovePitchAutomationPointCommand>(
          regionId_, from));
  command->add(
      std::make_unique<application::UpsertPitchAutomationPointCommand>(
          regionId_, normalized.value()));
  return commit(std::move(command));
}

core::Result<void> TechnicalEditController::removePitchPoint(time::Tick tick) {
  return commit(
      std::make_unique<application::RemovePitchAutomationPointCommand>(
          regionId_, tick));
}

core::Result<void> TechnicalEditController::resetPitchSegment(
    time::Tick start, time::Tick end) {
  if (start > end) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Pitch reset segment is reversed");
  }
  const auto* current = region();
  if (current == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Pitch reset region is missing");
  }
  auto command = std::make_unique<application::CompositeCommand>(
      "Reset pitch segment");
  std::size_t count = 0U;
  for (const auto& point : current->pitchAutomation.points()) {
    if (point.tick < start || point.tick > end) continue;
    command->add(
        std::make_unique<application::RemovePitchAutomationPointCommand>(
            regionId_, point.tick));
    ++count;
  }
  if (count == 0U) return core::success();
  return commit(std::move(command));
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
