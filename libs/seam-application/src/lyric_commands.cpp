#include "seam/application/lyric_commands.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/phonemizer/override_reconciliation.hpp"
#include "seam/phonemizer/pronunciation_resolver.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <unordered_map>

namespace seam::application {

CommandImpact SetLyricCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::PhraseAudio,
      .projectWide = false,
      .trackIds = {},
      .regionIds = {},
      .noteIds = {},
      .lyricIds = {lyricId_},
  };
}

CommandImpact BatchSetLyricsCommand::impact() const {
  CommandImpact result{
      .scope = CommandAudioImpact::PhraseAudio,
      .projectWide = false,
      .trackIds = {},
      .regionIds = {},
      .noteIds = {},
      .lyricIds = {},
  };
  result.lyricIds.reserve(edits_.size());
  for (const auto& edit : edits_) result.lyricIds.push_back(edit.lyricId);
  return result;
}

ApplyJapaneseReadingHintsCommand::ApplyJapaneseReadingHintsCommand(domain::RegionId regionId,
    std::vector<NoteExpressionEdit> notes, domain::PronunciationIdentity identity)
    : regionId_(regionId), afterNotes_(std::move(notes)), afterIdentity_(std::move(identity)) {}

CommandImpact ApplyJapaneseReadingHintsCommand::impact() const {
  CommandImpact result{.scope = CommandAudioImpact::PhraseAudio, .projectWide = false,
      .trackIds = {}, .regionIds = {regionId_}, .noteIds = {}, .lyricIds = {}};
  result.noteIds.reserve(afterNotes_.size());
  for (const auto& note : afterNotes_) result.noteIds.push_back(note.noteId);
  return result;
}

core::Result<void> ApplyJapaneseReadingHintsCommand::apply(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (!region) return core::failure(core::ErrorCode::NotFound, "Japanese reading region was not found");
  if (afterNotes_.empty() || afterNotes_.size() > 10000U) return core::failure(core::ErrorCode::InvalidArgument, "Japanese reading note count exceeds bounds");
  const auto identityValid = afterIdentity_.validate(); if (!identityValid) return identityValid;
  std::unordered_set<domain::NoteId> ids;
  std::vector<domain::Note*> notes; notes.reserve(afterNotes_.size());
  for (const auto& edit : afterNotes_) {
    if (!ids.insert(edit.noteId).second) return core::failure(core::ErrorCode::InvalidArgument, "Japanese reading repeats a note");
    auto* note = region->findNote(edit.noteId);
    if (!note) return core::failure(core::ErrorCode::NotFound, "Japanese reading note was not found", edit.noteId.toString());
    auto candidate = *note; candidate.vibrato = edit.vibrato; candidate.phoneticHint = edit.phoneticHint;
    const auto valid = candidate.validate(); if (!valid) return valid;
    notes.push_back(note);
  }
  if (!captured_) {
    std::vector<std::optional<std::string>> beforeHints;
    beforeHints.reserve(notes.size()); for (const auto* note : notes) beforeHints.push_back(note->phoneticHint);
    const auto beforePerformance = region->performance;
    auto next = region->performance;
    if (next.revision.pronunciation == std::numeric_limits<std::uint64_t>::max())
      return core::failure(core::ErrorCode::Conflict, "Pronunciation revision is exhausted");
    ++next.revision.pronunciation; next.pronunciation = afterIdentity_;
    const auto valid = next.validate(region->notes, region->durationTick); if (!valid) return valid;
    beforeHints_ = std::move(beforeHints); beforePerformance_ = beforePerformance;
    afterPerformance_ = std::move(next); captured_ = true;
  } else {
    if (!beforePerformance_ || !afterPerformance_ || region->performance != *beforePerformance_)
      return core::failure(core::ErrorCode::Conflict, "Japanese reading source changed before reapply");
    for (std::size_t i = 0U; i < notes.size(); ++i) if (notes[i]->phoneticHint != beforeHints_[i])
      return core::failure(core::ErrorCode::Conflict, "Japanese reading note hint changed before reapply");
  }
  for (std::size_t i = 0U; i < notes.size(); ++i) notes[i]->phoneticHint = afterNotes_[i].phoneticHint;
  region->performance = *afterPerformance_;
  return core::success();
}

