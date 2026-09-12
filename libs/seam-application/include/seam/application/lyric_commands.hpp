#pragma once

#include "seam/application/command.hpp"
#include "seam/application/performance_commands.hpp"

#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace seam::application {

class BatchSetLyricsCommand;

// For staged edits retaining all original override records (including records
// whose notes were removed; the caller prunes those after reconciliation). Reconcile
// phoneme bindings against base sounds, then samples/joins against effective sounds.
void reconcileRetainedNoteOverrides(const domain::VocalRegion& before,
                                    domain::VocalRegion& after);

class SetLyricCommand final : public ICommand {
public:
  SetLyricCommand(domain::LyricTokenId lyricId,
                  std::u32string surface,
                  domain::Language language);

  [[nodiscard]] std::string_view name() const noexcept override { return "Edit lyric"; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  [[nodiscard]] domain::LyricToken* find(domain::Project& project) const noexcept;

  domain::LyricTokenId lyricId_;
  std::u32string afterSurface_;
  domain::Language afterLanguage_{domain::Language::Unspecified};
  std::u32string beforeSurface_;
  domain::Language beforeLanguage_{domain::Language::Unspecified};
  bool captured_{false};
  std::shared_ptr<BatchSetLyricsCommand> batch_;
};

struct BatchLyricEdit final {
  domain::LyricTokenId lyricId;
  std::u32string before;
  std::u32string after;
  domain::Language language{domain::Language::Unspecified};
  domain::Language beforeLanguage{domain::Language::Unspecified};
};

class BatchSetLyricsCommand final : public ICommand {
public:
  explicit BatchSetLyricsCommand(std::vector<BatchLyricEdit> edits)
      : edits_(std::move(edits)) {}
  [[nodiscard]] std::string_view name() const noexcept override {
    return "Batch edit lyrics";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  std::vector<BatchLyricEdit> edits_;
  struct Dependencies final {
    domain::RegionId regionId;
    std::vector<domain::PhonemeOverride> phonemes;
    std::vector<domain::UnitSelectionOverride> units;
    std::vector<domain::SeamOverride> seams;
    std::optional<domain::PronunciationIdentity> pronunciation;
    domain::PerformanceRevision revision;
  };
  using OverrideState = std::vector<Dependencies>;
  OverrideState beforeOverrides_;
  OverrideState afterOverrides_;
  bool capturedOverrides_{false};
};

// Explicitly accepts validated external Japanese readings as per-note phone
// hints. It never rewrites visible lyric text and refuses ambiguous/cross-note
// plans before this command is constructed by the authoring layer.
class ApplyJapaneseReadingHintsCommand final : public ICommand {
public:
  ApplyJapaneseReadingHintsCommand(domain::RegionId regionId,
      std::vector<NoteExpressionEdit> notes, domain::PronunciationIdentity identity);
  [[nodiscard]] std::string_view name() const noexcept override { return "Apply Japanese reading"; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override { return CommandAudioImpact::PhraseAudio; }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;
private:
  domain::RegionId regionId_;
  std::vector<NoteExpressionEdit> afterNotes_;
  domain::PronunciationIdentity afterIdentity_;
  std::vector<std::optional<std::string>> beforeHints_;
  std::optional<domain::RegionPerformanceState> beforePerformance_;
  std::optional<domain::RegionPerformanceState> afterPerformance_;
  bool captured_{false};
};

struct PhonemeEditState final {
  std::vector<domain::PhonemeOverride> overrides;
  std::vector<domain::UnitSelectionOverride> units;
  std::vector<domain::SeamOverride> seams;
  std::optional<domain::PronunciationIdentity> identity;
  std::uint64_t pronunciationRevision{0U};
};

class UpsertPhonemeOverrideCommand final : public ICommand {
public:
  UpsertPhonemeOverrideCommand(domain::RegionId regionId,
                               domain::PhonemeOverride overrideValue);

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Edit phoneme override";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::RegionId regionId_;
  domain::PhonemeOverride after_;
  std::optional<PhonemeEditState> beforeState_;
  std::optional<PhonemeEditState> afterState_;
};

class RemovePhonemeOverrideCommand final : public ICommand {
public:
  RemovePhonemeOverrideCommand(domain::RegionId regionId, domain::PhonemeKey key)
      : regionId_(regionId), key_(key) {}

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Reset phoneme override";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::RegionId regionId_;
  domain::PhonemeKey key_;
  std::optional<PhonemeEditState> beforeState_;
  std::optional<PhonemeEditState> afterState_;
};

}  // namespace seam::application
