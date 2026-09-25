#include "seam/application/performance_commands.hpp"
#include "seam/phonemizer/language_resolver.hpp"

#include <cstddef>
#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <unordered_map>
#include <utility>

namespace seam::application {

namespace {

// The interval one accepted selection covers. A note scope resolves through the
// current region, so a decision is composed against the notes that exist now rather
// than against a stale identity.
std::optional<domain::PerformanceTimeRange> acceptedSpan(const domain::VocalRegion& region,
    const domain::AcceptedPerformanceSelection& selection) {
  if (const auto* noteId = std::get_if<domain::NoteId>(&selection.scope)) {
    const auto* note = region.findNote(*noteId);
    if (note == nullptr) return std::nullopt;
    return domain::PerformanceTimeRange{note->startTick, note->endTick()};
  }
  return std::get<domain::PerformanceTimeRange>(selection.scope);
}

bool spansMeet(const domain::PerformanceTimeRange& left,
    const domain::PerformanceTimeRange& right) {
  return left.startTick < right.endTick && right.startTick < left.endTick;
}

}  // namespace

AddPerformanceProposalCommand::AddPerformanceProposalCommand(domain::RegionId regionId,
    domain::RegionPerformanceState expected, domain::PerformanceTake proposal)
    : regionId_(regionId), before_(std::move(expected)), proposal_(std::move(proposal)) {}

CommandImpact AddPerformanceProposalCommand::impact() const {
  return {.scope = CommandAudioImpact::MetadataOnly, .projectWide = false,
          .trackIds = {}, .regionIds = {regionId_}, .noteIds = {}, .lyricIds = {}};
}

core::Result<void> AddPerformanceProposalCommand::apply(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (!region) return core::failure(core::ErrorCode::NotFound, "Performance region was not found");
  if (region->performance != before_) return core::failure(core::ErrorCode::Conflict,
      "Performance changed before proposal delivery");
  const auto valid = region->validate();
  if (!valid) return valid;
  if (!after_) {
    const auto proposalValid = proposal_.validate();
    if (!proposalValid) return proposalValid;
    if (before_.takes.size() >= domain::kMaximumPerformanceTakes ||
        proposal_.state != domain::PerformanceProposalState::Proposed ||
        proposal_.sourceRegionId != regionId_ || proposal_.range.endTick > region->durationTick ||
        std::any_of(before_.takes.begin(), before_.takes.end(),
            [&](const auto& take) { return take.id == proposal_.id; })) {
      return core::failure(core::ErrorCode::Conflict, "Proposal identity, source or capacity is invalid", proposal_.id);
    }
    const auto current = domain::validatePerformanceAcceptanceRevision(proposal_.capturedRevision, before_.revision);
    if (!current) return current;
    const auto pronunciation = phonemizer::resolvePronunciation(*region);
    if (!pronunciation) return core::Result<void>{pronunciation.error()};
    if (proposal_.pronunciation != pronunciation.value().identity) return core::failure(core::ErrorCode::Conflict,
        "Proposal pronunciation no longer matches the source", proposal_.id);
    auto next = before_;
    next.takes.push_back(proposal_);
    const auto nextValid = next.validate(region->notes, region->durationTick);
    if (!nextValid) return nextValid;
    after_ = std::move(next);
  }
  const auto afterValid = after_->validate(region->notes, region->durationTick);
  if (!afterValid) return afterValid;
  region->performance = *after_;
  return core::success();
}

core::Result<void> AddPerformanceProposalCommand::revert(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (!region) return core::failure(core::ErrorCode::NotFound, "Performance region was not found");
  if (!after_ || region->performance != *after_) return core::failure(core::ErrorCode::Conflict,
      "Cannot undo proposal delivery over changed state");
  const auto valid = before_.validate(region->notes, region->durationTick);
  if (!valid) return valid;
  region->performance = before_;
  return core::success();
}

SetAcceptedPerformanceCommand::SetAcceptedPerformanceCommand(domain::RegionId regionId,
    domain::RegionPerformanceState expected,
    std::vector<domain::AcceptedPerformanceSelection> selections,
    PerformanceAcceptanceMode mode)
    : regionId_(regionId), before_(std::move(expected)), selections_(std::move(selections)),
      mode_(mode) {}

CommandImpact SetAcceptedPerformanceCommand::impact() const {
  return {.scope = CommandAudioImpact::PhraseAudio, .projectWide = false,
          .trackIds = {}, .regionIds = {regionId_}, .noteIds = {}, .lyricIds = {}};
}

core::Result<void> SetAcceptedPerformanceCommand::apply(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (!region) return core::failure(core::ErrorCode::NotFound, "Performance region was not found");
  if (region->performance != before_) return core::failure(core::ErrorCode::Conflict,
      "Performance changed since the take selection was prepared");
  const auto valid = region->validate();
  if (!valid) return valid;
  if (!after_) {
    if (selections_.size() > domain::kMaximumPerformanceSelections) {
      return core::failure(core::ErrorCode::InvalidArgument, "Performance selection exceeds bounds");
    }
    auto next = before_;
    std::vector<domain::AcceptedPerformanceSelection> accepted;
    if (mode_ == PerformanceAcceptanceMode::Merge) {
      // A merge keeps every existing selection the new decision does not cover, and
      // lets each new selection replace only what meets it on the same channel.
      for (const auto& existing : before_.accepted) {
        const auto span = acceptedSpan(*region, existing);
        if (!span) {
          return core::failure(core::ErrorCode::InvariantViolation,
              "Accepted performance references a missing note");
        }
        const bool replaced = std::any_of(selections_.begin(), selections_.end(),
            [&](const auto& addition) {
              if (addition.channel != existing.channel) return false;
              const auto additionSpan = acceptedSpan(*region, addition);
              return additionSpan.has_value() && spansMeet(*span, *additionSpan);
            });
        if (!replaced) accepted.push_back(existing);
      }
    }
    accepted.insert(accepted.end(), selections_.begin(), selections_.end());
    if (accepted.size() > domain::kMaximumPerformanceSelections) {
      return core::failure(core::ErrorCode::InvalidArgument,
          "Performance selection exceeds bounds");
    }
    next.accepted = std::move(accepted);
    // Deciding a take is not a manual ownership edit, so it deliberately does not
    // advance the ownership revision. Advancing it here made every sibling proposal
    // captured at the same musical revision unusable the moment one of them was
    // accepted, which is exactly the alternate-take workflow this state exists for.
    // Manual ownership edits still advance the axis, so a take generated before the
    // creator locked a channel is still refused.
    const auto selectionValid = next.validate(region->notes, region->durationTick);
    if (!selectionValid) return selectionValid;
    std::optional<domain::PronunciationIdentity> pronunciation;
    for (const auto& selection : selections_) {
      if (std::find(before_.accepted.begin(), before_.accepted.end(), selection) != before_.accepted.end()) continue;
      const auto take = std::find_if(before_.takes.begin(), before_.takes.end(),
          [&](const auto& value) { return value.id == selection.takeId; });
      if (take == before_.takes.end()) {
        return core::failure(core::ErrorCode::NotFound,
            "Accepted performance references an absent take", selection.takeId);
      }
      const auto current = domain::validatePerformanceAcceptanceRevision(take->capturedRevision, before_.revision);
      if (!current) return current;
      if (!pronunciation) {
        const auto resolved = phonemizer::resolvePronunciation(*region);
        if (!resolved) return core::Result<void>{resolved.error()};
        pronunciation = resolved.value().identity;
      }
      if (take->sourceRegionId != regionId_ || take->pronunciation != *pronunciation) {
        return core::failure(core::ErrorCode::Conflict,
            "Performance take no longer matches its source pronunciation", take->id);
      }
    }
    after_ = std::move(next);
  }
  const auto afterValid = after_->validate(region->notes, region->durationTick);
  if (!afterValid) return afterValid;
  region->performance = *after_;
  return core::success();
}

core::Result<void> SetAcceptedPerformanceCommand::revert(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (!region) return core::failure(core::ErrorCode::NotFound, "Performance region was not found");
  if (!after_ || region->performance != *after_) return core::failure(core::ErrorCode::Conflict,
      "Cannot undo a performance selection over changed state");
  const auto valid = before_.validate(region->notes, region->durationTick);
  if (!valid) return valid;
  region->performance = before_;
  return core::success();
}

RejectPerformanceProposalCommand::RejectPerformanceProposalCommand(domain::RegionId regionId,
    domain::RegionPerformanceState expected, std::string takeId)
    : regionId_(regionId), before_(std::move(expected)), takeId_(std::move(takeId)) {}

CommandImpact RejectPerformanceProposalCommand::impact() const {
  return {.scope = CommandAudioImpact::PhraseAudio, .projectWide = false,
          .trackIds = {}, .regionIds = {regionId_}, .noteIds = {}, .lyricIds = {}};
}

core::Result<void> RejectPerformanceProposalCommand::apply(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (!region) return core::failure(core::ErrorCode::NotFound, "Performance region was not found");
  if (region->performance != before_) return core::failure(core::ErrorCode::Conflict,
      "Performance changed before the proposal decision");
  const auto valid = region->validate();
  if (!valid) return valid;
  if (!after_) {
    const auto take = std::find_if(before_.takes.begin(), before_.takes.end(),
        [&](const auto& value) { return value.id == takeId_; });
    if (take == before_.takes.end()) {
      return core::failure(core::ErrorCode::NotFound, "Performance proposal was not found", takeId_);
    }
    if (take->state != domain::PerformanceProposalState::Proposed) {
      return core::failure(core::ErrorCode::Conflict,
          "Performance proposal is not awaiting a decision", takeId_);
    }
    if (std::any_of(before_.accepted.begin(), before_.accepted.end(),
            [&](const auto& selection) { return selection.takeId == takeId_; })) {
      return core::failure(core::ErrorCode::Conflict,
          "Accepted performance selections must be replaced before the take is rejected", takeId_);
    }
    auto next = before_;
    const auto index = static_cast<std::size_t>(std::distance(before_.takes.begin(), take));
    next.takes[index].state = domain::PerformanceProposalState::Rejected;
    const auto nextValid = next.validate(region->notes, region->durationTick);
    if (!nextValid) return nextValid;
    after_ = std::move(next);
  }
  const auto afterValid = after_->validate(region->notes, region->durationTick);
  if (!afterValid) return afterValid;
  region->performance = *after_;
  return core::success();
}

core::Result<void> RejectPerformanceProposalCommand::revert(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (!region) return core::failure(core::ErrorCode::NotFound, "Performance region was not found");
  if (!after_ || region->performance != *after_) return core::failure(core::ErrorCode::Conflict,
      "Cannot undo a performance decision over changed state");
  const auto valid = before_.validate(region->notes, region->durationTick);
  if (!valid) return valid;
  region->performance = before_;
  return core::success();
}

CopyNotePerformanceCommand::CopyNotePerformanceCommand(
    domain::RegionId regionId, std::vector<domain::PerformanceNoteRemap> mapping)
    : regionId_(regionId), mapping_(std::move(mapping)) {}

CommandImpact CopyNotePerformanceCommand::impact() const {
  return {.scope = CommandAudioImpact::PhraseAudio, .projectWide = false,
          .trackIds = {}, .regionIds = {regionId_}, .noteIds = {}, .lyricIds = {}};
}

core::Result<void> CopyNotePerformanceCommand::apply(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) return core::failure(core::ErrorCode::NotFound, "Performance region was not found");
  if (!after_) {
    if (mapping_.empty() || mapping_.size() > 10'000U) {
      return core::failure(core::ErrorCode::InvalidArgument, "Invalid performance copy count");
    }
    const auto valid = region->performance.validate(region->notes, region->durationTick);
    if (!valid) return valid;
    auto next = region->performance;
    auto copiedPhonemes = region->phonemeOverrides;
    std::vector<domain::UnitSelectionOverride> copiedUnits;
    std::vector<domain::SeamOverride> copiedSeams;
    std::unordered_set<domain::NoteId> sources;
    std::unordered_set<domain::NoteId> targets;
    std::optional<std::int64_t> phraseTranslation;
    auto sourceStart = time::Tick{std::numeric_limits<std::int64_t>::max()};
    auto sourceEnd = time::Tick{0};
    auto targetStart = time::Tick{std::numeric_limits<std::int64_t>::max()};
    for (const auto& map : mapping_) {
      if (!sources.insert(map.source).second || !targets.insert(map.target).second) {
        return core::failure(core::ErrorCode::InvalidArgument, "Repeated performance copy mapping");
      }
    }
    for (const auto& map : mapping_) {
      const auto* source = region->findNote(map.source);
      const auto* target = region->findNote(map.target);
      if (source == nullptr || target == nullptr || sources.contains(map.target) ||
          source->durationTick != target->durationTick) {
        return core::failure(core::ErrorCode::InvalidArgument, "Invalid performance copy notes");
      }
      const auto sourceValid = source->validate();
      if (!sourceValid) return sourceValid;
      const auto targetValid = target->validate();
      if (!targetValid) return targetValid;
      const auto translation = target->startTick.value() - source->startTick.value();
      if (phraseTranslation.has_value() && *phraseTranslation != translation) {
        return core::failure(core::ErrorCode::InvalidArgument,
            "Time-scoped performance can only be copied with one phrase translation");
      }
      phraseTranslation = translation;
      sourceStart = std::min(sourceStart, source->startTick);
      sourceEnd = std::max(sourceEnd, source->endTick());
      targetStart = std::min(targetStart, target->startTick);
    }
    if (!phraseTranslation.has_value() || sourceStart >= sourceEnd) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Performance copy has no valid source phrase span");
    }
    const domain::PerformanceTimeRange sourceWindow{sourceStart, sourceEnd};
    auto transformedPerformance = domain::transformRegionPerformance(
        region->performance, region->notes, region->durationTick, mapping_,
        sourceWindow);
    if (!transformedPerformance) return core::Result<void>{transformedPerformance.error()};

