#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/ids.hpp"
#include "seam/domain/note.hpp"
#include "seam/domain/phoneme.hpp"
#include "seam/domain/render_controls.hpp"
#include "seam/time/meter_map.hpp"
#include "seam/time/tempo_map.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace seam::domain {

enum class CharacterDisplayMode { Full, Minimal, Off };

struct ProjectSettings final {
  double sampleRate{48000.0};
  CharacterDisplayMode characterDisplay{CharacterDisplayMode::Minimal};
  bool snapEnabled{true};
  time::Tick snapGrid{time::Tick{time::kDefaultPpq / 4}};

  friend bool operator==(const ProjectSettings&, const ProjectSettings&) = default;
};

struct VoicebankReference final {
  std::string id;
  std::string version;
  std::string contentHash;

  friend bool operator==(const VoicebankReference&, const VoicebankReference&) = default;
};

struct CharacterReference final {
  std::string id;
  std::string version;

  friend bool operator==(const CharacterReference&, const CharacterReference&) = default;
};

struct VocalRegion final {
  RegionId id;
  std::string name;
  time::Tick startTick;
  time::Tick durationTick;
  std::vector<LyricToken> lyrics;
  std::vector<Note> notes;
  std::vector<PhonemeOverride> phonemeOverrides;
  std::vector<UnitSelectionOverride> unitSelectionOverrides;
  std::vector<SeamOverride> seamOverrides;
  PitchAutomation pitchAutomation;

  [[nodiscard]] Note* findNote(NoteId noteId) noexcept;
  [[nodiscard]] const Note* findNote(NoteId noteId) const noexcept;
  [[nodiscard]] LyricToken* findLyric(LyricTokenId lyricId) noexcept;
  [[nodiscard]] const LyricToken* findLyric(LyricTokenId lyricId) const noexcept;
  [[nodiscard]] PhonemeOverride* findPhonemeOverride(PhonemeKey key) noexcept;
  [[nodiscard]] const PhonemeOverride* findPhonemeOverride(PhonemeKey key) const noexcept;
  [[nodiscard]] UnitSelectionOverride* findUnitSelectionOverride(PhonemeKey startKey) noexcept;
  [[nodiscard]] const UnitSelectionOverride* findUnitSelectionOverride(
      PhonemeKey startKey) const noexcept;
  [[nodiscard]] SeamOverride* findSeamOverride(PhonemeKey incomingStartKey) noexcept;
  [[nodiscard]] const SeamOverride* findSeamOverride(
      PhonemeKey incomingStartKey) const noexcept;
  [[nodiscard]] core::Result<void> validate() const;
  void sortNotes();

  friend bool operator==(const VocalRegion&, const VocalRegion&) = default;
};

struct VocalTrack final {
  TrackId id;
  std::string name;
  VoicebankReference voicebank;
  CharacterReference character;
  std::vector<VocalRegion> regions;
  float gainDb{0.0F};
  float pan{0.0F};
  bool muted{false};
  bool solo{false};

  [[nodiscard]] VocalRegion* findRegion(RegionId regionId) noexcept;
  [[nodiscard]] const VocalRegion* findRegion(RegionId regionId) const noexcept;

  friend bool operator==(const VocalTrack&, const VocalTrack&) = default;
};

struct AudioTrack final {
  TrackId id;
  std::string name;
  std::string mediaPath;
  time::Tick startTick;
  float gainDb{0.0F};
  bool muted{false};

  friend bool operator==(const AudioTrack&, const AudioTrack&) = default;
};

class Project final {
public:
  Project();
  Project(ProjectId id, std::string name, time::Ppq ppq = time::kDefaultPpq);

  [[nodiscard]] ProjectId id() const noexcept { return id_; }
  [[nodiscard]] const std::string& name() const noexcept { return name_; }
  void setName(std::string value) { name_ = std::move(value); }

  [[nodiscard]] time::Ppq ppq() const noexcept { return tempoMap_.ppq(); }
  [[nodiscard]] time::TempoMap& tempoMap() noexcept { return tempoMap_; }
  [[nodiscard]] const time::TempoMap& tempoMap() const noexcept { return tempoMap_; }
  [[nodiscard]] time::MeterMap& meterMap() noexcept { return meterMap_; }
  [[nodiscard]] const time::MeterMap& meterMap() const noexcept { return meterMap_; }
  [[nodiscard]] ProjectSettings& settings() noexcept { return settings_; }
  [[nodiscard]] const ProjectSettings& settings() const noexcept { return settings_; }

  [[nodiscard]] std::vector<VocalTrack>& vocalTracks() noexcept { return vocalTracks_; }
  [[nodiscard]] const std::vector<VocalTrack>& vocalTracks() const noexcept { return vocalTracks_; }
  [[nodiscard]] std::vector<AudioTrack>& audioTracks() noexcept { return audioTracks_; }
  [[nodiscard]] const std::vector<AudioTrack>& audioTracks() const noexcept { return audioTracks_; }

  [[nodiscard]] VocalTrack* findVocalTrack(TrackId trackId) noexcept;
  [[nodiscard]] const VocalTrack* findVocalTrack(TrackId trackId) const noexcept;
  [[nodiscard]] VocalRegion* findRegion(RegionId regionId) noexcept;
  [[nodiscard]] const VocalRegion* findRegion(RegionId regionId) const noexcept;
  [[nodiscard]] Note* findNote(NoteId noteId) noexcept;
  [[nodiscard]] const Note* findNote(NoteId noteId) const noexcept;

  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] std::size_t noteCount() const noexcept;

  friend bool operator==(const Project&, const Project&) = default;

private:
  ProjectId id_;
  std::string name_;
  time::TempoMap tempoMap_;
  time::MeterMap meterMap_;
  ProjectSettings settings_;
  std::vector<VocalTrack> vocalTracks_;
  std::vector<AudioTrack> audioTracks_;
};

}  // namespace seam::domain