core::Result<void> ApplyJapaneseReadingHintsCommand::revert(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (!region || !beforePerformance_ || !afterPerformance_ || region->performance != *afterPerformance_)
    return core::failure(core::ErrorCode::Conflict, "Japanese reading cannot be undone over changed state");
  for (const auto& edit : afterNotes_) {
    auto* note = region->findNote(edit.noteId); if (!note) return core::failure(core::ErrorCode::NotFound, "Japanese reading note disappeared during undo");
    const auto index = static_cast<std::size_t>(&edit - afterNotes_.data());
    if (index >= beforeHints_.size()) return core::failure(core::ErrorCode::Conflict, "Japanese reading undo state is incomplete");
    if (note->phoneticHint != edit.phoneticHint) return core::failure(core::ErrorCode::Conflict, "Japanese reading hint changed before undo");
    note->phoneticHint = beforeHints_[index];
  }
  region->performance = *beforePerformance_; return core::success();
}

CommandImpact UpsertPhonemeOverrideCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::PhraseAudio,
      .projectWide = false,
      .trackIds = {},
      .regionIds = {regionId_},
      .noteIds = {after_.key.noteId},
      .lyricIds = {},
  };
}

CommandImpact RemovePhonemeOverrideCommand::impact() const {
  return CommandImpact{
      .scope = CommandAudioImpact::PhraseAudio,
      .projectWide = false,
      .trackIds = {},
      .regionIds = {regionId_},
      .noteIds = {key_.noteId},
      .lyricIds = {},
  };
}

SetLyricCommand::SetLyricCommand(domain::LyricTokenId lyricId,
                                 std::u32string surface,
                                 domain::Language language)
    : lyricId_(lyricId),
      afterSurface_(std::move(surface)),
      afterLanguage_(language) {}