    const auto translateTick = [&](time::Tick tick) -> std::optional<time::Tick> {
      const auto value = tick.value();
      const auto translation = targetStart.value();
      if ((translation > 0 && value > std::numeric_limits<std::int64_t>::max() - translation) ||
          (translation < 0 && value < std::numeric_limits<std::int64_t>::min() - translation)) {
        return std::nullopt;
      }
      return time::Tick{value + translation};
    };
    if (transformedPerformance.value().ownership.size() >
            domain::kMaximumPerformanceOwnership - next.ownership.size() ||
        transformedPerformance.value().accepted.size() >
            domain::kMaximumPerformanceSelections - next.accepted.size()) {
      return core::failure(core::ErrorCode::InvalidArgument,
          "Copied time-scoped performance exceeds region limits");
    }
    for (auto copy : transformedPerformance.value().ownership) {
      if (auto* range = std::get_if<domain::PerformanceTimeRange>(&copy.scope)) {
        const auto start = translateTick(range->startTick);
        const auto end = translateTick(range->endTick);
        if (!start || !end) return core::failure(core::ErrorCode::InvalidArgument,
            "Copied performance ownership range overflows");
        range->startTick = *start;
        range->endTick = *end;
      }
      next.ownership.push_back(std::move(copy));
    }
    for (auto copy : transformedPerformance.value().accepted) {
      if (auto* range = std::get_if<domain::PerformanceTimeRange>(&copy.scope)) {
        const auto start = translateTick(range->startTick);
        const auto end = translateTick(range->endTick);
        if (!start || !end) return core::failure(core::ErrorCode::InvalidArgument,
            "Copied accepted performance range overflows");
        range->startTick = *start;
        range->endTick = *end;
      }
      const auto offset = copy.sourceTickOffset.value();
      if (targetStart.value() > 0 && offset <
          std::numeric_limits<std::int64_t>::min() + targetStart.value()) {
        return core::failure(core::ErrorCode::InvalidArgument,
            "Copied performance source offset overflows");
      }
      copy.sourceTickOffset = time::Tick{offset - targetStart.value()};
      next.accepted.push_back(std::move(copy));
    }

