#pragma once

#include "seam/core/result.hpp"
#include "seam/time/tick.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace seam::interchange {

// USTX is a YAML document, but the production reader intentionally accepts a
// small, typed 0.9 subset.  The limits are applied while lexing/parsing, before
// any collection is grown from an untrusted scalar.
struct UstxLimits final {
  std::size_t maximumInputBytes{4U * 1024U * 1024U};
  std::size_t maximumDepth{32U};
  std::size_t maximumNodes{100'000U};
  std::size_t maximumCollectionEntries{50'000U};
  std::size_t maximumScalarBytes{1U * 1024U * 1024U};
  std::size_t maximumTracks{64U};
  std::size_t maximumParts{256U};
  std::size_t maximumNotes{200'000U};
  std::size_t maximumTempoEvents{1'024U};
  std::size_t maximumMeterEvents{1'024U};
  std::size_t maximumCurvePoints{1'000'000U};
  std::int64_t maximumTick{std::numeric_limits<std::int64_t>::max() / 4};
};

enum class UstxIssueSeverity { Warning, Loss };

struct UstxIssue final {
  UstxIssueSeverity severity{UstxIssueSeverity::Warning};
  std::string path;
  std::string message;

  friend bool operator==(const UstxIssue&, const UstxIssue&) = default;
};

struct UstxTempo final {
  time::Tick position{time::Tick{0}};
  double bpm{120.0};

  friend bool operator==(const UstxTempo&, const UstxTempo&) = default;
};

struct UstxMeter final {
  std::int64_t barPosition{0};
  std::uint8_t numerator{4U};
  std::uint8_t denominator{4U};

  friend bool operator==(const UstxMeter&, const UstxMeter&) = default;
};

struct UstxPitchPoint final {
  // Signed milliseconds relative to note onset; negative values are normal
  // OpenUtau pickup/portamento points, not negative project positions.
  double offsetMilliseconds{0.0};
  double y{0.0};
  std::string shape{"l"};

  friend bool operator==(const UstxPitchPoint&, const UstxPitchPoint&) = default;
};

struct UstxVibrato final {
  double length{0.0};
  double period{175.0};
  double depth{25.0};
  double fadeIn{10.0};
  double fadeOut{10.0};
  double shift{0.0};
  double drift{0.0};
  double volumeLink{0.0};

  friend bool operator==(const UstxVibrato&, const UstxVibrato&) = default;
};

struct UstxNote final {
  time::Tick position{time::Tick{0}};
  time::Tick duration{time::Tick{480}};
  std::uint8_t tone{60U};
  std::string lyric;
  double tuning{0.0};
  std::vector<UstxPitchPoint> pitch;
  bool snapFirst{true};
  UstxVibrato vibrato{};
  bool hasVibrato{false};

  friend bool operator==(const UstxNote&, const UstxNote&) = default;
};

struct UstxPart final {
  std::string name;
  std::uint32_t trackNo{0U};
  time::Tick position{time::Tick{0}};
  time::Tick duration{time::Tick{480}};
  std::vector<UstxNote> notes;

  friend bool operator==(const UstxPart&, const UstxPart&) = default;
};

struct UstxTrack final {
  std::string name;
  std::string singer;
  double volume{0.0};
  double pan{0.0};
  bool mute{false};
  bool solo{false};
  std::vector<std::string> voiceColors;

  friend bool operator==(const UstxTrack&, const UstxTrack&) = default;
};

struct UstxDocument final {
  std::string version{"0.9"};
  std::string name;
  std::vector<UstxTempo> tempos;
  std::vector<UstxMeter> meters;
  std::vector<UstxTrack> tracks;
  std::vector<UstxPart> parts;
  std::vector<UstxIssue> issues;

  [[nodiscard]] core::Result<void> validate(const UstxLimits& limits = {}) const;
  friend bool operator==(const UstxDocument&, const UstxDocument&) = default;
};

// The decoder rejects YAML aliases, tags, multiple documents, malformed
// indentation and unsupported scalar forms.  Unknown USTX fields that are
// structurally safe are retained as bounded loss issues in the result.
[[nodiscard]] core::Result<UstxDocument> decodeUstx(
    std::span<const std::uint8_t> bytes, UstxLimits limits = {});

// Emits a deterministic USTX 0.9 subset.  Unsupported source metadata is
// represented by bounded issues on the document; it is never silently erased.
[[nodiscard]] core::Result<std::vector<std::uint8_t>> encodeUstx(
    const UstxDocument& document, UstxLimits limits = {});

}  // namespace seam::interchange
