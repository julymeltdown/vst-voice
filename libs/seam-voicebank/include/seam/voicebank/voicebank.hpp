#pragma once

#include "seam/core/result.hpp"
#include "seam/domain/note.hpp"
#include "seam/time/tick.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seam::voicebank {

enum class UnitKind {
  Cv,
  Vcv,
  Vc,
  Vv,
  Cc,
  Sustain,
  Release,
  Breath,
  Glottal,
  Special,
};

enum class RendererHint {
  Raw,
  ClassicPsola,
  SpectralClassic,
  Stretch,
};

struct UnitMarkers final {
  time::SampleFrame audioOffset{0};
  time::SampleFrame consonantEnd{0};
  time::SampleFrame vowelOnset{0};
  time::SampleFrame stableStart{0};
  std::optional<time::SampleFrame> loopStart;
  std::optional<time::SampleFrame> loopEnd;
  std::optional<time::SampleFrame> releaseStart;
  time::SampleFrame audioEnd{0};

  [[nodiscard]] core::Result<void> validate(time::SampleFrame totalFrames) const;
  friend bool operator==(const UnitMarkers&, const UnitMarkers&) = default;
};

struct PitchMark final {
  time::SampleFrame frame{0};
  float confidence{1.0F};
  bool locked{false};

  friend bool operator==(const PitchMark&, const PitchMark&) = default;
};

struct Unit final {
  std::string id;
  std::string alias;
  std::vector<std::string> phones;
  UnitKind kind{UnitKind::Cv};
  std::filesystem::path audioPath;
  std::int32_t rootMidi{60};
  std::string style{"original"};
  std::int32_t take{1};
  std::int32_t priority{0};
  float gainDb{0.0F};
  RendererHint renderer{RendererHint::Raw};
  UnitMarkers markers;
  std::vector<PitchMark> pitchMarks;
  bool enabled{true};

  [[nodiscard]] core::Result<void> validate() const;
  friend bool operator==(const Unit&, const Unit&) = default;
};

struct Manifest final {
  static constexpr std::int32_t kSchemaVersion = 3;

  std::string id;
  std::string version;
  std::string displayName;
  std::string characterId;
  std::string characterVersion;
  domain::Language language{domain::Language::Unspecified};
  std::uint32_t expectedSampleRate{48000};
  std::vector<std::string> styles{"original"};
  std::vector<Unit> units;

  [[nodiscard]] const Unit* findUnit(std::string_view unitId) const noexcept;
  [[nodiscard]] Unit* findUnit(std::string_view unitId) noexcept;
  [[nodiscard]] core::Result<void> validate() const;

  friend bool operator==(const Manifest&, const Manifest&) = default;
};

[[nodiscard]] std::string_view unitKindName(UnitKind kind) noexcept;
[[nodiscard]] UnitKind parseUnitKind(std::string_view value) noexcept;
[[nodiscard]] std::string_view rendererHintName(RendererHint hint) noexcept;
[[nodiscard]] RendererHint parseRendererHint(std::string_view value) noexcept;

}  // namespace seam::voicebank