    for (const auto& map : mapping_) {
      for (const auto& edit : region->phonemeOverrides) {
        if (edit.key.noteId != map.source) continue;
        if (copiedPhonemes.size() >= 4096U) {
          return core::failure(core::ErrorCode::InvalidArgument, "Copied phoneme edits exceed bounds");
        }
        auto copy = edit;
        copy.key.noteId = map.target;
        copiedPhonemes.push_back(std::move(copy));
      }
      // Valid note starts are nonnegative, so their signed difference is bounded.
      for (const auto& edit : region->unitSelectionOverrides) {
        if (edit.startKey.noteId != map.source) continue;
        if (region->unitSelectionOverrides.size() + copiedUnits.size() >= 4096U) {
          return core::failure(core::ErrorCode::InvalidArgument, "Copied unit edits exceed bounds");
        }
        auto copy = edit;
        copy.startKey.noteId = map.target;
        if (region->findUnitSelectionOverride(copy.startKey)) return core::failure(core::ErrorCode::Conflict, "Copied unit target is occupied");
        copiedUnits.push_back(std::move(copy));
      }
      for (const auto& edit : region->seamOverrides) {
        if (edit.incomingStartKey.noteId != map.source) continue;
        if (region->seamOverrides.size() + copiedSeams.size() >= 4096U) {
          return core::failure(core::ErrorCode::InvalidArgument, "Copied seam edits exceed bounds");
        }
        auto copy = edit;
        copy.incomingStartKey.noteId = map.target;
        if (region->findSeamOverride(copy.incomingStartKey)) return core::failure(core::ErrorCode::Conflict, "Copied seam target is occupied");
        copiedSeams.push_back(std::move(copy));
      }
    }
    next.pronunciation.reset();
    const auto nextValid = next.validate(region->notes, region->durationTick);
    if (!nextValid) return nextValid;
    auto candidate = *region;
    candidate.performance = next;
    candidate.phonemeOverrides = std::move(copiedPhonemes);
    if (std::any_of(candidate.phonemeOverrides.begin(), candidate.phonemeOverrides.end(),
                    [](const auto& edit) { return edit.sourceContextId && !edit.unresolved; })) {
      std::vector<domain::PerformanceNoteRemap> bindingMap = mapping_;
      for (const auto& note : region->notes) {
        if (!targets.contains(note.id)) bindingMap.push_back({note.id, note.id});
      }
      const auto rebound = phonemizer::rebindTransferredPhonemeContexts(*region, candidate, bindingMap);
      if (!rebound) return rebound;
    }
    // Validate only copied records with only copied-note mappings. An uncopied
    // neighbor must not accidentally complete a partially copied relationship.
    candidate.unitSelectionOverrides = std::move(copiedUnits);
    candidate.seamOverrides = std::move(copiedSeams);
    const auto transferred = phonemizer::validateTransferredRenderEdits(*region, candidate, mapping_);
    if (!transferred) return transferred;
    candidate.unitSelectionOverrides.insert(candidate.unitSelectionOverrides.begin(), region->unitSelectionOverrides.begin(), region->unitSelectionOverrides.end());
    candidate.seamOverrides.insert(candidate.seamOverrides.begin(), region->seamOverrides.begin(), region->seamOverrides.end());
    const auto candidateValid = candidate.validate();
    if (!candidateValid) return candidateValid;
    before_ = region->performance;
    after_ = std::move(next);
    beforePhonemes_ = region->phonemeOverrides;
    beforeUnits_ = region->unitSelectionOverrides;
    beforeSeams_ = region->seamOverrides;
    afterUnits_ = std::move(candidate.unitSelectionOverrides);
    afterSeams_ = std::move(candidate.seamOverrides);
    afterPhonemes_ = std::move(candidate.phonemeOverrides);
  }
  auto staged = *region;
  staged.performance = *after_;
  staged.phonemeOverrides = afterPhonemes_;
  staged.unitSelectionOverrides = afterUnits_;
  staged.seamOverrides = afterSeams_;
  const auto valid = staged.validate();
  if (!valid) return valid;
  std::swap(region->performance, staged.performance);
  region->phonemeOverrides.swap(staged.phonemeOverrides);
  region->unitSelectionOverrides.swap(staged.unitSelectionOverrides);
  region->seamOverrides.swap(staged.seamOverrides);
  return core::success();
}