namespace {

using LyricIndex = std::unordered_map<domain::LyricTokenId, domain::LyricToken*>;
core::Result<LyricIndex> indexLyrics(domain::Project& project, const std::vector<BatchLyricEdit>& edits) {
  std::unordered_set<domain::LyricTokenId> targets;
  for (const auto& edit : edits) targets.insert(edit.lyricId);
  LyricIndex index;
  for (auto& track : project.vocalTracks()) {
    for (auto& region : track.regions) {
      for (auto& lyric : region.lyrics) {
        if (!targets.contains(lyric.id)) continue;
        if (!index.emplace(lyric.id, &lyric).second)
          return core::failure<LyricIndex>(core::ErrorCode::Conflict, "Batch lyric target is ambiguous across regions");
      }
    }
  }
  return core::success(std::move(index));
}

core::Result<void> refreshPronunciation(domain::VocalRegion& region) {
  if (region.performance.revision.pronunciation == std::numeric_limits<std::uint64_t>::max()) {
    return core::failure(core::ErrorCode::Conflict, "Pronunciation revision is exhausted");
  }
  ++region.performance.revision.pronunciation;
  region.performance.pronunciation.reset();
  const bool supported = std::all_of(region.notes.begin(), region.notes.end(), [&](const auto& note) {
    const auto* lyric = region.findLyric(note.lyricTokenId);
    return lyric && phonemizer::hasPronunciationService(lyric->language);
  });
  if (supported) {
    const auto resolved = phonemizer::resolvePronunciation(region);
    if (resolved && (resolved.value().identity.language == domain::Language::Japanese ||
                     resolved.value().pronunciation.warnings.empty())) {
      region.performance.pronunciation = resolved.value().identity;
    }
  }
  return core::success();
}

PhonemeEditState phonemeState(const domain::VocalRegion& region) {
  return {region.phonemeOverrides, region.unitSelectionOverrides, region.seamOverrides,
          region.performance.pronunciation,
          region.performance.revision.pronunciation};
}

core::Result<void> publishPhonemeState(domain::VocalRegion& region, const PhonemeEditState& state) {
  auto candidate = region;
  candidate.phonemeOverrides = state.overrides;
  candidate.unitSelectionOverrides = state.units;
  candidate.seamOverrides = state.seams;
  candidate.performance.pronunciation = state.identity;
  candidate.performance.revision.pronunciation = state.pronunciationRevision;
  const auto valid = candidate.validate();
  if (!valid) return valid;
  region.phonemeOverrides.swap(candidate.phonemeOverrides);
  region.unitSelectionOverrides.swap(candidate.unitSelectionOverrides);
  region.seamOverrides.swap(candidate.seamOverrides);
  region.performance.pronunciation.swap(candidate.performance.pronunciation);
  region.performance.revision.pronunciation = state.pronunciationRevision;
  return core::success();
}

void reconcileOverrides(const domain::VocalRegion& before, domain::VocalRegion& after,
                        bool rebindPhonemes = true) {
  if (before.phonemeOverrides.empty() && before.unitSelectionOverrides.empty() &&
      before.seamOverrides.empty()) return;
  auto oldBase = before;
  auto newBase = after;
  if (rebindPhonemes) {
    oldBase.phonemeOverrides.clear();
    newBase.phonemeOverrides.clear();
  }
  const auto oldResolved = phonemizer::resolvePronunciation(oldBase);
  const auto newResolved = phonemizer::resolvePronunciation(newBase);
  const auto sameService = [](const domain::PronunciationIdentity& oldIdentity,
                              const domain::PronunciationIdentity& newIdentity) {
    return oldIdentity.language == newIdentity.language &&
        oldIdentity.resolverId == newIdentity.resolverId &&
        oldIdentity.resolverVersion == newIdentity.resolverVersion &&
        oldIdentity.resourceHash == newIdentity.resourceHash;
  };
  // A shared phone spelling does not prove correspondence across inventories
  // or resource versions. Keep the old binding for explicit review and undo.
  if (!oldResolved || !newResolved ||
      !sameService(oldResolved.value().identity, newResolved.value().identity)) {
    if (rebindPhonemes) for (auto& edit : after.phonemeOverrides) edit.unresolved = true;
    for (auto& edit : after.unitSelectionOverrides) edit.unresolved = true;
    for (auto& edit : after.seamOverrides) edit.unresolved = true;
    return;
  }
  auto oldTokens = oldResolved.value().pronunciation;
  auto newTokens = newResolved.value().pronunciation;
  // Dependent samples/joins follow effective sounds, not the timing/lock values
  // being deliberately edited. Normalize those attributes only for matching.
  if (!rebindPhonemes) {
    for (auto& token : oldTokens.tokens) { token.timing = {}; token.locked = false; }
    for (auto& token : newTokens.tokens) { token.timing = {}; token.locked = false; }
  }
  const auto regionMap = phonemizer::reconcileRegionPhonemes(oldTokens.tokens, newTokens.tokens);
  const auto compatible = [&](domain::NoteId id) {
    const auto* oldNote = before.findNote(id);
    const auto* newNote = after.findNote(id);
    if (!oldNote || !newNote) return false;
    const auto supported = [](const auto* lyric) {
      return lyric && phonemizer::hasPronunciationService(lyric->language);
    };
    const auto* oldLyric = before.findLyric(oldNote->lyricTokenId);
    const auto* newLyric = after.findLyric(newNote->lyricTokenId);
    if (!supported(oldLyric) || !supported(newLyric) ||
        oldLyric->language != newLyric->language) return false;
    const auto warned = [id](const auto& warning) { return warning.noteId == id; };
    return !std::any_of(oldTokens.warnings.begin(), oldTokens.warnings.end(), warned) &&
           !std::any_of(newTokens.warnings.begin(), newTokens.warnings.end(), warned);
  };
  for (const auto& note : before.notes) {
    std::vector<domain::PhonemeOverride> originals;
    for (const auto& edit : before.phonemeOverrides) {
      if (edit.key.noteId == note.id) originals.push_back(edit);
    }
    const auto* oldLyric = before.findLyric(note.lyricTokenId);
    const auto* newNote = after.findNote(note.id);
    const auto* newLyric = newNote ? after.findLyric(newNote->lyricTokenId) : nullptr;
    const auto supported = [](const auto* lyric) {
      return lyric != nullptr && phonemizer::hasPronunciationService(lyric->language);
    };
    const auto warned = [&](const auto& result) {
      return std::any_of(result.warnings.begin(), result.warnings.end(),
                         [&](const auto& warning) { return warning.noteId == note.id; });
    };
    auto replacements = originals;
    bool uncertain = !supported(oldLyric) || !supported(newLyric) ||
                     oldLyric->language != newLyric->language ||
                     warned(oldTokens) || warned(newTokens);
    const auto mapSpan = [&](domain::PhonemeKey key, std::uint16_t count)
        -> std::optional<domain::PhonemeKey> {
      if (!regionMap) return std::nullopt;
      const auto start = std::find_if(oldTokens.tokens.begin(), oldTokens.tokens.end(),
                                     [key](const auto& token) { return token.key == key; });
      if (start == oldTokens.tokens.end() ||
          count > static_cast<std::size_t>(oldTokens.tokens.end() - start)) return std::nullopt;
      for (std::size_t i = 0U; i < count; ++i) {
        if (!compatible(start[static_cast<std::ptrdiff_t>(i)].key.noteId)) return std::nullopt;
      }
      return regionMap.value().mapSpan(key, count);
    };
    std::unordered_set<std::uint16_t> unitKeys;
    bool unitCollision = false;
    for (auto& unit : after.unitSelectionOverrides) {
      if (unit.startKey.noteId != note.id) continue;
      const auto mapped = unit.unresolved ? std::nullopt : mapSpan(unit.startKey, unit.tokenCount);
      if (mapped) unit.startKey = *mapped;
      else unit.unresolved = true;
      if (!unitKeys.insert(unit.startKey.ordinal).second) unitCollision = true;
    }
    if (unitCollision) {
      for (std::size_t i = 0U; i < after.unitSelectionOverrides.size(); ++i) {
        if (before.unitSelectionOverrides[i].startKey.noteId != note.id) continue;
        after.unitSelectionOverrides[i] = before.unitSelectionOverrides[i];
        after.unitSelectionOverrides[i].unresolved = true;
      }
    }
    std::unordered_set<std::uint16_t> seamKeys;
    bool seamCollision = false;
    for (auto& seam : after.seamOverrides) {
      if (seam.incomingStartKey.noteId != note.id) continue;
      if (!seam.unresolved) {
        const auto incoming = std::find_if(oldTokens.tokens.begin(), oldTokens.tokens.end(),
            [&](const auto& token) { return token.key == seam.incomingStartKey; });
        const bool verified = regionMap && compatible(note.id) && incoming != oldTokens.tokens.end() &&
            (incoming == oldTokens.tokens.begin() || compatible((incoming - 1)->key.noteId));
        const auto mapped = verified ? regionMap.value().mapBoundary(seam.incomingStartKey)
                                     : std::nullopt;
        if (mapped) seam.incomingStartKey = *mapped;
        else seam.unresolved = true;
      }
      if (!seamKeys.insert(seam.incomingStartKey.ordinal).second) seamCollision = true;
    }
    if (seamCollision) {
      for (std::size_t i = 0U; i < after.seamOverrides.size(); ++i) {
        if (before.seamOverrides[i].incomingStartKey.noteId != note.id) continue;
        after.seamOverrides[i] = before.seamOverrides[i];
        after.seamOverrides[i].unresolved = true;
      }
    }
    if (!rebindPhonemes) continue;
    if (!uncertain) {
      const auto result = phonemizer::reconcilePhonemeOverrides(
          note.id, oldTokens.tokensForNote(note.id), newTokens.tokensForNote(note.id), originals);
      if (!result) {
        uncertain = true;
      } else {
        std::unordered_set<std::uint16_t> keys;
        for (std::size_t i = 0U; i < replacements.size(); ++i) {
          const bool verifiedBinding = !originals[i].sourceContextId ||
              originals[i].sourceContextId == phonemizer::phonemeEditContextId(oldResolved.value(), originals[i].key);
          if (!originals[i].unresolved && verifiedBinding && result.value()[i].rebound) {
            replacements[i] = *result.value()[i].rebound;
            if (originals[i].sourceContextId) {
              replacements[i].sourceContextId = phonemizer::phonemeEditContextId(newResolved.value(), replacements[i].key);
              if (!replacements[i].sourceContextId) replacements[i].unresolved = true;
            }
          } else {
            replacements[i].unresolved = true;
          }
          if (!keys.insert(replacements[i].key.ordinal).second) uncertain = true;
        }
      }
    }
    // Until persistent edit IDs replace ordinal storage, a collision retains
    // every original key and payload unresolved instead of dropping either edit.
    if (uncertain) {
      replacements = originals;
      for (auto& edit : replacements) edit.unresolved = true;
    }
    std::size_t index = 0U;
    for (auto& edit : after.phonemeOverrides) {
      if (edit.key.noteId == note.id) edit = replacements[index++];
    }
  }
}

}

