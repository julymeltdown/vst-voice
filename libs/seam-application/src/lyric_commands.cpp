#include "seam/application/lyric_commands.hpp"

#include <algorithm>

namespace seam::application {

SetLyricCommand::SetLyricCommand(domain::LyricTokenId lyricId,
                                 std::u32string surface,
                                 domain::Language language)
    : lyricId_(lyricId),
      afterSurface_(std::move(surface)),
      afterLanguage_(language) {}

namespace {

domain::LyricToken* findLyric(domain::Project& project,
                              domain::LyricTokenId lyricId) noexcept {
  for (auto& track : project.vocalTracks()) {
    for (auto& region : track.regions) {
      if (auto* lyric = region.findLyric(lyricId)) return lyric;
    }
  }
  return nullptr;
}

}

core::Result<void> BatchSetLyricsCommand::apply(domain::Project& project) {
  if (edits_.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Batch lyric edit requires at least one lyric");
  }
  for (const auto& edit : edits_) {
    if (edit.after.empty()) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Batch lyric text must not be empty");
    }
    auto* lyric = findLyric(project, edit.lyricId);
    if (lyric == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Batch lyric target was not found",
                           edit.lyricId.toString());
    }
  }
  for (auto& edit : edits_) {
    auto* lyric = findLyric(project, edit.lyricId);
    lyric->surface = edit.after;
    lyric->language = edit.language;
  }
  return core::success();
}

core::Result<void> BatchSetLyricsCommand::revert(domain::Project& project) {
  for (const auto& edit : edits_) {
    auto* lyric = findLyric(project, edit.lyricId);
    if (lyric == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Batch lyric undo target was not found",
                           edit.lyricId.toString());
    }
  }
  for (const auto& edit : edits_) {
    auto* lyric = findLyric(project, edit.lyricId);
    lyric->surface = edit.before;
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
  }
  lyric->surface = afterSurface_;
  lyric->language = afterLanguage_;
  return core::success();
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
  lyric->surface = beforeSurface_;
  lyric->language = beforeLanguage_;
  return core::success();
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
  auto* existing = region->findPhonemeOverride(after_.key);
  if (!captured_) {
    if (existing != nullptr) {
      before_ = *existing;
    }
    captured_ = true;
  }
  if (existing != nullptr) {
    *existing = after_;
  } else {
    region->phonemeOverrides.push_back(after_);
    std::stable_sort(region->phonemeOverrides.begin(), region->phonemeOverrides.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.key < rhs.key; });
  }
  return core::success();
}

core::Result<void> UpsertPhonemeOverrideCommand::revert(domain::Project& project) {
  if (!captured_) {
    return core::failure(core::ErrorCode::Conflict,
                         "Phoneme override command has no captured state");
  }
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for phoneme override was not found during undo",
                         regionId_.toString());
  }
  auto* existing = region->findPhonemeOverride(after_.key);
  if (before_.has_value()) {
    if (existing != nullptr) {
      *existing = *before_;
    } else {
      region->phonemeOverrides.push_back(*before_);
    }
  } else {
    std::erase_if(region->phonemeOverrides,
                  [this](const auto& value) { return value.key == after_.key; });
  }
  return core::success();
}

core::Result<void> RemovePhonemeOverrideCommand::apply(domain::Project& project) {
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for phoneme override was not found",
                         regionId_.toString());
  }
  const auto iterator = std::find_if(region->phonemeOverrides.begin(),
                                     region->phonemeOverrides.end(),
      [this](const auto& value) { return value.key == key_; });
  if (iterator == region->phonemeOverrides.end()) {
    return core::failure(core::ErrorCode::NotFound,
                         "Phoneme override was not found",
                         key_.toString());
  }
  removed_ = *iterator;
  region->phonemeOverrides.erase(iterator);
  return core::success();
}

core::Result<void> RemovePhonemeOverrideCommand::revert(domain::Project& project) {
  if (!removed_.has_value()) {
    return core::failure(core::ErrorCode::Conflict,
                         "No phoneme override was removed");
  }
  auto* region = project.findRegion(regionId_);
  if (region == nullptr) {
    return core::failure(core::ErrorCode::NotFound,
                         "Region for phoneme override was not found during undo",
                         regionId_.toString());
  }
  if (region->findPhonemeOverride(key_) != nullptr) {
    return core::failure(core::ErrorCode::Conflict,
                         "Phoneme override already exists during undo",
                         key_.toString());
  }
  region->phonemeOverrides.push_back(*removed_);
  std::stable_sort(region->phonemeOverrides.begin(), region->phonemeOverrides.end(),
      [](const auto& lhs, const auto& rhs) { return lhs.key < rhs.key; });
  return core::success();
}

}  // namespace seam::application