core::Result<void> CopyNotePerformanceCommand::revert(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (region == nullptr || !before_) {
    return core::failure(core::ErrorCode::Conflict, "Performance copy has no state to restore");
  }
  auto staged = *region;
  staged.performance = *before_;
  staged.phonemeOverrides = beforePhonemes_;
  staged.unitSelectionOverrides = beforeUnits_;
  staged.seamOverrides = beforeSeams_;
  const auto valid = staged.validate();
  if (!valid) return valid;
  std::swap(region->performance, staged.performance);
  region->phonemeOverrides.swap(staged.phonemeOverrides);
  region->unitSelectionOverrides.swap(staged.unitSelectionOverrides);
  region->seamOverrides.swap(staged.seamOverrides);
  return core::success();
}

namespace {

constexpr std::size_t kMaximumEdits = 10'000U;

template <typename Project>
auto expressionNoteIndex(Project& project, const std::vector<NoteExpressionEdit>& edits)
    -> core::Result<std::unordered_map<domain::NoteId, decltype(project.findNote(domain::NoteId{}))>> {
  using Index = std::unordered_map<domain::NoteId, decltype(project.findNote(domain::NoteId{}))>;
  if (edits.size() > kMaximumEdits) return core::failure<Index>(core::ErrorCode::InvalidArgument, "Too many performance note edits");
  std::unordered_set<domain::NoteId> requested;
  for (const auto& edit : edits) if (!requested.insert(edit.noteId).second)
    return core::failure<Index>(core::ErrorCode::InvalidArgument, "Performance edit repeats a note", edit.noteId.toString());
  Index index; index.reserve(requested.size());
  if (requested.empty()) return index;
  for (auto& track : project.vocalTracks()) for (auto& region : track.regions) for (auto& note : region.notes) {
    if (requested.contains(note.id) && !index.emplace(note.id, &note).second)
      return core::failure<Index>(core::ErrorCode::Conflict, "Performance note target is ambiguous", note.id.toString());
  }
  for (const auto& edit : edits) if (!index.contains(edit.noteId))
    return core::failure<Index>(core::ErrorCode::NotFound, "Performance edit note was not found", edit.noteId.toString());
  return index;
}

template <typename Index>
core::Result<void> validateEdits(
    const domain::Project& project,
    const std::vector<NoteExpressionEdit>& notes,
    const std::vector<RegionDynamicsEdit>& regions,
    const std::vector<RegionFormantEdit>& formant,
    const std::vector<RegionBreathinessEdit>& breathiness,
    const std::vector<RegionTensionEdit>& tension,
    const std::vector<RegionAirinessEdit>& airiness,
    const std::vector<RegionGenderEdit>& gender,
    const std::vector<RegionGrowlEdit>& growl,
    const std::vector<TrackStyleEdit>& tracks,
    std::size_t ownershipCount, const Index& noteIndex) {
  if ((notes.empty() && regions.empty() && formant.empty() && breathiness.empty() && tension.empty() &&
       airiness.empty() && gender.empty() && growl.empty() && tracks.empty() && ownershipCount == 0U) ||
      notes.size() > kMaximumEdits || regions.size() > kMaximumEdits ||
      formant.size() > kMaximumEdits || breathiness.size() > kMaximumEdits ||
      tension.size() > kMaximumEdits || airiness.size() > kMaximumEdits ||
      gender.size() > kMaximumEdits || growl.size() > kMaximumEdits ||
      tracks.size() > kMaximumEdits ||
      ownershipCount > kMaximumEdits ||
      notes.size() + regions.size() + formant.size() + breathiness.size() + tension.size() +
              airiness.size() + gender.size() + growl.size() + tracks.size() + ownershipCount >
          kMaximumEdits) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Performance edit target count is outside supported bounds");
  }