void reconcileRetainedNoteOverrides(const domain::VocalRegion& before, domain::VocalRegion& after) {
  reconcileOverrides(before, after);
  after.unitSelectionOverrides = before.unitSelectionOverrides;
  after.seamOverrides = before.seamOverrides;
  reconcileOverrides(before, after, false);
}

core::Result<void> BatchSetLyricsCommand::apply(domain::Project& project) {
  if (edits_.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Batch lyric edit requires at least one lyric");
  }
  auto live = indexLyrics(project, edits_); if (!live) return core::Result<void>{live.error()};
  std::unordered_set<domain::LyricTokenId> ids;
  for (const auto& edit : edits_) {
    if (!ids.insert(edit.lyricId).second) {
      return core::failure(core::ErrorCode::InvalidArgument, "Repeated batch lyric target");
    }
    if (edit.after.empty()) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Batch lyric text must not be empty");
    }
    if (!live.value().contains(edit.lyricId)) {
      return core::failure(core::ErrorCode::NotFound,
                           "Batch lyric target was not found",
                           edit.lyricId.toString());
    }
  }
  auto staged = project;
  auto draft = indexLyrics(staged, edits_); if (!draft) return core::Result<void>{draft.error()};
  for (const auto& edit : edits_) {
    auto* lyric = draft.value().at(edit.lyricId);
    lyric->surface = edit.after;
    lyric->language = edit.language;
  }
  OverrideState prior;
  OverrideState next;
  if (!capturedOverrides_) {
    for (auto& track : staged.vocalTracks()) for (auto& region : track.regions) {
      const auto* original = project.findRegion(region.id);
      if (original->lyrics == region.lyrics) continue;
      prior.push_back({region.id, original->phonemeOverrides,
                      original->unitSelectionOverrides, original->seamOverrides,
                      original->performance.pronunciation, original->performance.revision});
      reconcileOverrides(*original, region);
      const auto refreshed = refreshPronunciation(region);
      if (!refreshed) return refreshed;
      next.push_back({region.id, region.phonemeOverrides,
                     region.unitSelectionOverrides, region.seamOverrides,
                     region.performance.pronunciation, region.performance.revision});
    }
  } else {
    for (const auto& [id, overrides, units, seams, pronunciation, revision] : afterOverrides_) {
      auto* region = staged.findRegion(id);
      if (!region) return core::failure(core::ErrorCode::NotFound, "Lyric dependency region was not found");
      region->phonemeOverrides = overrides;
      region->unitSelectionOverrides = units;
      region->seamOverrides = seams;
      region->performance.pronunciation = pronunciation;
      region->performance.revision = revision;
    }
  }
  const auto valid = staged.validate();
  if (!valid) return valid;
  if (!capturedOverrides_) {
    beforeOverrides_ = std::move(prior);
    afterOverrides_ = std::move(next);
    capturedOverrides_ = true;
  }
  for (const auto& edit : edits_) {
    auto* target = live.value().at(edit.lyricId);
    auto* replacement = draft.value().at(edit.lyricId);
    target->surface.swap(replacement->surface);
    target->language = replacement->language;
  }
  for (const auto& state : afterOverrides_) {
    auto* target = project.findRegion(state.regionId);
    auto* replacement = staged.findRegion(state.regionId);
    target->phonemeOverrides.swap(replacement->phonemeOverrides);
    target->unitSelectionOverrides.swap(replacement->unitSelectionOverrides);
    target->seamOverrides.swap(replacement->seamOverrides);
    target->performance.pronunciation.swap(replacement->performance.pronunciation);
    target->performance.revision = replacement->performance.revision;
  }
  return core::success();
}

