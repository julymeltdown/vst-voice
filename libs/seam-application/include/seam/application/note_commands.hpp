#pragma once

#include "seam/application/command.hpp"

#include <vector>

namespace seam::application {

struct NoteMove final {
  domain::NoteId noteId;
  time::Tick before;
  time::Tick after;
  std::uint8_t beforeKey{60};
  std::uint8_t afterKey{60};
};

struct NoteResize final {
  domain::NoteId noteId;
  time::Tick beforeStart;
  time::Tick beforeDuration;
  time::Tick afterStart;
  time::Tick afterDuration;
};

class AddNoteCommand final : public ICommand {
public:
  AddNoteCommand(domain::RegionId regionId, domain::LyricToken lyric, domain::Note note);

  [[nodiscard]] std::string_view name() const noexcept override { return "Add note"; }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::RegionId regionId_;
  domain::LyricToken lyric_;
  domain::Note note_;
};

class RemoveNotesCommand final : public ICommand {
public:
  explicit RemoveNotesCommand(std::vector<domain::NoteId> noteIds)
      : noteIds_(std::move(noteIds)) {}

  [[nodiscard]] std::string_view name() const noexcept override { return "Delete notes"; }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  struct RemovedNote final {
    domain::RegionId regionId;
    domain::Note note;
    std::size_t originalIndex{0};
  };

  struct RemovedLyric final {
    domain::RegionId regionId;
    domain::LyricToken lyric;
    std::size_t originalIndex{0};
  };

  struct RemovedPhonemeOverride final {
    domain::RegionId regionId;
    domain::PhonemeOverride overrideValue;
    std::size_t originalIndex{0};
  };

  struct RemovedUnitSelectionOverride final {
    domain::RegionId regionId;
    domain::UnitSelectionOverride overrideValue;
    std::size_t originalIndex{0};
  };

  struct RemovedSeamOverride final {
    domain::RegionId regionId;
    domain::SeamOverride overrideValue;
    std::size_t originalIndex{0};
  };

  [[nodiscard]] core::Result<void> capture(const domain::Project& project);
  [[nodiscard]] core::Result<void> removeCaptured(domain::Project& project) const;

  std::vector<domain::NoteId> noteIds_;
  std::vector<RemovedNote> removedNotes_;
  std::vector<RemovedLyric> removedLyrics_;
  std::vector<RemovedPhonemeOverride> removedOverrides_;
  std::vector<RemovedUnitSelectionOverride> removedUnitOverrides_;
  std::vector<RemovedSeamOverride> removedSeamOverrides_;
  bool captured_{false};
};

class MoveNotesCommand final : public ICommand {
public:
  explicit MoveNotesCommand(std::vector<NoteMove> moves) : moves_(std::move(moves)) {}

  [[nodiscard]] std::string_view name() const noexcept override { return "Move notes"; }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  [[nodiscard]] core::Result<void> set(domain::Project& project, bool after);
  std::vector<NoteMove> moves_;
};

class ResizeNotesCommand final : public ICommand {
public:
  explicit ResizeNotesCommand(std::vector<NoteResize> resizes)
      : resizes_(std::move(resizes)) {}

  [[nodiscard]] std::string_view name() const noexcept override { return "Resize notes"; }
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  [[nodiscard]] core::Result<void> set(domain::Project& project, bool after);
  std::vector<NoteResize> resizes_;
};

}  // namespace seam::application