  std::unordered_set<domain::NoteId> noteIds;
  for (const auto& edit : notes) {
    if (!noteIds.insert(edit.noteId).second) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Performance edit repeats a note", edit.noteId.toString());
    }
    const auto* note = noteIndex.at(edit.noteId);
    if (note == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Performance edit note was not found", edit.noteId.toString());
    }
    auto replacement = *note;
    replacement.vibrato = edit.vibrato;
    replacement.phoneticHint = edit.phoneticHint;
    const auto validation = replacement.validate();
    if (!validation) return validation;
  }

  std::unordered_set<domain::RegionId> regionIds;
  for (const auto& edit : regions) {
    if (!regionIds.insert(edit.regionId).second) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Performance edit repeats a region", edit.regionId.toString());
    }
    const auto* region = project.findRegion(edit.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Performance edit region was not found", edit.regionId.toString());
    }
    const auto validation = edit.curve.validate();
    if (!validation) return validation;
    if (!edit.curve.points().empty() &&
        edit.curve.points().back().tick > region->durationTick) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Dynamics automation extends beyond the region",
                           edit.regionId.toString());
    }
  }

  std::unordered_set<domain::TrackId> trackIds;
  std::unordered_set<domain::RegionId> formantIds;
  std::unordered_set<domain::RegionId> tensionIds;
  for (const auto& edit : tension) {
    if (!tensionIds.insert(edit.regionId).second) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Performance edit repeats a tension region",
                           edit.regionId.toString());
    }
    const auto* region = project.findRegion(edit.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Performance tension region was not found",
                           edit.regionId.toString());
    }
    const auto validation = edit.curve.validate();
    if (!validation) return validation;
    if (!edit.curve.points().empty() &&
        edit.curve.points().back().tick > region->durationTick) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Tension automation extends beyond the region",
                           edit.regionId.toString());
    }
  }
  for (const auto& edit : formant) {
    if (!formantIds.insert(edit.regionId).second) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Performance edit repeats a formant region",
                           edit.regionId.toString());
    }
    const auto* region = project.findRegion(edit.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Performance formant region was not found",
                           edit.regionId.toString());
    }
    const auto validation = edit.curve.validate();
    if (!validation) return validation;
    if (!edit.curve.points().empty() &&
        edit.curve.points().back().tick > region->durationTick) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Formant automation extends beyond the region",
                           edit.regionId.toString());
    }
  }
  std::unordered_set<domain::RegionId> breathinessIds;
  std::unordered_set<domain::RegionId> airinessIds;
  std::unordered_set<domain::RegionId> genderIds;
  std::unordered_set<domain::RegionId> growlIds;
  for (const auto& edit : growl) {
    if (!growlIds.insert(edit.regionId).second) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Performance edit repeats a growl region",
                           edit.regionId.toString());
    }
    const auto* region = project.findRegion(edit.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Performance growl region was not found",
                           edit.regionId.toString());
    }
    const auto validation = edit.curve.validate();
    if (!validation) return validation;
    if (!edit.curve.points().empty() &&
        edit.curve.points().back().tick > region->durationTick) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Growl automation extends beyond the region",
                           edit.regionId.toString());
    }
  }
  for (const auto& edit : gender) {
    if (!genderIds.insert(edit.regionId).second) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Performance edit repeats a gender region",
                           edit.regionId.toString());
    }
    const auto* region = project.findRegion(edit.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Performance gender region was not found",
                           edit.regionId.toString());
    }
    const auto validation = edit.curve.validate();
    if (!validation) return validation;
    if (!edit.curve.points().empty() &&
        edit.curve.points().back().tick > region->durationTick) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Gender automation extends beyond the region",
                           edit.regionId.toString());
    }
  }
  for (const auto& edit : airiness) {
    if (!airinessIds.insert(edit.regionId).second) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Performance edit repeats an airiness region",
                           edit.regionId.toString());
    }
    const auto* region = project.findRegion(edit.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Performance airiness region was not found",
                           edit.regionId.toString());
    }
    const auto validation = edit.curve.validate();
    if (!validation) return validation;
    if (!edit.curve.points().empty() &&
        edit.curve.points().back().tick > region->durationTick) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Airiness automation extends beyond the region",
                           edit.regionId.toString());
    }
  }
  for (const auto& edit : breathiness) {
    if (!breathinessIds.insert(edit.regionId).second) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Performance edit repeats a breathiness region",
                           edit.regionId.toString());
    }
    const auto* region = project.findRegion(edit.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Performance breathiness region was not found",
                           edit.regionId.toString());
    }
    const auto validation = edit.curve.validate();
    if (!validation) return validation;
    if (!edit.curve.points().empty() &&
        edit.curve.points().back().tick > region->durationTick) {
      return core::failure(core::ErrorCode::InvariantViolation,
                           "Breathiness automation extends beyond the region",
                           edit.regionId.toString());
    }
  }
  for (const auto& edit : tracks) {
    if (!trackIds.insert(edit.trackId).second) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Performance edit repeats a track", edit.trackId.toString());
    }
    if (project.findVocalTrack(edit.trackId) == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Performance edit track was not found", edit.trackId.toString());
    }
    const auto validation = edit.selection.validate();
    if (!validation) return validation;
  }
  return core::success();
}

}

