#pragma once

#include "seam/application/command.hpp"
#include "seam/application/project_factory.hpp"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace seam::application {

// Intervals are relative to the tonic in strictly ascending pitch-class order.
// A source note outside the scale is refused rather than silently quantized.
struct DiatonicHarmony final {
  std::uint8_t tonicPitchClass{0U};
  std::vector<std::uint8_t> scaleIntervals{0U,2U,4U,5U,7U,9U,11U};
  std::int32_t degreeOffset{2};
};

struct HarmonyRequest final {
  domain::RegionId regionId;
  std::vector<domain::NoteId> sourceNotes;
  std::int32_t intervalSemitones{7};
  std::uint8_t minimumMidi{0U};
  std::uint8_t maximumMidi{127U};
  // When set, degreeOffset replaces chromatic intervalSemitones.
  std::optional<DiatonicHarmony> diatonic{};
};

struct HarmonyDraft final {
  domain::RegionId regionId;
  std::vector<domain::Note> expectedNotes;
  std::vector<domain::LyricToken> lyrics;
  std::vector<domain::Note> notes;
};

struct HarmonyTrackDraft final {
  domain::TrackId sourceTrackId;
  domain::VocalTrack expectedSourceTrack;
  domain::VocalTrack harmonyTrack;
};

// Builds a deterministic, editable harmony proposal without changing the
// source project. Fresh lyric identities retain the source's shared-lyric
// melisma relationships; copied notes never refer back to the lead's lyrics.
[[nodiscard]] core::Result<HarmonyDraft> prepareHarmony(
    const domain::Project& project, ProjectFactory& factory,
    HarmonyRequest request);

// Produces a separately editable vocal track with its own region, note and
// lyric identities. Singer/style routing is copied from the lead, but its
// absolute performance curves are not: those would overwrite harmony pitch.
[[nodiscard]] core::Result<HarmonyTrackDraft> prepareHarmonyTrack(
    const domain::Project& project, ProjectFactory& factory,
    HarmonyRequest request);

class AddHarmonyTrackCommand final : public ICommand {
public:
  explicit AddHarmonyTrackCommand(HarmonyTrackDraft draft)
      : draft_(std::move(draft)) {}
  [[nodiscard]] std::string_view name() const noexcept override { return "Add harmony track"; }
  [[nodiscard]] CommandAudioImpact audioImpact() const noexcept override {
    return CommandAudioImpact::ProjectAudio;
  }
  [[nodiscard]] CommandImpact impact() const override;
  [[nodiscard]] core::Result<void> apply(domain::Project& project) override;
  [[nodiscard]] core::Result<void> revert(domain::Project& project) override;

private:
  HarmonyTrackDraft draft_;
  bool applied_{false};
};

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
