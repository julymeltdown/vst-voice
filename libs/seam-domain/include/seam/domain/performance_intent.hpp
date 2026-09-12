#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/ids.hpp"
#include "seam/domain/note.hpp"
#include "seam/time/tick.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace seam::domain {

enum class PerformanceChannel {
  Pitch, Timing, Dynamics, Breathiness, Tension, Airiness,
  Formant, Gender, StyleBlend, Growl, Attack, Release,
};

enum class ManualPerformanceMode { Replace, PitchOffset };

struct PerformanceRevision final {
  std::uint64_t musical{0U};
  std::uint64_t pronunciation{0U};
  std::uint64_t ownership{0U};

  friend bool operator==(const PerformanceRevision&, const PerformanceRevision&) = default;
};

struct PerformanceTimeRange final {
  time::Tick startTick;
  time::Tick endTick;

  [[nodiscard]] bool contains(time::Tick tick) const noexcept;
  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const PerformanceTimeRange&, const PerformanceTimeRange&) = default;
};

using PerformanceScope = std::variant<NoteId, PerformanceTimeRange>;

struct ManualPerformanceOwnership final {
  PerformanceChannel channel{PerformanceChannel::Pitch};
  PerformanceScope scope{NoteId{}};
  ManualPerformanceMode mode{ManualPerformanceMode::Replace};
  PerformanceRevision revision;

  [[nodiscard]] core::Result<void> validate() const;
  [[nodiscard]] bool appliesTo(NoteId noteId, time::Tick tick) const noexcept;

  friend bool operator==(const ManualPerformanceOwnership&,
                         const ManualPerformanceOwnership&) = default;
};

[[nodiscard]] core::Result<void> validatePerformanceAcceptanceRevision(
    PerformanceRevision captured, PerformanceRevision current);

enum class SingerResourceKind { Sample, Procedural, Neural };

struct SingerResourceIdentity final {
  SingerResourceKind kind{SingerResourceKind::Sample};
  std::string id;
  std::string version;
  std::string contentHash;

  [[nodiscard]] core::Result<void> validate() const;
  friend bool operator==(const SingerResourceIdentity&, const SingerResourceIdentity&) = default;
};

struct PronunciationIdentity final {
  Language language{Language::Unspecified};
  std::string resolverId;
  std::string resolverVersion;
  std::string resourceHash;
  std::string inputHash;
  std::string sequenceHash;

  [[nodiscard]] core::Result<void> validate() const;
  friend bool operator==(const PronunciationIdentity&, const PronunciationIdentity&) = default;
};

struct PerformancePoint final {
  time::Tick tick;
  std::optional<double> value;
  friend bool operator==(const PerformancePoint&, const PerformancePoint&) = default;
};

struct PerformanceLane final {
  PerformanceChannel channel{PerformanceChannel::Pitch};
  std::vector<PerformancePoint> points;

  [[nodiscard]] core::Result<void> validate(PerformanceTimeRange range) const;
  friend bool operator==(const PerformanceLane&, const PerformanceLane&) = default;
};

[[nodiscard]] std::string_view performanceChannelUnit(PerformanceChannel channel) noexcept;

enum class PerformanceProposalState { Proposed, Rejected };

struct PerformanceTake final {
  std::string id;
  RegionId sourceRegionId;
  PerformanceRevision capturedRevision;
  SingerResourceIdentity resource;
  PronunciationIdentity pronunciation;
  std::string generatorId;
  std::string generatorVersion;
  std::uint64_t seed{0U};
  PerformanceTimeRange range;
  PerformanceProposalState state{PerformanceProposalState::Proposed};
  std::vector<PerformanceLane> lanes;

  [[nodiscard]] core::Result<void> validate() const;
  friend bool operator==(const PerformanceTake&, const PerformanceTake&) = default;
};

struct AcceptedPerformanceSelection final {
  std::string takeId;
  PerformanceChannel channel{PerformanceChannel::Pitch};
  PerformanceScope scope{NoteId{}};
  time::Tick sourceTickOffset;

  friend bool operator==(const AcceptedPerformanceSelection&,
                         const AcceptedPerformanceSelection&) = default;
};

inline constexpr std::size_t kMaximumPerformanceOwnership{4096U};
inline constexpr std::size_t kMaximumPerformanceTakes{16U};
inline constexpr std::size_t kMaximumPerformanceSelections{4096U};
inline constexpr std::size_t kMaximumPerformanceLanePoints{16384U};
inline constexpr std::size_t kMaximumPerformanceStatePoints{65536U};

struct RegionPerformanceState final {
  PerformanceRevision revision;
  std::optional<PronunciationIdentity> pronunciation;
  std::vector<ManualPerformanceOwnership> ownership;
  std::vector<PerformanceTake> takes;
  std::vector<AcceptedPerformanceSelection> accepted;

  [[nodiscard]] core::Result<void> validate(std::span<const Note> notes,
                                           time::Tick regionDuration) const;
  [[nodiscard]] bool permitsGenerated(PerformanceChannel channel, NoteId noteId,
                                      time::Tick tick, bool manualVibrato) const noexcept;
  friend bool operator==(const RegionPerformanceState&, const RegionPerformanceState&) = default;
};

struct PerformanceNoteRemap final {
  NoteId source;
  NoteId target;
};

[[nodiscard]] core::Result<RegionPerformanceState> transformRegionPerformance(
    const RegionPerformanceState& state, std::span<const Note> sourceNotes,
    time::Tick sourceDuration, std::span<const PerformanceNoteRemap> noteMap,
    PerformanceTimeRange sourceWindow);

}