EditPerformanceCommand::EditPerformanceCommand(
    std::vector<NoteExpressionEdit> notes,
    std::vector<RegionDynamicsEdit> regions,
    std::vector<TrackStyleEdit> tracks,
    std::vector<RegionOwnershipEdit> ownership,
    std::vector<RegionFormantEdit> formant,
    std::vector<RegionBreathinessEdit> breathiness,
    std::vector<RegionTensionEdit> tension,
    std::vector<RegionAirinessEdit> airiness,
    std::vector<RegionGenderEdit> gender,
    std::vector<RegionGrowlEdit> growl)
    : afterNotes_(std::move(notes)),
      afterRegions_(std::move(regions)),
      afterFormant_(std::move(formant)),
      afterBreathiness_(std::move(breathiness)),
      afterTension_(std::move(tension)),
      afterAiriness_(std::move(airiness)),
      afterGender_(std::move(gender)),
      afterGrowl_(std::move(growl)),
      afterTracks_(std::move(tracks)),
      ownershipEdits_(std::move(ownership)) {}

std::string_view EditPerformanceCommand::name() const noexcept {
  return "Edit performance";
}

CommandAudioImpact EditPerformanceCommand::audioImpact() const noexcept {
  return afterTracks_.empty() ? CommandAudioImpact::PhraseAudio
                              : CommandAudioImpact::ProjectAudio;
}

CommandImpact EditPerformanceCommand::impact() const {
  CommandImpact result{
      .scope = audioImpact(),
      .projectWide = false,
      .trackIds = {},
      .regionIds = {},
      .noteIds = {},
      .lyricIds = {},
  };
  result.noteIds.reserve(afterNotes_.size());
  result.regionIds.reserve(afterRegions_.size());
  result.trackIds.reserve(afterTracks_.size());
  for (const auto& edit : afterNotes_) result.noteIds.push_back(edit.noteId);
  for (const auto& edit : afterRegions_) result.regionIds.push_back(edit.regionId);
  for (const auto& edit : afterTracks_) result.trackIds.push_back(edit.trackId);
  for (const auto& edit : ownershipEdits_) {
    bool present = false;
    for (const auto id : result.regionIds) present = present || id == edit.regionId;
    if (!present) result.regionIds.push_back(edit.regionId);
  }
  return result;
}

core::Result<void> EditPerformanceCommand::apply(domain::Project& project) {
  return set(project, true);
}

core::Result<void> EditPerformanceCommand::revert(domain::Project& project) {
  if (!captured_) {
    return core::failure(core::ErrorCode::Conflict,
                         "Performance edit has no captured state to restore");
  }
  return set(project, false);
}

