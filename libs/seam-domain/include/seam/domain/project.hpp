#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/ids.hpp"
#include "seam/domain/dynamics_automation.hpp"
#include "seam/domain/note.hpp"
#include "seam/domain/phoneme.hpp"
#include "seam/domain/performance_intent.hpp"
#include "seam/domain/render_controls.hpp"
#include "seam/domain/routing.hpp"
#include "seam/domain/voice_style_selection.hpp"
#include "seam/time/meter_map.hpp"
#include "seam/time/tempo_map.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seam::domain {

enum class CharacterDisplayMode { Full, Minimal, Off };

enum class TechnicalLane { Phoneme, Unit, Seam, Pitch, Dynamics };
inline constexpr std::size_t kTechnicalLaneCount{5U};
enum class TechnicalLaneMode { Auto, Collapsed, Preview, Expanded };

struct TechnicalLanePresentation final {
  TechnicalLaneMode mode{TechnicalLaneMode::Auto};
  double expandedHeight{120.0};

  friend bool operator==(const TechnicalLanePresentation&,
                         const TechnicalLanePresentation&) = default;
};

// Which timing a final bounce is rendered against. Fixed Audio renders the score against its
// own tempo map; Follow Host renders it against the timing the host reported for that range.
enum class BounceTimingAuthority { FixedAudio, FollowHost };

[[nodiscard]] std::string_view bounceTimingAuthorityName(
    BounceTimingAuthority authority) noexcept;

struct ProjectSettings final {
  double sampleRate{48000.0};
  // Which timing a final bounce is rendered against. Fixed Audio uses the score's own map,
  // which is what a project means by default; Follow Host uses the timing the host reported
  // for the rendered range and is an explicit choice, never an inference.
  BounceTimingAuthority bounceTimingAuthority{BounceTimingAuthority::FixedAudio};
  CharacterDisplayMode characterDisplay{CharacterDisplayMode::Minimal};
  std::array<TechnicalLanePresentation, kTechnicalLaneCount> technicalLanes{};
  bool snapEnabled{true};
  time::Tick snapGrid{time::Tick{time::kDefaultPpq / 4}};
  // Host position at this musical tick maps to source frame zero. This keeps
  // plug-in pre-roll and project-offset semantics in the canonical state.
  time::Tick hostStartOffsetTick{time::Tick{0}};

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

enum class MediaOwnership { ExternalReference, ProjectCopy };

[[nodiscard]] std::string_view mediaOwnershipName(MediaOwnership ownership) noexcept;

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
  DynamicsAutomation dynamicsAutomation;
  RegionPerformanceState performance;

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

struct ProceduralRecipeReference final {
  SingerResourceIdentity resource;
  std::string path;
  std::string style{"neutral"};
  [[nodiscard]] core::Result<void> validate() const;
  friend bool operator==(const ProceduralRecipeReference&, const ProceduralRecipeReference&) = default;
};

// Persisted selection of an installed neural singer bundle. Identity only: the
// application resolves this identity to a local admitted bundle and its first
// party helper. No filesystem path, helper command or resolved execution state
// belongs in a project file.
struct NeuralResourceReference final {
  SingerResourceIdentity resource;
  [[nodiscard]] core::Result<void> validate() const;
  friend bool operator==(const NeuralResourceReference&, const NeuralResourceReference&) = default;
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
  TrackOutputRoute outputRoute{};
  VoiceStyleSelection styleSelection;
  std::optional<ProceduralRecipeReference> proceduralRecipe{};
  // A track may select one singer family. It selects neither, one recipe, or
  // one neural bundle; selecting both is an invalid project, not a preference.
  std::optional<NeuralResourceReference> neuralResource{};

  [[nodiscard]] VocalRegion* findRegion(RegionId regionId) noexcept;
  [[nodiscard]] const VocalRegion* findRegion(RegionId regionId) const noexcept;

  friend bool operator==(const VocalTrack&, const VocalTrack&) = default;
};

struct AudioTrack final {
  TrackId id;
  std::string name;
  std::string mediaPath;
  std::string mediaHash;
  MediaOwnership mediaOwnership{MediaOwnership::ExternalReference};
  std::string originalFilename;
  std::uint32_t sourceSampleRate{0U};
  std::uint16_t sourceChannels{0U};
  std::uint64_t sourceFrameCount{0U};
  std::uint64_t trimStartFrame{0U};
  std::optional<std::uint64_t> trimEndFrame;
  time::Tick startTick;
  float gainDb{0.0F};
  float pan{0.0F};
  bool muted{false};
  bool solo{false};
  TrackOutputRoute outputRoute{};

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
  [[nodiscard]] ProjectRouting& routing() noexcept { return routing_; }
  [[nodiscard]] const ProjectRouting& routing() const noexcept { return routing_; }

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
  ProjectRouting routing_;
  std::vector<VocalTrack> vocalTracks_;
  std::vector<AudioTrack> audioTracks_;
};

}  // namespace seam::domain