core::Result<void> BatchSetLyricsCommand::revert(domain::Project& project) {
  if (!capturedOverrides_) return core::failure(core::ErrorCode::Conflict, "Lyric edit has no captured dependencies");
  auto live = indexLyrics(project, edits_); if (!live) return core::Result<void>{live.error()};
  for (const auto& edit : edits_) {
    if (!live.value().contains(edit.lyricId)) {
      return core::failure(core::ErrorCode::NotFound,
                           "Batch lyric undo target was not found",
                           edit.lyricId.toString());
    }
  }
  auto staged = project;
  auto draft = indexLyrics(staged, edits_); if (!draft) return core::Result<void>{draft.error()};
  for (const auto& edit : edits_) {
    auto* lyric = draft.value().at(edit.lyricId);
    lyric->surface = edit.before;
    lyric->language = edit.beforeLanguage;
  }
  for (const auto& [id, overrides, units, seams, pronunciation, revision] : beforeOverrides_) {
    auto* region = staged.findRegion(id);
    if (!region) return core::failure(core::ErrorCode::NotFound, "Lyric dependency region was not found");
    region->phonemeOverrides = overrides;
    region->unitSelectionOverrides = units;
    region->seamOverrides = seams;
    region->performance.pronunciation = pronunciation;
    region->performance.revision = revision;
  }
  const auto valid = staged.validate();
  if (!valid) return valid;
  for (const auto& edit : edits_) {
    auto* target = live.value().at(edit.lyricId);
    auto* replacement = draft.value().at(edit.lyricId);
    target->surface.swap(replacement->surface);
    target->language = replacement->language;
  }
  for (const auto& state : beforeOverrides_) {
    auto* target = project.findRegion(state.regionId);
    auto* replacement = staged.findRegion(state.regionId);
    target->phonemeOverrides.swap(replacement->phonemeOverrides);
    target->unitSelectionOverrides.swap(replacement->unitSelectionOverrides);
    target->seamOverrides.swap(replacement->seamOverrides);
    target->performance.pronunciation.swap(replacement->performance.pronunciation);
    target->performance.revision = replacement->performance.revision;
  }
  return core::success();
}

