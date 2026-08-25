#pragma once

#include "seam/application/command.hpp"

#include <optional>
#include <string>
#include <vector>

namespace seam::application {

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
  std::optional<domain::PhonemeOverride> before_;
  bool captured_{false};
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
  std::optional<domain::PhonemeOverride> removed_;
};

}  // namespace seam::application