core::Result<void> EditPerformanceCommand::set(domain::Project& project,
                                               bool after) {
  std::vector<NoteHintEdit> hintEdits;
  if (!captured_) {
    const auto indexed = expressionNoteIndex(project, afterNotes_);
    if (!indexed) return core::Result<void>{indexed.error()};
    for (const auto& edit : afterNotes_) {
      const auto* note = indexed.value().at(edit.noteId);
      if (!note) return core::failure(core::ErrorCode::NotFound, "Expression hint note is missing");
      if (note->phoneticHint != edit.phoneticHint) hintEdits.push_back({note->id, note->phoneticHint, edit.phoneticHint});
    }
  }
  if (!hints_ && hintEdits.empty()) return setExpressions(project, after);

  // Compose against a private project and command copy: an invalid hint must
  // not publish vibrato/style/ownership edits or partially capture history.
  auto staged = project;
  auto command = *this;
  if (!command.hints_) command.hints_.emplace(std::move(hintEdits));
  if (after) {
    const auto expressions = command.setExpressions(staged, true); if (!expressions) return expressions;
    // The expression snapshot carries hints for backwards-compatible history.
    // Restore their source values before the dedicated reconciliation command.
    const auto sourceNotes = expressionNoteIndex(project, command.afterNotes_);
    const auto stagedNotes = expressionNoteIndex(staged, command.afterNotes_);
    if (!sourceNotes) return core::Result<void>{sourceNotes.error()};
    if (!stagedNotes) return core::Result<void>{stagedNotes.error()};
    for (const auto& edit : command.afterNotes_)
      stagedNotes.value().at(edit.noteId)->phoneticHint = sourceNotes.value().at(edit.noteId)->phoneticHint;
    const auto hints = command.hints_->apply(staged); if (!hints) return hints;
    for (auto& ownership : command.afterOwnership_) {
      const auto prior = std::find_if(command.beforeOwnership_.begin(), command.beforeOwnership_.end(),
          [&](const auto& value) { return value.regionId == ownership.regionId; });
      if (prior != command.beforeOwnership_.end() && prior->revision.ownership != ownership.revision.ownership) {
        auto& state = staged.findRegion(ownership.regionId)->performance;
        for (auto& owner : state.ownership) owner.revision = state.revision;
        ownership.revision = state.revision; ownership.ownership = state.ownership;
      }
    }
  } else {
    const auto hints = command.hints_->revert(staged); if (!hints) return hints;
    const auto expressions = command.setExpressions(staged, false); if (!expressions) return expressions;
  }
  const auto valid = staged.validate(); if (!valid) return valid;
  project = std::move(staged); *this = std::move(command);
  return core::success();
}