domain::LyricToken* SetLyricCommand::find(domain::Project& project) const noexcept {
  for (auto& track : project.vocalTracks()) {
    for (auto& region : track.regions) {
      if (auto* lyric = region.findLyric(lyricId_)) {
        return lyric;
      }
    }
  }
  return nullptr;
}

core::Result<void> SetLyricCommand::apply(domain::Project& project) {
  auto* lyric = find(project);
  if (lyric == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Lyric token was not found",
                         lyricId_.toString());
  }
  if (afterSurface_.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Lyric text must not be empty",
                         lyricId_.toString());
  }
  if (!captured_) {
    beforeSurface_ = lyric->surface;
    beforeLanguage_ = lyric->language;
    captured_ = true;
    batch_ = std::make_shared<BatchSetLyricsCommand>(std::vector<BatchLyricEdit>{
        {lyricId_, beforeSurface_, afterSurface_, afterLanguage_, beforeLanguage_}});
  }
  return batch_->apply(project);
}

core::Result<void> SetLyricCommand::revert(domain::Project& project) {
  if (!captured_) {
    return core::failure(core::ErrorCode::Conflict,
                         "Lyric edit command has no captured state");
  }
  auto* lyric = find(project);
  if (lyric == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Lyric token was not found during undo",
                         lyricId_.toString());
  }
  return batch_->revert(project);
}

