#pragma once

#include "seam/application/command.hpp"

#include <optional>
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
  enum class LyricMode { Create, ReuseExact };
  AddNoteCommand(domain::RegionId regionId, domain::LyricToken lyric, domain::Note note,
                 LyricMode lyricMode = LyricMode::Create);

  [[nodiscard]] std::string_view name() const noexcept override { return "Add note"; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::RegionId regionId_;
  domain::LyricToken lyric_;
  domain::Note note_;
  LyricMode lyricMode_;
  std::optional<domain::PronunciationIdentity> beforePronunciation_;
  std::optional<domain::PronunciationIdentity> afterPronunciation_;
  std::vector<domain::PhonemeOverride> beforePhonemes_, afterPhonemes_;
  std::vector<domain::UnitSelectionOverride> beforeUnits_, afterUnits_;
  std::vector<domain::SeamOverride> beforeSeams_, afterSeams_;
  std::uint64_t beforePronunciationRevision_{0U};
  bool pronunciationCaptured_{false};
};

class RemoveNotesCommand final : public ICommand {
public:
  explicit RemoveNotesCommand(std::vector<domain::NoteId> noteIds)
      : noteIds_(std::move(noteIds)) {}

  [[nodiscard]] std::string_view name() const noexcept override { return "Delete notes"; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
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

  struct PerformanceChange final {
    domain::RegionId regionId;
    domain::RegionPerformanceState before;
    domain::RegionPerformanceState after;
    std::vector<domain::PhonemeOverride> beforePhonemes, afterPhonemes;
    std::vector<domain::UnitSelectionOverride> beforeUnits, afterUnits;
    std::vector<domain::SeamOverride> beforeSeams, afterSeams;
  };

  [[nodiscard]] core::Result<void> capture(const domain::Project& project);
  [[nodiscard]] core::Result<void> removeCaptured(domain::Project& project) const;

  std::vector<domain::NoteId> noteIds_;
  std::vector<RemovedNote> removedNotes_;
  std::vector<RemovedLyric> removedLyrics_;
  std::vector<RemovedPhonemeOverride> removedOverrides_;
  std::vector<RemovedUnitSelectionOverride> removedUnitOverrides_;
  std::vector<RemovedSeamOverride> removedSeamOverrides_;
  std::vector<PerformanceChange> performanceChanges_;
  bool captured_{false};
};

struct NotePronunciationChange final {
  domain::RegionId regionId;
  std::optional<domain::PronunciationIdentity> before;
  std::optional<domain::PronunciationIdentity> after;
  std::uint64_t beforeRevision{0U};
  std::vector<domain::PhonemeOverride> beforePhonemes, afterPhonemes;
  std::vector<domain::UnitSelectionOverride> beforeUnits, afterUnits;
  std::vector<domain::SeamOverride> beforeSeams, afterSeams;
};

struct NoteHintEdit final {
  domain::NoteId noteId;
  std::optional<std::string> before, after;
};
class SetNoteHintsCommand final : public ICommand {
public:
  explicit SetNoteHintsCommand(std::vector<NoteHintEdit> edits) : edits_(std::move(edits)) {}
  [[nodiscard]] std::string_view name() const noexcept override { return "Edit pronunciation hints"; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override { return CommandAudioImpact::PhraseAudio; }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
private:
  [[nodiscard]] core::Result<void> set(domain::Project& project, bool after);
  std::vector<NoteHintEdit> edits_;
  std::vector<NotePronunciationChange> pronunciationChanges_;
  bool pronunciationCaptured_{false};
};

class MoveNotesCommand final : public ICommand {
public:
  explicit MoveNotesCommand(std::vector<NoteMove> moves) : moves_(std::move(moves)) {}

  [[nodiscard]] std::string_view name() const noexcept override { return "Move notes"; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  [[nodiscard]] core::Result<void> set(domain::Project& project, bool after);
  std::vector<NoteMove> moves_;
  std::vector<NotePronunciationChange> pronunciationChanges_;
  bool pronunciationCaptured_{false};
};

class ResizeNotesCommand final : public ICommand {
public:
  explicit ResizeNotesCommand(std::vector<NoteResize> resizes)
      : resizes_(std::move(resizes)) {}

  [[nodiscard]] std::string_view name() const noexcept override { return "Resize notes"; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  [[nodiscard]] core::Result<void> set(domain::Project& project, bool after);
  std::vector<NoteResize> resizes_;
  std::vector<NotePronunciationChange> pronunciationChanges_;
  bool pronunciationCaptured_{false};
};

struct NotePerformanceEdit final {
  domain::NoteId noteId;
  domain::NoteArticulation beforeArticulation{domain::NoteArticulation::Normal};
  domain::NoteArticulation afterArticulation{domain::NoteArticulation::Normal};
  std::optional<std::uint64_t> beforeSlurGroup;
  std::optional<std::uint64_t> afterSlurGroup;
  domain::LyricTokenId beforeLyricTokenId;
  domain::LyricTokenId afterLyricTokenId;
};

class SetNotePerformanceCommand final : public ICommand {
public:
  explicit SetNotePerformanceCommand(std::vector<NotePerformanceEdit> edits)
      : edits_(std::move(edits)) {}

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Edit note articulation";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  [[nodiscard]] core::Result<void> set(domain::Project& project, bool after);
  std::vector<NotePerformanceEdit> edits_;
  std::vector<NotePronunciationChange> pronunciationChanges_;
  bool pronunciationCaptured_{false};
};

}  // namespace seam::application