core::Result<void> EditPerformanceCommand::setExpressions(domain::Project& project,
                                               bool after) {
  const auto& notes = after ? afterNotes_ : beforeNotes_;
  const auto& regions = after ? afterRegions_ : beforeRegions_;
  const auto& formant = after ? afterFormant_ : beforeFormant_;
  const auto& breathiness = after ? afterBreathiness_ : beforeBreathiness_;
  const auto& tension = after ? afterTension_ : beforeTension_;
  const auto& airiness = after ? afterAiriness_ : beforeAiriness_;
  const auto& gender = after ? afterGender_ : beforeGender_;
  const auto& growl = after ? afterGrowl_ : beforeGrowl_;
  const auto& tracks = after ? afterTracks_ : beforeTracks_;
  const auto indexedNotes = expressionNoteIndex(project, notes);
  if (!indexedNotes) return core::Result<void>{indexedNotes.error()};
  const auto validation = validateEdits(project, notes, regions, formant, breathiness, tension,
                                       airiness, gender, growl, tracks, ownershipEdits_.size(),
                                       indexedNotes.value());
  if (!validation) return validation;

  std::vector<OwnershipState> priorOwnership;
  std::vector<OwnershipState> nextOwnership;
  if (!captured_) {
    std::unordered_set<domain::RegionId> ids;
    for (const auto& edit : ownershipEdits_) {
      const auto* region = project.findRegion(edit.regionId);
      if (!ids.insert(edit.regionId).second || region == nullptr) {
        return core::failure(core::ErrorCode::InvalidArgument,
                             "Ownership region is missing or repeated");
      }
      const auto& state = region->performance;
      if (state.revision != edit.expectedRevision ||
          state.ownership != edit.expectedOwnership) {
        return core::failure(core::ErrorCode::Conflict,
                             "Ownership changed since the edit was prepared");
      }
      auto replacement = state;
      replacement.ownership = edit.ownership;
      if (replacement.ownership != state.ownership) {
        if (state.revision.ownership == std::numeric_limits<std::uint64_t>::max()) {
          return core::failure(core::ErrorCode::Conflict,
                               "Ownership revision is exhausted");
        }
        ++replacement.revision.ownership;
        for (auto& owner : replacement.ownership) owner.revision = replacement.revision;
      }
      const auto valid = replacement.validate(region->notes, region->durationTick);
      if (!valid) return valid;
      priorOwnership.push_back({edit.regionId, state.revision, state.ownership});
      nextOwnership.push_back({edit.regionId, replacement.revision,
                               std::move(replacement.ownership)});
    }
  }
  auto stagedOwnership = captured_ ? (after ? afterOwnership_ : beforeOwnership_)
                                  : nextOwnership;
  for (const auto& edit : stagedOwnership) {
    const auto* region = project.findRegion(edit.regionId);
    if (region == nullptr) {
      return core::failure(core::ErrorCode::NotFound, "Ownership region was not found");
    }
    auto replacement = region->performance;
    replacement.revision = edit.revision;
    replacement.ownership = edit.ownership;
    const auto valid = replacement.validate(region->notes, region->durationTick);
    if (!valid) return valid;
  }

  auto stagedNotes = notes;
  auto stagedRegions = regions;
  auto stagedFormant = formant;
  auto stagedBreathiness = breathiness;
  auto stagedTension = tension;
  auto stagedAiriness = airiness;
  auto stagedGender = gender;
  auto stagedGrowl = growl;
  auto stagedTracks = tracks;
  if (!captured_) {
    std::vector<NoteExpressionEdit> priorNotes;
    std::vector<RegionDynamicsEdit> priorRegions;
    std::vector<RegionFormantEdit> priorFormant;
    std::vector<RegionBreathinessEdit> priorBreathiness;
    std::vector<RegionTensionEdit> priorTension;
    std::vector<RegionAirinessEdit> priorAiriness;
    std::vector<RegionGenderEdit> priorGender;
    std::vector<RegionGrowlEdit> priorGrowl;
    std::vector<TrackStyleEdit> priorTracks;
    priorNotes.reserve(notes.size());
    priorRegions.reserve(regions.size());
    priorFormant.reserve(formant.size());
    priorBreathiness.reserve(breathiness.size());
    priorTension.reserve(tension.size());
    priorAiriness.reserve(airiness.size());
    priorGender.reserve(gender.size());
    priorGrowl.reserve(growl.size());
    priorTracks.reserve(tracks.size());
    for (const auto& edit : notes) {
      const auto* note = indexedNotes.value().at(edit.noteId);
      priorNotes.push_back({note->id, note->vibrato, note->phoneticHint});
    }
    for (const auto& edit : regions) {
      const auto* region = project.findRegion(edit.regionId);
      priorRegions.push_back({region->id, region->dynamicsAutomation});
    }
    for (const auto& edit : formant) {
      const auto* region = project.findRegion(edit.regionId);
      priorFormant.push_back({region->id, region->formantAutomation});
    }
    for (const auto& edit : breathiness) {
      const auto* region = project.findRegion(edit.regionId);
      priorBreathiness.push_back({region->id, region->breathinessAutomation});
    }
    for (const auto& edit : tension) {
      const auto* region = project.findRegion(edit.regionId);
      priorTension.push_back({region->id, region->tensionAutomation});
    }
    for (const auto& edit : airiness) {
      const auto* region = project.findRegion(edit.regionId);
      priorAiriness.push_back({region->id, region->airinessAutomation});
    }
    for (const auto& edit : gender) {
      const auto* region = project.findRegion(edit.regionId);
      priorGender.push_back({region->id, region->genderAutomation});
    }
    for (const auto& edit : growl) {
      const auto* region = project.findRegion(edit.regionId);
      priorGrowl.push_back({region->id, region->growlAutomation});
    }
    for (const auto& edit : tracks) {
      const auto* track = project.findVocalTrack(edit.trackId);
      priorTracks.push_back({track->id, track->styleSelection});
    }
    beforeNotes_ = std::move(priorNotes);
    beforeRegions_ = std::move(priorRegions);
    beforeFormant_ = std::move(priorFormant);
    beforeBreathiness_ = std::move(priorBreathiness);
    beforeTension_ = std::move(priorTension);
    beforeAiriness_ = std::move(priorAiriness);
    beforeGender_ = std::move(priorGender);
    beforeGrowl_ = std::move(priorGrowl);
    beforeTracks_ = std::move(priorTracks);
    beforeOwnership_ = std::move(priorOwnership);
    afterOwnership_ = std::move(nextOwnership);
    captured_ = true;
  }

  for (auto& edit : stagedNotes) {
    auto* note = indexedNotes.value().at(edit.noteId);
    note->vibrato = edit.vibrato;
    note->phoneticHint.swap(edit.phoneticHint);
  }
  for (auto& edit : stagedRegions) {
    std::swap(project.findRegion(edit.regionId)->dynamicsAutomation, edit.curve);
  }
  for (auto& edit : stagedFormant) {
    std::swap(project.findRegion(edit.regionId)->formantAutomation, edit.curve);
  }
  for (auto& edit : stagedBreathiness) {
    std::swap(project.findRegion(edit.regionId)->breathinessAutomation, edit.curve);
  }
  for (auto& edit : stagedTension) {
    std::swap(project.findRegion(edit.regionId)->tensionAutomation, edit.curve);
  }
  for (auto& edit : stagedAiriness) {
    std::swap(project.findRegion(edit.regionId)->airinessAutomation, edit.curve);
  }
  for (auto& edit : stagedGender) {
    std::swap(project.findRegion(edit.regionId)->genderAutomation, edit.curve);
  }
  for (auto& edit : stagedGrowl) {
    std::swap(project.findRegion(edit.regionId)->growlAutomation, edit.curve);
  }
  for (auto& edit : stagedTracks) {
    std::swap(project.findVocalTrack(edit.trackId)->styleSelection, edit.selection);
  }
  for (auto& edit : stagedOwnership) {
    auto& state = project.findRegion(edit.regionId)->performance;
    state.revision = edit.revision;
    state.ownership.swap(edit.ownership);
  }
  return core::success();
}

}