UpsertPhonemeOverrideCommand::UpsertPhonemeOverrideCommand(
    domain::RegionId regionId, domain::PhonemeOverride overrideValue)
    : regionId_(regionId), after_(std::move(overrideValue)) {}

core::Result<void> UpsertPhonemeOverrideCommand::apply(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for phoneme override was not found",
                         regionId_.toString());
  }
  const auto validation = after_.validate();
  if (!validation) {
    return validation;
  }
  if (region->findNote(after_.key.noteId) == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Phoneme override references a missing note",
                         after_.key.toString());
  }
  if (afterState_) return publishPhonemeState(*region, *afterState_);
  auto candidate = *region;
  auto* existing = candidate.findPhonemeOverride(after_.key);
  auto replacement = after_;
  if (!after_.unresolved && (!existing || *existing != after_)) {
    auto baseRegion = *region;
    baseRegion.phonemeOverrides.clear();
    const auto base = phonemizer::resolvePronunciation(baseRegion);
    const auto context = base ? phonemizer::phonemeEditContextId(base.value(), after_.key) : std::nullopt;
    if (after_.sourceContextId && after_.sourceContextId != context) {
      return core::failure(core::ErrorCode::Conflict, "Phoneme edit source context is stale");
    }
    replacement.sourceContextId = context;
    if (!context) replacement.unresolved = true;
  }
  if (existing != nullptr) {
    *existing = replacement;
  } else {
    candidate.phonemeOverrides.push_back(replacement);
    std::stable_sort(candidate.phonemeOverrides.begin(), candidate.phonemeOverrides.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.key < rhs.key; });
  }
  if (candidate.phonemeOverrides != region->phonemeOverrides) {
    reconcileOverrides(*region, candidate, false);
    const auto refreshed = refreshPronunciation(candidate);
    if (!refreshed) return refreshed;
  }
  const auto valid = candidate.validate();
  if (!valid) return valid;
  beforeState_ = phonemeState(*region);
  afterState_ = phonemeState(candidate);
  return publishPhonemeState(*region, *afterState_);
}

core::Result<void> UpsertPhonemeOverrideCommand::revert(domain::Project& project) {
  if (!beforeState_) {
    return core::failure(core::ErrorCode::Conflict,
                         "Phoneme override command has no captured state");
  }
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for phoneme override was not found during undo",
                         regionId_.toString());
  }
  return publishPhonemeState(*region, *beforeState_);
}

core::Result<void> RemovePhonemeOverrideCommand::apply(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for phoneme override was not found",
                         regionId_.toString());
  }
  if (afterState_) return publishPhonemeState(*region, *afterState_);
  auto candidate = *region;
  const auto iterator = std::find_if(candidate.phonemeOverrides.begin(),
                                     candidate.phonemeOverrides.end(),
      [this](const auto& value) { return value.key == key_; });
  if (iterator == candidate.phonemeOverrides.end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Phoneme override was not found",
                         key_.toString());
  }
  candidate.phonemeOverrides.erase(iterator);
  reconcileOverrides(*region, candidate, false);
  const auto refreshed = refreshPronunciation(candidate);
  if (!refreshed) return refreshed;
  const auto valid = candidate.validate();
  if (!valid) return valid;
  beforeState_ = phonemeState(*region);
  afterState_ = phonemeState(candidate);
  return publishPhonemeState(*region, *afterState_);
}

core::Result<void> RemovePhonemeOverrideCommand::revert(domain::Project& project) {
  if (!beforeState_) {
    return core::failure(core::ErrorCode::Conflict,
                         "No phoneme override was removed");
  }
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for phoneme override was not found during undo",
                         regionId_.toString());
  }
  return publishPhonemeState(*region, *beforeState_);
}

}  // namespace seam::application
