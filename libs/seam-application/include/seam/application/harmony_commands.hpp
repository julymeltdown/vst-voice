#pragma once

#include "seam/application/command.hpp"
#include "seam/application/project_factory.hpp"

#include <cstdint>
#include <vector>

namespace seam::application {

struct HarmonyRequest final {
  domain::RegionId regionId;
  std::vector<domain::NoteId> sourceNotes;
  std::int32_t intervalSemitones{7};
  std::uint8_t minimumMidi{0U};
  std::uint8_t maximumMidi{127U};
};

struct HarmonyDraft final {
  domain::RegionId regionId;
  std::vector<domain::Note> expectedNotes;
  std::vector<domain::LyricToken> lyrics;
  std::vector<domain::Note> notes;
};

// Builds a deterministic, editable harmony proposal without changing the
// source project. Every generated note gets a fresh lyric token, so copied
// notes cannot accidentally create a shared-melisma relationship.
[[nodiscard]] core::Result<HarmonyDraft> prepareHarmony(
    const domain::Project& project, ProjectFactory& factory,
    HarmonyRequest request);

// Applies one prepared harmony layer atomically. The expected note vector is
// checked before publication; stale regeneration cannot overwrite newer edits.
class AddHarmonyCommand final : public ICommand {
public:
  AddHarmonyCommand(domain::RegionId regionId,
                    std::vector<domain::Note> expectedNotes,
                    std::vector<domain::LyricToken> lyrics,
                    std::vector<domain::Note> notes);

  [[nodiscard]] std::string_view name() const noexcept override {
    return "Add harmony";
  }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::PhraseAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  domain::RegionId regionId_;
  std::vector<domain::Note> expectedNotes_;
  std::vector<domain::LyricToken> lyrics_;
  std::vector<domain::Note> notes_;
  bool applied_{false};
};

}  // namespace seam::application
